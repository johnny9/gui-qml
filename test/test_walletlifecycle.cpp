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

// Pause only at public scheduler boundaries; TaskQueue still owns every
// admission, lease, key and outcome decision used by production.
class BoundaryScheduler final : public execution::Scheduler
{
public:
    explicit BoundaryScheduler(std::shared_ptr<execution::Scheduler> target) : m_target(std::move(target)) {}
    std::function<void()> after_reserve, after_borrow, after_submit;
    std::shared_ptr<std::function<void()>> before_execute{std::make_shared<std::function<void()>>()};
    execution::Admission submit(std::shared_ptr<execution::Task> task, execution::TaskSpec spec) override
    {
        struct PausedTask final : execution::Task {
            std::shared_ptr<execution::Task> task;
            std::shared_ptr<std::function<void()>> pause;
            PausedTask(std::shared_ptr<execution::Task> t, std::shared_ptr<std::function<void()>> p) : task(std::move(t)), pause(std::move(p)) {}
            void execute() override { if (*pause) std::exchange(*pause, {})(); task->execute(); }
            void notStarted(execution::NotStarted::Cause cause) override { task->notStarted(cause); }
            void deliver() override { task->deliver(); }
        };
        auto result = m_target->submit(std::make_shared<PausedTask>(std::move(task), before_execute), spec);
        if (after_submit) std::exchange(after_submit, {})();
        return result;
    }
    execution::Admission reserveCleanup(std::shared_ptr<execution::Task> task, execution::TaskSpec spec) override
    {
        auto result = m_target->reserveCleanup(std::move(task), spec);
        if (after_reserve) std::exchange(after_reserve, {})();
        return result;
    }
    execution::BorrowAdmission borrow(execution::ResourceKey key) override
    {
        auto result = m_target->borrow(key);
        if (after_borrow) std::exchange(after_borrow, {})();
        return result;
    }
    bool activateCleanup(execution::TaskId id) override { return m_target->activateCleanup(id); }
    execution::ResourceKey allocateKey() override { return m_target->allocateKey(); }
    size_t pending() const override { return m_target->pending(); }
    void closeOrdinaryAdmission() override { m_target->closeOrdinaryAdmission(); }
    bool cancelQueued(execution::TaskId id) override { return m_target->cancelQueued(id); }
private:
    const std::shared_ptr<execution::Scheduler> m_target;
};

struct Probe {
    std::optional<Lane> lane;
    std::vector<std::pair<std::string, std::optional<Lane>>> names, destroyed;
    std::vector<std::optional<Lane>> registrations, captures, disconnects;
    std::function<void()> during_name;
};

class TrackedWallet final : public StubWallet
{
public:
    TrackedWallet(std::shared_ptr<Probe> probe, std::string name) : m_probe(std::move(probe)), m_name(std::move(name)) {}
    ~TrackedWallet() override { m_probe->destroyed.emplace_back(m_name, m_probe->lane); }
    std::string getWalletName() override { m_probe->names.emplace_back(m_name, m_probe->lane); if (m_probe->during_name) std::exchange(m_probe->during_name, {})(); return m_name; }
private:
    const std::shared_ptr<Probe> m_probe;
    const std::string m_name;
};

