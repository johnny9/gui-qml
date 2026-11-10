// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/walletlifecycle.h>
#include <qml/backendexecutor.h>
#include <test/mocks/manualexecutionenvironment.h>
#include <test/mocks/mockwallet.h>
#include <test/qt_test_registry.h>

#include <QtTest/QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <stdexcept>
#include <atomic>
#include <thread>

namespace {
using execution::Lane;
using walletlife::Publication;

struct Probe {
    std::optional<Lane> lane;
    std::vector<std::pair<std::string, std::optional<Lane>>> names, destroyed;
    std::vector<std::optional<Lane>> registrations, captures, disconnects;
};

class TrackedWallet final : public StubWallet
{
public:
    TrackedWallet(std::shared_ptr<Probe> probe, std::string name) : m_probe(std::move(probe)), m_name(std::move(name)) {}
    ~TrackedWallet() override { m_probe->destroyed.emplace_back(m_name, m_probe->lane); }
    std::string getWalletName() override { m_probe->names.emplace_back(m_name, m_probe->lane); return m_name; }
private:
    const std::shared_ptr<Probe> m_probe;
    const std::string m_name;
};

struct Fixture {
    explicit Fixture(execution::Limits limits = execution::Limits::Bounded(), bool coherent_resync = false) : env(limits)
    {
        const auto capture = [this] {
            probe->captures.push_back(probe->lane);
            if (capture_failure) throw std::runtime_error("synthetic capture failure");
            if (during_capture) during_capture();
            std::vector<std::unique_ptr<interfaces::Wallet>> wallets;
            wallets.reserve(catalog.size());
            for (const auto& name : catalog) wallets.push_back(wallet(name));
            return wallets;
        };
        owner = std::make_shared<walletlife::LoadSubscription>(env.scheduler,
            [this](walletlife::LoadSubscription::Callback fn) {
                probe->registrations.push_back(probe->lane);
                callback = std::move(fn);
                if (during_registration) during_registration();
                if (registration_missing) return std::unique_ptr<interfaces::Handler>{};
                return interfaces::MakeCleanupHandler([this] { probe->disconnects.push_back(probe->lane); });
            }, capture, [this](walletlife::Update update) { publications.push_back(update); },
            coherent_resync ? walletlife::LoadSubscription::CoherentResync{capture} : walletlife::LoadSubscription::CoherentResync{});
    }
    ~Fixture()
    {
        if (owner) owner->retire();
        env.scheduler->closeAdmission();
        drain();
    }
    std::unique_ptr<interfaces::Wallet> wallet(const std::string& name) { return std::make_unique<TrackedWallet>(probe, name); }
    bool step(Lane lane)
    {
        const auto previous = std::exchange(probe->lane, lane);
        const auto restore = qScopeGuard([&] { probe->lane = previous; });
        return env.step(lane);
    }
    void drain() { while (!env.queued.empty()) step(env.queued.front().lane); }
    void notify(const std::string& name)
    {
        const auto previous = std::exchange(probe->lane, Lane::Core);
        callback(wallet(name));
        probe->lane = previous;
    }
    int count(Publication kind) const
    {
        return std::count_if(publications.begin(), publications.end(), [kind](auto update) { return update.kind == kind; });
    }
    ManualRoutedExecution env;
    std::shared_ptr<Probe> probe{std::make_shared<Probe>()};
    std::shared_ptr<walletlife::LoadSubscription> owner;
    walletlife::LoadSubscription::Callback callback;
    std::function<void()> during_registration, during_capture;
    std::vector<std::string> catalog;
    std::vector<walletlife::Update> publications;
    bool capture_failure{false}, registration_missing{false};
};

struct PhysicalProbe {
    QThread* gui{QThread::currentThread()};
    QSemaphore entered, release, ready, retired, initial_disposed, changed;
    std::atomic_bool registration_off_gui{false}, metadata_off_gui{true}, destruction_off_gui{true}, notification_off_gui{true};
    std::atomic_int captures{0}, disconnects{0}, destroyed{0}, gui_publications{0};
    std::atomic_bool disconnect_off_gui{false}, drained{false}, heartbeat{false};
    walletlife::LoadSubscription::Callback callback;
};

class PhysicalWallet final : public StubWallet
{
public:
    PhysicalWallet(std::shared_ptr<PhysicalProbe> probe, std::string name) : m_probe(std::move(probe)), m_name(std::move(name)) {}
    ~PhysicalWallet() override
    {
        if (QThread::currentThread() == m_probe->gui) m_probe->destruction_off_gui = false;
        ++m_probe->destroyed;
    }
    std::string getWalletName() override
    {
        if (QThread::currentThread() == m_probe->gui) m_probe->metadata_off_gui = false;
        return m_name;
    }
private:
    const std::shared_ptr<PhysicalProbe> m_probe;
    const std::string m_name;
};
} // namespace

class WalletLifecycleTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void preAddOverflowMustNotClaimACompleteCatalog()
    {
        Fixture f;
        QVERIFY(f.owner->start()); f.drain();
        for (int i{0}; i < 64; ++i) {
            const auto name = std::string{"committed-"} + manualexecution_detail::Number(i);
            f.notify(name);                 // Core NotifyWalletLoaded.
            f.catalog.push_back(name);      // Core AddWallet completes.
            f.drain();
        }
        f.notify("pending-add");           // Producer pauses after Notify returns.
        f.drain();                         // Another worker captures the catalog.
        f.catalog.push_back("pending-add"); // AddWallet commits with no later notification.
        bool observed{false};
        for (;;) {
            auto handoff = f.owner->takeNotifications();
            if (!handoff || handoff->entries.empty()) break;
            for (const auto& entry : handoff->entries) observed |= entry.name == "pending-add";
            handoff.reset(); f.drain();
        }
        QVERIFY2(observed || f.count(Publication::Failed) == 1,
                 "A pre-AddWallet callback was lost while the catalog still claimed to be current");
        QCOMPARE(f.count(Publication::Failed), 1);
        QCOMPARE(f.publications.back().reason, walletlife::FailureReason::CoherentResyncUnavailable);
        QCOMPARE(f.probe->captures.size(), size_t{1});
        QVERIFY(f.env.quiescent());
    }
    void notificationChurnResyncsBeforeUnpublishedHandlesGrow()
    {
        Fixture f{execution::Limits::Bounded(), true};
        QVERIFY(f.owner->start()); f.drain();
        bool invalidated_claim_blocked{false};
        f.during_capture = [&] { invalidated_claim_blocked = !f.owner->takeNotifications(); };
        for (int i{0}; i < 96; ++i) {
            const auto name = std::string{"churn-"} + manualexecution_detail::Number(i);
            f.catalog = {name}; // Previous wallets were independently unloaded.
            f.notify(name); f.drain();
        }
        QVERIFY(f.probe->captures.size() >= 2);
        QVERIFY(invalidated_claim_blocked);
        QVERIFY(f.probe->destroyed.size() >= 32);
        bool found_latest{false};
        for (;;) {
            auto handoff = f.owner->takeNotifications();
            QVERIFY(handoff);
            if (handoff->entries.empty()) break;
            QVERIFY(handoff->entries.size() <= 16);
            for (const auto& entry : handoff->entries) found_latest |= entry.name == "churn-95";
            handoff.reset(); f.drain();
        }
        QVERIFY(found_latest);
    }
    void realCallbackConcurrentWithInitialCaptureKeepsItsRefresh()
    {
        const auto probe = std::make_shared<PhysicalProbe>();
        BackendExecutor executor{execution::Limits::Bounded()};
        auto owner = std::make_shared<walletlife::LoadSubscription>(executor.scheduler(),
            [probe](walletlife::LoadSubscription::Callback callback) {
                probe->registration_off_gui = QThread::currentThread() != probe->gui;
                probe->callback = std::move(callback);
                return interfaces::MakeCleanupHandler([probe] {
                    probe->disconnect_off_gui = QThread::currentThread() != probe->gui;
                    ++probe->disconnects;
                });
            }, [probe] {
                ++probe->captures;
                probe->entered.release(); probe->release.acquire();
                std::vector<std::unique_ptr<interfaces::Wallet>> wallets;
                wallets.push_back(std::make_unique<PhysicalWallet>(probe, "initial"));
                return wallets;
            }, [probe](walletlife::Update update) {
                if (QThread::currentThread() == probe->gui) probe->notification_off_gui = false;
                if (update.kind == Publication::Ready) probe->ready.release();
                if (update.kind == Publication::InitialHandoff) probe->initial_disposed.release();
                if (update.kind == Publication::Changed) probe->changed.release();
            });
        const auto cleanup = qScopeGuard([&] { probe->release.release(); owner->retire(); executor.shutdown(); });
        QVERIFY(owner->start());
        QVERIFY(probe->entered.tryAcquire(1, 5000));
        // Core owns the producer thread; it can enter ingress while the
        // executor's capture is held. Joining follows the completed callback.
        QSemaphore callback_finished;
        std::thread producer{[probe, &callback_finished] {
            probe->callback(std::make_unique<PhysicalWallet>(probe, "concurrent"));
            callback_finished.release();
        }};
        const bool callback_done = callback_finished.tryAcquire(1, 5000);
        if (!callback_done) probe->release.release();
        producer.join();
        QVERIFY(callback_done);
        probe->release.release();
        QVERIFY(probe->ready.tryAcquire(1, 5000));
        auto initial = owner->takeInitial();
        QVERIFY(initial); QCOMPARE(initial->entries.size(), size_t{1});
        QCOMPARE(initial->entries.front().name, std::string{"initial"});
        initial.reset();
        QVERIFY(probe->initial_disposed.tryAcquire(1, 5000));
        QVERIFY(probe->changed.tryAcquire(1, 5000));
        auto changed = owner->takeNotifications();
        QVERIFY(changed);
        QCOMPARE(changed->entries.size(), size_t{1});
        QCOMPARE(changed->entries.front().name, std::string{"concurrent"});
        changed.reset(); owner->retire(); executor.shutdown();
        QTRY_VERIFY(executor.isDrained());
        QCOMPARE(probe->captures.load(), 1);
        QCOMPARE(probe->disconnects.load(), 1);
        QCOMPARE(probe->destroyed.load(), 2);
        QVERIFY(probe->registration_off_gui && probe->metadata_off_gui && probe->disconnect_off_gui && probe->destruction_off_gui && probe->notification_off_gui);
    }
    void realRegistrationCanBeRetiredWhileBlockedWithoutBlockingGui()
    {
        const auto probe = std::make_shared<PhysicalProbe>();
        BackendExecutor executor{execution::Limits::Bounded()};
        auto owner = std::make_shared<walletlife::LoadSubscription>(executor.scheduler(),
            [probe](walletlife::LoadSubscription::Callback callback) {
                probe->registration_off_gui = QThread::currentThread() != probe->gui;
                callback(std::make_unique<PhysicalWallet>(probe, "inline"));
                probe->entered.release(); probe->release.acquire();
                return interfaces::MakeCleanupHandler([probe] {
                    probe->disconnect_off_gui = QThread::currentThread() != probe->gui;
                    ++probe->disconnects;
                });
            }, [probe] { ++probe->captures; return std::vector<std::unique_ptr<interfaces::Wallet>>{}; },
            [probe](walletlife::Update update) {
                if (QThread::currentThread() == probe->gui) probe->notification_off_gui = false;
                if (update.kind == Publication::Retired) probe->retired.release();
            });
        const auto cleanup = qScopeGuard([&] { probe->release.release(); owner->retire(); executor.shutdown(); });
        QVERIFY(owner->start());
        QVERIFY(probe->entered.tryAcquire(1, 5000));
        owner->retire(); executor.shutdown();
        QTimer::singleShot(0, this, [probe] { probe->heartbeat = true; });
        QTRY_VERIFY(probe->heartbeat.load());
        QVERIFY(!executor.isDrained()); QCOMPARE(probe->retired.available(), 0);
        probe->release.release();
        QTRY_VERIFY(executor.isDrained());
        QCOMPARE(probe->retired.available(), 1);
        QCOMPARE(probe->captures.load(), 0);
        QCOMPARE(probe->disconnects.load(), 1);
        QCOMPARE(probe->destroyed.load(), 1);
        QVERIFY(probe->registration_off_gui && probe->disconnect_off_gui && probe->destruction_off_gui && probe->notification_off_gui);
    }
    void realObserverLossAndDroppedHandoffStillDrainOwningResources()
    {
        const auto probe = std::make_shared<PhysicalProbe>();
        auto executor = std::make_unique<BackendExecutor>(execution::Limits::Bounded());
        auto observer = std::make_unique<QObject>();
        const QPointer<QObject> guard{observer.get()};
        const auto bridge = std::shared_ptr<QObject>{new QObject, [](QObject* object) {
            if (QThread::currentThread() == object->thread()) delete object;
            else object->deleteLater();
        }};
        auto owner = std::make_shared<walletlife::LoadSubscription>(executor->scheduler(),
            [probe](walletlife::LoadSubscription::Callback callback) {
                probe->registration_off_gui = QThread::currentThread() != probe->gui;
                callback(std::make_unique<PhysicalWallet>(probe, "inline"));
                return interfaces::MakeCleanupHandler([probe] {
                    probe->disconnect_off_gui = QThread::currentThread() != probe->gui;
                    ++probe->disconnects;
                });
            }, [probe] {
                ++probe->captures;
                std::vector<std::unique_ptr<interfaces::Wallet>> wallets;
                wallets.push_back(std::make_unique<PhysicalWallet>(probe, "initial"));
                return wallets;
            }, [probe, bridge, guard](walletlife::Update update) {
                if (QThread::currentThread() == probe->gui) probe->notification_off_gui = false;
                QMetaObject::invokeMethod(bridge.get(), [probe, guard, update] {
                    (void)update; // Only a value reaches the observer's GUI turn.
                    if (guard) ++probe->gui_publications;
                }, Qt::QueuedConnection);
                if (update.kind == Publication::Ready) probe->ready.release();
                if (update.kind == Publication::Retired) probe->retired.release();
            });
        const auto cleanup = qScopeGuard([&] { if (owner) owner->retire(); if (executor) executor->shutdown(); });
        QVERIFY(owner->start());
        // Withhold GUI dispatch until after destroying its observer.
        QVERIFY(probe->ready.tryAcquire(1, 5000));
        auto handoff = owner->takeInitial();
        QVERIFY(handoff); QCOMPARE(handoff->entries.size(), size_t{1});
        observer.reset(); owner->retire(); handoff.reset(); owner.reset(); executor.reset();
        BackendExecutor::shutdownAll(this, [probe] { probe->drained = true; });
        QTRY_VERIFY(probe->drained.load());
        QCOMPARE(probe->gui_publications.load(), 0);
        QCOMPARE(probe->retired.available(), 1);
        QCOMPARE(probe->disconnects.load(), 1);
        QCOMPARE(probe->destroyed.load(), 2);
        QVERIFY(probe->registration_off_gui && probe->metadata_off_gui && probe->disconnect_off_gui && probe->destruction_off_gui && probe->notification_off_gui);
    }
    void notificationRetainsTheProducingCommandContext()
    {
        Fixture f;
        QVERIFY(f.owner->start()); f.drain();
        f.owner->setNotificationContext("unfinished", true);
        f.notify("unfinished");
        f.notify("unrelated");
        f.owner->setNotificationContext({}, false);
        f.drain();
        auto handoff = f.owner->takeNotifications();
        QVERIFY(handoff); QCOMPARE(handoff->entries.size(), size_t{1});
        QCOMPARE(handoff->entries.front().name, std::string{"unrelated"});
        QVERIFY(!handoff->entries.front().select_wallet);
        QCOMPARE(f.probe->destroyed.size(), size_t{1});
        QCOMPARE(f.probe->destroyed.front().first, std::string{"unfinished"});
        QCOMPARE(f.probe->destroyed.front().second, std::optional<Lane>{Lane::Core});
        handoff.reset(); f.drain();
        f.notify("after-command"); f.drain();
        handoff = f.owner->takeNotifications();
        QVERIFY(handoff); QCOMPARE(handoff->entries.size(), size_t{1});
        QVERIFY(handoff->entries.front().select_wallet);
    }
    void legalRetirementSchedules_data()
    {
        QTest::addColumn<int>("stage");
        QTest::addColumn<bool>("inline_callback");
        for (int stage{0}; stage < 8; ++stage) {
            for (const bool callback : {false, true}) {
                const QByteArray tag = QByteArray{"retire-"} + QByteArray::number(stage) + (callback ? "-inline" : "-snapshot");
                QTest::newRow(tag.constData()) << stage << callback;
            }
        }
    }
    void legalRetirementSchedules()
    {
        QFETCH(int, stage);
        QFETCH(bool, inline_callback);
        Fixture f; f.catalog = {"DO_NOT_TRACE_private_wallet"};
        const auto record_trace = qScopeGuard([&] {
            QJsonArray trace;
            for (const auto& event : f.env.trace) trace.append(QString::fromStdString(event));
            qInfo().noquote() << QJsonDocument{QJsonObject{{"stage", stage}, {"inline_callback", inline_callback}, {"events", trace}}}.toJson(QJsonDocument::Compact);
        });
        if (inline_callback) f.during_registration = [&] { f.callback(f.wallet("DO_NOT_TRACE_callback")); };
        QVERIFY(f.owner->start());
        if (stage >= 1) QVERIFY(f.step(Lane::Core));
        if (stage >= 2) QVERIFY(f.step(Lane::Domain));
        std::unique_ptr<walletlife::LoadSubscription::Handoff> handoff;
        if (stage >= 3) { handoff = f.owner->takeInitial(); QVERIFY(handoff); }
        if (stage >= 4) handoff.reset();
        if (stage >= 5) QVERIFY(f.step(Lane::Cleanup));
        if (stage >= 6) QVERIFY(f.step(Lane::Domain));
        if (stage >= 7) f.drain();
        f.owner->retire();
        f.env.scheduler->closeAdmission();
        if (handoff) {
            // Cancelling a queued refresh may use Cleanup independently. The
            // parent handler and borrowed wallet must still remain alive.
            while (f.step(Lane::Cleanup)) {}
            QVERIFY(f.probe->disconnects.empty());
            QVERIFY(f.probe->destroyed.empty());
            handoff.reset();
        }
        f.drain();
        QCOMPARE(f.count(Publication::Retired), 1);
        QCOMPARE(f.probe->disconnects.size(), f.probe->registrations.size());
        QCOMPARE(f.count(Publication::Ready), stage >= 2 ? 1 : 0);
        if (f.callback) f.notify("DO_NOT_TRACE_late");
        f.drain();
        QVERIFY(f.env.quiescent());
        for (const auto& event : f.env.trace) QVERIFY(event.find("DO_NOT_TRACE") == std::string::npos);
        for (const auto& entry : f.probe->destroyed) QVERIFY(entry.second == Lane::Core || entry.second == Lane::Cleanup);
    }
    void callbackDuringRegistrationRetainsBothInitialAndNotifiedWallets()
    {
        Fixture f;
        f.catalog = {"initial"};
        f.during_registration = [&] { f.callback(f.wallet("inline")); };
        QVERIFY(f.owner->start());
        QVERIFY(!f.owner->takeInitial());
        QVERIFY(f.step(Lane::Core));
        QCOMPARE(f.probe->registrations, std::vector<std::optional<Lane>>{Lane::Core});
        QCOMPARE(f.probe->captures, std::vector<std::optional<Lane>>{Lane::Core});
        QVERIFY(f.publications.empty());
        f.drain();
        QCOMPARE(f.count(Publication::Ready), 1);
        auto initial = f.owner->takeInitial();
        QVERIFY(initial); QCOMPARE(initial->entries.size(), size_t{1});
        QCOMPARE(initial->entries.front().name, std::string{"initial"});
        QVERIFY(!f.owner->takeNotifications());
        initial.reset(); f.drain();
        auto changed = f.owner->takeNotifications();
        QVERIFY(changed); QCOMPARE(changed->entries.size(), size_t{1});
        QCOMPARE(changed->entries.front().name, std::string{"inline"});
        changed.reset(); f.drain();
        f.owner->retire(); f.drain();
        QCOMPARE(f.count(Publication::Retired), 1);
        QCOMPARE(f.probe->destroyed.size(), size_t{2});
        for (const auto& entry : f.probe->names) QCOMPARE(entry.second, std::optional<Lane>{Lane::Core});
        for (const auto& entry : f.probe->destroyed) QCOMPARE(entry.second, std::optional<Lane>{Lane::Cleanup});
        QCOMPARE(f.probe->disconnects, std::vector<std::optional<Lane>>{Lane::Cleanup});
        QVERIFY(f.env.quiescent());
    }
    void retirementBeforeRegistrationOutcomeCannotPublishReady()
    {
        Fixture f; f.catalog = {"retired"};
        QVERIFY(f.owner->start());
        QVERIFY(f.step(Lane::Core));
        f.owner->retire();
        QVERIFY(!f.step(Lane::Cleanup));
        QVERIFY(f.step(Lane::Domain));
        f.drain();
        QCOMPARE(f.count(Publication::Ready), 0);
        QCOMPARE(f.count(Publication::Retired), 1);
        QVERIFY(!f.owner->takeInitial());
        QCOMPARE(f.probe->destroyed.size(), size_t{1});
        QCOMPARE(f.probe->destroyed.front().second, std::optional<Lane>{Lane::Cleanup});
        QVERIFY(f.env.quiescent());
    }
    void droppedGuiHandoffRetiresShadowsBeforeParentCleanup()
    {
        Fixture f; f.catalog = {"one", "two"};
        QVERIFY(f.owner->start()); f.drain();
        auto handoff = f.owner->takeInitial();
        QVERIFY(handoff); QCOMPARE(handoff->entries.size(), size_t{2});
        f.owner->retire(); f.env.scheduler->closeAdmission();
        QVERIFY(!f.step(Lane::Cleanup));
        handoff.reset();
        QVERIFY(f.probe->destroyed.empty());
        QVERIFY(f.step(Lane::Cleanup));
        QCOMPARE(f.probe->destroyed.size(), size_t{2});
        QVERIFY(f.probe->disconnects.empty());
        QVERIFY(!f.step(Lane::Cleanup));
        QVERIFY(f.step(Lane::Domain));
        f.drain();
        QCOMPARE(f.probe->disconnects.size(), size_t{1});
        QCOMPARE(f.count(Publication::InitialHandoff), 0);
        QCOMPARE(f.count(Publication::Retired), 1);
        QVERIFY(f.env.quiescent());
    }
    void initialPresentationIsBoundedAndHasAnOwnedContinuation()
    {
        Fixture f;
        for (int i{0}; i < 35; ++i) f.catalog.push_back("wallet-" + manualexecution_detail::Number(i));
        QVERIFY(f.owner->start()); f.drain();
        size_t presented{0}; int batches{0};
        for (;;) {
            auto handoff = f.owner->takeInitial();
            QVERIFY(handoff);
            if (handoff->entries.empty()) break;
            QVERIFY(handoff->entries.size() <= 16);
            presented += handoff->entries.size(); ++batches;
            QVERIFY(!f.owner->takeInitial());
            const auto before = f.count(Publication::InitialHandoff);
            handoff.reset(); f.drain();
            QCOMPARE(f.count(Publication::InitialHandoff), before + 1);
        }
        QCOMPARE(presented, size_t{35}); QCOMPARE(batches, 3);
        QCOMPARE(f.probe->destroyed.size(), size_t{35});
    }
    void notificationOverflowCoalescesIntoAnAuthoritativeResync()
    {
        Fixture f{execution::Limits::Bounded(), true};
        QVERIFY(f.owner->start()); f.drain();
        f.catalog = {"authoritative"};
        for (int i{0}; i < 100; ++i) f.notify("notification-" + manualexecution_detail::Number(i));
        QCOMPARE(f.env.scheduler->pending(), size_t{2}); // Cleanup plus one coalesced refresh.
        QCOMPARE(f.probe->destroyed.size(), size_t{36});
        f.drain();
        QCOMPARE(f.probe->captures.size(), size_t{2});
        auto handoff = f.owner->takeNotifications();
        QVERIFY(handoff); QCOMPARE(handoff->entries.size(), size_t{1});
        QCOMPARE(handoff->entries.front().name, std::string{"authoritative"});
        handoff.reset(); f.drain();
        QCOMPARE(f.probe->destroyed.size(), size_t{101});
        QCOMPARE(f.count(Publication::Changed), 2); // Refresh and handoff-disposal continuation.
    }
    void absentProducerIsAnExplicitFailureBeforeSnapshotCapture()
    {
        Fixture f; f.registration_missing = true;
        QVERIFY(f.owner->start()); f.drain();
        QCOMPARE(f.count(Publication::Ready), 0);
        QCOMPARE(f.count(Publication::Failed), 1);
        QCOMPARE(f.count(Publication::Retired), 1);
        QVERIFY(f.probe->captures.empty()); QVERIFY(f.probe->disconnects.empty());
        QVERIFY(f.env.quiescent());
    }
    void captureFailureOwnsOneTerminalFailureAndDisconnect()
    {
        Fixture f; f.capture_failure = true;
        QVERIFY(f.owner->start()); f.drain();
        QCOMPARE(f.count(Publication::Ready), 0);
        QCOMPARE(f.count(Publication::Failed), 1);
        QCOMPARE(f.count(Publication::Retired), 1);
        QCOMPARE(f.probe->disconnects.size(), size_t{1});
        f.notify("late"); f.drain();
        QCOMPARE(f.count(Publication::Changed), 0);
        QCOMPARE(f.probe->destroyed.back().second, std::optional<Lane>{Lane::Core});
        QVERIFY(f.env.quiescent());
    }
    void cleanupCapacityRejectionAcquiresNoBackendResources()
    {
        auto limits = execution::Limits::Bounded(); limits.cleanup.tasks = 0;
        Fixture f{limits};
        QCOMPARE(f.owner->start().rejection, execution::Rejection::Capacity);
        QVERIFY(f.probe->registrations.empty()); QVERIFY(f.probe->captures.empty());
        QVERIFY(f.publications.empty()); QVERIFY(f.env.quiescent());
    }
    void handoffReservationFailureLeavesHandlesForBackgroundCleanup()
    {
        auto limits = execution::Limits::Bounded(); limits.cleanup.tasks = 1;
        Fixture f{limits}; f.catalog = {"owned"};
        QVERIFY(f.owner->start()); f.drain();
        QVERIFY(!f.owner->takeInitial());
        QVERIFY(f.probe->destroyed.empty());
        f.drain();
        QCOMPARE(f.count(Publication::Failed), 1);
        QCOMPARE(f.count(Publication::Retired), 1);
        QCOMPARE(f.probe->destroyed.size(), size_t{1});
        QCOMPARE(f.probe->destroyed.front().second, std::optional<Lane>{Lane::Cleanup});
        QVERIFY(f.env.quiescent());
    }
};

BITCOINQML_REGISTER_QT_TEST(WalletLifecycleTests)
#include <test_walletlifecycle.moc>
