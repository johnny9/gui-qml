// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/backendexecutor.h>
#include <qml/models/walletqmlmodel.h>
#include <qml/walletopening.h>
#include <test/mocks/manualexecutionenvironment.h>
#include <test/mocks/manualwalletdrain.h>
#include <test/mocks/feepreviewchain.h>
#include <test/mocks/mockwallet.h>
#include <test/qt_test_registry.h>

#include <QScopeGuard>
#include <QSemaphore>
#include <QtTest/QtTest>

#include <stdexcept>

namespace {
using execution::Lane;
using walletopening::Phase;
struct Wallet final : StubWallet {
    std::string getWalletName() override { ++reads; if (fail_read) throw std::runtime_error("synthetic metadata failure"); return "original"; }
    bool privateKeysDisabled() override { return no_keys; }
    bool hasExternalSigner() override { return external; }
    bool isCrypted() override { return true; }
    bool isLocked() override { return locked; }
    bool canGetAddresses() override { return addresses; }
    bool taprootEnabled() override { return taproot; }
    std::unique_ptr<interfaces::Handler> handleCanGetAddressesChanged(CanGetAddressesChangedFn fn) override { address_capability = std::move(fn); return {}; }
    std::unique_ptr<interfaces::Handler> handleStatusChanged(StatusChangedFn fn) override { status = std::move(fn); return {}; }
    std::unique_ptr<interfaces::Handler> handleUnload(UnloadFn fn) override { unload = std::move(fn); return {}; }
    std::unique_ptr<interfaces::Handler> handleTransactionChanged(TransactionChangedFn fn) override { transaction = std::move(fn); return {}; }
    int reads{0};
    bool fail_read{false};
    bool no_keys{false}, external{false}, locked{true}, addresses{true}, taproot{true};
    CanGetAddressesChangedFn address_capability;
    StatusChangedFn status;
    UnloadFn unload;
    TransactionChangedFn transaction;
};
struct Fixture {
    ManualRoutedExecution commands, retirement;
    std::shared_ptr<Wallet> wallet{std::make_shared<Wallet>()};
    int wakes{0};
    std::shared_ptr<walletopening::Session> session{std::make_shared<walletopening::Session>(
        commands.scheduler, retirement.scheduler, std::make_shared<std::atomic_bool>(false), [this] { ++wakes; })};
    ~Fixture() { session->retire(); commands.drain(); retirement.drain(); }
    void start()
    {
        QVERIFY(session->start(wallet));
        commands.drain();
        QVERIFY(session->take()->metadata.has_value());
    }
    walletopening::Question question()
    {
        session->requestClose();
        commands.drain();
        const auto update = session->take();
        if (!update || !update->question) { QTest::qFail("Missing close question", __FILE__, __LINE__); return {}; }
        return *update->question;
    }
};
}

class WalletOpeningTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void originalActionScopeCompletesDependentEffectsAfterFullQueueClosure()
    {
        Fixture fixture; fixture.start();
        std::vector<QString> trace;
        auto action = fixture.session->startAction(); QVERIFY(action);
        auto session = fixture.session;
        QVERIFY(session->planAction(action, [session, action, &trace] {
            trace.emplace_back("plan-before-write");
            QVERIFY(session->perform(action, [&trace](interfaces::Wallet&) { trace.emplace_back("commit"); return true; },
                [session, action, &trace](bool committed) {
                    QVERIFY(committed); trace.emplace_back("actual-commit-outcome");
                    QVERIFY(session->planAction(action, [session, action, &trace] {
                        trace.emplace_back("plan-original-notes");
                        QVERIFY(session->perform(action, [&trace](interfaces::Wallet&) { trace.emplace_back("notes-write"); return true; },
                            [&trace](bool saved) { QVERIFY(saved); trace.emplace_back("actual-notes-outcome"); }));
                    }));
                }));
        }));
        fixture.session->closeAdmission(); fixture.commands.scheduler->closeAdmission();
        fixture.session->retire(); QVERIFY(!fixture.session->startAction()); action.reset();
        QVERIFY(!fixture.retirement.step(Lane::Cleanup)); // Original accepted action still pins its native wallet.
        QCOMPARE(trace.size(), size_t{0});
        fixture.commands.drain();
        QCOMPARE(trace, std::vector<QString>({"plan-before-write", "commit", "actual-commit-outcome", "plan-original-notes", "notes-write", "actual-notes-outcome"}));
        fixture.retirement.drain();
        QCOMPARE(fixture.session->take()->phase, Phase::Closed);
        QVERIFY(fixture.commands.quiescent()); QVERIFY(fixture.retirement.quiescent());
    }
    void facadeSeparatesSecurityAdmissionEffectOutcomeAndPublication()
    {
        struct Native final : StubWallet {
            std::string getWalletName() override { return "original"; }
            bool isCrypted() override { return encrypted; }
            bool isLocked() override { return encrypted; }
            bool encryptWallet(const SecureString& password) override { ++effects; encrypted = password == SecureString{"secret"}; return encrypted; }
            int effects{0}; bool encrypted{false};
        };
        ManualRoutedExecution commands;
        auto native = std::make_shared<Native>();
        WalletQmlModel model{WalletQmlModel::SharedWallet{native, commands.scheduler}, nullptr,
                             FeeReadServices{}, psbtfee::Services{}, backup::Services{}};
        ManualWalletDrain cleanup{commands, &model};
        for (int n{0}; n < 4; ++n) { commands.drain(); QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall); }
        QVERIFY(model.capabilitiesKnown());
        QVERIFY(model.encryptWallet("secret"));
        QVERIFY(!model.encryptWallet("duplicate"));
        QCOMPARE(native->effects, 0);
        QCOMPARE(model.securityState(), WalletQmlModel::SecurityState::SecurityPending);
        QVERIFY(commands.step(Lane::Core));
        QCOMPARE(native->effects, 1);
        QVERIFY(!model.isEncrypted());
        QVERIFY(commands.step(Lane::Domain));
        QCOMPARE(model.securityState(), WalletQmlModel::SecurityState::SecurityPending);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QCOMPARE(model.securityState(), WalletQmlModel::SecurityState::SecuritySucceeded);
        QVERIFY(model.isEncrypted()); QVERIFY(model.isLocked());
        commands.drain();
    }
    void actualSecurityActionInvalidatesOlderCloseConsent()
    {
        struct Native final : StubWallet {
            std::string getWalletName() override { return "original"; }
            bool encryptWallet(const SecureString&) override { ++effects; return true; }
            int effects{0};
        };
        ManualRoutedExecution commands;
        auto native = std::make_shared<Native>();
        WalletQmlModel model{WalletQmlModel::SharedWallet{native, commands.scheduler}, nullptr,
                             FeeReadServices{}, psbtfee::Services{}, backup::Services{}};
        ManualWalletDrain cleanup{commands, &model};
        for (int n{0}; n < 4; ++n) { commands.drain(); QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall); }
        model.requestClose(); commands.drain(); QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        const auto original_question = model.closeQuestionToken();
        QVERIFY(!original_question.isEmpty());
        QVERIFY(model.available());
        QVERIFY(model.encryptWallet("secret"));
        model.answerClose(original_question, true);
        commands.drain(); QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QCOMPARE(native->effects, 1);
        QVERIFY(model.available());
        QVERIFY(!model.closeQuestionToken().isEmpty());
        QVERIFY(model.closeQuestionToken() != original_question);
        model.answerClose(original_question, true); commands.drain();
        QVERIFY(model.available());
    }
    void retirementClosingDuringStartupDoesNotAcquireNativeOwnership()
    {
        struct Closing final : execution::Scheduler {
            explicit Closing(std::shared_ptr<execution::TaskQueue> queue) : queue(std::move(queue)) {}
            execution::Admission submit(std::shared_ptr<execution::Task> task, execution::TaskSpec spec) override { return queue->submit(std::move(task), spec); }
            execution::Admission reserveCleanup(std::shared_ptr<execution::Task> task, execution::TaskSpec spec) override
            {
                const auto result = queue->reserveCleanup(std::move(task), spec);
                queue->closeAdmission();
                return result;
            }
            bool activateCleanup(execution::TaskId id) override { return queue->activateCleanup(id); }
            execution::BorrowAdmission borrow(execution::ResourceKey key) override { return queue->borrow(key); }
            execution::ResourceKey allocateKey() override { return queue->allocateKey(); }
            size_t pending() const override { return queue->pending(); }
            void closeOrdinaryAdmission() override { queue->closeOrdinaryAdmission(); }
            bool cancelQueued(execution::TaskId id) override { return queue->cancelQueued(id); }
            const std::shared_ptr<execution::TaskQueue> queue;
        };
        ManualRoutedExecution commands, retirement;
        auto native = std::make_shared<Wallet>();
        auto session = std::make_shared<walletopening::Session>(commands.scheduler, std::make_shared<Closing>(retirement.scheduler),
            std::make_shared<std::atomic_bool>(false), [] {});
        const auto started = session->start(native);
        QCOMPARE(started.admission.rejection, execution::Rejection::Closed);
        QVERIFY(!started.borrower);
        QCOMPARE(native.use_count(), 1L);
        QCOMPARE(native->reads, 0);
        retirement.drain(); commands.drain();
        QVERIFY(retirement.quiescent());
        QCOMPARE(session->take()->phase, Phase::Closed);
    }
    void startupFailureCannotRetireTheReturnedClientBeforeHandoff()
    {
        struct BrokenWallet final : StubWallet {
            std::string getWalletName() override { throw std::runtime_error("synthetic startup capture failure"); }
        };
        ManualRoutedExecution commands, retirement;
        auto native = std::make_shared<BrokenWallet>();
        auto session = std::make_shared<walletopening::Session>(commands.scheduler, retirement.scheduler,
            std::make_shared<std::atomic_bool>(false), [] {});
        auto started = session->start(native);
        QVERIFY(started);
        QVERIFY(started.borrower);
        commands.drain();
        QVERIFY(!session->available());
        QVERIFY(!retirement.step(Lane::Cleanup));
        QCOMPARE(started.borrower.get(), native.get());
        started.borrower.reset();
        retirement.drain();
        QVERIFY(retirement.quiescent());
    }
    void duplicateOpeningStartDoesNotRetireItsLiveResource()
    {
        Fixture f; f.start();
        QCOMPARE(f.session->start(f.wallet).admission.rejection, execution::Rejection::Invalid);
        QVERIFY(f.session->available());
        QVERIFY(f.session->borrow());
    }
    void registrationAndCaptureWaitForCoreAndDomain()
    {
        Fixture f;
        QVERIFY(f.session->start(f.wallet));
        QCOMPARE(f.wallet->reads, 0);
        QVERIFY(!f.wallet->status);
        QVERIFY(!f.session->take());
        QVERIFY(f.commands.step(Lane::Core));
        QCOMPARE(f.wallet->reads, 1);
        QVERIFY(f.wallet->status);
        QCOMPARE(f.wakes, 0);
        QVERIFY(f.commands.step(Lane::Domain));
        QCOMPARE(f.wakes, 1);
        const auto update = f.session->take();
        QVERIFY(update && update->metadata);
        QCOMPARE(update->metadata->name, std::string{"original"});
        QVERIFY(update->metadata->encrypted);
        QVERIFY(update->metadata->locked);
    }
    void receiveCapabilitiesRefreshFromTheirActualProducer()
    {
        Fixture f; f.start();
        QVERIFY(f.wallet->address_capability);
        f.wallet->taproot = false;
        f.wallet->address_capability();
        QVERIFY(!f.session->take());
        f.commands.drain();
        const auto update = f.session->take();
        QVERIFY(update && update->metadata);
        QVERIFY(update->metadata->can_get_addresses);
        QVERIFY(!update->metadata->taproot);
    }
    void queuedPreferenceChangesKeepLatestOutcomePending()
    {
        ManualRoutedExecution commands;
        auto native = std::make_shared<Wallet>();
        std::string saved;
        int writes{0};
        walletopening::Session::Preferences preferences{
            [&](const std::string&) { return saved; },
            [&](const std::string&, const std::string& type) { saved = type; ++writes; return true; }};
        WalletQmlModel model{WalletQmlModel::SharedWallet{native, commands.scheduler, {}, std::move(preferences)}, nullptr,
                             FeeReadServices{}, psbtfee::Services{}, backup::Services{}};
        ManualWalletDrain cleanup{commands, &model};
        for (int n{0}; n < 4; ++n) { commands.drain(); QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall); }
        QVERIFY(model.setDefaultReceiveAddressType("legacy"));
        QVERIFY(model.setDefaultReceiveAddressType("bech32"));
        QCOMPARE(writes, 0);
        QVERIFY(commands.step(Lane::Core)); QVERIFY(commands.step(Lane::Domain));
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QCOMPARE(writes, 1);
        QVERIFY(model.property("receivePreferencePending").toBool());
        QVERIFY(commands.step(Lane::Core)); QVERIFY(commands.step(Lane::Domain));
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QCOMPARE(writes, 2);
        QVERIFY(!model.property("receivePreferencePending").toBool());
        QCOMPARE(saved, std::string{"bech32"});
        QCOMPARE(model.defaultReceiveAddressType(), QString{"bech32"});
    }
    void publicSettingsPortPreservesOtherWalletAndReportsWriteFailure_data()
    {
        QTest::addColumn<bool>("write_ok");
        QTest::newRow("persisted") << true;
        QTest::newRow("write-failed-memory-kept") << false;
    }
    void publicSettingsPortPreservesOtherWalletAndReportsWriteFailure()
    {
        QFETCH(bool, write_ok);
        struct Settings final : FeePreviewChain {
            common::SettingsValue getRwSetting(const std::string&) override { return values; }
            bool updateRwSetting(const std::string&, const interfaces::SettingsUpdate& update) override
            {
                ++writes;
                const auto action = update(values);
                return action && *action == interfaces::SettingsAction::WRITE && write_ok;
            }
            common::SettingsValue values{UniValue::VOBJ};
            int writes{0}; bool write_ok{true};
        } settings;
        settings.write_ok = write_ok;
        settings.values.pushKV("other-wallet", "bech32");
        ManualRoutedExecution commands, retirement;
        auto native = std::make_shared<Wallet>();
        auto session = std::make_shared<walletopening::Session>(commands.scheduler, retirement.scheduler,
            std::make_shared<std::atomic_bool>(false), [] {}, walletopening::ChainPreferences(settings));
        const auto cleanup = qScopeGuard([&] { session->retire(); commands.drain(); retirement.drain(); });
        QVERIFY(session->start(native)); commands.drain(); session->take();
        QVERIFY(session->setReceivePreference("legacy"));
        QCOMPARE(settings.writes, 0);
        commands.drain();
        const auto result = session->take();
        QVERIFY(result && result->preference_saved && result->metadata);
        QCOMPARE(*result->preference_saved, write_ok);
        QCOMPARE(result->metadata->receive_preference, std::string{"legacy"});
        QCOMPARE(settings.values["other-wallet"].get_str(), std::string{"bech32"});
        commands.drain(); QCOMPARE(settings.writes, 1);
    }
    void receivePreferenceSeparatesEffectOutcomeAndCachesReadback()
    {
        ManualRoutedExecution commands, retirement;
        auto native = std::make_shared<Wallet>();
        std::string saved;
        int writes{0};
        auto session = std::make_shared<walletopening::Session>(commands.scheduler, retirement.scheduler,
            std::make_shared<std::atomic_bool>(false), [] {}, walletopening::Session::Preferences{
                [&](const std::string&) { return saved; },
                [&](const std::string& name, const std::string& type) { if (name != "original") QTest::qFail("Preference retargeted", __FILE__, __LINE__); saved = type; ++writes; return true; }});
        const auto cleanup = qScopeGuard([&] { session->retire(); commands.drain(); retirement.drain(); });
        QVERIFY(session->start(native)); commands.drain(); session->take();
        QCOMPARE(session->setReceivePreference("invalid").rejection, execution::Rejection::Invalid);
        QVERIFY(session->setReceivePreference("p2sh-segwit"));
        QCOMPARE(writes, 0);
        QVERIFY(commands.step(Lane::Core));
        QCOMPARE(writes, 1); QCOMPARE(saved, std::string{"p2sh-segwit"});
        QVERIFY(!session->take());
        QVERIFY(commands.step(Lane::Domain));
        auto update = session->take();
        QVERIFY(update && update->preference_saved && *update->preference_saved && update->metadata);
        QCOMPARE(update->metadata->receive_preference, saved);
        native->taproot = false;
        QVERIFY(session->setReceivePreference("bech32m")); commands.drain();
        update = session->take();
        QVERIFY(update && update->preference_saved && !*update->preference_saved);
        QCOMPARE(writes, 1); QCOMPARE(saved, std::string{"p2sh-segwit"});
    }
    void closeQuestionDoesNotFenceNewActionsAndOldConsentCannotClose()
    {
        Fixture f; f.start();
        const auto before = f.question();
        QVERIFY(f.session->available());
        QVERIFY(f.session->admitAction());
        f.session->answerClose(before, true);
        f.commands.drain();
        QVERIFY(f.session->available());
        const auto changed = f.session->take();
        QVERIFY(changed && changed->question);
        QVERIFY(changed->question->number != before.number);
        f.session->answerClose(before, true);
        f.commands.drain();
        QVERIFY(f.session->available());
        f.session->answerClose(*changed->question, false);
        f.commands.drain();
        const auto dismissed = f.session->take();
        QVERIFY(dismissed && !dismissed->question);
        QVERIFY(f.session->available());
    }
    void currentConsentFencesAdmissionAndWaitsForBorrower()
    {
        Fixture f; f.start();
        auto held = f.session->borrow(); QVERIFY(held);
        const auto question = f.question();
        f.session->answerClose(question, true);
        f.commands.drain();
        QVERIFY(!f.session->available());
        QVERIFY(!f.session->admitAction());
        QVERIFY(!f.session->borrow());
        QVERIFY(!f.retirement.step(Lane::Cleanup));
        held.reset();
        QVERIFY(f.retirement.step(Lane::Cleanup));
        const auto before_outcome = f.session->take();
        QVERIFY(before_outcome && before_outcome->phase == Phase::Closing);
        QVERIFY(f.retirement.step(Lane::Domain));
        const auto closed = f.session->take();
        QVERIFY(closed && closed->phase == Phase::Closed && !closed->question);
        QVERIFY(f.retirement.quiescent());
    }
    void rejectedLatePreferenceCannotReopenClosingPublication()
    {
        Fixture f; f.start();
        auto held = f.session->borrow();
        f.session->answerClose(f.question(), true); f.commands.drain();
        QVERIFY(!f.session->available());
        QCOMPARE(f.session->setReceivePreference("legacy").rejection, execution::Rejection::Closed);
        const auto update = f.session->take();
        QVERIFY(update && update->phase == Phase::Closing && !update->question);
        held.reset(); f.retirement.drain();
        QCOMPARE(f.session->take()->phase, Phase::Closed);
    }
    void oldQuestionCannotCloseReopenedWalletWithSameName()
    {
        Fixture old; old.start(); const auto question = old.question();
        old.session->retire(); old.retirement.drain();
        Fixture reopened; reopened.start();
        QVERIFY(old.session->identity() != reopened.session->identity());
        reopened.question();
        reopened.session->answerClose(question, true); reopened.commands.drain();
        QVERIFY(reopened.session->available());
        QVERIFY(reopened.session->take()->question.has_value());
    }
    void externalUnloadSupersedesQuestionBeforeGuiDelivery()
    {
        Fixture f; f.start(); const auto question = f.question();
        QVERIFY(f.wallet->unload);
        f.wallet->unload();
        QVERIFY(!f.session->available());
        f.session->answerClose(question, true);
        f.commands.drain(); f.retirement.drain();
        const auto update = f.session->take();
        QVERIFY(update && update->unloaded && update->phase == Phase::Closed && !update->question);
    }
    void admittedReadRetainsOutcomeUntilDomainConsumption()
    {
        Fixture f; f.start();
        int outcomes{0};
        QVERIFY(f.session->read([](interfaces::Wallet&) { return 42; }, [&](int value) { QCOMPARE(value, 42); ++outcomes; }));
        QVERIFY(f.commands.step(Lane::Core));
        f.session->retire();
        QVERIFY(!f.retirement.step(Lane::Cleanup));
        QCOMPARE(outcomes, 0);
        QVERIFY(f.commands.step(Lane::Domain));
        QCOMPARE(outcomes, 1);
        f.retirement.drain();
        QVERIFY(f.retirement.quiescent());
    }
    void rejectedNoticeRetainsReconciliationAndUsesControlledBackoff()
    {
        ManualRoutedExecution commands, retirement;
        auto native = std::make_shared<Wallet>();
        std::chrono::steady_clock::time_point now{};
        auto session = std::make_shared<walletopening::Session>(commands.scheduler, retirement.scheduler,
            std::make_shared<std::atomic_bool>(false), [] {}, walletopening::Session::Preferences{}, [&] { return now; });
        const auto cleanup = qScopeGuard([&] { session->retire(); commands.drain(); retirement.drain(); });
        QVERIFY(session->start(native)); commands.drain(); session->take();
        commands.rejectLane(Lane::Core);
        native->transaction(Txid::FromUint256(uint256::ONE), CT_NEW);
        QCOMPARE(native->reads, 1);
        QVERIFY(session->notificationsPending());
        QVERIFY(session->take()->failed);
        for (int n{0}; n < 100; ++n) { session->retryNotifications(); native->status(); }
        QVERIFY(commands.queuedIdle());
        now += std::chrono::milliseconds{999}; session->retryNotifications();
        QVERIFY(commands.queuedIdle());
        now += std::chrono::milliseconds{1}; session->retryNotifications();
        native->fail_read = true; commands.drain();
        QVERIFY(session->take()->failed);
        const auto before = native->reads;
        now += std::chrono::milliseconds{1999}; session->retryNotifications();
        QVERIFY(commands.queuedIdle()); QCOMPARE(native->reads, before);
        native->fail_read = false;
        now += std::chrono::milliseconds{1}; session->retryNotifications(); commands.drain();
        const auto recovered = session->take();
        QVERIFY(recovered && recovered->rescan && recovered->metadata);
        QVERIFY(session->notificationsPending());
        session->acknowledgeTransactions();
        QVERIFY(!session->notificationsPending());
    }
    void pendingNotificationsCoalesceWithoutGuiOrCapturePolling()
    {
        Fixture f; f.start();
        for (int n{0}; n < 100; ++n) f.wallet->status();
        QCOMPARE(f.wallet->reads, 1);
        f.commands.drain();
        const auto reads = f.wallet->reads;
        QVERIFY(reads <= 3);
        const auto update = f.session->take();
        QVERIFY(update && update->status && update->metadata);
        f.commands.drain();
        QCOMPARE(f.wallet->reads, reads);
    }

};

#include <test_walletopening.moc>
BITCOINQML_REGISTER_QT_TEST(WalletOpeningTests)