struct Fixture {
    explicit Fixture(execution::Limits limits = execution::Limits::Bounded()) : env(limits)
    {
        const auto capture = [this] {
            probe->captures.push_back(probe->lane);
            if (capture_failure) throw std::runtime_error("synthetic capture failure");
            if (during_capture) during_capture();
            std::vector<std::unique_ptr<interfaces::Wallet>> wallets;
            if (invalid_snapshot) { wallets.emplace_back(); return wallets; }
            wallets.reserve(catalog.size());
            for (const auto& name : catalog) wallets.push_back(wallet(name));
            return wallets;
        };
        owner = std::make_shared<walletlife::LoadSubscription>(boundary,
            [this](walletlife::LoadSubscription::Callback fn) {
                probe->registrations.push_back(probe->lane);
                callback = std::move(fn);
                if (during_registration) during_registration();
                if (registration_missing) return std::unique_ptr<interfaces::Handler>{};
                return interfaces::MakeCleanupHandler([this] { probe->disconnects.push_back(probe->lane); });
            }, capture, [this](walletlife::Update update) { publications.push_back(update); });
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
    void drain() { while (!env.queuedIdle()) step(env.nextLane().value()); }
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
    std::shared_ptr<BoundaryScheduler> boundary{std::make_shared<BoundaryScheduler>(env.scheduler)};
    std::shared_ptr<Probe> probe{std::make_shared<Probe>()};
    std::shared_ptr<walletlife::LoadSubscription> owner;
    walletlife::LoadSubscription::Callback callback;
    std::function<void()> during_registration, during_capture;
    std::vector<std::string> catalog;
    std::vector<walletlife::Update> publications;
    bool capture_failure{false}, registration_missing{false}, invalid_snapshot{false};
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
    void rejectedHandleReleasePrecedesCallbackLeaseAndDrain()
    {
        struct Probe {
            QThread* gui{QThread::currentThread()};
            QSemaphore capture_entered, capture_release, rejected_entered, rejected_release, core_outcome, witness;
            std::atomic_int destroyed{0}, disconnected{0};
            std::atomic_bool off_gui{true};
            walletlife::LoadSubscription::Callback callback;
        };
        const auto probe = std::make_shared<Probe>();
        struct Wallet : StubWallet {
            std::shared_ptr<Probe> probe;
            bool reject;
            Wallet(std::shared_ptr<Probe> p, bool r) : probe(std::move(p)), reject(r) {}
            ~Wallet() override {
                if (reject) { probe->rejected_entered.release(); probe->rejected_release.acquire(); }
                if (QThread::currentThread() == probe->gui) probe->off_gui = false;
                ++probe->destroyed;
            }
            std::string getWalletName() override { return "wallet"; }
        };
        BackendExecutor executor{execution::Limits::Bounded()};
        const auto scheduler = executor.scheduler();
        auto owner = std::make_shared<walletlife::LoadSubscription>(scheduler,
            [probe](walletlife::LoadSubscription::Callback callback) {
                probe->callback = std::move(callback);
                // Deliberately do not make disconnect wait for this producer.
                // The subscription's counted callback lease must do that work.
                return interfaces::MakeCleanupHandler([probe] { ++probe->disconnected; });
            }, [probe] {
                probe->capture_entered.release(); probe->capture_release.acquire();
                return std::vector<std::unique_ptr<interfaces::Wallet>>{};
            }, [](walletlife::Update) {});
        std::thread producer;
        const auto cleanup = qScopeGuard([&] {
            probe->capture_release.release(); probe->rejected_release.release();
            if (producer.joinable()) producer.join();
            owner->retire(); executor.shutdown();
            QTRY_VERIFY(executor.isDrained());
        });
        const auto registration = owner->start(); QVERIFY(registration);
        QVERIFY(probe->capture_entered.tryAcquire(1, 5000));
        const auto witness_key = scheduler->allocateKey();
        const auto witness = execution::Submit(*scheduler, [probe] { probe->witness.release(); }, [] {}, {},
            {Lane::Cleanup, 0, witness_key}, true);
        QVERIFY(witness);
        const auto core_key = scheduler->allocateKey();
        QVERIFY(execution::Submit(*scheduler, [] {}, [probe] { probe->core_outcome.release(); }, {},
            {Lane::Core, 0, core_key}));
        producer = std::thread{[probe] {
            for (int i{0}; i < 64; ++i) probe->callback(std::make_unique<Wallet>(probe, false));
            probe->callback(std::make_unique<Wallet>(probe, true));
        }};
        QVERIFY(probe->rejected_entered.tryAcquire(1, 5000));
        owner->retire(); probe->capture_release.release();
        // One Core worker posts registration's outcome before this independent
        // effect's outcome. Domain consumption of the latter is our barrier.
        QVERIFY(probe->core_outcome.tryAcquire(1, 5000));
        // This Cleanup turn is posted after registration's outcome is consumed.
        // Any prematurely eligible parent Cleanup must run before the witness.
        QVERIFY(scheduler->activateCleanup(witness.id));
        QVERIFY(probe->witness.tryAcquire(1, 5000));
        QCOMPARE(probe->disconnected.load(), 0);
        QCOMPARE(probe->destroyed.load(), 0);
        probe->rejected_release.release(); producer.join();
        executor.shutdown(); QTRY_VERIFY(executor.isDrained());
        QCOMPARE(probe->destroyed.load(), 65);
        QCOMPARE(probe->disconnected.load(), 1);
        QVERIFY(probe->off_gui.load());
        QCOMPARE(scheduler->pending(), size_t{0});
    }
    void lateRefreshContinuationCannotReopenStoppingOrFailedOwner_data()
    {
        QTest::addColumn<bool>("failure");
        QTest::newRow("close-before-continuation") << false;
        QTest::newRow("failure-before-continuation") << true;
    }
    void lateRefreshContinuationCannotReopenStoppingOrFailedOwner()
    {
        QFETCH(bool, failure);
        Fixture f; QVERIFY(f.owner->start()); f.drain();
        if (failure) f.callback({}); else f.owner->retire();
        const auto before = f.env.scheduler->pending();
        // Resume a continuation that had passed its producer-side check before
        // close/failure. The real admission entry must reject it without work.
        f.owner->requestScan();
        QCOMPARE(f.env.scheduler->pending(), before);
        f.drain();
        QCOMPARE(f.count(Publication::Ready), 1); QCOMPARE(f.count(Publication::Changed), 0);
        QCOMPARE(f.count(Publication::Failed), failure ? 1 : 0);
        QCOMPARE(f.count(Publication::Retired), 1); QCOMPARE(f.probe->captures.size(), size_t{1});
        QVERIFY(f.env.quiescent());
    }
    void retirementDuringRefreshAdmissionPreservesOwnedOutcomes_data()
    {
        QTest::addColumn<bool>("accept");
        QTest::newRow("accepted-before-close") << true;
        QTest::newRow("rejected-during-close") << false;
    }
    void retirementDuringRefreshAdmissionPreservesOwnedOutcomes()
    {
        QFETCH(bool, accept);
        auto limits = execution::Limits::Bounded();
        if (!accept) limits.observations.tasks = 0;
        Fixture f{limits}; QVERIFY(f.owner->start()); f.drain();
        std::thread stopper;
        const auto join = qScopeGuard([&] { if (stopper.joinable()) stopper.join(); });
        f.boundary->after_submit = [&] {
            stopper = std::thread{[&] { f.owner->retire(); }};
            // retire sets this atomic intent before waiting for the mailbox
            // mutex held at admission. The observed flag is the barrier;
            // the timeout only detects lack of progress.
            QVERIFY(QTest::qWaitFor([&] { return f.owner->m_stopping.load(); }, 5000));
        };
        f.notify("closing"); stopper.join(); f.drain();
        QCOMPARE(f.count(Publication::Changed), 0); QCOMPARE(f.count(Publication::Failed), 0);
        QCOMPARE(f.count(Publication::Retired), 1); QCOMPARE(f.probe->captures.size(), size_t{1});
        QVERIFY(f.probe->names.empty()); QCOMPARE(f.probe->destroyed.size(), size_t{1});
        QCOMPARE(f.probe->destroyed.front().second, std::optional<Lane>{Lane::Cleanup});
        QVERIFY(f.env.quiescent());
    }
    void retirementBetweenCleanupReservationAndRegistration()
    {
        Fixture f;
        f.boundary->after_reserve = [&] { f.owner->retire(); };
        QCOMPARE(f.owner->start().rejection, execution::Rejection::Closed);
        f.drain();
        QVERIFY(f.probe->registrations.empty()); QVERIFY(f.probe->captures.empty());
        QCOMPARE(f.count(Publication::Ready), 0); QCOMPARE(f.count(Publication::Retired), 1);
        QVERIFY(f.env.quiescent());
    }
    void handoffAdmissionRacesRetainEveryHandle_data()
    {
        QTest::addColumn<int>("boundary");
        QTest::newRow("close-after-parent-lease") << 0;
        QTest::newRow("close-after-disposal-reservation") << 1;
        QTest::newRow("close-after-disposal-lease") << 2;
        QTest::newRow("disposal-lease-saturated") << 3;
    }
    void handoffAdmissionRacesRetainEveryHandle()
    {
        QFETCH(int, boundary);
        auto limits = execution::Limits::Bounded();
        if (boundary == 3) limits.resource_borrowers = 1;
        Fixture f{limits}; f.catalog = {"never-exposed"};
        QVERIFY(f.owner->start()); f.drain();
        if (boundary == 0) f.boundary->after_borrow = [&] { f.owner->retire(); };
        if (boundary == 1) f.boundary->after_reserve = [&] { f.owner->retire(); f.env.scheduler->closeAdmission(); };
        if (boundary == 2) f.boundary->after_reserve = [&] {
            f.boundary->after_borrow = [&] { f.owner->retire(); };
        };
        QVERIFY(!f.owner->takeInitial());
        QVERIFY(f.probe->destroyed.empty());
        f.drain();
        QCOMPARE(f.count(Publication::Retired), 1);
        QCOMPARE(f.count(Publication::Failed), boundary == 3 ? 1 : 0);
        QCOMPARE(f.probe->destroyed.size(), size_t{1});
        QCOMPARE(f.probe->destroyed.front().second, std::optional<Lane>{Lane::Cleanup});
        QVERIFY(f.env.quiescent());
    }
    void callbackRetirementAfterItsLeaseDisposesBeforeCleanup()
    {
        Fixture f; QVERIFY(f.owner->start()); f.drain();
        f.boundary->after_borrow = [&] { f.owner->retire(); };
        f.notify("retired-in-callback");
        QCOMPARE(f.probe->destroyed.size(), size_t{1});
        QCOMPARE(f.probe->destroyed.front().second, std::optional<Lane>{Lane::Core});
        f.drain(); QCOMPARE(f.count(Publication::Changed), 0);
        QCOMPARE(f.count(Publication::Retired), 1); QVERIFY(f.env.quiescent());
    }
    void repeatedFailureBeforeControlConsumptionCoalesces()
    {
        Fixture f; QVERIFY(f.owner->start()); f.drain();
        f.callback({}); f.callback({}); f.notify("after-failure-fence");
        QVERIFY(!f.owner->takeInitial());
        f.drain();
        QCOMPARE(f.count(Publication::Failed), 1); QCOMPARE(f.count(Publication::Retired), 1);
        QCOMPARE(f.probe->destroyed.size(), size_t{1});
        QCOMPARE(f.probe->captures.size(), size_t{1}); QVERIFY(f.env.quiescent());
    }
    void initialClaimWaitsForRegistrationOutcome()
    {
        Fixture f; f.catalog = {"initial"};
        QVERIFY(f.owner->start()); QVERIFY(f.step(Lane::Core));
        QVERIFY(!f.owner->takeInitial());
        f.drain(); auto handoff = f.owner->takeInitial();
        QVERIFY(handoff); QCOMPARE(handoff->entries.front().name, std::string{"initial"});
        handoff.reset(); f.owner->retire(); f.drain(); QVERIFY(f.env.quiescent());
    }
    void notificationDuringMetadataOwnsANewScanAfterTheCurrentOutcome()
    {
        Fixture f; QVERIFY(f.owner->start()); f.drain();
        f.probe->during_name = [&] { f.notify("second"); };
        f.notify("first"); f.drain();
        auto handoff = f.owner->takeNotifications(); QVERIFY(handoff);
        QCOMPARE(handoff->entries.size(), size_t{2});
        QCOMPARE(f.probe->captures.size(), size_t{1});
        QCOMPARE(f.probe->names.size(), size_t{2});
        QCOMPARE(f.count(Publication::Changed), 1);
        handoff.reset(); f.owner->retire(); f.drain(); QVERIFY(f.env.quiescent());
    }
    void retirementAfterDispatchBeforeScanStartsCannotPublish()
    {
        Fixture f; QVERIFY(f.owner->start()); f.drain();
        *f.boundary->before_execute = [&] { f.owner->retire(); };
        f.notify("retired-before-scan-body"); f.drain();
        QVERIFY(f.probe->names.empty());
        QCOMPARE(f.count(Publication::Changed), 0); QCOMPARE(f.count(Publication::Retired), 1);
        QCOMPARE(f.probe->destroyed.front().second, std::optional<Lane>{Lane::Core});
        QVERIFY(f.env.quiescent());
    }
    void missingPortsAreRejected_data()
    {
        QTest::addColumn<int>("missing");
        for (int i{0}; i < 4; ++i) QTest::newRow(QByteArray::number(i).constData()) << i;
    }
    void missingPortsAreRejected()
    {
        QFETCH(int, missing);
        ManualRoutedExecution env;
        auto scheduler = std::static_pointer_cast<execution::Scheduler>(env.scheduler);
        walletlife::LoadSubscription::Register registration = [](auto) { return interfaces::MakeCleanupHandler([] {}); };
        walletlife::LoadSubscription::Capture capture = [] { return std::vector<std::unique_ptr<interfaces::Wallet>>{}; };
        walletlife::LoadSubscription::Notify notify = [](auto) {};
        if (missing == 0) scheduler.reset();
        if (missing == 1) registration = {};
        if (missing == 2) capture = {};
        if (missing == 3) notify = {};
        bool rejected{false};
        try { walletlife::LoadSubscription owner{scheduler, registration, capture, notify}; }
        catch (const std::invalid_argument&) { rejected = true; }
        QVERIFY(rejected);
        QVERIFY(env.quiescent());
    }
    void repeatedStartAndLateCallbackCannotResurrectSubscription()
    {
        Fixture f;
        QVERIFY(f.owner->start());
        QCOMPARE(f.owner->start().rejection, execution::Rejection::Invalid);
        f.drain();
        f.owner->retire(); f.drain();
        QCOMPARE(f.owner->start().rejection, execution::Rejection::Closed);
        f.owner.reset();
        f.notify("after-owner-loss");
        QCOMPARE(f.count(Publication::Ready), 1);
        QCOMPARE(f.count(Publication::Retired), 1);
        QCOMPARE(f.probe->destroyed.size(), size_t{1});
        QCOMPARE(f.probe->destroyed.front().second, std::optional<Lane>{Lane::Core});
        QVERIFY(f.env.quiescent());
    }
    void invalidSnapshotFailsAndDisconnectsProducer()
    {
        Fixture f; f.invalid_snapshot = true;
        QVERIFY(f.owner->start()); f.drain();
        QCOMPARE(f.count(Publication::Ready), 0);
        QCOMPARE(f.count(Publication::Failed), 1);
        QCOMPARE(f.count(Publication::Retired), 1);
        QCOMPARE(f.probe->disconnects.size(), size_t{1});
        QVERIFY(f.env.quiescent());
    }
    void nullNotificationOwnsFailureWithoutRecapture()
    {
        Fixture f;
        QVERIFY(f.owner->start()); f.drain();
        f.callback({}); f.drain();
        QCOMPARE(f.count(Publication::Failed), 1);
        QCOMPARE(f.count(Publication::Retired), 1);
        QCOMPARE(f.probe->captures.size(), size_t{1});
        QCOMPARE(f.publications.back().reason, walletlife::FailureReason::Backend);
        QVERIFY(f.env.quiescent());
    }
    void ordinarySaturationBeforeRegistrationOwnsFailure()
    {
        auto limits = execution::Limits::Bounded(); limits.ordinary.tasks = 0;
        Fixture f{limits};
        QCOMPARE(f.owner->start().rejection, execution::Rejection::Capacity);
        f.drain();
        QVERIFY(f.probe->registrations.empty()); QVERIFY(f.probe->captures.empty());
        QCOMPARE(f.count(Publication::Failed), 1);
        QCOMPARE(f.count(Publication::Retired), 1);
        QVERIFY(f.env.quiescent());
    }
    void rejectedRefreshHasAnOwnedTerminalOutcome_data()
    {
        QTest::addColumn<bool>("control_capacity");
        QTest::newRow("control-available") << true;
        QTest::newRow("control-saturated") << false;
    }
    void rejectedRefreshHasAnOwnedTerminalOutcome()
    {
        QFETCH(bool, control_capacity);
        auto limits = execution::Limits::Bounded(); limits.observations.tasks = 0;
        if (!control_capacity) limits.control.tasks = 0;
        Fixture f{limits};
        QVERIFY(f.owner->start()); f.drain();
        f.notify("rejected-refresh"); f.drain();
        QCOMPARE(f.probe->captures.size(), size_t{1});
        QCOMPARE(f.count(Publication::Failed), 1);
        QCOMPARE(f.count(Publication::Retired), 1);
        QCOMPARE(f.probe->destroyed.size(), size_t{1});
        QCOMPARE(f.probe->destroyed.front().second, std::optional<Lane>{Lane::Cleanup});
        QVERIFY(f.env.quiescent());
    }
    void callbackLeaseSaturationReportsUnavailableResync()
    {
        auto limits = execution::Limits::Bounded(); limits.resource_borrowers = 0;
        Fixture f{limits};
        QVERIFY(f.owner->start()); f.drain();
        f.notify("lease-rejected"); f.drain();
        QCOMPARE(f.count(Publication::Failed), 1);
        QCOMPARE(f.publications.back().reason, walletlife::FailureReason::IngressCapacity);
        QCOMPARE(f.probe->destroyed.front().second, std::optional<Lane>{Lane::Core});
        QVERIFY(f.env.quiescent());
    }
    void presentationLeaseSaturationRetiresTheSubscription()
    {
        auto limits = execution::Limits::Bounded(); limits.resource_borrowers = 0;
        Fixture f{limits}; f.catalog = {"unclaimed"};
        QVERIFY(f.owner->start()); f.drain();
        QVERIFY(!f.owner->takeInitial()); f.drain();
        QCOMPARE(f.count(Publication::Failed), 1);
        QCOMPARE(f.count(Publication::Retired), 1);
        QCOMPARE(f.probe->destroyed.front().second, std::optional<Lane>{Lane::Cleanup});
        QVERIFY(f.env.quiescent());
    }
    void schedulerClosureFencesProducerBeforeCleanupExecutes()
    {
        Fixture f;
        QVERIFY(f.owner->start()); f.drain();
        f.env.scheduler->closeAdmission();
        f.notify("after-fence"); f.drain();
        QCOMPARE(f.count(Publication::Changed), 0);
        QCOMPARE(f.count(Publication::Retired), 1);
        QCOMPARE(f.probe->destroyed.front().second, std::optional<Lane>{Lane::Core});
        QVERIFY(f.env.quiescent());
    }
    void overflowNeverSubstitutesTheStartupGetter_data()
    {
        QTest::addColumn<bool>("catalog_empty");
        QTest::newRow("empty-getter") << true;
        QTest::newRow("getter-has-unrelated-wallet") << false;
    }
    void overflowNeverSubstitutesTheStartupGetter()
    {
        QFETCH(bool, catalog_empty);
        Fixture f;
        QVERIFY(f.owner->start()); f.drain();
        if (!catalog_empty) f.catalog = {"unrelated"};
        f.owner->setNotificationContext({}, true);
        for (int i{0}; i < 70; ++i) f.notify(manualexecution_detail::Number(i));
        QVERIFY(!f.owner->takeNotifications());
        f.drain();
        QCOMPARE(f.count(Publication::Failed), 1);
        QCOMPARE(f.count(Publication::Retired), 1);
        QCOMPARE(f.publications.back().reason, walletlife::FailureReason::IngressCapacity);
        QCOMPARE(f.probe->captures.size(), size_t{1});
        QCOMPARE(f.probe->destroyed.size(), size_t{70});
        QVERIFY(f.env.quiescent());
    }
    void repeatedNotificationsKeepOnePendingPublication()
    {
        Fixture f;
        QVERIFY(f.owner->start()); f.drain();
        for (int i{0}; i < 256; ++i) { f.notify("same-wallet"); f.drain(); }
        QCOMPARE(f.count(Publication::Changed), 1);
        QCOMPARE(f.probe->destroyed.size(), size_t{255});
        auto handoff = f.owner->takeNotifications();
        QVERIFY(handoff); QCOMPARE(handoff->entries.size(), size_t{1});
        handoff.reset(); f.drain();
        QCOMPARE(f.count(Publication::Changed), 2);
        auto empty = f.owner->takeNotifications();
        QVERIFY(empty); QVERIFY(empty->entries.empty());
        f.notify("new-wallet"); f.drain();
        QCOMPARE(f.count(Publication::Changed), 3);
        auto next = f.owner->takeNotifications();
        QVERIFY(next); QCOMPARE(next->entries.front().name, std::string{"new-wallet"});
        next.reset(); f.drain();
    }
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
        f.catalog.emplace_back("pending-add"); // AddWallet commits with no later notification.
        bool observed{false};
        size_t claimed{0};
        for (;;) {
            auto handoff = f.owner->takeNotifications();
            if (!handoff || handoff->entries.empty()) break;
            for (const auto& entry : handoff->entries) { observed |= entry.name == "pending-add"; ++claimed; }
            handoff.reset(); f.drain();
        }
        QVERIFY2(observed, "The notification handle must preserve the pre-AddWallet discovery");
        QCOMPARE(claimed, size_t{65});
        QCOMPARE(f.count(Publication::Failed), 0);
        QCOMPARE(f.probe->captures.size(), size_t{1});
        f.owner->retire(); f.drain();
        QVERIFY(f.env.quiescent());
    }
    void announcedWalletsRemainModelStateWithoutRecapture()
    {
        Fixture f;
        QVERIFY(f.owner->start()); f.drain();
        for (int i{0}; i < 96; ++i) {
            const auto name = std::string{"wallet-"} + manualexecution_detail::Number(i);
            f.notify(name);
            f.catalog.push_back(name);
            f.drain();
        }
        QCOMPARE(f.probe->captures.size(), size_t{1});
        QCOMPARE(f.count(Publication::Failed), 0);
        QCOMPARE(f.count(Publication::Changed), 1);
        QCOMPARE(f.env.scheduler->pending(), size_t{1}); // Only the owner Cleanup reservation.
        size_t presented{0};
        for (;;) {
            auto handoff = f.owner->takeNotifications();
            QVERIFY(handoff);
            if (handoff->entries.empty()) break;
            QVERIFY(handoff->entries.size() <= 16);
            presented += handoff->entries.size();
            handoff.reset(); f.drain();
        }
        QCOMPARE(presented, size_t{96});
        QCOMPARE(f.probe->captures.size(), size_t{1});
        QCOMPARE(f.probe->destroyed.size(), size_t{96});
        f.owner->retire(); f.drain();
        QVERIFY(f.env.quiescent());
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
    void capturedQStringContextKeepsOriginalUtf8AndRetiresNatively()
    {
        Fixture f;QVERIFY(f.owner->start());f.drain();
        QString source=QStringLiteral("  ")+QString::fromUtf8("pending-₿-")+QString(40'000,QLatin1Char('x'))+QStringLiteral("  ");const auto native=source.trimmed().toStdString();
        QVERIFY(f.owner->setNotificationContext(source,true,71));source.fill(QLatin1Char('z'));
        f.notify(native);f.notify("independent");QVERIFY(f.owner->setNotificationContext({},false,72));f.drain();
        auto handoff=f.owner->takeNotifications();QVERIFY(handoff);QCOMPARE(handoff->entries.size(),size_t{1});QCOMPARE(handoff->entries.front().name,std::string{"independent"});
        QVERIFY(!handoff->entries.front().select_wallet);QCOMPARE(handoff->entries.front().selection_revision,uint64_t{71});
        handoff.reset();f.drain();
        {std::lock_guard lock{f.owner->m_context_inventory->mutex};QCOMPARE(f.owner->m_context_inventory->count,size_t{1});}
        f.owner->retire();f.drain();
        {std::lock_guard lock{f.owner->m_context_inventory->mutex};QCOMPARE(f.owner->m_context_inventory->count,size_t{0});QCOMPARE(f.owner->m_context_inventory->bytes,size_t{0});}
    }
    void whitespaceContextDoesNotHideTheUnnamedWallet()
    {
        Fixture f;QVERIFY(f.owner->start());f.drain();QVERIFY(f.owner->setNotificationContext(QStringLiteral("   "),true,83));f.notify("");f.drain();
        auto handoff=f.owner->takeNotifications();QVERIFY(handoff);QCOMPARE(handoff->entries.size(),size_t{1});QVERIFY(handoff->entries.front().name.empty());QCOMPARE(handoff->entries.front().selection_revision,uint64_t{83});
    }
    void notificationContextCapacityFailureFencesStaleOrigin_data()
    {
        QTest::addColumn<int>("capacity");QTest::newRow("context-count")<<0;QTest::newRow("retained-byte-weight")<<1;QTest::newRow("oversized-original-literal")<<2;
    }
    void notificationContextCapacityFailureFencesStaleOrigin()
    {
        QFETCH(int,capacity);Fixture f;QVERIFY(f.owner->start());f.drain();
        if(capacity==0){for(int index=0;index<128;++index)QVERIFY(f.owner->setNotificationContext(QString::number(index),true,index));QVERIFY(!f.owner->setNotificationContext("overflow",false,999));}
        if(capacity==1){QVERIFY(f.owner->setNotificationContext(QString(300'000,QLatin1Char('x')),true,1));QVERIFY(!f.owner->setNotificationContext(QString(300'000,QLatin1Char('y')),false,999));}
        if(capacity==2){QVERIFY(f.owner->setNotificationContext("old-origin",true,1));QVERIFY(!f.owner->setNotificationContext(QString(600'000,QLatin1Char('x')),false,999));}
        f.notify("must-not-use-old-origin");f.drain();QVERIFY(!f.owner->takeNotifications());
        QVERIFY(std::any_of(f.publications.begin(),f.publications.end(),[](const auto& update){return update.reason==walletlife::FailureReason::IngressCapacity;}));
        {std::lock_guard lock{f.owner->m_context_inventory->mutex};QCOMPARE(f.owner->m_context_inventory->count,size_t{0});QCOMPARE(f.owner->m_context_inventory->bytes,size_t{0});}
    }
    void notificationContextCleanupRejectionDoesNotCaptureOriginal()
    {
        auto limits=execution::Limits::Bounded();limits.cleanup_reservations.tasks=1;Fixture f{limits};QVERIFY(f.owner->start());f.drain();
        QVERIFY(!f.owner->setNotificationContext(QString(40'000,QLatin1Char('x')),true,1));
        {std::lock_guard lock{f.owner->m_context_inventory->mutex};QCOMPARE(f.owner->m_context_inventory->count,size_t{0});}
        f.drain();QVERIFY(!f.owner->takeNotifications());
        QVERIFY(std::any_of(f.publications.begin(),f.publications.end(),[](const auto& update){return update.reason==walletlife::FailureReason::IngressCapacity;}));
    }
    void opaqueAdoptionContextKeepsOriginalIdentityAndNativeRetirement()
    {
        Fixture f;
        QVERIFY(f.owner->start());
        f.drain();
        std::optional<Lane> destroyed;
        auto origin = std::shared_ptr<void>(new uint64_t{91}, [&](void* value) {
            destroyed = f.probe->lane;
            delete static_cast<uint64_t*>(value);
        });
        const std::weak_ptr<void> original = origin;
        QVERIFY(f.owner->setNotificationContext({}, false, 1, origin));
        f.notify("original-context");
        QVERIFY(f.owner->setNotificationContext({}, false, 2, {}));
        origin.reset();
        f.drain();
        auto handoff = f.owner->takeNotifications();
        QVERIFY(handoff);
        QCOMPARE(handoff->entries.size(), size_t{1});
        QVERIFY(handoff->entries.front().adoption_origin == original.lock());
        QCOMPARE(handoff->entries.front().selection_revision, uint64_t{1});
        QVERIFY(!original.expired());
        handoff.reset();
        f.drain();
        QVERIFY(original.expired());
        QVERIFY(destroyed == Lane::Core || destroyed == Lane::Cleanup);
    }

    void notificationRetainsTheProducingCommandContext()
    {
        Fixture f;
        QVERIFY(f.owner->start()); f.drain();
        f.owner->setNotificationContext("unfinished", true, 41);
        f.notify("unfinished");
        f.notify("unrelated");
        f.owner->setNotificationContext({}, false, 42);
        f.drain();
        auto handoff = f.owner->takeNotifications();
        QVERIFY(handoff); QCOMPARE(handoff->entries.size(), size_t{1});
        QCOMPARE(handoff->entries.front().name, std::string{"unrelated"});
        QVERIFY(!handoff->entries.front().select_wallet);
        QCOMPARE(handoff->entries.front().selection_revision, uint64_t{41});
        QCOMPARE(f.probe->destroyed.size(), size_t{1});
        QCOMPARE(f.probe->destroyed.front().first, std::string{"unfinished"});
        QCOMPARE(f.probe->destroyed.front().second, std::optional<Lane>{Lane::Core});
        handoff.reset(); f.drain();
        f.notify("after-command"); f.drain();
        handoff = f.owner->takeNotifications();
        QVERIFY(handoff); QCOMPARE(handoff->entries.size(), size_t{1});
        QVERIFY(handoff->entries.front().select_wallet);
        QCOMPARE(handoff->entries.front().selection_revision, uint64_t{42});
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
            for (const auto& event : f.env.traceSnapshot()) trace.append(QString::fromStdString(event));
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
        for (const auto& event : f.env.traceSnapshot()) QVERIFY(event.find("DO_NOT_TRACE") == std::string::npos);
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
    void overflowInvalidatesAnEarlierUnclaimedReadyInventory()
    {
        Fixture f;
        QVERIFY(f.owner->start()); f.drain();
        f.notify("earlier-ready"); f.drain();
        for (int i{0}; i < 100; ++i) f.notify("notification-" + manualexecution_detail::Number(i));
        QVERIFY(!f.owner->takeNotifications());
        f.drain();
        QCOMPARE(f.count(Publication::Failed), 1);
        QCOMPARE(f.count(Publication::Retired), 1);
        QCOMPARE(f.probe->captures.size(), size_t{1});
        QCOMPARE(f.probe->destroyed.size(), size_t{101});
        QCOMPARE(f.count(Publication::Changed), 1); // Only the earlier inventory's wakeup.
        QVERIFY(f.env.quiescent());
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
        // Handoffs reserve idle ownership; runnable Cleanup turns have a separate budget.
        auto limits = execution::Limits::Bounded(); limits.cleanup_reservations.tasks = 1;
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
