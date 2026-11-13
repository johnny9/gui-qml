// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <chainparams.h>
#include <common/args.h>
#include <common/settings.h>
#include <interfaces/handler.h>
#include <interfaces/wallet.h>
#include <outputtype.h>
#include <qml/walletqmlcontroller.h>
#include <qml/models/walletqmlmodeltransaction.h>
#include <scheduler.h>
#include <script/descriptor.h>
#include <test/mocks/mocknode.h>
#include <test/mocks/manualexecutionenvironment.h>
#include <deque>
#include <util/string.h>
#include <util/translation.h>
#include <wallet/imports.h>
#include <wallet/types.h>
#include <wallet/walletutil.h>

#include <QEventLoop>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QScopeGuard>
#include <QSemaphore>
#include <QTemporaryDir>
#include <algorithm>
#include <set>
#include <vector>
#include <atomic>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

#ifndef BITCOINQML_NO_TEST_MAIN
const TranslateFn G_TRANSLATION_FUN{nullptr};
#endif

namespace {

class DrainingWalletController : public WalletQmlController
{
public:
    explicit DrainingWalletController(interfaces::Node& node) : DrainingWalletController(node, execution::Limits::Bounded()) {}
    DrainingWalletController(interfaces::Node& node, execution::Limits limits) : WalletQmlController(node, limits)
    {
        connect(this, &WalletQmlController::walletsDrained, this, [this] { m_drained = true; });
    }
    ~DrainingWalletController()
    {
        disconnect(this, nullptr, nullptr, nullptr);
        QSignalSpy drained{this, &WalletQmlController::walletsDrained};
        beginShutdown();
        if (!m_drained && drained.empty()) QVERIFY(drained.wait(10000));
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
private:
    bool m_drained{false};
};

bool AwaitWalletOperation(WalletQmlController& controller)
{
    return QTest::qWaitFor([&] { return controller.walletOperationState() != WalletQmlController::WalletOperationState::WalletOperationPending &&
        !controller.walletLoadInProgress() && !controller.walletMigrationInProgress() && !controller.walletImportErrorPending(); }, 10000);
}
bool PumpRetirementTurns(std::deque<std::function<void()>>& turns, const std::function<bool()>& complete)
{
    return QTest::qWaitFor([&] {
        if (!turns.empty()) {
            auto next = std::exchange(turns.front(), {});
            turns.pop_front();
            next();
        }
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        return complete();
    }, 10000);
}
void DrainControlledController(std::unique_ptr<WalletQmlController>& controller,
                               std::deque<std::function<void()>>& turns,
                               const std::weak_ptr<execution::Scheduler>& original)
{
    if (controller) controller->beginShutdown();
    controller.reset();
    QObject receiver;
    bool native_done{false};
    BackendExecutor::shutdownAll(&receiver, [&] { native_done = true; });
    QVERIFY(PumpRetirementTurns(turns, [&] { return native_done && turns.empty() && original.expired(); }));
}
bool CheckedXpub(WalletQmlController& controller, const QString& key)
{
    std::unique_ptr<WalletProvisioningCheck> check{controller.createProvisioningCheck()};
    if (!check) { QTest::qFail("Wallet check could not be created", __FILE__, __LINE__); return false; }
    check->setXpub(key);
    if (!QTest::qWaitFor([&] { return !check->pending(); }, 5000) || !check->ready()) {
        QTest::qFail("Extended public key check did not complete", __FILE__, __LINE__); return false;
    }
    return check->xpubValid();
}
QString CheckedWalletName(WalletQmlController& controller, const QString& name)
{
    std::unique_ptr<WalletProvisioningCheck> check{controller.createProvisioningCheck()};
    if (!check) { QTest::qFail("Wallet check could not be created", __FILE__, __LINE__); return QStringLiteral("Check unavailable"); }
    check->setName(name);
    if (!QTest::qWaitFor([&] { return !check->pending(); }, 5000) || !check->ready()) {
        QTest::qFail("Wallet name check did not complete", __FILE__, __LINE__); return QStringLiteral("Check did not complete");
    }
    return check->nameError();
}
bool AwaitWalletLocation(WalletQmlController& controller, WalletQmlModel* wallet)
{
    if (!controller.openWalletLocation(wallet)) return false;
    return QTest::qWaitFor([&] { return !controller.walletLocationPending(); }, 5000) && controller.walletLocationOpenError().isEmpty();
}

constexpr auto NOT_INITIALIZED_ERROR{"Wallets are still loading. Try again in a moment."};

std::unique_ptr<interfaces::Handler> MakeNoopHandler()
{
    return interfaces::MakeCleanupHandler([] {});
}

class FakeExternalSigner : public interfaces::ExternalSigner
{
public:
    explicit FakeExternalSigner(std::string name) : m_name(std::move(name)) {}
    std::string getName() override { return m_name; }

private:
    std::string m_name;
};

std::vector<std::unique_ptr<interfaces::ExternalSigner>> MakeSigners(std::initializer_list<const char*> names)
{
    std::vector<std::unique_ptr<interfaces::ExternalSigner>> signers;
    signers.reserve(names.size());
    for (const char* name : names) {
        signers.emplace_back(std::make_unique<FakeExternalSigner>(name));
    }
    return signers;
}

class FakeWalletLoader : public interfaces::WalletLoader
{
public:
    int create_wallet_calls{0};
    int migrate_wallet_calls{0};
    int is_encrypted_calls{0};
    int handle_load_wallet_calls{0};
    int get_wallets_calls{0};
    int list_wallet_dir_calls{0};
    int load_wallet_calls{0};
    std::string wallet_dir;

    std::function<util::Result<std::unique_ptr<interfaces::Wallet>>(const std::string&, const SecureString&, uint64_t, std::vector<bilingual_str>&)>
        create_wallet_fn = [](const std::string&, const SecureString&, uint64_t, std::vector<bilingual_str>&) {
            return util::Error{Untranslated("Unexpected createWallet call")};
        };
    std::function<util::Result<std::unique_ptr<interfaces::Wallet>>(const std::string&, std::vector<bilingual_str>&)>
        load_wallet_fn = [](const std::string&, std::vector<bilingual_str>&) {
            return util::Error{Untranslated("Unexpected loadWallet call")};
        };
    std::function<util::Result<interfaces::WalletMigrationResult>(const std::string&, const SecureString&, bool)>
        migrate_wallet_fn = [](const std::string&, const SecureString&, bool) {
            return util::Error{Untranslated("Unexpected migrateWallet call")};
        };
    std::function<util::Result<std::unique_ptr<interfaces::Wallet>>()>
        restore_wallet_fn = [] { return util::Error{Untranslated("Unexpected restoreWallet call")}; };
    std::function<bool(const std::string&)> is_encrypted_fn = [](const std::string&) {
        return false;
    };
    std::function<std::vector<std::unique_ptr<interfaces::Wallet>>()> get_wallets_fn = [] {
        return std::vector<std::unique_ptr<interfaces::Wallet>>{};
    };
    std::vector<std::pair<std::string, std::string>> wallet_dir_entries;
    LoadWalletFn captured_load_wallet_fn;
    std::function<std::unique_ptr<interfaces::Handler>()> handler_fn{MakeNoopHandler};

    void registerRpcs() override {}
    bool verify() override { return true; }
    bool load() override { return true; }
    void start(CScheduler&) override {}
    void stop() override {}
    void setMockTime(int64_t) override {}
    void schedulerMockForward(std::chrono::seconds) override {}
    util::Result<std::unique_ptr<interfaces::Wallet>> createWallet(
        const std::string& name,
        const SecureString& passphrase,
        uint64_t wallet_creation_flags,
        std::vector<bilingual_str>& warnings) override
    {
        ++create_wallet_calls;
        return create_wallet_fn(name, passphrase, wallet_creation_flags, warnings);
    }
    util::Result<std::unique_ptr<interfaces::Wallet>> loadWallet(const std::string& name, std::vector<bilingual_str>& warnings) override
    {
        ++load_wallet_calls;
        return load_wallet_fn(name, warnings);
    }
    std::atomic<QThread*> directory_path_thread{nullptr};
    std::string getWalletDir() override { directory_path_thread.store(QThread::currentThread()); return wallet_dir; }
    util::Result<std::unique_ptr<interfaces::Wallet>> restoreWallet(const fs::path&, const std::string&, std::vector<bilingual_str>&, bool) override
    {
        return restore_wallet_fn();
    }
    util::Result<interfaces::WalletMigrationResult> migrateWallet(const std::string& name, const SecureString& passphrase, bool load_wallet) override
    {
        ++migrate_wallet_calls;
        return migrate_wallet_fn(name, passphrase, load_wallet);
    }
    bool isEncrypted(const std::string& name) override
    {
        ++is_encrypted_calls;
        return is_encrypted_fn(name);
    }
    std::atomic<QThread*> directory_thread{nullptr};
    std::vector<std::pair<std::string, std::string>> listWalletDir() override
    {
        directory_thread.store(QThread::currentThread());
        ++list_wallet_dir_calls;
        return wallet_dir_entries;
    }
    std::vector<std::unique_ptr<interfaces::Wallet>> getWallets() override
    {
        ++get_wallets_calls;
        return get_wallets_fn();
    }
    std::unique_ptr<interfaces::Handler> handleLoadWallet(LoadWalletFn fn) override
    {
        ++handle_load_wallet_calls;
        captured_load_wallet_fn = std::move(fn);
        return handler_fn();
    }
};

class FakeWallet : public interfaces::Wallet
{
public:
    struct State {
        struct Data {
            std::atomic<int> remove_calls{0};
            std::atomic<CAmount> balance{0};
            std::atomic<int> name_reads{0};
            std::atomic<int> imports{0}, private_accesses{0};
            std::function<std::vector<wallet::ImportResult>(std::vector<wallet::ImportDescriptorRequest>&)> import_fn;
            std::mutex mutex;
            interfaces::Wallet::UnloadFn unload_fn;
            void setUnload(interfaces::Wallet::UnloadFn fn) { std::lock_guard lock(mutex); unload_fn = std::move(fn); }
            bool hasUnload() { std::lock_guard lock(mutex); return bool(unload_fn); }
            void notifyUnload() { interfaces::Wallet::UnloadFn fn; { std::lock_guard lock(mutex); fn = unload_fn; } if (fn) fn(); }
        };
        // The Native wallet and its cleanup handler can outlive the stack
        // observation facade during intentional reentrant controller deletion.
        // Keep the original fixture data alive through their actual teardown.
        const std::shared_ptr<Data> retained{std::make_shared<Data>()};
        std::atomic<int>& remove_calls{retained->remove_calls};
        std::atomic<CAmount>& balance{retained->balance};
        std::atomic<int>& name_reads{retained->name_reads};
        std::atomic<int>& imports{retained->imports};
        std::atomic<int>& private_accesses{retained->private_accesses};
        std::mutex& mutex{retained->mutex};
        std::function<std::vector<wallet::ImportResult>(std::vector<wallet::ImportDescriptorRequest>&)>& import_fn{retained->import_fn};
        State() = default;
        State(const State&) = delete;
        State& operator=(const State&) = delete;
        bool hasUnload() { return retained->hasUnload(); }
        void notifyUnload() { retained->notifyUnload(); }
    };

    explicit FakeWallet(std::string wallet_name, State* state)
        : m_wallet_name(std::move(wallet_name)), m_state(state ? state->retained : nullptr) {}

    bool private_keys_disabled{false};
    bool external_signer{false};

    bool encryptWallet(const SecureString&) override { return true; }
    bool isCrypted() override { return false; }
    bool lock() override { return true; }
    util::Expected<void, wallet::WalletError> unlock(const SecureString&) override { return {}; }
    bool isLocked() override { return false; }
    util::Expected<void, wallet::WalletError> changeWalletPassphrase(const SecureString&, const SecureString&) override { return {}; }
    void abortRescan() override {}
    bool backupWallet(const std::string&) override { return true; }
    std::string getWalletName() override { if (m_state) ++m_state->name_reads; return m_wallet_name; }
    util::Result<CTxDestination> getNewDestination(const OutputType, const std::string&) override
    {
        return CTxDestination{CNoDestination{}};
    }
    bool getPubKey(const CScript&, const CKeyID&, CPubKey&) override { return false; }
    util::Expected<CExtPubKey, wallet::WalletError> addHDKey(const std::optional<CExtKey>& key) override { return util::Unexpected{wallet::WalletError{wallet::WalletErrorCode::GenericError, Untranslated("not implemented")}}; }
    SigningResult signMessage(const std::string&, const PKHash&, std::string&) override
    {
        return SigningResult::PRIVATE_KEY_NOT_AVAILABLE;
    }
    bool isSpendable(const CTxDestination&) override { return false; }
    bool setAddressBook(const CTxDestination&, const std::string&, const std::optional<wallet::AddressPurpose>&) override { return true; }
    bool delAddressBook(const CTxDestination&) override { return true; }
    bool getAddress(const CTxDestination&, std::string*, wallet::AddressPurpose*) override { return false; }
    std::vector<interfaces::WalletAddress> getAddresses() override { return {}; }
    std::vector<std::string> getAddressReceiveRequests() override { return {}; }
    bool setAddressReceiveRequest(const CTxDestination&, const std::string&, const std::string&) override { return true; }
    util::Result<void> displayAddress(const CTxDestination&) override { return {}; }
    bool lockCoin(const COutPoint&, const bool) override { return true; }
    bool unlockCoin(const COutPoint&) override { return true; }
    bool isLockedCoin(const COutPoint&) override { return false; }
    void listLockedCoins(std::vector<COutPoint>& outputs) override { outputs.clear(); }
    util::Result<wallet::CreatedTransactionResult> createTransaction(const std::vector<wallet::CRecipient>&,
                                                    const wallet::CCoinControl&,
                                                    bool,
                                                    std::optional<unsigned int>) override
    {
        return util::Error{Untranslated("Unexpected createTransaction call")};
    }
    void commitTransaction(CTransactionRef, const std::vector<std::string>&) override {}
    bool transactionCanBeAbandoned(const Txid&) override { return false; }
    bool abandonTransaction(const Txid&) override { return false; }
    bool transactionCanBeBumped(const Txid&) override { return false; }
    bool createBumpTransaction(const Txid&, const wallet::CCoinControl&, std::vector<bilingual_str>&, CAmount&, CAmount&, CMutableTransaction&) override
    {
        return false;
    }
    bool signBumpTransaction(CMutableTransaction&) override { return false; }
    bool commitBumpTransaction(const Txid&, CMutableTransaction&&, std::vector<bilingual_str>&, Txid&) override { return false; }
    CTransactionRef getTx(const Txid&) override { return {}; }
    interfaces::WalletTx getWalletTx(const Txid&) override { return {}; }
    std::set<interfaces::WalletTx> getWalletTxs() override { return {}; }
    bool tryGetTxStatus(const Txid&, interfaces::WalletTxStatus&, int&, int64_t&) override { return false; }
    interfaces::WalletTx getWalletTxDetails(const Txid&, interfaces::WalletTxStatus&, std::vector<std::string>&, std::vector<std::string>&, bool&, int&) override { return {}; }
    std::optional<common::PSBTError> fillPSBT(const common::PSBTFillOptions&, size_t*, PartiallySignedTransaction&, bool&) override
    {
        return std::nullopt;
    }
    wallet::CWallet* wallet() override {
        if (m_state) ++m_state->private_accesses;
        throw std::runtime_error{"Private wallet access forbidden"};
    }
    std::vector<wallet::ImportResult> importDescriptors(std::vector<wallet::ImportDescriptorRequest>& requests) override {
        if (m_state) {
            ++m_state->imports;
            if (m_state->import_fn) return m_state->import_fn(requests);
        }
        return {wallet::ImportResult{wallet::WalletErrorCode::GenericError, "not implemented", /*warnings=*/{}, /*general_error=*/true}};
    }
    interfaces::WalletBalances getBalances() override { return {}; }
    bool tryGetBalances(interfaces::WalletBalances& balances, uint256& tip) override
    {
        balances.balance = m_state ? m_state->balance.load() : 0;
        tip = uint256{};
        return true;
    }
    CAmount getBalance() override { return m_state ? m_state->balance.load() : 0; }
    CAmount getAvailableBalance(const wallet::CCoinControl&) override { return 0; }
    bool txinIsMine(const CTxIn&) override { return false; }
    bool txoutIsMine(const CTxOut&) override { return false; }
    CAmount getDebit(const CTxIn&) override { return 0; }
    CAmount getCredit(const CTxOut&) override { return 0; }
    CoinsList listCoins() override { return {}; }
    std::vector<interfaces::WalletTxOut> getCoins(const std::vector<COutPoint>&) override { return {}; }
    CAmount getRequiredFee(unsigned int) override { return 0; }
    CAmount getMinimumFee(unsigned int, const wallet::CCoinControl&, std::optional<int>*, FeeReason*) override { return 0; }
    unsigned int getConfirmTarget() override { return 6; }
    bool hdEnabled() override { return true; }
    bool canGetAddresses() override { return true; }
    bool privateKeysDisabled() override { return private_keys_disabled; }
    bool taprootEnabled() override { return true; }
    bool hasExternalSigner() override { return external_signer; }
    OutputType getDefaultAddressType() override { return OutputType::BECH32; }
    CAmount getMaxTxFee() override { return COIN; }
    void remove() override
    {
        if (m_state) {
            ++m_state->remove_calls;
        }
    }
    std::unique_ptr<interfaces::Handler> handleUnload(UnloadFn fn) override
    {
        if (m_state) {
            m_state->setUnload(std::move(fn));
            return interfaces::MakeCleanupHandler([state = m_state] {
                state->setUnload({});
            });
        }
        return MakeNoopHandler();
    }
    std::unique_ptr<interfaces::Handler> handleShowProgress(ShowProgressFn) override { return MakeNoopHandler(); }
    std::unique_ptr<interfaces::Handler> handleStatusChanged(StatusChangedFn) override { return MakeNoopHandler(); }
    std::unique_ptr<interfaces::Handler> handleAddressBookChanged(AddressBookChangedFn) override { return MakeNoopHandler(); }
    std::unique_ptr<interfaces::Handler> handleTransactionChanged(TransactionChangedFn) override { return MakeNoopHandler(); }
    std::unique_ptr<interfaces::Handler> handleCanGetAddressesChanged(CanGetAddressesChangedFn) override { return MakeNoopHandler(); }
    util::Result<std::string> exportWatchOnlyWallet(const fs::path&) override { return util::Error{Untranslated("not implemented")}; }

private:
    std::string m_wallet_name;
    std::shared_ptr<State::Data> m_state;
};

void ConfigureExpectedControllerInitialization(MockNode& node, FakeWalletLoader& loader)
{
    node.wallet_loader_fn = [&loader]() -> interfaces::WalletLoader& { return loader; };
    node.get_persistent_setting_fn = [](const std::string&) { return common::SettingsValue{}; };
    node.force_setting_fn = [](const std::string&, const common::SettingsValue&) {};
    node.list_external_signers_fn = [] { return std::vector<std::unique_ptr<interfaces::ExternalSigner>>{}; };
    node.ExpectAtLeast(node.calls.walletLoader, 2);
    node.ExpectAtLeast(node.calls.getPersistentSetting, 1);
    node.get_dust_relay_fee_fn = [] { return CFeeRate{3000}; };
    node.ExpectExactly(node.calls.forceSetting, 1);
    node.ExpectExactly(node.calls.listExternalSigners, 1);
}

class ControllerBoundaryScheduler final : public execution::Scheduler {
public:
    explicit ControllerBoundaryScheduler(std::shared_ptr<execution::Scheduler> target) : m_target(std::move(target)) {}
    std::function<void()> after_reserve, after_borrow;
    std::optional<size_t> reject_cleanup_above;
    bool reject_next_borrow{false};
    int rejected_borrows{0};
    std::shared_ptr<execution::Scheduler> openActionScope(size_t bytes) override { return m_target->openActionScope(bytes); }
    execution::Admission submit(std::shared_ptr<execution::Task> task, execution::TaskSpec spec) override { return m_target->submit(std::move(task), spec); }
    execution::Admission reserveCleanup(std::shared_ptr<execution::Task> task, execution::TaskSpec spec) override {
        if (reject_cleanup_above && spec.payload_bytes > *reject_cleanup_above) return {{}, execution::Rejection::Capacity};
        const auto result = m_target->reserveCleanup(std::move(task), spec);
        if (after_reserve) std::exchange(after_reserve, {})();
        return result;
    }
    execution::BorrowAdmission borrow(execution::ResourceKey key) override {
        if (std::exchange(reject_next_borrow, false)) {
            ++rejected_borrows;
            return {{}, execution::Rejection::Capacity};
        }
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
    std::shared_ptr<execution::Scheduler> m_target;
};

struct ManualControllerGraph {
    ManualRoutedExecution native;
    std::deque<std::function<void()>> posts;
    std::function<void()> native_drained;
    std::unique_ptr<WalletQmlController> controller;
    std::atomic<int> lane{-1};
    bool drained{false};
    int physical_completions{0};

    explicit ManualControllerGraph(interfaces::Node& node, execution::Limits limits = execution::Limits::Bounded(),
        std::function<std::shared_ptr<execution::Scheduler>(std::shared_ptr<execution::Scheduler>)> decorate = {})
        : native(limits)
    {
        WalletQmlController::Services services;
        services.scheduler = decorate ? decorate(native.scheduler) : native.scheduler;
        services.post = [this](auto callback) { posts.push_back(std::move(callback)); };
        services.shutdown = [this](auto callback) {
            native.scheduler->closeAdmission();
            native_drained = std::move(callback);
        };
        controller = std::make_unique<WalletQmlController>(node, std::move(services));
        QObject::connect(controller.get(), &WalletQmlController::walletsDrained, controller.get(), [this] { drained = true; });
    }
    bool step(execution::Lane value)
    {
        lane = static_cast<int>(value);
        const bool ran = native.step(value);
        lane = -1;
        return ran;
    }
    void runNative()
    {
        while (!native.queuedIdle()) {
            if (!step(native.nextLane().value())) break;
        }
    }
    void pump()
    {
        runNative();
        if (!posts.empty()) { auto next = std::move(posts.front()); posts.pop_front(); next(); }
        QCoreApplication::processEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        if (native_drained && native.scheduler->pending() == 0 && native.queuedIdle() && native.activeIdle()) {
            // An actual physical ledger drain, never an idle-cleanup heuristic.
            ++physical_completions;
            posts.push_back(std::exchange(native_drained, {}));
        }
    }
    bool until(std::function<bool()> condition)
    {
        return QTest::qWaitFor([&] { pump(); return condition() && native.queuedIdle() && posts.empty(); }, 5000);
    }
    void shutdown()
    {
        if (!controller) return;
        controller->beginShutdown();
        const bool complete = until([&] { return drained; });
        if (!complete) QTest::qFail("Original controller graph did not physically drain", __FILE__, __LINE__);
        if (complete) { QCOMPARE(native.scheduler->pending(), size_t{0}); QCOMPARE(physical_completions, 1); }
    }
    ~ManualControllerGraph()
    {
        shutdown();
        controller.reset();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
};

const QString VALID_XPUB{
    "xpub661MyMwAqRbcFtXgS5sYJABqqG9YLmC4Q1Rdap9gSE8NqtwybGhePY2gZ29ESFjqJoCu1Rupje8YtGqsefD265TMg7usUDFdp6W1EGMcet8"};
} // namespace

class WalletQmlControllerTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void fakeWalletCleanupRetainsOriginalFixtureState();
    void importErrorProjectionUsesOriginalRevisionAndVirtualFullText();
    void importErrorCaptureOverflowPreservesActualOutcomeAndRetries();
    void migrationErrorReceiptPreparesBoundedMessageAndFrozenFullDetails();
    void importErrorProjectionRejectionAndUnknownOversizedSourceStayExplicit();
    void createErrorProjectionRetainsNativeDocumentAndFencesSupersededPublication();
    void manualShutdownFencesInventoryBeforeBoundedRetirement();
    void manualShutdownSurvivesOwnerDestructionDuringFirstBatch();
    void manualReviewRetirementGatesFinalNativeDrain();
    void failureDuringReadyPublicationCannotResumeNotifications();
    void earlierNotificationKeepsItsOriginWhenCreationStartsDuringMetadata();
    void initialHandoffCapacityFailureNeverPublishesReady();
    void constructionOwnershipCapacityFailureNeverStartsCapture();
    void cleanupFailureKeepsItsExplicitTerminalError();
    void successfulEmptyMigrationDoesNotFabricateAWallet();
    void changedWakeupWaitsForAnEarlierShadowDisposal();
    void failureConsumedBeforeAnEarlierReadyMessageCannotResurrectInitialization();
    void catalogCommandsBeforeInitializationAcquireNoWalletResources();
    void rejectedInitializationStaysUnavailableUntilRestart();
    void initializationIsSingleShotAcrossPendingSuccessFailureAndShutdown_data();
    void initializationIsSingleShotAcrossPendingSuccessFailureAndShutdown();
    void runtimeDisabledWalletDoesNotAcquireItsLoader();
    void successfulImportPublishesTheOwnedReturnedWallet();
    void successfulMigrationPublishesTheOwnedReturnedWallet();
    void notificationsDuringBatchedStartupArePresentedAfterInitialization();
    void failurePublicationSurvivesReentrantRetirement_data();
    void failurePublicationSurvivesReentrantRetirement();
    void notificationDuringNestedPresentationKeepsItsContinuation();
    void subscriptionFailureDisablesCatalogCommands();
    void notificationOverflowReportsUnavailableResync();
    void startupSnapshotMergesNotificationHandlesWithoutDuplicateModels();
    void lifecyclePublicationSurvivesReentrantRetirement_data();
    void lifecyclePublicationSurvivesReentrantRetirement();
    void signerDiscoveryUsesDefaultNativeWorker();
    void closeTargetPublicationCanRetireOriginalFacade();
    void shutdownDrainsWalletQueueAndPlaceholder();
    void reentrantShutdownRetiresTheNewWalletModel();
    void shutdownRetiresQueuedLoadNotificationOffGui();
    void shutdownReleasesNotificationsBeforeMigrationRollback();
    void shutdownWaitsForNotificationRetirement();
    void walletCommandCompletionDuringShutdown_data();
    void walletCommandCompletionDuringShutdown();
    void injectedControllerServicesRejectPartialGraphBeforeAdmission();
    void manualMatchingLoadNotificationAndResult_data();
    void manualMatchingLoadNotificationAndResult();
    void manualOldOpeningPromptAndOutcomeCannotResolveReopenedName();
    void manualUnknownBindingNotificationCannotResurrectOrResolveReopenedName_data();
    void manualUnknownBindingNotificationCannotResurrectOrResolveReopenedName();
    void manualObservedUnloadAllowsFreshSameNameLoad_data();
    void manualObservedUnloadAllowsFreshSameNameLoad();
    void manualInsertionCallbacksPreserveOriginalAdoption_data();
    void manualInsertionCallbacksPreserveOriginalAdoption();
    void manualAdoptionChargesCanonicalKeyAndKeepsActualNativeSuccess();
    void manualCloseEscrowRefusalKeepsActualSuccessWithoutResurrection();
    void manualControllerAdmissionSurvivesReentrantOwnerLoss_data();
    void manualControllerAdmissionSurvivesReentrantOwnerLoss();
    void manualInjectedShutdownOwnsAcceptedOutcomeAfterObserverLoss_data();
    void manualInjectedShutdownOwnsAcceptedOutcomeAfterObserverLoss();
    void acceptedCreationDoesNotRetargetLaterSelection();
    void openWalletRowsPreserveIndependentViews();
    void openWalletTreeRemovalPreservesRowsAndPersistentIdentities();
    void provisioningChecksOwnManualPhases_data();
    void provisioningChecksOwnManualPhases();
    void validateXpubAcceptsValidKey();
    void validateXpubRejectsGarbage();
    void validateXpubRejectsEmpty();
    void validateXpubTrimsWhitespace();
    void createWatchOnlyInvalidXpubSetsError();
    void createWatchOnlyCleansUpWhenDescriptorImportFails();
    void createWatchOnlyUsesPublicDescriptorImport_data();
    void createWatchOnlyUsesPublicDescriptorImport();
    void externalSignerCreationRequiresConfiguredPath();
    void externalSignerCreationRequiresExactlyOneSigner();
    void externalSignerSuggestionUsesSignerName();
    void signerDiscoveryKeepsNewerIntentAcrossHeldPublication();
    void signerDiscoveryRejectsPublicationAfterQuietClose();
    void initializedControllerSignalsMigrationForLegacyWallet();
    void createWalletBeforeInitializationReturnsFalseAndSetsError();
    void importWalletBeforeInitializationSetsLoadError();
    void migrateWalletBeforeInitializationSetsMigrationError();
    void selectWalletBeforeInitializationSetsLoadError();
    void initializedControllerPropagatesCreateErrors();
    void initializedControllerForwardsMigrationPassphrase_data();
    void initializedControllerForwardsMigrationPassphrase();
    void capturedSecretSurvivesQueueDelayAndQuietClose_data();
    void capturedSecretSurvivesQueueDelayAndQuietClose();
    void capturedSecretTransportRefusalDoesNotCallNativeLoader_data();
    void capturedSecretTransportRefusalDoesNotCallNativeLoader();
    void initializedControllerForwardsUtf8CreatePassphrase_data();
    void initializedControllerForwardsUtf8CreatePassphrase();
    void initializedControllerEmitsWalletCreateSucceeded();
    void initializedControllerDoesNotCompleteCreateForUnrelatedLoadNotification();
    void initializedControllerDoesNotCompleteLoadForUnrelatedLoadNotification();
    void initializedControllerReportsCreateWarnings();
    void initializedControllerIgnoresDuplicateCreateWhileInProgress();
    void watchOnlyCreateBeforeInitializationSetsLoadError();
    void watchOnlyCreateWhileWalletLoadInProgressIsIgnored();
    void watchOnlyCreateFailureAfterCreateDoesNotPublishWallet();
    void initializedControllerRequestsPassphraseBeforeEncryptedMigration();
    void initializedControllerMigratesUnencryptedWalletWithoutPassphrase();
    void initializedControllerClosesSelectedWalletAndSelectsRemainingLoadedWallet();
    void initializedControllerClosesNonSelectedWalletWithoutChangingSelection();
    void reopeningRetiringWalletReportsLoadErrorAndSucceedsAfterDrain();
    void initializedControllerEmitsWalletLoadStateChanged();
    void initializedControllerHandlesExternalWalletUnload();
    void initializedControllerUnloadWalletsClearsSelectionAndOpenWallets();
    void initializedControllerEmitsLoadingThenLoadErrorOnFailedLoad();
    void publishOpenWalletsInfoEmitsWalletInfoChangedForEachOpenWallet();
    void walletNameAvailabilityErrorRejectsEmptyAndWhitespace();
    void walletNameAvailabilityErrorRejectsExistingNameWithSurroundingWhitespace();
    void walletNameAvailabilityErrorReturnsEmptyForAvailableName();
    void openWalletLocationOpensWalletDirectory();
    void openWalletLocationReportsOpenFailure();
    void openWalletLocationReportsMissingWalletPath();
    void openWalletLocationReportsMissingWallet();
};

void WalletQmlControllerTests::initTestCase()
{
    SelectParams(ChainType::MAIN);
}

void WalletQmlControllerTests::fakeWalletCleanupRetainsOriginalFixtureState()
{
    std::unique_ptr<FakeWallet> wallet;
    std::unique_ptr<interfaces::Handler> handler;
    std::weak_ptr<FakeWallet::State::Data> original;
    {
        FakeWallet::State state;
        original = state.retained;
        wallet = std::make_unique<FakeWallet>("original", &state);
        handler = wallet->handleUnload([] {});
        QVERIFY(state.hasUnload());
    }
    // End the stack facade before the actual Native cleanup, reproducing the
    // ordering from TSan without leaving a dangling stack mutex behind.
    QVERIFY(!original.expired());
    bool disconnected{false};
    std::thread native{[wallet = std::move(wallet), handler = std::move(handler), original, &disconnected]() mutable {
        auto retained = original.lock();
        handler.reset();
        disconnected = retained && !retained->hasUnload();
        retained.reset();
        wallet.reset();
    }};
    native.join();
    QVERIFY(disconnected);
    QVERIFY(original.expired());
}

void WalletQmlControllerTests::failureDuringReadyPublicationCannotResumeNotifications()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader; ConfigureExpectedControllerInitialization(node, loader);
    DrainingWalletController controller{node};
    bool interrupted{false}, failed_inside_ready{false};
    connect(&controller, &WalletQmlController::initializedChanged, &controller, [&] {
        if (interrupted || !controller.initialized()) return;
        interrupted = true;
        QEventLoop loop;
        connect(&controller, &WalletQmlController::walletLoadErrorChanged, &loop, &QEventLoop::quit);
        QTimer watchdog; watchdog.setSingleShot(true);
        connect(&watchdog, &QTimer::timeout, &loop, &QEventLoop::quit);
        loader.captured_load_wallet_fn({});
        watchdog.start(5000); loop.exec();
        failed_inside_ready = !controller.initialized() && !controller.walletLoadError().isEmpty();
    });
    controller.initialize(); QTRY_VERIFY(interrupted);
    QVERIFY(failed_inside_ready); QVERIFY(!controller.initialized());
    QCOMPARE(controller.openWallets().size(), 0); QCOMPARE(loader.get_wallets_calls, 1);
}

void WalletQmlControllerTests::earlierNotificationKeepsItsOriginWhenCreationStartsDuringMetadata()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader; ConfigureExpectedControllerInitialization(node, loader);
    struct Probe {
        QSemaphore metadata_entered, metadata_release, create_entered, create_release;
        std::atomic_bool metadata_held{false};
    };
    const auto probe = std::make_shared<Probe>();
    DrainingWalletController controller{node};
    const auto unblock = qScopeGuard([&] { probe->metadata_release.release(); probe->create_release.release(); });
    controller.initialize(); QTRY_VERIFY(controller.initialized());
    struct EarlierWallet : FakeWallet {
        std::shared_ptr<Probe> probe;
        explicit EarlierWallet(std::shared_ptr<Probe> p) : FakeWallet("crossed", nullptr), probe(std::move(p)) {}
        std::string getWalletName() override {
            if (!probe->metadata_held.exchange(true)) { probe->metadata_entered.release(); probe->metadata_release.acquire(); }
            return FakeWallet::getWalletName();
        }
    };
    loader.captured_load_wallet_fn(std::make_unique<EarlierWallet>(probe));
    QVERIFY(probe->metadata_entered.tryAcquire(1, 5000));
    loader.create_wallet_fn = [probe](const auto&, const auto&, uint64_t, auto&) {
        probe->create_entered.release(); probe->create_release.acquire();
        return util::Result<std::unique_ptr<interfaces::Wallet>>{util::Error{Untranslated("Name already in use")}};
    };
    controller.createWatchOnlyWallet("crossed", VALID_XPUB);
    QVERIFY(controller.walletLoadInProgress());
    QCOMPARE(controller.walletOperationState(), WalletQmlController::WalletOperationState::WalletOperationPending);
    QCOMPARE(loader.create_wallet_calls, 0);
    struct DeliveryCounter : QObject {
        int count{0};
        bool eventFilter(QObject* target, QEvent* event) override {
            if (target->objectName() == QStringLiteral("wallet-notifications") && event->type() == QEvent::MetaCall) ++count;
            return false;
        }
    } delivered;
    QCoreApplication::instance()->installEventFilter(&delivered);
    const auto detach = qScopeGuard([&] { QCoreApplication::instance()->removeEventFilter(&delivered); });
    probe->metadata_release.release();
    // Both effects use the original serialized Core lane. The GUI command was
    // admitted while metadata was held; let Native metadata finish before
    // waiting for Native creation. Keep GUI publication held during this phase
    // so its name-registry update cannot turn the intended creation into a
    // preflight-only conflict. The queued earlier notification is released below.
    QVERIFY(probe->create_entered.tryAcquire(1, 5000));
    QTRY_VERIFY(delivered.count > 0);
    // This wallet was announced before the new command began. Its captured
    // producer context must survive the later same-name GUI create attempt.
    QCOMPARE(controller.openWallets().size(), 1);
    QVERIFY(controller.isWalletOpen("crossed"));
    QVERIFY(controller.walletLoadInProgress());
    probe->create_release.release(); QTRY_VERIFY(!controller.walletLoadInProgress());
    QVERIFY(!controller.walletLoadError().isEmpty());
    QVERIFY(controller.isWalletOpen("crossed"));
    QCOMPARE(loader.create_wallet_calls, 1); QCOMPARE(loader.get_wallets_calls, 1);
}

void WalletQmlControllerTests::constructionOwnershipCapacityFailureNeverStartsCapture()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    node.wallet_loader_fn = [&]() -> interfaces::WalletLoader& { return loader; };
    auto limits = execution::Limits::Bounded();
    limits.resource_borrowers = 0;
    DrainingWalletController controller{node, limits};
    QSignalSpy ready{&controller, &WalletQmlController::initializedChanged};
    controller.initialize();
    QTRY_VERIFY(!controller.walletLoadError().isEmpty());
    QVERIFY(!controller.initialized());
    QCOMPARE(ready.count(), 0);
    QCOMPARE(controller.openWallets().size(), 0);
    QCOMPARE(loader.get_wallets_calls, 0);
}

void WalletQmlControllerTests::initialHandoffCapacityFailureNeverPublishesReady()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    node.wallet_loader_fn = [&]() -> interfaces::WalletLoader& { return loader; };
    node.ExpectExactly(node.calls.walletLoader, 2);
    node.ExpectNoCalls(node.calls.forceSetting);
    node.ExpectNoCalls(node.calls.listExternalSigners);
    std::shared_ptr<ControllerBoundaryScheduler> boundary;
    ManualControllerGraph graph{node, execution::Limits::Bounded(), [&](auto target) {
        boundary = std::make_shared<ControllerBoundaryScheduler>(std::move(target));
        return boundary;
    }};
    QSignalSpy ready{graph.controller.get(), &WalletQmlController::initializedChanged};
    graph.controller->initialize();
    graph.runNative(); // Registration and the actual public snapshot are complete; GUI Ready is held.
    QCOMPARE(loader.get_wallets_calls, 1);
    QCOMPARE(ready.count(), 0);
    QVERIFY(!graph.controller->initialized());
    QVERIFY(!graph.posts.empty());
    boundary->reject_next_borrow = true;
    QVERIFY(graph.until([&] { return !graph.controller->walletLoadError().isEmpty(); }));
    QCOMPARE(boundary->rejected_borrows, 1);
    QVERIFY(!boundary->reject_next_borrow);
    QVERIFY(!graph.controller->initialized());
    QCOMPARE(ready.count(), 0);
    QCOMPARE(graph.controller->openWallets().size(), 0);
    QCOMPARE(loader.get_wallets_calls, 1);
    graph.shutdown();
    QCOMPARE(graph.native.scheduler->pending(), size_t{0});
}

void WalletQmlControllerTests::cleanupFailureKeepsItsExplicitTerminalError()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader; ConfigureExpectedControllerInitialization(node, loader);
    struct Handler : interfaces::Handler {
        void disconnect() override { throw std::runtime_error("synthetic disconnect failure"); }
    };
    loader.handler_fn = [] { return std::make_unique<Handler>(); };
    DrainingWalletController controller{node}; controller.initialize(); QTRY_VERIFY(controller.initialized());
    loader.captured_load_wallet_fn({});
    QTRY_COMPARE(controller.walletLoadError(), QString{"Wallet notification cleanup failed."});
    QVERIFY(!controller.initialized()); QCOMPARE(controller.openWallets().size(), 0);
    QCOMPARE(loader.get_wallets_calls, 1);
}

void WalletQmlControllerTests::changedWakeupWaitsForAnEarlierShadowDisposal()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader; ConfigureExpectedControllerInitialization(node, loader);
    loader.get_wallets_fn = [] {
        std::vector<std::unique_ptr<interfaces::Wallet>> wallets;
        wallets.push_back(std::make_unique<FakeWallet>("existing", nullptr)); return wallets;
    };
    struct Probe { QThread* gui{QThread::currentThread()}; QSemaphore disposing, release, metadata; std::atomic_bool off_gui{false}; };
    const auto probe = std::make_shared<Probe>();
    DrainingWalletController controller{node};
    const auto unblock = qScopeGuard([probe] { probe->release.release(); });
    controller.initialize(); QTRY_VERIFY(controller.initialized());
    struct Duplicate : FakeWallet {
        std::shared_ptr<Probe> probe;
        explicit Duplicate(std::shared_ptr<Probe> p) : FakeWallet("existing", nullptr), probe(std::move(p)) {}
        ~Duplicate() override {
            probe->off_gui = QThread::currentThread() != probe->gui;
            probe->disposing.release(); probe->release.acquire();
        }
    };
    struct Next : FakeWallet {
        std::shared_ptr<Probe> probe;
        explicit Next(std::shared_ptr<Probe> p) : FakeWallet("next", nullptr), probe(std::move(p)) {}
        std::string getWalletName() override {
            if (QThread::currentThread() != probe->gui) probe->metadata.release();
            return FakeWallet::getWalletName();
        }
    };
    struct DeliveryCounter : QObject {
        int count{0};
        bool eventFilter(QObject* target, QEvent* event) override {
            if (target->objectName() == QStringLiteral("wallet-notifications") && event->type() == QEvent::MetaCall) ++count;
            return false;
        }
    } delivered;
    QCoreApplication::instance()->installEventFilter(&delivered);
    const auto detach = qScopeGuard([&] { QCoreApplication::instance()->removeEventFilter(&delivered); });
    bool second_started{false}, metadata_ready{false};
    connect(&controller, &WalletQmlController::walletLoadStateChanged, &controller, [&](const QString& name) {
        if (name != "existing" || std::exchange(second_started, true)) return;
        loader.captured_load_wallet_fn(std::make_unique<Next>(probe));
        metadata_ready = probe->metadata.tryAcquire(1, 5000);
    });
    loader.captured_load_wallet_fn(std::make_unique<Duplicate>(probe));
    QTRY_VERIFY(probe->disposing.available() > 0);
    QTRY_VERIFY(delivered.count >= 2);
    QVERIFY(metadata_ready); QVERIFY(probe->off_gui.load());
    QVERIFY(!controller.isWalletOpen("next")); // Disposal still owns the previous handoff.
    probe->release.release();
    QTRY_VERIFY(controller.isWalletOpen("next"));
    QCOMPARE(controller.openWallets().size(), 2); QCOMPARE(loader.get_wallets_calls, 1);
}

void WalletQmlControllerTests::failureConsumedBeforeAnEarlierReadyMessageCannotResurrectInitialization()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    node.wallet_loader_fn = [&]() -> interfaces::WalletLoader& { return loader; };
    node.ExpectExactly(node.calls.walletLoader, 2);
    node.ExpectNoCalls(node.calls.forceSetting);
    node.ExpectNoCalls(node.calls.listExternalSigners);
    ManualControllerGraph graph{node};
    QSignalSpy initialized{graph.controller.get(), &WalletQmlController::initializedChanged};
    graph.controller->initialize();
    graph.runNative(); // Actual subscription registration/capture and Ready production, with GUI still held.
    QCOMPARE(loader.get_wallets_calls, 1);
    QVERIFY(loader.captured_load_wallet_fn);
    QVERIFY(!graph.posts.empty());
    QVERIFY(!graph.controller->initialized());
    QCOMPARE(initialized.count(), 0);
    // Preserve every earlier publication, including Ready, without guessing
    // that the first transport MetaCall is a lifecycle publication.
    auto earlier_publications = std::move(graph.posts);
    graph.posts.clear();
    const auto callback = loader.captured_load_wallet_fn;
    callback({});
    QVERIFY(graph.until([&] { return !graph.controller->walletLoadError().isEmpty(); }));
    QVERIFY(!graph.controller->initialized());
    QCOMPARE(graph.controller->openWallets().size(), 0);
    while (!earlier_publications.empty()) {
        auto earlier = std::exchange(earlier_publications.front(), {});
        earlier_publications.pop_front();
        earlier();
        QVERIFY(!graph.controller->initialized());
        QCOMPARE(graph.controller->openWallets().size(), 0);
    }
    QCOMPARE(initialized.count(), 0);
    QCOMPARE(loader.get_wallets_calls, 1);
    graph.shutdown();
    QCOMPARE(graph.native.scheduler->pending(), size_t{0});
}

void WalletQmlControllerTests::successfulEmptyMigrationDoesNotFabricateAWallet()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader; loader.wallet_dir_entries = {{"empty-legacy", "bdb"}};
    ConfigureExpectedControllerInitialization(node, loader);
    std::atomic_bool load_requested{false};
    loader.migrate_wallet_fn = [&load_requested](const auto&, const auto&, bool load_wallet) {
        load_requested = load_wallet;
        // Pinned Core can remove an empty migrated wallet and return success
        // without loading a primary handle. The result is still owned.
        return util::Result<interfaces::WalletMigrationResult>{interfaces::WalletMigrationResult{}};
    };
    DrainingWalletController controller{node}; controller.initialize(); QTRY_VERIFY(controller.initialized());
    QSignalSpy completed{&controller, &WalletQmlController::walletMigrationSucceeded};
    controller.migrateWallet("empty-legacy", "");
    QTRY_COMPARE(completed.count(), 1);
    QVERIFY(load_requested.load());
    QVERIFY(controller.initialized()); QCOMPARE(controller.openWallets().size(), 0);
    QVERIFY(!controller.isWalletLoaded()); QCOMPARE(controller.selectedWallet()->name(), QString{});
    QVERIFY(!controller.walletMigrationInProgress()); QVERIFY(controller.walletMigrationError().isEmpty());
    QCOMPARE(loader.migrate_wallet_calls, 1);
}

void WalletQmlControllerTests::catalogCommandsBeforeInitializationAcquireNoWalletResources()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    DrainingWalletController controller{node};
    controller.closeWallet("unavailable");
    QCOMPARE(controller.walletLoadError(), QString{NOT_INITIALIZED_ERROR});
    QVERIFY(!controller.createExternalSignerWallet("unavailable"));
    QCOMPARE(controller.walletLoadError(), QString{NOT_INITIALIZED_ERROR});
    QCOMPARE(node.calls.walletLoader.load(), 0);
}

void WalletQmlControllerTests::rejectedInitializationStaysUnavailableUntilRestart()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    auto limits = execution::Limits::Bounded(); limits.cleanup.tasks = 0;
    DrainingWalletController controller{node, limits};
    QSignalSpy ready{&controller, &WalletQmlController::initializedChanged};
    controller.initialize();
    QVERIFY(!controller.initialized());
    QCOMPARE(controller.walletLoadError(), QString{"Wallet loading could not be started."});
    controller.initialize();
    controller.closeWallet("unavailable");
    QVERIFY(controller.walletLoadError().contains("discovery is unavailable"));
    QCOMPARE(ready.count(), 0);
    QCOMPARE(node.calls.walletLoader.load(), 0);
}

void WalletQmlControllerTests::initializationIsSingleShotAcrossPendingSuccessFailureAndShutdown_data()
{
    QTest::addColumn<int>("phase");
    QTest::newRow("pending") << 0;
    QTest::newRow("ready") << 1;
    QTest::newRow("failed") << 2;
    QTest::newRow("closing") << 3;
}

void WalletQmlControllerTests::initializationIsSingleShotAcrossPendingSuccessFailureAndShutdown()
{
    QFETCH(int, phase);
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    node.wallet_loader_fn = [&loader]() -> interfaces::WalletLoader& { return loader; };
    if (phase < 2) ConfigureExpectedControllerInitialization(node, loader);
    QSemaphore entered, release;
    const auto unblock = qScopeGuard([&] { release.release(); });
    loader.get_wallets_fn = [&] {
        entered.release(); release.acquire();
        if (phase == 2) throw std::runtime_error("capture unavailable");
        return std::vector<std::unique_ptr<interfaces::Wallet>>{};
    };
    DrainingWalletController controller{node};
    if (phase == 3) {
        controller.beginShutdown(); controller.initialize();
        QCOMPARE(loader.handle_load_wallet_calls, 0); QCOMPARE(loader.get_wallets_calls, 0);
        return;
    }
    controller.initialize();
    QVERIFY(entered.tryAcquire(1, 5000));
    if (phase == 0) controller.initialize();
    release.release();
    if (phase == 2) QTRY_VERIFY(!controller.walletLoadError().isEmpty());
    else QTRY_VERIFY(controller.initialized());
    controller.initialize();
    QCOMPARE(loader.handle_load_wallet_calls, 1); QCOMPARE(loader.get_wallets_calls, 1);
    QCOMPARE(controller.initialized(), phase != 2);
}

void WalletQmlControllerTests::runtimeDisabledWalletDoesNotAcquireItsLoader()
{
    std::map<std::string, common::SettingsValue> original;
    gArgs.LockSettings([&](common::Settings& settings) { original = settings.forced_settings; });
    const auto restore = qScopeGuard([&] { gArgs.LockSettings([&](common::Settings& settings) { settings.forced_settings = original; }); });
    gArgs.ForceSetArg("-disablewallet", "1");
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    DrainingWalletController controller{node};
    controller.initialize();
    QVERIFY(!controller.initialized()); QVERIFY(controller.walletLoadError().isEmpty());
    QCOMPARE(node.calls.walletLoader.load(), 0);
}

void WalletQmlControllerTests::successfulImportPublishesTheOwnedReturnedWallet()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader; ConfigureExpectedControllerInitialization(node, loader);
    DrainingWalletController controller{node}; controller.initialize(); QTRY_VERIFY(controller.initialized());
    QTemporaryDir dir; QVERIFY(dir.isValid());
    QFile backup{dir.filePath("wallet.bak")}; QVERIFY(backup.open(QIODevice::WriteOnly)); backup.close();
    loader.restore_wallet_fn = [] {
        return util::Result<std::unique_ptr<interfaces::Wallet>>{std::make_unique<FakeWallet>("imported", nullptr)};
    };
    QSignalSpy imported{&controller, &WalletQmlController::walletImportSucceeded};
    controller.importWallet(backup.fileName());
    QTRY_COMPARE(imported.count(), 1);
    QCOMPARE(qvariant_cast<WalletQmlModel*>(imported.front().front()), controller.selectedWallet());
    QCOMPARE(controller.selectedWallet()->name(), QString{"imported"});
    QVERIFY(controller.isWalletOpen("imported")); QVERIFY(!controller.walletLoadInProgress());
    QVERIFY(controller.walletLoadError().isEmpty());
}

void WalletQmlControllerTests::successfulMigrationPublishesTheOwnedReturnedWallet()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader; loader.wallet_dir_entries = {{"legacy", "bdb"}};
    ConfigureExpectedControllerInitialization(node, loader);
    std::atomic_bool load_requested{false};
    loader.migrate_wallet_fn = [&load_requested](const auto&, const auto&, bool load_wallet) {
        load_requested = load_wallet;
        interfaces::WalletMigrationResult result;
        result.wallet = std::make_unique<FakeWallet>("migrated", nullptr);
        return util::Result<interfaces::WalletMigrationResult>{std::move(result)};
    };
    DrainingWalletController controller{node}; controller.initialize(); QTRY_VERIFY(controller.initialized());
    QSignalSpy migrated{&controller, &WalletQmlController::walletMigrationSucceeded};
    controller.migrateWallet("legacy", "");
    QTRY_COMPARE(migrated.count(), 1);
    QVERIFY(load_requested.load());
    QVERIFY(controller.isWalletOpen("migrated"));
    QCOMPARE(controller.selectedWallet()->name(), QString{"migrated"});
    QVERIFY(!controller.walletMigrationInProgress()); QVERIFY(controller.walletMigrationError().isEmpty());
    QCOMPARE(loader.migrate_wallet_calls, 1);
}

void WalletQmlControllerTests::notificationsDuringBatchedStartupArePresentedAfterInitialization()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader; ConfigureExpectedControllerInitialization(node, loader);
    loader.get_wallets_fn = [&] {
        loader.captured_load_wallet_fn(std::make_unique<FakeWallet>("notified", nullptr));
        std::vector<std::unique_ptr<interfaces::Wallet>> wallets;
        for (int i{0}; i < 35; ++i) wallets.push_back(std::make_unique<FakeWallet>(QStringLiteral("initial-%1").arg(i).toStdString(), nullptr));
        return wallets;
    };
    DrainingWalletController controller{node};
    controller.initialize(); QTRY_VERIFY(controller.initialized());
    QTRY_COMPARE(controller.openWallets().size(), 36);
    QVERIFY(controller.isWalletOpen("notified"));
    for (int i{0}; i < 35; ++i) QVERIFY(controller.isWalletOpen("initial-" + QString::number(i)));
    QCOMPARE(loader.get_wallets_calls, 1); QVERIFY(controller.walletLoadError().isEmpty());
}

void WalletQmlControllerTests::failurePublicationSurvivesReentrantRetirement_data()
{
    QTest::addColumn<bool>("destroy_owner");
    QTest::newRow("close-on-readiness-invalidation") << false;
    QTest::newRow("destroy-on-readiness-invalidation") << true;
}

void WalletQmlControllerTests::failurePublicationSurvivesReentrantRetirement()
{
    QFETCH(bool, destroy_owner);
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader; ConfigureExpectedControllerInitialization(node, loader);
    auto controller = std::make_unique<WalletQmlController>(node);
    const auto drain = qScopeGuard([&] {
        controller.reset(); QObject receiver; bool drained{false};
        BackendExecutor::shutdownAll(&receiver, [&] { drained = true; });
        QTRY_VERIFY(drained); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    });
    controller->initialize(); QTRY_VERIFY(controller->initialized());
    QObject receiver; bool interrupted{false};
    connect(controller.get(), &WalletQmlController::initializedChanged, &receiver, [&] {
        if (interrupted) return;
        interrupted = true;
        QVERIFY(!controller->initialized()); QVERIFY(!controller->walletLoadError().isEmpty());
        if (destroy_owner) controller.reset(); else controller->beginShutdown();
    });
    loader.captured_load_wallet_fn({});
    QTRY_VERIFY(interrupted);
    QCOMPARE(loader.create_wallet_calls, 0);
}

void WalletQmlControllerTests::notificationDuringNestedPresentationKeepsItsContinuation()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    ConfigureExpectedControllerInitialization(node, loader);
    DrainingWalletController controller{node};
    controller.initialize(); QTRY_VERIFY(controller.initialized());
    struct Probe { QThread* gui{QThread::currentThread()}; QSemaphore metadata; };
    auto probe = std::make_shared<Probe>();
    struct NotifiedWallet : FakeWallet {
        std::shared_ptr<Probe> probe;
        explicit NotifiedWallet(std::shared_ptr<Probe> p) : FakeWallet("second", nullptr), probe(std::move(p)) {}
        std::string getWalletName() override {
            if (QThread::currentThread() != probe->gui) probe->metadata.release();
            return FakeWallet::getWalletName();
        }
    };
    bool nested_delivery{false}, intercepted{false}, metadata_ready{false};
    connect(&controller, &WalletQmlController::openWalletsChanged, &controller, [&] {
        if (intercepted) return;
        intercepted = true;
        const auto callback = loader.captured_load_wallet_fn;
        std::thread producer{[callback, probe] { callback(std::make_unique<NotifiedWallet>(probe)); }};
        producer.join();
        metadata_ready = probe->metadata.tryAcquire(1, 5000);
        if (!metadata_ready) return;
        QEventLoop loop;
        struct DeliveryObserver : QObject {
            QEventLoop& loop;
            bool delivered{false};
            explicit DeliveryObserver(QEventLoop& l) : loop(l) {}
            bool eventFilter(QObject* receiver, QEvent* event) override {
                if (receiver->objectName() == QStringLiteral("wallet-notifications") && event->type() == QEvent::MetaCall) {
                    delivered = true; loop.quit();
                }
                return false;
            }
        } observer{loop};
        QCoreApplication::instance()->installEventFilter(&observer);
        QTimer::singleShot(5000, &loop, &QEventLoop::quit);
        loop.exec();
        QCoreApplication::instance()->removeEventFilter(&observer);
        nested_delivery = observer.delivered;
    });
    const auto callback = loader.captured_load_wallet_fn;
    std::thread producer{[callback] { callback(std::make_unique<FakeWallet>("first", nullptr)); }};
    producer.join();
    QTRY_VERIFY(intercepted);
    QVERIFY(metadata_ready); QVERIFY(nested_delivery);
    // No third backend callback or user action may be required to expose the
    // second wallet after its wakeup arrived during an active presentation.
    QTRY_VERIFY(controller.isWalletOpen("second"));
    QVERIFY(controller.isWalletOpen("first"));
    QCOMPARE(loader.get_wallets_calls, 1);
}

void WalletQmlControllerTests::subscriptionFailureDisablesCatalogCommands()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    ConfigureExpectedControllerInitialization(node, loader);
    DrainingWalletController controller{node};
    controller.initialize();
    QTRY_VERIFY(controller.initialized());
    struct InvalidMetadata : FakeWallet {
        InvalidMetadata() : FakeWallet("unavailable", nullptr) {}
        std::string getWalletName() override { throw std::runtime_error("synthetic metadata failure"); }
    };
    loader.captured_load_wallet_fn(std::make_unique<InvalidMetadata>());
    QTRY_VERIFY(!controller.walletLoadError().isEmpty());
    QVERIFY(!controller.initialized());
    controller.createSingleSigWallet("after-failure", "");
    QCOMPARE(loader.create_wallet_calls, 0);
    QVERIFY(!controller.walletCreateError().isEmpty());
}

void WalletQmlControllerTests::notificationOverflowReportsUnavailableResync()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    ConfigureExpectedControllerInitialization(node, loader);
    DrainingWalletController controller{node};
    QSignalSpy readiness{&controller, &WalletQmlController::initializedChanged};
    controller.initialize();
    QTRY_VERIFY(controller.initialized());
    QCOMPARE(readiness.count(), 1);
    QSignalSpy rows{&controller, &WalletQmlController::openWalletsChanged};
    QSignalSpy selection{&controller, &WalletQmlController::selectedWalletChanged};
    QSignalSpy load_states{&controller, &WalletQmlController::walletLoadStateChanged};
    QSignalSpy error_changed{&controller, &WalletQmlController::walletLoadErrorChanged};
    const auto callback = loader.captured_load_wallet_fn;
    struct Probe { QSemaphore entered, release; std::atomic_bool held{false}; };
    const auto probe = std::make_shared<Probe>();
    const auto release = qScopeGuard([probe] { probe->release.release(); });
    struct HeldMetadata : FakeWallet {
        std::shared_ptr<Probe> probe;
        explicit HeldMetadata(std::shared_ptr<Probe> p) : FakeWallet("held", nullptr), probe(std::move(p)) {}
        std::string getWalletName() override {
            if (!probe->held.exchange(true)) { probe->entered.release(); probe->release.acquire(); }
            return FakeWallet::getWalletName();
        }
    };
    std::thread first{[callback, probe] { callback(std::make_unique<HeldMetadata>(probe)); }};
    first.join();
    QVERIFY(probe->entered.tryAcquire(1, 5000));
    // Hold actual metadata execution so ingress capacity is deterministically
    // exceeded. A large ready inventory alone must not trigger this failure.
    std::thread producer{[callback] {
        for (int i{0}; i < 256; ++i) {
            callback(std::make_unique<FakeWallet>(QString::number(i).toStdString(), nullptr));
        }
    }};
    producer.join();
    QTRY_VERIFY(!controller.initialized());
    QVERIFY(controller.walletLoadError().contains("unavailable"));
    QCOMPARE(readiness.count(), 2); // One Ready, then unavailable; transport cleanup posts are not domain updates.
    QCOMPARE(rows.count(), 0);
    QCOMPARE(selection.count(), 0);
    QCOMPARE(load_states.count(), 0);
    probe->release.release();
    QTRY_COMPARE(error_changed.count(), 1);
    QCOMPARE(loader.get_wallets_calls, 1);
    controller.createSingleSigWallet("after-overflow", "");
    QCOMPARE(loader.create_wallet_calls, 0);
    QVERIFY(controller.walletCreateError().contains("unavailable"));
    QSignalSpy drained{&controller, &WalletQmlController::walletsDrained};
    controller.beginShutdown();
    QTRY_COMPARE(drained.count(), 1); // Every retained ingress/cleanup outcome has now completed.
    QCOMPARE(rows.count(), 0);
    QCOMPARE(selection.count(), 0);
    QCOMPARE(load_states.count(), 0);
    QCOMPARE(controller.openWallets().size(), 0);
    QVERIFY(!controller.initialized());
    QCOMPARE(loader.get_wallets_calls, 1);
    QCOMPARE(error_changed.count(), 1);
}

void WalletQmlControllerTests::startupSnapshotMergesNotificationHandlesWithoutDuplicateModels()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    FakeWallet::State state;
    ConfigureExpectedControllerInitialization(node, loader);
    struct Probe {
        QThread* gui{QThread::currentThread()};
        std::atomic_bool subscribed_before_capture{false}, duplicate_destroyed{false}, duplicate_off_gui{false};
    };
    const auto probe = std::make_shared<Probe>();
    struct DuplicateWallet : FakeWallet {
        std::shared_ptr<Probe> probe;
        DuplicateWallet(State* state, std::shared_ptr<Probe> p) : FakeWallet("shared", state), probe(std::move(p)) {}
        ~DuplicateWallet() override {
            probe->duplicate_off_gui = QThread::currentThread() != probe->gui;
            probe->duplicate_destroyed = true;
        }
    };
    loader.get_wallets_fn = [&loader, &state, probe] {
        probe->subscribed_before_capture = bool(loader.captured_load_wallet_fn);
        if (loader.captured_load_wallet_fn) {
            loader.captured_load_wallet_fn(std::make_unique<DuplicateWallet>(&state, probe));
            loader.captured_load_wallet_fn(std::make_unique<FakeWallet>("notification-only", nullptr));
        }
        std::vector<std::unique_ptr<interfaces::Wallet>> wallets;
        wallets.push_back(std::make_unique<FakeWallet>("shared", &state));
        return wallets;
    };
    DrainingWalletController controller{node};
    controller.initialize();
    QTRY_VERIFY(controller.initialized());
    QTRY_VERIFY(probe->duplicate_destroyed.load());
    QVERIFY(probe->subscribed_before_capture.load());
    QVERIFY(probe->duplicate_off_gui.load());
    QCOMPARE(controller.openWallets().size(), 2);
    QVERIFY(controller.isWalletOpen("shared"));
    QVERIFY(controller.isWalletOpen("notification-only"));
    QCOMPARE(loader.get_wallets_calls, 1);
    QVERIFY(controller.walletLoadError().isEmpty());
}

void WalletQmlControllerTests::lifecyclePublicationSurvivesReentrantRetirement_data()
{
    QTest::addColumn<bool>("initial");
    QTest::addColumn<bool>("destroy_owner");
    QTest::addColumn<int>("signal_index");
    for (bool initial : {false, true}) {
        for (bool destroy : {false, true}) {
            for (int signal{0}; signal < (initial ? 8 : 6); ++signal) {
                const QByteArray label = QByteArray(initial ? "initial-" : "notification-") +
                    (destroy ? "destroy-" : "close-") + QByteArray::number(signal);
                QTest::newRow(label.constData()) << initial << destroy << signal;
            }
        }
    }
}

void WalletQmlControllerTests::lifecyclePublicationSurvivesReentrantRetirement()
{
    QFETCH(bool, initial);
    QFETCH(bool, destroy_owner);
    QFETCH(int, signal_index);
    FakeWallet::State state;
    std::atomic<int> signer_reads{0}, unexpected_settings{0}, gui_metadata_reads{0};
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    if (!initial) ConfigureExpectedControllerInitialization(node, loader);
    else {
        node.wallet_loader_fn = [&loader]() -> interfaces::WalletLoader& { return loader; };
        node.get_persistent_setting_fn = [&](const std::string& name) {
            if (name == "signer") {
                ++signer_reads;
                return signal_index == 6 ? common::SettingsValue{std::string{"synthetic-signer"}} : common::SettingsValue{};
            }
            if (name == "qml_receive_address_types") {
                if (QThread::currentThread() == qApp->thread()) ++gui_metadata_reads;
            } else ++unexpected_settings;
            return common::SettingsValue{};
        };
        node.force_setting_fn = [](const std::string&, const common::SettingsValue&) {};
        node.list_external_signers_fn = [] { return std::vector<std::unique_ptr<interfaces::ExternalSigner>>{}; };
        node.get_dust_relay_fee_fn = [] { return CFeeRate{3000}; };
        node.ExpectExactly(node.calls.walletLoader, 2);
        const int signer_calls = 0;
        node.ExpectAtLeast(node.calls.getPersistentSetting, signer_calls);
        node.ExpectExactly(node.calls.forceSetting, signer_calls);
        node.ExpectExactly(node.calls.listExternalSigners, signer_calls);
    }
    if (initial) loader.get_wallets_fn = [&state] {
        std::vector<std::unique_ptr<interfaces::Wallet>> wallets;
        wallets.push_back(std::make_unique<FakeWallet>("initial-wallet", &state));
        return wallets;
    };
    ManualRoutedExecution signer_environment;
    auto controller = std::make_unique<WalletQmlController>(node);
    controller->setExternalSignerServices({signer_environment.scheduler, {}});
    const auto drain = qScopeGuard([&] {
        controller.reset();
        signer_environment.drain();
        QVERIFY(signer_environment.quiescent());
        QObject receiver;
        bool drained{false};
        BackendExecutor::shutdownAll(&receiver, [&] { drained = true; });
        QTRY_VERIFY(drained);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        if (initial) {
            QCOMPARE(signer_reads.load(), 0);
            QCOMPARE(unexpected_settings.load(), 0);
            QCOMPARE(gui_metadata_reads.load(), 0);
        }
    });
    if (!initial) {
        controller->initialize();
        QTRY_VERIFY(controller->initialized());
        signer_environment.drain();
        QTRY_VERIFY(!controller->externalSignerPending());
    }
    controller->setNoWalletsFound(true);
    QSignalSpy retired{controller.get(), &WalletQmlController::walletsDrained};
    std::vector<bool> readiness;
    QObject receiver;
    const QPointer<WalletQmlController> observed{controller.get()};
    connect(controller.get(), &WalletQmlController::initializedChanged, &receiver, [observed, &readiness] {
        if (observed) readiness.push_back(observed->initialized());
    });
    bool interrupted{false};
    bool original_signer_observation_held{false};
    const auto interrupt = [&] {
        if (interrupted) return;
        interrupted = true;
        if (initial && signal_index >= 6) {
            original_signer_observation_held = (signer_environment.queuedCountIf([](const auto& turn) {
                return turn.lane == execution::Lane::Core && turn.phase == execution::Phase::Queued;
            }) != 0);
        }
        if (destroy_owner) controller.reset();
        else controller->beginShutdown();
    };
    switch (signal_index) {
    case 0: connect(controller.get(), &WalletQmlController::walletLoadStateChanged, &receiver, interrupt); break;
    case 1: connect(controller.get(), &WalletQmlController::walletInfoChanged, &receiver, interrupt); break;
    case 2: connect(controller.get(), &WalletQmlController::openWalletsChanged, &receiver, interrupt); break;
    case 3: connect(controller.get(), &WalletQmlController::isWalletLoadedChanged, &receiver, interrupt); break;
    case 4: connect(controller.get(), &WalletQmlController::noWalletsFoundChanged, &receiver, interrupt); break;
    case 5: connect(controller.get(), &WalletQmlController::selectedWalletChanged, &receiver, interrupt); break;
    case 6: connect(controller.get(), &WalletQmlController::externalSignerStatusChanged, &receiver, interrupt); break;
    case 7: connect(controller.get(), &WalletQmlController::initializedChanged, &receiver, interrupt); break;
    }
    if (initial) controller->initialize();
    else {
        const auto callback = loader.captured_load_wallet_fn;
        std::thread producer{[&state, callback] { callback(std::make_unique<FakeWallet>("notified-wallet", &state)); }};
        producer.join();
    }
    QTRY_VERIFY(interrupted);
    if (initial && signal_index >= 6) QVERIFY(original_signer_observation_held);
    if (!destroy_owner) {
        QTRY_COMPARE(retired.size(), 1);
        QVERIFY(!controller->initialized());
    }
    QCOMPARE(std::count(readiness.begin(), readiness.end(), true), initial && signal_index == 7 ? 1 : 0);
    QCOMPARE(loader.create_wallet_calls, 0);
    QCOMPARE(loader.get_wallets_calls, 1);
}

void WalletQmlControllerTests::closeTargetPublicationCanRetireOriginalFacade()
{
    MockNode node;
    node.get_persistent_setting_fn = [](const std::string&) { return common::SettingsValue{}; };
    node.force_setting_fn = [](const std::string&, const common::SettingsValue&) {};
    FakeWalletLoader loader;
    FakeWallet::State state;
    loader.get_wallets_fn = [&state] {
        std::vector<std::unique_ptr<interfaces::Wallet>> values;
        values.push_back(std::make_unique<FakeWallet>("original", &state));
        return values;
    };
    node.wallet_loader_fn = [&]() -> interfaces::WalletLoader& { return loader; };
    DrainingWalletController controller{node};
    controller.initialize(); QTRY_VERIFY(controller.initialized());
    QPointer<WalletQmlModel> original{controller.selectedWallet()};
    bool retired{false};
    connect(&controller, &WalletQmlController::closeTargetChanged, this, [&] {
        if (retired || !controller.closeTarget()) return;
        retired = true;
        controller.closeWallet("original");
        QVERIFY(QTest::qWaitFor([&] {
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            return original.isNull();
        }, 5000));
    });
    controller.requestCloseWallet(original);
    QVERIFY(retired); QVERIFY(original.isNull());
    QVERIFY(!controller.closeTarget());
    QVERIFY(!controller.isWalletOpen("original"));
    QCOMPARE(state.remove_calls.load(), 1);
}

void WalletQmlControllerTests::importErrorProjectionUsesOriginalRevisionAndVirtualFullText()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    ConfigureExpectedControllerInitialization(node, loader);
    ManualExecutionEnvironment env;
    DrainingWalletController controller{node};
    controller.initialize(); QTRY_VERIFY(controller.initialized());
    controller.setImportErrorServicesForTesting({env.scheduler, [&env](auto publish) { env.post(std::move(publish)); }});
    QSignalSpy raw_changed{&controller, &WalletQmlController::walletLoadErrorChanged};
    controller.setWalletLoadError(QStringLiteral("old legacy wallet"));
    QVERIFY(controller.walletImportErrorPending());
    env.drain(); // Native prepared the old revision; its GUI delivery is held.
    const QString original = QStringLiteral("  verification failed: ") + QString(5000, QLatin1Char('x')) + QStringLiteral("  ");
    controller.setWalletLoadError(original);
    env.publishAll(); // Old completion cannot classify the newer original error.
    QVERIFY(controller.walletImportErrorPending());
    QVERIFY(!controller.walletImportErrorTitle().contains(QStringLiteral("migrated")));
    env.drain(); env.publishAll();
    QVERIFY(!controller.walletImportErrorPending());
    QCOMPARE(controller.walletLoadError(), original);
    QCOMPARE(controller.walletImportErrorTitle(), QStringLiteral("This wallet type is not supported"));
    QCOMPARE(raw_changed.count(), 1);
    auto* lines = controller.walletImportErrorHelpModel();
    QVERIFY(lines->rowCount() > 1);
    QString complete;
    for (int i = 0; i < lines->rowCount(); ++i) {
        const auto chunk = lines->data(lines->index(i, 0), Qt::UserRole).toString();
        QVERIFY(chunk.size() <= 1024); complete += chunk;
    }
    QCOMPARE(complete, original);
    controller.setWalletLoadError(QString{original.constData(), original.size()}); // Native equality suppresses duplicate raw notification.
    env.drain(); env.publishAll();
    QCOMPARE(raw_changed.count(), 1);
    bool replaced{false};
    const auto reentrant = connect(lines, &QAbstractItemModel::modelAboutToBeReset, &controller, [&] {
        if (std::exchange(replaced, true)) return;
        controller.setWalletLoadError(QStringLiteral("winner does not exist"));
    });
    controller.setWalletLoadError(QStringLiteral("losing legacy wallet"));
    env.drain(); env.publishAll();
    QVERIFY(replaced);
    QCOMPARE(controller.walletLoadError(), QStringLiteral("winner does not exist"));
    QCOMPARE(controller.walletImportErrorTitle(), QStringLiteral("We couldn't find that file"));
    QCOMPARE(raw_changed.count(), 2);
    disconnect(reentrant);
    controller.setWalletLoadError(QStringLiteral("late legacy wallet"));
    controller.closeAdmission();
    env.drain(); env.publishAll(); // Quiet fence suppresses accepted late observations.
    QCOMPARE(raw_changed.count(), 2);
}

void WalletQmlControllerTests::importErrorCaptureOverflowPreservesActualOutcomeAndRetries()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    ConfigureExpectedControllerInitialization(node, loader);
    ManualExecutionEnvironment env;
    DrainingWalletController controller{node};
    controller.initialize(); QTRY_VERIFY(controller.initialized());
    controller.setImportErrorServicesForTesting({env.scheduler, [&env](auto publish) { env.post(std::move(publish)); }});
    for (int i = 0; i < 128; ++i) controller.setWalletLoadError(QStringLiteral("held error %1").arg(i));
    QVERIFY(controller.walletImportErrorPending());
    const QString actual_error = QStringLiteral("verification failed: ") + QString(5000, QLatin1Char('z'));
    QVERIFY(controller.submitWalletCommand([actual_error] {
        WalletQmlController::CommandResult result;
        result.error = actual_error; result.effect_succeeded = true; result.incomplete = true;
        return result;
    }, [&controller](const auto& result) { controller.setWalletLoadError(result.error); }, [](auto) {}));
    QTRY_VERIFY(controller.walletOperationState() != WalletQmlController::WalletOperationState::WalletOperationPending);
    QVERIFY(controller.walletImportErrorUnavailable());
    QVERIFY(controller.walletImportErrorRetryAvailable());
    QVERIFY(controller.walletLoadError() != actual_error); // Stale original details are hidden, actual operation receipt is unchanged.
    const auto actual = controller.m_command->result();
    QVERIFY(actual && actual->value);
    QVERIFY(actual->value->effect_succeeded);
    QVERIFY(actual->value->incomplete);
    QCOMPARE(actual->value->error, actual_error);
    env.drain(); env.publishAll(); // Native disposal releases old frame capacity.
    QVERIFY(controller.retryWalletImportErrorPresentation());
    QVERIFY(!controller.walletImportErrorUnavailable());
    env.drain(); env.publishAll();
    QVERIFY(!controller.walletImportErrorPending());
    QCOMPARE(controller.walletLoadError(), actual_error);
    QCOMPARE(controller.walletImportErrorTitle(), QStringLiteral("This wallet type is not supported"));
    QVERIFY(controller.walletImportErrorProjectionError().isEmpty());
    controller.closeAdmission(); env.drain(); env.publishAll();
}

void WalletQmlControllerTests::importErrorProjectionRejectionAndUnknownOversizedSourceStayExplicit()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    ConfigureExpectedControllerInitialization(node, loader);
    ManualExecutionEnvironment env;
    DrainingWalletController controller{node};
    controller.initialize(); QTRY_VERIFY(controller.initialized());
    controller.setImportErrorServicesForTesting({env.scheduler, [&env](auto publish) { env.post(std::move(publish)); }});
    env.rejectNext(true);
    controller.setWalletLoadError(QStringLiteral("file does not exist"));
    env.drain(); env.publishAll();
    QVERIFY(!controller.walletImportErrorPending());
    QVERIFY(!controller.walletImportErrorProjectionError().isEmpty());
    QVERIFY(controller.walletImportErrorRetryAvailable());
    QVERIFY(controller.retryWalletImportErrorPresentation());
    env.drain(); env.publishAll();
    QCOMPARE(controller.walletImportErrorTitle(), QStringLiteral("We couldn't find that file"));
    QVERIFY(controller.walletImportErrorProjectionError().isEmpty());
    // No Native command owns this synthetic producer. It exceeds the explicit
    // retained byte budget and must never masquerade as the prior failure.
    const QString oversized(8 * 1024 * 1024 + 1, QLatin1Char('q'));
    controller.setWalletLoadError(oversized);
    QVERIFY(controller.walletImportErrorUnavailable());
    QVERIFY(!controller.walletImportErrorRetryAvailable());
    QVERIFY(!controller.retryWalletImportErrorPresentation());
    QVERIFY(controller.walletLoadError() != QStringLiteral("file does not exist"));
    QCOMPARE(controller.walletImportErrorHelpModel()->rowCount(), 0);
    controller.closeAdmission(); env.drain(); env.publishAll();
}

void WalletQmlControllerTests::createErrorProjectionRetainsNativeDocumentAndFencesSupersededPublication()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    ConfigureExpectedControllerInitialization(node, loader);
    DrainingWalletController controller{node};
    controller.initialize(); QTRY_VERIFY(controller.initialized());
    const QString original = QStringLiteral("Actual creation failure: ") + QString(5000, QLatin1Char('c'));
    QVERIFY(controller.submitWalletCommand([original] {
        WalletQmlController::CommandResult result; result.error = original; result.incomplete = true; result.effect_succeeded = true; return result;
    }, [&controller](const auto& result) { controller.setWalletCreateError(result.error); }, [](auto) {}));
    QVERIFY(AwaitWalletOperation(controller));
    QCOMPARE(controller.walletCreateError(), original);
    QCOMPARE(controller.walletCreateErrorDisplay(), original.left(1024));
    QVERIFY(controller.walletCreateHasError());
    auto* details = controller.walletCreateErrorHelpModel();
    QString complete;
    for (int i = 0; i < details->rowCount(); ++i) {
        const auto chunk = details->data(details->index(i, 0), Qt::UserRole).toString();
        QVERIFY(chunk.size() <= 1024); complete += chunk;
    }
    QCOMPARE(complete, original);
    QVERIFY(controller.m_command->result()->value->effect_succeeded);
    bool successor{false};
    const auto reentrant = connect(&controller, &WalletQmlController::walletCreateErrorChanged, &controller, [&] {
        if (std::exchange(successor, true)) return;
        QVERIFY(controller.submitWalletCommand([] {
            WalletQmlController::CommandResult result; result.error = QStringLiteral("Successor failure"); return result;
        }, [&controller](const auto& result) { controller.setWalletCreateError(result.error); }, [](auto) {}));
    });
    QVERIFY(controller.submitWalletCommand([] {
        WalletQmlController::CommandResult result; result.error = QStringLiteral("Superseded failure"); return result;
    }, [&controller](const auto& result) {
        controller.setWalletCreateError(result.error); // Signal admits the successor and replaces member callbacks.
        controller.setWalletLoadError(result.error); // Remaining old publication must not capture a successor receipt.
    }, [](auto) {}));
    QTRY_VERIFY(successor);
    QVERIFY(AwaitWalletOperation(controller));
    QCOMPARE(controller.walletCreateError(), QStringLiteral("Successor failure"));
    QVERIFY(!controller.walletLoadHasError());
    disconnect(reentrant);
    controller.closeAdmission(); QCOMPARE(details->rowCount(), 0);
}

void WalletQmlControllerTests::migrationErrorReceiptPreparesBoundedMessageAndFrozenFullDetails()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    ConfigureExpectedControllerInitialization(node, loader);
    loader.wallet_dir_entries = {{"legacy-wallet", "bdb"}};
    const QString original = QStringLiteral("Incorrect PaSsPhRaSe: ") + QString(5000, QLatin1Char('p'));
    const auto gui = QThread::currentThread();
    std::atomic_bool native{false};
    loader.migrate_wallet_fn = [&](const auto&, const auto&, bool) -> util::Result<interfaces::WalletMigrationResult> {
        native = QThread::currentThread() != gui;
        return util::Error{Untranslated(original.toStdString())};
    };
    DrainingWalletController controller{node};
    controller.initialize(); QTRY_VERIFY(controller.initialized());
    QSignalSpy notices{&controller, &WalletQmlController::walletMigrationErrorNotice};
    controller.migrateWallet("legacy-wallet", "password");
    QVERIFY(AwaitWalletOperation(controller));
    QVERIFY(native.load()); QCOMPARE(notices.count(), 1);
    QCOMPARE(notices.front().at(0).toString(), QStringLiteral("legacy-wallet"));
    QVERIFY(notices.front().at(2).toBool());
    QCOMPARE(notices.front().at(3).toString(), original.left(1024));
    QCOMPARE(controller.walletMigrationError(), original);
    const auto ticket = notices.front().at(1).toULongLong();
    auto* details = controller.captureMigrationErrorDetails(ticket, 1);
    QVERIFY(details); QVERIFY(details->rowCount() > 1);
    controller.setWalletMigrationError(QStringLiteral("Another bounded diagnostic"));
    QString complete;
    for (int i = 0; i < details->rowCount(); ++i) {
        const auto chunk = details->data(details->index(i, 0), Qt::UserRole).toString();
        QVERIFY(chunk.size() <= 1024); complete += chunk;
    }
    QCOMPARE(complete, original); // Full original document is frozen independently of the latest error.
    controller.closeAdmission(); QCOMPARE(details->rowCount(), 0);
}

void WalletQmlControllerTests::manualShutdownFencesInventoryBeforeBoundedRetirement()
{
    StrictMockNode node; [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader; ConfigureExpectedControllerInitialization(node, loader);
    loader.get_wallets_fn = [] { std::vector<std::unique_ptr<interfaces::Wallet>> result; result.reserve(33); for (int i = 0; i < 33; ++i) result.push_back(std::make_unique<FakeWallet>("batch-" + util::ToString(i), nullptr)); return result; };
    std::deque<std::function<void()>> turns;
    auto controller = std::make_unique<WalletQmlController>(node);
    controller->setRetirementPostForTesting([&turns](auto callback) { turns.push_back(std::move(callback)); });
    const std::weak_ptr<execution::Scheduler> original_scheduler = controller->backgroundScheduler();
    const auto cleanup = qScopeGuard([&] { DrainControlledController(controller, turns, original_scheduler); });
    controller->initialize(); QTRY_VERIFY(controller->initialized()); QTRY_COMPARE(controller->openWallets().size(), 33);
    const auto wallets = controller->openWallets();
    std::vector<std::unique_ptr<WalletProvisioningCheck>> forms;forms.reserve(65);
    for (int i = 0; i < 65; ++i) forms.emplace_back(controller->createProvisioningCheck());
    for (const auto& form : forms) QVERIFY(form);
    struct Probe { QSemaphore entered, release; std::atomic<int> effects{0}, outcomes{0}; std::atomic_bool on_gui{false}; };
    const auto probe = std::make_shared<Probe>();
    const auto unblock = qScopeGuard([probe] { probe->release.release(); });
    auto scheduler = controller->backgroundScheduler();
    QVERIFY(execution::Submit(*scheduler, [probe] { probe->entered.release(); probe->release.acquire(); }, [] {}, {}, {execution::Lane::Core, 256, {}, execution::WorkClass::Action}));
    QTRY_VERIFY(probe->entered.available() > 0);
    auto* original = qobject_cast<WalletQmlModel*>(wallets.first()); QVERIFY(original);
    QVERIFY(original->opening()->read([probe](interfaces::Wallet&) { if (QThread::currentThread() == qApp->thread()) probe->on_gui = true; ++probe->effects; return true; }, [probe](bool) { ++probe->outcomes; }, {}, execution::WorkClass::Action));
    QSignalSpy closed(controller.get(), &WalletQmlController::walletLoadStateChanged);
    QSignalSpy drained(controller.get(), &WalletQmlController::walletsDrained);
    controller->closeAdmission();
    for (auto* object : wallets) { auto* wallet = qobject_cast<WalletQmlModel*>(object); QVERIFY(!wallet->available()); QVERIFY(!wallet->opening()->startAction()); }
    for (const auto& form : forms) { QVERIFY(!form->ready()); const auto before = form->name(); form->setName("late change"); QCOMPARE(form->name(), before); }
    const auto late = loader.captured_load_wallet_fn; late(std::make_unique<FakeWallet>("late-wallet", nullptr));
    controller->beginShutdown(); QCOMPARE(closed.size(), 0); QCOMPARE(probe->effects.load(), 0); QVERIFY(drained.empty());
    int batches{0};
    while (!turns.empty()) {
        auto next = std::move(turns.front()); turns.pop_front(); const int before = closed.size(); next(); ++batches;
        QVERIFY(closed.size() - before <= 32);
    }
    QCOMPARE(batches, 4); //65 forms and33 wallets share one32-object GUI budget.
    QCOMPARE(closed.size(), 33); QVERIFY(drained.empty()); QCOMPARE(probe->effects.load(), 0);
    bool heartbeat{false}; QTimer::singleShot(0, [&] { heartbeat = true; }); QTRY_VERIFY(heartbeat);
    probe->release.release();
    QVERIFY(PumpRetirementTurns(turns, [&] { return drained.size() == 1; }));
    QCOMPARE(scheduler->pending(), size_t{0}); QCOMPARE(probe->effects.load(), 1); QCOMPARE(probe->outcomes.load(), 1); QVERIFY(!probe->on_gui);
    QCOMPARE(controller->openWallets().size(), 0); QVERIFY(!controller->walletForName("late-wallet"));
}
void WalletQmlControllerTests::manualShutdownSurvivesOwnerDestructionDuringFirstBatch()
{
    StrictMockNode node; [[maybe_unused]] auto verify_node = node.VerifyOnExit(); FakeWalletLoader loader; ConfigureExpectedControllerInitialization(node, loader);
    std::atomic<int> destroyed{0}; std::atomic_bool on_gui{false};
    struct ObservedWallet : FakeWallet {
        std::atomic<int>& destroyed; std::atomic_bool& on_gui;
        ObservedWallet(std::string name, std::atomic<int>& count, std::atomic_bool& gui) : FakeWallet(std::move(name), nullptr), destroyed(count), on_gui(gui) {}
        ~ObservedWallet() override { if (QThread::currentThread() == qApp->thread()) on_gui = true; ++destroyed; }
    };
    loader.get_wallets_fn = [&] { std::vector<std::unique_ptr<interfaces::Wallet>> result; result.reserve(33); for (int i = 0; i < 33; ++i) result.push_back(std::make_unique<ObservedWallet>("original-" + util::ToString(i), destroyed, on_gui)); return result; };
    std::deque<std::function<void()>> turns; auto controller = std::make_unique<WalletQmlController>(node);
    controller->setRetirementPostForTesting([&turns](auto callback) { turns.push_back(std::move(callback)); });
    const std::weak_ptr<execution::Scheduler> original_scheduler = controller->backgroundScheduler();
    const auto cleanup = qScopeGuard([&] { DrainControlledController(controller, turns, original_scheduler); });
    controller->initialize(); QTRY_VERIFY(controller->initialized()); QTRY_COMPARE(controller->openWallets().size(), 33);
    QObject receiver; bool interrupted{false};
    connect(controller.get(), &WalletQmlController::walletLoadStateChanged, &receiver, [&](const QString&, WalletListModel::LoadState state, const QString&) { if (!interrupted && state == WalletListModel::LoadState::Closed) { interrupted = true; controller.reset(); } });
    controller->beginShutdown(); QVERIFY(!turns.empty());
    auto first = std::exchange(turns.front(), {});
    turns.pop_front();
    first();
    first = {}; // The consumed callback must not retain the original Retirement owner.
    QVERIFY(interrupted);
    QVERIFY(!controller);
    QVERIFY(!turns.empty()); while (!turns.empty()) { auto next = std::move(turns.front()); turns.pop_front(); next(); }
    QObject native_receiver;
    bool native_done{false};
    BackendExecutor::shutdownAll(&native_receiver, [&] { native_done = true; });
    QVERIFY(PumpRetirementTurns(turns, [&] { return native_done && turns.empty() && original_scheduler.expired(); }));
    QCOMPARE(destroyed.load(), 33);
    QVERIFY(!on_gui);
}
void WalletQmlControllerTests::manualReviewRetirementGatesFinalNativeDrain()
{
    StrictMockNode node; [[maybe_unused]] auto verify_node = node.VerifyOnExit(); FakeWalletLoader loader; ConfigureExpectedControllerInitialization(node, loader);
    loader.get_wallets_fn = [] { std::vector<std::unique_ptr<interfaces::Wallet>> result; result.push_back(std::make_unique<FakeWallet>("reviews", nullptr)); return result; };
    std::deque<std::function<void()>> review_turns;
    auto controller = std::make_unique<WalletQmlController>(node);
    const auto cleanup = qScopeGuard([&] { if (controller) controller->beginShutdown(); const bool presentation_drained = QTest::qWaitFor([&] { while (!review_turns.empty()) { auto next = std::move(review_turns.front()); review_turns.pop_front(); next(); } QCoreApplication::processEvents(); return review_turns.empty(); }, 5000); if (!presentation_drained) QTest::qFail("Review retirement presentation did not drain", __FILE__, __LINE__); controller.reset(); QObject receiver; bool done{false}; BackendExecutor::shutdownAll(&receiver, [&] { done = true; }); QTRY_VERIFY(done); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); });
    controller->initialize(); QTRY_VERIFY(controller->initialized()); QTRY_COMPARE(controller->openWallets().size(), 1);
    auto* wallet = controller->walletForName("reviews"); QVERIFY(wallet);
    wallet->setReviewRetirementPostForTesting([&review_turns](auto callback) { review_turns.push_back(std::move(callback)); });
    std::vector<QPointer<WalletQmlModelTransaction>> reviews;
    for (int i = 0; i < 65; ++i) { walletreview::Snapshot value; value.amount = 1000; reviews.emplace_back(new WalletQmlModelTransaction(value, wallet->opening(), [](auto callback) { QTimer::singleShot(0, QCoreApplication::instance(), std::move(callback)); }, wallet)); }
    QSignalSpy drained(controller.get(), &WalletQmlController::walletsDrained);
    controller->beginShutdown(); QTRY_VERIFY(!review_turns.empty()); QVERIFY(drained.empty());
    int batches{0};
    while (!review_turns.empty()) { const auto before = std::count_if(reviews.begin(), reviews.end(), [](const auto& value) { return value.isNull(); }); auto next = std::move(review_turns.front()); review_turns.pop_front(); next(); ++batches; const auto after = std::count_if(reviews.begin(), reviews.end(), [](const auto& value) { return value.isNull(); }); QVERIFY(after - before <= 32); }
    QCOMPARE(batches, 3); for (const auto& review : reviews) QVERIFY(review.isNull()); QTRY_COMPARE(drained.size(), 1);
}

void WalletQmlControllerTests::shutdownDrainsWalletQueueAndPlaceholder()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    ConfigureExpectedControllerInitialization(node, loader);
    DrainingWalletController controller(node);
    controller.initialize();
    QTRY_VERIFY(controller.initialized());
    QSemaphore entered, release;
    const auto empty_executor = controller.selectedWallet()->backendExecutor();
    const auto gui_thread = QThread::currentThread();
    std::atomic_bool off_gui{false};
    empty_executor->submit(&controller, [&] {
        off_gui = QThread::currentThread() != gui_thread;
        entered.release();
        release.acquire();
    }, [] {});
    struct Release { QSemaphore& semaphore; ~Release() { semaphore.release(); } } release_on_exit{release};
    QTRY_VERIFY(entered.available() > 0);
    QSignalSpy drained{&controller, &WalletQmlController::walletsDrained};
    controller.beginShutdown();
    bool heartbeat{false};
    QTimer::singleShot(0, &controller, [&] { heartbeat = true; });
    QTRY_VERIFY(heartbeat);
    QVERIFY(off_gui.load());
    QVERIFY(drained.empty());
    release.release();
    QTRY_COMPARE(drained.size(), 1);
    QVERIFY(empty_executor->isDrained());
}

void WalletQmlControllerTests::reentrantShutdownRetiresTheNewWalletModel()
{
    FakeWallet::State state; StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader; ConfigureExpectedControllerInitialization(node, loader);
    struct Probe { std::atomic_bool destroyed{false}, off_gui{false}; };
    auto probe = std::make_shared<Probe>();
    struct RetiringWallet : FakeWallet {
        std::shared_ptr<Probe> probe; QThread* gui;
        RetiringWallet(State* state, std::shared_ptr<Probe> value, QThread* thread)
            : FakeWallet("reentrant-wallet", state), probe(std::move(value)), gui(thread) {}
        ~RetiringWallet() override { probe->off_gui = QThread::currentThread() != gui; probe->destroyed = true; }
    };
    const auto gui = QThread::currentThread();
    loader.create_wallet_fn = [&](const auto&, const auto&, uint64_t, auto&) {
        return util::Result<std::unique_ptr<interfaces::Wallet>>{std::make_unique<RetiringWallet>(&state, probe, gui)};
    };
    DrainingWalletController controller{node}; controller.initialize();
    QTRY_VERIFY(controller.initialized());
    QSignalSpy drained{&controller, &WalletQmlController::walletsDrained};
    connect(&controller, &WalletQmlController::walletLoadInProgressChanged, &controller, [&] {
        if (!controller.walletLoadInProgress()) controller.beginShutdown();
    });
    controller.createSingleSigWallet("reentrant-wallet", "");
    QTRY_COMPARE(drained.count(), 1);
    QVERIFY(probe->destroyed.load()); QVERIFY(probe->off_gui.load());
    QVERIFY(!controller.isWalletOpen("reentrant-wallet")); QVERIFY(!controller.isWalletLoaded());
}

void WalletQmlControllerTests::shutdownRetiresQueuedLoadNotificationOffGui()
{
    FakeWallet::State state;
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    ConfigureExpectedControllerInitialization(node, loader);
    DrainingWalletController controller(node);
    controller.initialize();
    QTRY_VERIFY(controller.initialized());
    struct Probe { std::atomic_bool destroyed{false}, off_gui{false}; };
    const auto probe = std::make_shared<Probe>();
    struct RetiringWallet : FakeWallet {
        std::shared_ptr<Probe> probe;
        QThread* gui;
        RetiringWallet(State* state, std::shared_ptr<Probe> result)
            : FakeWallet("late-wallet", state), probe(std::move(result)), gui(QThread::currentThread()) {}
        ~RetiringWallet() override { probe->off_gui = QThread::currentThread() != gui; probe->destroyed = true; }
    };
    QVERIFY(loader.captured_load_wallet_fn);
    loader.captured_load_wallet_fn(std::make_unique<RetiringWallet>(&state, probe));
    QSignalSpy drained{&controller, &WalletQmlController::walletsDrained};
    controller.beginShutdown();
    QTRY_COMPARE(drained.size(), 1);
    QVERIFY(probe->destroyed.load());
    QVERIFY(probe->off_gui.load());
    QVERIFY(!controller.isWalletOpen("late-wallet"));
}

void WalletQmlControllerTests::shutdownReleasesNotificationsBeforeMigrationRollback()
{
    FakeWallet::State state;
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    ConfigureExpectedControllerInitialization(node, loader);
    loader.wallet_dir_entries = {{"legacy_wallet", "bdb"}};
    DrainingWalletController controller(node);
    controller.initialize();
    QTRY_VERIFY(controller.initialized());
    struct MigrationRollbackProbe {
        QSemaphore notification_queued, rollback_can_finish;
        std::atomic_bool destroyed{false}, off_gui{false}, rollback_finished{false};
    };
    const auto probe = std::make_shared<MigrationRollbackProbe>();
    const auto gui_thread = QThread::currentThread();
    struct RollbackBlockingWallet : FakeWallet {
        std::shared_ptr<MigrationRollbackProbe> probe;
        QThread* gui_thread;
        RollbackBlockingWallet(State* state, std::shared_ptr<MigrationRollbackProbe> probe, QThread* gui_thread)
            : FakeWallet("legacy_wallet_watchonly", state), probe(std::move(probe)), gui_thread(gui_thread) {}
        ~RollbackBlockingWallet() override {
            probe->off_gui = QThread::currentThread() != gui_thread;
            probe->destroyed = true;
            probe->rollback_can_finish.release();
        }
    };
    loader.migrate_wallet_fn = [&, probe, gui_thread](const std::string&, const SecureString&, bool) -> util::Result<interfaces::WalletMigrationResult> {
        loader.captured_load_wallet_fn(std::make_unique<RollbackBlockingWallet>(&state, probe, gui_thread));
        probe->notification_queued.release();
        probe->rollback_can_finish.acquire();
        probe->rollback_finished = true;
        return util::Error{Untranslated("Later migrated wallet failed to load")};
    };
    const auto release_on_failure = qScopeGuard([probe] { probe->rollback_can_finish.release(); });
    controller.migrateWallet("legacy_wallet", "");
    const bool notification_queued_without_gui_dispatch{probe->notification_queued.tryAcquire(1, 5000)};
    QVERIFY(notification_queued_without_gui_dispatch);
    QSignalSpy drained{&controller, &WalletQmlController::walletsDrained};
    controller.beginShutdown();
    QTRY_COMPARE_WITH_TIMEOUT(drained.size(), 1, 5000);
    QVERIFY(probe->destroyed.load());
    QVERIFY(probe->off_gui.load());
    QVERIFY(probe->rollback_finished.load());
    QVERIFY(!controller.isWalletOpen("legacy_wallet_watchonly"));
}

void WalletQmlControllerTests::shutdownWaitsForNotificationRetirement()
{
    FakeWallet::State state;
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    ConfigureExpectedControllerInitialization(node, loader);
    DrainingWalletController controller(node);
    controller.initialize();
    QTRY_VERIFY(controller.initialized());
    struct Probe { QSemaphore entered, release; std::atomic_bool destroyed{false}; };
    const auto probe = std::make_shared<Probe>();
    struct NotificationWallet : FakeWallet {
        std::shared_ptr<Probe> probe;
        NotificationWallet(State* state, std::shared_ptr<Probe> probe)
            : FakeWallet("late-wallet", state), probe(std::move(probe)) {}
        ~NotificationWallet() override {
            probe->entered.release();
            probe->release.acquire();
            probe->destroyed = true;
        }
    };
    const auto release_on_failure = qScopeGuard([probe] { probe->release.release(); });
    loader.captured_load_wallet_fn(std::make_unique<NotificationWallet>(&state, probe));
    QSignalSpy drained{&controller, &WalletQmlController::walletsDrained};
    controller.beginShutdown();
    QTRY_VERIFY(probe->entered.available() > 0);
    bool heartbeat{false};
    QTimer::singleShot(50, &controller, [&] { heartbeat = true; });
    QTRY_VERIFY(heartbeat);
    QVERIFY(drained.empty());
    QVERIFY(!probe->destroyed.load());
    probe->release.release();
    QTRY_COMPARE_WITH_TIMEOUT(drained.size(), 1, 5000);
    QVERIFY(probe->destroyed.load());
}

void WalletQmlControllerTests::walletCommandCompletionDuringShutdown_data()
{
    QTest::addColumn<QString>("operation");
    QTest::addColumn<bool>("throws");
    for (const auto* operation : {"create", "load", "import", "migrate"}) {
        for (bool throws : {false, true}) {
            const QByteArray tag = QByteArray{operation} + (throws ? "-exception" : "-success");
            QTest::newRow(tag.constData()) << QString::fromLatin1(operation) << throws;
        }
    }
}

void WalletQmlControllerTests::walletCommandCompletionDuringShutdown()
{
    QFETCH(QString, operation);
    QFETCH(bool, throws);
    FakeWallet::State state;
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    ConfigureExpectedControllerInitialization(node, loader);
    if (operation == "migrate") loader.wallet_dir_entries = {{"late-wallet", "bdb"}};
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QFile backup(directory.filePath("late-wallet.bak"));
    QVERIFY(backup.open(QIODevice::WriteOnly));
    backup.close();
    struct Probe {
        QSemaphore entered, release;
        std::atomic_bool destroyed{false}, off_gui{false};
    };
    auto probe = std::make_shared<Probe>();
    struct ResultWallet : FakeWallet {
        std::shared_ptr<Probe> probe;
        QThread* gui;
        ResultWallet(State* state, std::shared_ptr<Probe> probe, QThread* gui)
            : FakeWallet("late-wallet", state), probe(std::move(probe)), gui(gui) {}
        ~ResultWallet() override {
            probe->off_gui = QThread::currentThread() != gui;
            probe->destroyed = true;
        }
    };
    const auto gui_thread = QThread::currentThread();
    const auto complete = [&]() -> std::unique_ptr<interfaces::Wallet> {
        probe->entered.release();
        probe->release.acquire();
        if (throws) throw std::runtime_error{"wallet command failed"};
        return std::make_unique<ResultWallet>(&state, probe, gui_thread);
    };
    loader.create_wallet_fn = [&](const auto&, const auto&, uint64_t, auto&) { return complete(); };
    loader.load_wallet_fn = [&](const auto&, auto&) { return complete(); };
    loader.restore_wallet_fn = [&] { return complete(); };
    loader.migrate_wallet_fn = [&](const auto&, const auto&, bool) {
        interfaces::WalletMigrationResult result;
        result.wallet = complete();
        return result;
    };
    DrainingWalletController controller(node);
    controller.initialize();
    QTRY_VERIFY(controller.initialized());
    const auto release_on_failure = qScopeGuard([probe] { probe->release.release(); });
    QSignalSpy created{&controller, &WalletQmlController::walletCreateSucceeded};
    QSignalSpy loaded{&controller, &WalletQmlController::walletLoadSucceeded};
    QSignalSpy imported{&controller, &WalletQmlController::walletImportSucceeded};
    QSignalSpy migrated{&controller, &WalletQmlController::walletMigrationSucceeded};
    QSignalSpy migration_failed{&controller, &WalletQmlController::walletMigrationFailed};
    if (operation == "create") controller.createSingleSigWallet("late-wallet", "");
    else if (operation == "load") controller.setSelectedWallet("late-wallet", "sqlite");
    else if (operation == "import") controller.importWallet(backup.fileName());
    else controller.migrateWallet("late-wallet", "");
    QTRY_VERIFY_WITH_TIMEOUT(probe->entered.available() > 0, 5000);
    QSignalSpy drained{&controller, &WalletQmlController::walletsDrained};
    controller.beginShutdown();
    controller.beginShutdown();
    bool heartbeat{false};
    QTimer::singleShot(0, &controller, [&] { heartbeat = true; });
    QTRY_VERIFY(heartbeat);
    QVERIFY(drained.empty());
    probe->release.release();
    QTRY_COMPARE_WITH_TIMEOUT(drained.size(), 1, 5000);
    QCOMPARE(probe->destroyed.load(), !throws);
    QCOMPARE(probe->off_gui.load(), !throws);
    QVERIFY(!controller.isWalletOpen("late-wallet"));
    QCOMPARE(created.size() + loaded.size() + imported.size() + migrated.size() + migration_failed.size(), 0);
    QVERIFY(controller.walletCreateError().isEmpty());
    QVERIFY(controller.walletLoadError().isEmpty());
    QVERIFY(controller.walletMigrationError().isEmpty());
}

void WalletQmlControllerTests::injectedControllerServicesRejectPartialGraphBeforeAdmission()
{
    MockNode node;
    ManualRoutedExecution native;
    WalletQmlController::Services services;
    services.scheduler = native.scheduler;
    const auto rejects_partial = [&](WalletQmlController::Services partial) {
        try {
            WalletQmlController rejected{node, std::move(partial)};
        } catch (const std::invalid_argument&) {
            return true;
        } catch (...) {
            return false;
        }
        return false;
    };
    QVERIFY(rejects_partial(services));
    services.shutdown = [](auto) {};
    QVERIFY(rejects_partial(services));
    services.scheduler.reset();
    services.post = [](auto) {};
    QVERIFY(rejects_partial(services));
    QCOMPARE(node.calls.walletLoader.load(), 0);
    QCOMPARE(native.scheduler->pending(), size_t{0});
    QVERIFY(native.queuedIdle());
}

void WalletQmlControllerTests::manualMatchingLoadNotificationAndResult_data()
{
    QTest::addColumn<bool>("notification_first");
    QTest::addColumn<int>("later_selection");
    for (bool first : {false, true}) {
        for (int selection = 0; selection < 5; ++selection) {
            const QByteArray tag = QByteArray(first ? "notification-first-" : "result-first-") + QByteArray::number(selection);
            QTest::newRow(tag.constData()) << first << selection;
        }
    }
}

void WalletQmlControllerTests::manualMatchingLoadNotificationAndResult()
{
    QFETCH(bool, notification_first);
    QFETCH(int, later_selection); // none, accepted original, null, foreign, closing.
    FakeWallet::State original_state, standby_state, loaded_state, foreign_state;
    MockNode node, foreign_node;
    FakeWalletLoader loader, foreign_loader;
    ConfigureExpectedControllerInitialization(node, loader);
    ConfigureExpectedControllerInitialization(foreign_node, foreign_loader);
    loader.get_wallets_fn = [&] {
        std::vector<std::unique_ptr<interfaces::Wallet>> wallets;
        wallets.push_back(std::make_unique<FakeWallet>("original", &original_state));
        wallets.push_back(std::make_unique<FakeWallet>("standby", &standby_state));
        return wallets;
    };
    ManualControllerGraph graph{node};
    auto& controller = *graph.controller;
    QVERIFY(!controller.m_wallet_command_executor); // No real-thread controller fallback.
    controller.initialize();
    QVERIFY(graph.until([&] { return controller.initialized() && controller.walletForName("original") && controller.walletForName("original")->capabilitiesKnown(); }));
    auto* original = controller.walletForName("original");
    QCOMPARE(controller.selectedWallet(), original);
    std::unique_ptr<ManualControllerGraph> foreign;
    if (later_selection == 3) {
        foreign_loader.get_wallets_fn = [&] { std::vector<std::unique_ptr<interfaces::Wallet>> wallets; wallets.push_back(std::make_unique<FakeWallet>("foreign", &foreign_state)); return wallets; };
        foreign = std::make_unique<ManualControllerGraph>(foreign_node);
        foreign->controller->initialize();
        QVERIFY(foreign->until([&] { return foreign->controller->initialized(); }));
    }
    QPointer<WalletQmlModel> closing;
    std::shared_ptr<interfaces::Wallet> held;
    if (later_selection == 4) {
        closing = controller.walletForName("standby"); QVERIFY(closing);
        auto borrow = closing->opening()->borrow(); QVERIFY(borrow);
        held = std::move(borrow);
        controller.closeWallet("standby");
        QVERIFY(graph.until([&] { return !controller.isWalletOpen("standby"); }));
        QVERIFY(closing); QVERIFY(!closing->available());
    }
    struct Handles { std::atomic<int> destroyed{0}; std::atomic_bool native{true}; };
    const auto handles = std::make_shared<Handles>();
    struct LoadedHandle final : FakeWallet {
        std::shared_ptr<Handles> handles; std::atomic<int>* lane; QThread* gui;
        LoadedHandle(State* state, std::shared_ptr<Handles> value, std::atomic<int>* execution_lane)
            : FakeWallet("target", state), handles(std::move(value)), lane(execution_lane), gui(QThread::currentThread()) {}
        ~LoadedHandle() override {
            const bool native = QThread::currentThread() != gui || lane->load() == int(execution::Lane::Core) || lane->load() == int(execution::Lane::Cleanup);
            if (!native) handles->native = false;
            ++handles->destroyed;
        }
    };
    loader.load_wallet_fn = [&](const auto&, auto&) -> util::Result<std::unique_ptr<interfaces::Wallet>> {
        loader.captured_load_wallet_fn(std::make_unique<LoadedHandle>(&loaded_state, handles, &graph.lane));
        return std::unique_ptr<interfaces::Wallet>{std::make_unique<LoadedHandle>(&loaded_state, handles, &graph.lane)};
    };
    QSignalSpy loaded{&controller, &WalletQmlController::walletLoadSucceeded};
    QSignalSpy inserted{controller.openWalletModel(), &QAbstractItemModel::rowsInserted};
    controller.setSelectedWallet("target", "sqlite");
    const auto ticket = controller.walletOperationId();
    auto command = controller.m_command; QVERIFY(command);
    const auto selection = controller.m_selection_revision;
    if (later_selection == 1) QVERIFY(controller.selectWallet(original));
    if (later_selection == 2) QVERIFY(!controller.selectWallet(nullptr));
    if (later_selection == 3) QVERIFY(!controller.selectWallet(foreign->controller->selectedWallet()));
    if (later_selection == 4) QVERIFY(!controller.selectWallet(closing));
    QCOMPARE(controller.m_selection_revision, selection + (later_selection == 1 ? 1 : 0));
    QCOMPARE(controller.walletOperationId(), ticket);
    QCOMPARE(loader.load_wallet_calls, 0);
    QVERIFY(graph.step(execution::Lane::Core));
    QCOMPARE(loader.load_wallet_calls, 1);
    QVERIFY(!command->result());
    QVERIFY(graph.step(execution::Lane::Domain));
    QVERIFY(command->result()); // Original result is owned, but GUI has not consumed it.
    QCOMPARE(graph.posts.size(), size_t{1});
    auto command_notice = std::move(graph.posts.front()); graph.posts.pop_front();
    graph.runNative();
    QCOMPARE(graph.posts.size(), size_t{1});
    auto notification_notice = std::move(graph.posts.front()); graph.posts.pop_front();
    QVERIFY(controller.walletLoadInProgress());
    if (notification_first) notification_notice(); else command_notice();
    QCOMPARE(loaded.size(), notification_first ? 0 : 1);
    QCOMPARE(controller.walletLoadInProgress(), notification_first);
    auto* first = controller.walletForName("target"); QVERIFY(first);
    const auto identity = first->opening()->identity();
    if (notification_first) command_notice(); else notification_notice();
    QVERIFY(graph.until([&] { return first->capabilitiesKnown(); }));
    QCOMPARE(controller.walletForName("target"), first);
    QVERIFY(first->opening()->identity() == identity);
    QCOMPARE(inserted.size(), 1);
    QCOMPARE(loaded.size(), 1);
    QCOMPARE(loaded.front().front().value<WalletQmlModel*>(), first);
    QCOMPARE(controller.walletOperationId(), ticket);
    QCOMPARE(controller.walletOperationState(), WalletQmlController::WalletOperationState::WalletOperationSucceeded);
    QCOMPARE(controller.walletOperationName(), QStringLiteral("target"));
    QCOMPARE(controller.selectedWallet(), later_selection == 1 ? original : first);
    command_notice(); notification_notice(); // Reliable transport retries cannot replay either publication.
    QCOMPARE(loaded.size(), 1); QCOMPARE(inserted.size(), 1);
    QCOMPARE(controller.selectedWallet(), later_selection == 1 ? original : first);
    QCOMPARE(loader.load_wallet_calls, 1);
    held.reset(); command.reset();
    graph.shutdown();
    QCOMPARE(handles->destroyed.load(), 2);
    QVERIFY(handles->native.load());
    QCOMPARE(loaded_state.private_accesses.load(), 0);
}

void WalletQmlControllerTests::manualOldOpeningPromptAndOutcomeCannotResolveReopenedName()
{
    FakeWallet::State original_state, reopened_state, survivor_state;
    MockNode node;
    FakeWalletLoader loader;
    ConfigureExpectedControllerInitialization(node, loader);
    loader.get_wallets_fn = [&] {
        std::vector<std::unique_ptr<interfaces::Wallet>> wallets;
        wallets.push_back(std::make_unique<FakeWallet>("survivor", &survivor_state));
        return wallets;
    };
    loader.load_wallet_fn = [&](const auto&, auto&) -> util::Result<std::unique_ptr<interfaces::Wallet>> {
        auto* state = loader.load_wallet_calls == 1 ? &original_state : &reopened_state;
        loader.captured_load_wallet_fn(std::make_unique<FakeWallet>("reloaded", state));
        return std::unique_ptr<interfaces::Wallet>{std::make_unique<FakeWallet>("reloaded", state)};
    };
    ManualControllerGraph graph{node};
    auto& controller = *graph.controller;
    controller.initialize();
    QVERIFY(graph.until([&] { return controller.initialized() && controller.selectedWallet()->capabilitiesKnown(); }));
    auto* survivor = controller.selectedWallet();
    QSignalSpy loaded{&controller, &WalletQmlController::walletLoadSucceeded};
    controller.setSelectedWallet("./reloaded", "sqlite"); // Original request alias is not the canonical name.
    const auto original_ticket = controller.walletOperationId();
    auto original_command = controller.m_command;
    const auto original_receipt = controller.m_command_receipt;
    QVERIFY(graph.step(execution::Lane::Core));
    QVERIFY(graph.step(execution::Lane::Domain));
    QCOMPARE(graph.posts.size(), size_t{1});
    auto delayed_result = std::move(graph.posts.front());
    graph.posts.pop_front();
    graph.runNative();
    QCOMPARE(graph.posts.size(), size_t{1});
    auto notification = std::move(graph.posts.front());
    graph.posts.pop_front();
    notification();
    QPointer<WalletQmlModel> original{controller.walletForName("reloaded")};
    QVERIFY(original);
    QVERIFY(graph.until([&] { return original->capabilitiesKnown(); }));
    const auto original_opening = original->opening();
    const auto original_identity = original_opening->identity();
    controller.requestCloseWallet(original);
    QVERIFY(graph.until([&] { return !original->closeQuestionToken().isEmpty(); }));
    const QString old_question = original->closeQuestionToken();
    auto held = original_opening->borrow();
    QVERIFY(held);
    const auto* native_original = held.get();
    std::optional<bool> original_outcome;
    std::function<void()> delayed_old_gui;
    int native_reads{0}, stale_gui_writes{0};
    QVERIFY(original_opening->read([&](interfaces::Wallet& wallet) {
        ++native_reads;
        return &wallet == native_original && wallet.getWalletName() == "reloaded";
    }, [&, original, original_identity](bool outcome) {
        original_outcome = outcome;
        delayed_old_gui = [&, original, original_identity] {
            if (original && original->available() && original->opening()->identity() == original_identity) ++stale_gui_writes;
        };
    }));
    QVERIFY(graph.step(execution::Lane::Core));
    QVERIFY(!original_outcome);
    controller.closeWallet("reloaded");
    QVERIFY(original);
    QVERIFY(!original->available());
    QVERIFY(!controller.selectWallet(original));
    controller.requestCloseWallet(original);
    controller.setSelectedWallet("reloaded", "sqlite");
    QCOMPARE(loader.load_wallet_calls, 1);
    QVERIFY(controller.walletLoadError().contains(QStringLiteral("still closing")));
    graph.runNative();
    QVERIFY(original_outcome && *original_outcome);
    QCOMPARE(native_reads, 1);
    delayed_result(); // The actual successful command cannot resurrect its closed adopted opening.
    QVERIFY(!controller.isWalletOpen("reloaded"));
    QCOMPARE(controller.selectedWallet(), survivor);
    QCOMPARE(controller.walletOperationId(), original_ticket);
    QCOMPARE(controller.walletOperationState(), WalletQmlController::WalletOperationState::WalletOperationSucceeded);
    QCOMPARE(controller.walletOperationAdoptionState(), WalletQmlController::WalletAdoptionState::AdoptionClosed);
    QVERIFY(!controller.walletOperationAdoptionError().isEmpty());
    QCOMPARE(loaded.size(), 0);
    QVERIFY(original_command->result());
    QVERIFY(original_command->result()->value);
    QCOMPARE(original_command->result()->value->name, QStringLiteral("reloaded"));
    original_command.reset();
    held.reset();
    QVERIFY(graph.until([&] { return original.isNull(); }));
    QCOMPARE(original_state.remove_calls.load(), 1);
    controller.setSelectedWallet("reloaded", "sqlite");
    QVERIFY(graph.until([&] { return controller.walletForName("reloaded") && controller.walletForName("reloaded")->capabilitiesKnown() && !controller.walletLoadInProgress(); }));
    auto* reopened = controller.walletForName("reloaded");
    QVERIFY(reopened);
    QVERIFY(reopened->opening()->identity() != original_identity);
    QCOMPARE(loader.load_wallet_calls, 2);
    QCOMPARE(loaded.size(), 1);
    QCOMPARE(controller.selectedWallet(), reopened);
    controller.requestCloseWallet(reopened);
    QVERIFY(graph.until([&] { return !reopened->closeQuestionToken().isEmpty(); }));
    const QString new_question = reopened->closeQuestionToken();
    reopened->answerClose(old_question, true);
    delayed_result();
    notification();
    QVERIFY(delayed_old_gui);
    delayed_old_gui();
    QVERIFY(graph.until([&] { return reopened->available(); }));
    QCOMPARE(reopened->closeQuestionToken(), new_question);
    QCOMPARE(stale_gui_writes, 0);
    QCOMPARE(reopened_state.remove_calls.load(), 0);
    QCOMPARE(controller.walletForName("reloaded"), reopened);
    QCOMPARE(controller.selectedWallet(), reopened);
    QCOMPARE(loaded.size(), 1);
    {
        std::lock_guard lock{original_receipt->mutex};
        QCOMPARE(original_receipt->state, WalletQmlController::WalletOperationState::WalletOperationSucceeded);
        QCOMPARE(original_receipt->name, QStringLiteral("reloaded"));
    }
    delayed_result = {};
    notification = {};
    delayed_old_gui = {};
}

void WalletQmlControllerTests::manualUnknownBindingNotificationCannotResurrectOrResolveReopenedName_data()
{
    QTest::addColumn<bool>("reopen_first");
    QTest::addColumn<bool>("many_native_events");
    QTest::newRow("late-before-reopen-native-ahead") << false << true;
    QTest::newRow("late-after-reopen-native-ahead") << true << true;
    QTest::newRow("late-before-reopen-origins-ahead") << false << false;
    QTest::newRow("late-after-reopen-origins-ahead") << true << false;
}

void WalletQmlControllerTests::manualUnknownBindingNotificationCannotResurrectOrResolveReopenedName()
{
    QFETCH(bool, reopen_first);
    QFETCH(bool, many_native_events);
    FakeWallet::State old_state, new_state, survivor_state;
    MockNode node;
    FakeWalletLoader loader;
    ConfigureExpectedControllerInitialization(node, loader);
    loader.get_wallets_fn = [&] {
        std::vector<std::unique_ptr<interfaces::Wallet>> wallets;
        wallets.push_back(std::make_unique<FakeWallet>("original", &old_state));
        wallets.push_back(std::make_unique<FakeWallet>("survivor", &survivor_state));
        return wallets;
    };
    loader.load_wallet_fn = [&](const auto&, auto&) -> util::Result<std::unique_ptr<interfaces::Wallet>> {
        return std::unique_ptr<interfaces::Wallet>{std::make_unique<FakeWallet>("original", &new_state)};
    };
    ManualControllerGraph graph{node};
    auto& controller = *graph.controller;
    controller.initialize();
    QVERIFY(graph.until([&] { return controller.initialized() && controller.walletForName("original")->capabilitiesKnown(); }));
    QPointer<WalletQmlModel> old{controller.walletForName("original")};
    auto* survivor = controller.walletForName("survivor");
    const auto identity = old->opening()->identity();
    // The two monotonic domains need not start at the same offset. Native
    // freshness must not determine origin-lifetime retention, or vice versa.
    if (many_native_events) controller.m_adoption_clock->value += 100000;
    else controller.m_origin_sequence += 100000;
    // Initial facades have stable tokens, but this source has never adopted
    // the name through its notification map. Hold its real queued publication.
    loader.captured_load_wallet_fn(std::make_unique<FakeWallet>("original", &old_state));
    graph.runNative();
    QCOMPARE(graph.posts.size(), size_t{1});
    auto late = std::move(graph.posts.front());
    graph.posts.pop_front();
    controller.closeWallet("original");
    QVERIFY(graph.until([&] { return old.isNull(); }));
    QCOMPARE(old_state.remove_calls.load(), 1);
    QVERIFY(!controller.isWalletOpen("original"));
    QCOMPARE(controller.adoptionFenceCount(), size_t{1}); // Held old ingress still needs its escrow.
    if (reopen_first) {
        controller.setSelectedWallet("./original", "sqlite");
        QVERIFY(graph.until([&] { return controller.walletForName("original") && controller.walletForName("original")->capabilitiesKnown() && !controller.walletLoadInProgress(); }));
        QVERIFY(controller.walletForName("original")->opening()->identity() != identity);
    }
    QSignalSpy states{&controller, &WalletQmlController::walletLoadStateChanged};
    late();
    QVERIFY(graph.until([] { return true; }));
    QVERIFY(graph.until([&] { return controller.adoptionFenceCount() == 0; })); // Origin lifetime, not Native counter, retires escrow.
    QCOMPARE(states.size(), 0); // Old metadata cannot even republish the replacement as its outcome.
    QCOMPARE(controller.isWalletOpen("original"), reopen_first);
    if (!reopen_first) {
        QCOMPARE(controller.selectedWallet(), survivor);
        controller.setSelectedWallet("original", "sqlite");
        QVERIFY(graph.until([&] { return controller.walletForName("original") && controller.walletForName("original")->capabilitiesKnown() && !controller.walletLoadInProgress(); }));
    }
    QCOMPARE(loader.load_wallet_calls, 1);
    auto* actual = controller.walletForName("original");
    QVERIFY(actual);
    QVERIFY(actual->opening()->identity() != identity);
    QCOMPARE(new_state.remove_calls.load(), 0);
    states.clear();
    loader.captured_load_wallet_fn(std::make_unique<FakeWallet>("survivor", &survivor_state));
    QVERIFY(graph.until([&] { return !states.empty(); }));
    QCOMPARE(states.size(), 1);
    QCOMPARE(states.front().front().toString(), QStringLiteral("survivor"));
    QCOMPARE(controller.walletForName("survivor"), survivor);
    QCOMPARE(controller.selectedWallet(), survivor);
    QCOMPARE(controller.openWalletModel()->rowCount(), 2);
    QCOMPARE(survivor_state.remove_calls.load(), 0);
    late = {};
}

void WalletQmlControllerTests::manualObservedUnloadAllowsFreshSameNameLoad_data()
{
    QTest::addColumn<bool>("unload_gui_first");
    QTest::newRow("old-unload-gui-before-fresh-load") << true;
    QTest::newRow("fresh-load-before-old-unload-gui") << false;
}

void WalletQmlControllerTests::manualObservedUnloadAllowsFreshSameNameLoad()
{
    QFETCH(bool, unload_gui_first);
    FakeWallet::State old_state, fresh_state, survivor_state;
    MockNode node;
    FakeWalletLoader loader;
    ConfigureExpectedControllerInitialization(node, loader);
    loader.get_wallets_fn = [&] {
        std::vector<std::unique_ptr<interfaces::Wallet>> wallets;
        wallets.push_back(std::make_unique<FakeWallet>("survivor", &survivor_state));
        return wallets;
    };
    loader.load_wallet_fn = [&](const auto&, auto&) -> util::Result<std::unique_ptr<interfaces::Wallet>> {
        loader.captured_load_wallet_fn(std::make_unique<FakeWallet>("same-name", &old_state));
        return std::unique_ptr<interfaces::Wallet>{std::make_unique<FakeWallet>("same-name", &old_state)};
    };
    ManualControllerGraph graph{node};
    auto& controller = *graph.controller;
    controller.initialize();
    QVERIFY(graph.until([&] { return controller.initialized() && controller.selectedWallet()->capabilitiesKnown(); }));
    auto* survivor = controller.selectedWallet();
    controller.setSelectedWallet("./same-name", "sqlite");
    const auto ticket = controller.walletOperationId();
    QVERIFY(graph.step(execution::Lane::Core));
    QVERIFY(graph.step(execution::Lane::Domain));
    QCOMPARE(graph.posts.size(), size_t{1});
    auto original_result = std::move(graph.posts.front());
    graph.posts.pop_front();
    graph.runNative();
    QCOMPARE(graph.posts.size(), size_t{1});
    auto original_notification = std::move(graph.posts.front());
    graph.posts.pop_front();
    original_notification();
    QPointer<WalletQmlModel> old{controller.walletForName("same-name")};
    QVERIFY(old);
    QVERIFY(graph.until([&] { return old->capabilitiesKnown() && old_state.hasUnload(); }));
    const auto identity = old->opening()->identity();
    old_state.notifyUnload(); // Public Native unload observation, before any GUI delivery.
    QVERIFY(!old->available());
    // Advance model/backend retirement, but hold the Controller's actual old
    // unload post. The new Core load uses the still-original GUI context.
    QVERIFY(QTest::qWaitFor([&] {
        graph.runNative();
        QCoreApplication::processEvents();
        return old && old->shutdownComplete();
    }, 5000));
    auto old_posts = std::move(graph.posts);
    graph.posts.clear();
    QVERIFY(!old_posts.empty());
    loader.captured_load_wallet_fn(std::make_unique<FakeWallet>("same-name", &fresh_state));
    graph.runNative();
    QCOMPARE(graph.posts.size(), size_t{1});
    auto fresh_notification = std::move(graph.posts.front());
    graph.posts.pop_front();
    const auto publish_unload = [&] {
        while (!old_posts.empty()) {
            auto post = std::move(old_posts.front());
            old_posts.pop_front();
            post();
        }
    };
    if (unload_gui_first) publish_unload();
    fresh_notification();
    if (!unload_gui_first) publish_unload();
    QVERIFY(graph.until([&] {
        auto* fresh = controller.walletForName("same-name");
        return fresh && fresh->available() && fresh->capabilitiesKnown() && fresh->opening()->identity() != identity;
    }));
    auto* fresh = controller.walletForName("same-name");
    QVERIFY(controller.selectWallet(survivor)); // Independent valid selection wins held old outcomes.
    original_result();
    original_notification();
    QVERIFY(graph.until([&] { return !controller.walletLoadInProgress(); }));
    QCOMPARE(controller.walletOperationId(), ticket);
    QCOMPARE(controller.walletOperationState(), WalletQmlController::WalletOperationState::WalletOperationSucceeded);
    QCOMPARE(controller.walletOperationAdoptionState(), WalletQmlController::WalletAdoptionState::AdoptionClosed);
    QCOMPARE(controller.walletForName("same-name"), fresh);
    QCOMPARE(controller.selectedWallet(), survivor);
    QCOMPARE(controller.openWalletModel()->rowCount(), 2);
    QCOMPARE(loader.load_wallet_calls, 1);
    QCOMPARE(old_state.remove_calls.load(), 0);
    QCOMPARE(fresh_state.remove_calls.load(), 0);
    QCOMPARE(survivor_state.remove_calls.load(), 0);
    original_result = {};
    original_notification = {};
    fresh_notification = {};
}

void WalletQmlControllerTests::manualInsertionCallbacksPreserveOriginalAdoption_data()
{
    QTest::addColumn<bool>("about_to_insert");
    QTest::addColumn<int>("intent");
    QTest::newRow("about-close-original") << true << 0;
    QTest::newRow("inserted-close-original") << false << 0;
    QTest::newRow("about-select-independent") << true << 1;
    QTest::newRow("inserted-select-independent") << false << 1;
    QTest::newRow("about-destroy-controller") << true << 2;
    QTest::newRow("inserted-destroy-controller") << false << 2;
}

void WalletQmlControllerTests::manualInsertionCallbacksPreserveOriginalAdoption()
{
    QFETCH(bool, about_to_insert);
    QFETCH(int, intent);
    FakeWallet::State survivor_state, target_state;
    MockNode node;
    FakeWalletLoader loader;
    ConfigureExpectedControllerInitialization(node, loader);
    loader.get_wallets_fn = [&] {
        std::vector<std::unique_ptr<interfaces::Wallet>> wallets;
        wallets.push_back(std::make_unique<FakeWallet>("survivor", &survivor_state));
        return wallets;
    };
    loader.load_wallet_fn = [&](const auto&, auto&) -> util::Result<std::unique_ptr<interfaces::Wallet>> {
        return std::unique_ptr<interfaces::Wallet>{std::make_unique<FakeWallet>("target", &target_state)};
    };
    QObject receiver;
    ManualControllerGraph graph{node};
    auto* controller = graph.controller.get();
    controller->initialize();
    QVERIFY(graph.until([&] { return controller->initialized() && controller->selectedWallet()->capabilitiesKnown(); }));
    auto* survivor = controller->selectedWallet();
    QPointer<WalletQmlModel> target;
    int callbacks{0}, target_open{0};
    auto on_insert = [&] {
        if (callbacks++) return;
        target = controller->walletForName("target");
        QVERIFY(target);
        if (intent == 0) controller->closeWallet("target");
        if (intent == 1) QVERIFY(controller->selectWallet(survivor));
        if (intent == 2) graph.controller.reset();
    };
    if (about_to_insert) QObject::connect(controller->openWalletModel(), &QAbstractItemModel::rowsAboutToBeInserted, &receiver, on_insert);
    else QObject::connect(controller->openWalletModel(), &QAbstractItemModel::rowsInserted, &receiver, on_insert);
    QObject::connect(controller, &WalletQmlController::walletLoadStateChanged, &receiver,
        [&](const QString& name, WalletListModel::LoadState state, const QString&) {
            if (name == QStringLiteral("target") && state == WalletListModel::LoadState::Open) ++target_open;
        });
    controller->setSelectedWallet("target", "sqlite");
    const auto receipt = controller->m_command_receipt;
    QVERIFY(graph.until([&] {
        if (intent == 2) return !graph.controller && graph.physical_completions == 1;
        return !controller->walletLoadInProgress() && (intent != 0 || target.isNull());
    }));
    QCOMPARE(callbacks, 1);
    QCOMPARE(loader.load_wallet_calls, 1);
    {
        std::lock_guard lock{receipt->mutex};
        QCOMPARE(receipt->state, WalletQmlController::WalletOperationState::WalletOperationSucceeded);
    }
    if (intent == 2) {
        QCOMPARE(graph.native.scheduler->pending(), size_t{0});
        QCOMPARE(target_open, 0);
        return;
    }
    QCOMPARE(controller->selectedWallet(), survivor);
    QCOMPARE(controller->openWalletModel()->rowCount(), intent == 0 ? 1 : 2);
    QCOMPARE(controller->openWalletModel()->data(controller->openWalletModel()->index(0, 0), Qt::UserRole + 1).value<WalletQmlModel*>(), survivor);
    if (intent == 0) {
        QVERIFY(!controller->isWalletOpen("target"));
        QCOMPARE(target_open, 0);
        QCOMPARE(controller->walletOperationAdoptionState(), WalletQmlController::WalletAdoptionState::AdoptionClosed);
        QCOMPARE(target_state.remove_calls.load(), 1);
    } else {
        QCOMPARE(controller->walletForName("target"), target.data());
        QCOMPARE(controller->openWalletModel()->data(controller->openWalletModel()->index(1, 0), Qt::UserRole + 1).value<WalletQmlModel*>(), target.data());
        QCOMPARE(target_open, 1);
        QCOMPARE(target_state.remove_calls.load(), 0);
    }
    QCOMPARE(survivor_state.remove_calls.load(), 0);
}

void WalletQmlControllerTests::manualAdoptionChargesCanonicalKeyAndKeepsActualNativeSuccess()
{
    MockNode node;
    FakeWalletLoader loader;
    FakeWallet::State native_state;
    ConfigureExpectedControllerInitialization(node, loader);
    const QString canonical = QStringLiteral("canonical-") + QString(300, QLatin1Char('x'));
    loader.load_wallet_fn = [&](const auto&, auto&) -> util::Result<std::unique_ptr<interfaces::Wallet>> {
        return std::unique_ptr<interfaces::Wallet>{std::make_unique<FakeWallet>(canonical.toStdString(), &native_state)};
    };
    auto limits = execution::Limits::Bounded();
    limits.cleanup.payload_bytes = 1280; // Fixed512 ownership fits; dynamic canonical binding does not.
    ManualControllerGraph graph{node, limits};
    auto& controller = *graph.controller;
    controller.initialize();
    QVERIFY(graph.until([&] { return controller.initialized(); }));
    QSignalSpy loaded{&controller, &WalletQmlController::walletLoadSucceeded};
    controller.setSelectedWallet("alias", "sqlite");
    QVERIFY(graph.until([&] { return !controller.walletLoadInProgress(); }));
    QCOMPARE(loader.load_wallet_calls, 1);
    QCOMPARE(controller.walletOperationState(), WalletQmlController::WalletOperationState::WalletOperationSucceeded);
    QCOMPARE(controller.walletOperationName(), canonical);
    QCOMPARE(controller.walletOperationAdoptionState(), WalletQmlController::WalletAdoptionState::AdoptionUnavailable);
    QVERIFY(!controller.walletOperationAdoptionError().isEmpty());
    QCOMPARE(loaded.size(), 0);
    QCOMPARE(controller.openWalletModel()->rowCount(), 0);
    QCOMPARE(native_state.remove_calls.load(), 0);
}

void WalletQmlControllerTests::manualCloseEscrowRefusalKeepsActualSuccessWithoutResurrection()
{
    MockNode node;
    FakeWalletLoader loader;
    FakeWallet::State state, survivor_state;
    ConfigureExpectedControllerInitialization(node, loader);
    loader.get_wallets_fn = [&] {
        std::vector<std::unique_ptr<interfaces::Wallet>> wallets;
        wallets.push_back(std::make_unique<FakeWallet>("survivor", &survivor_state));
        return wallets;
    };
    loader.load_wallet_fn = [&](const auto&, auto&) -> util::Result<std::unique_ptr<interfaces::Wallet>> {
        loader.captured_load_wallet_fn(std::make_unique<FakeWallet>("target", &state));
        return std::unique_ptr<interfaces::Wallet>{std::make_unique<FakeWallet>("target", &state)};
    };
    std::shared_ptr<ControllerBoundaryScheduler> boundary;
    ManualControllerGraph graph{node, execution::Limits::Bounded(), [&](auto target) {
        boundary = std::make_shared<ControllerBoundaryScheduler>(std::move(target));
        return boundary;
    }};
    auto& controller = *graph.controller;
    controller.initialize();
    QVERIFY(graph.until([&] { return controller.initialized(); }));
    controller.setSelectedWallet("target", "sqlite");
    const auto receipt = controller.m_command_receipt;
    QVERIFY(graph.step(execution::Lane::Core));
    QVERIFY(graph.step(execution::Lane::Domain));
    QCOMPARE(graph.posts.size(), size_t{1});
    auto delayed = std::move(graph.posts.front());
    graph.posts.pop_front();
    graph.runNative();
    QCOMPARE(graph.posts.size(), size_t{1});
    auto notification = std::move(graph.posts.front());
    graph.posts.pop_front();
    notification();
    QVERIFY(graph.until([&] { return controller.walletForName("target") && controller.walletForName("target")->capabilitiesKnown(); }));
    QPointer<WalletQmlModel> original{controller.walletForName("target")};
    boundary->reject_cleanup_above = 700; // Fixed origins still fit; dynamic close escrow is refused.
    controller.closeWallet("target");
    delayed();
    QCOMPARE(controller.walletOperationState(), WalletQmlController::WalletOperationState::WalletOperationSucceeded);
    QCOMPARE(controller.walletOperationAdoptionState(), WalletQmlController::WalletAdoptionState::AdoptionUnavailable);
    QVERIFY(!controller.walletOperationAdoptionError().isEmpty());
    QVERIFY(!controller.isWalletOpen("target"));
    QCOMPARE(loader.load_wallet_calls, 1);
    {
        std::lock_guard lock{receipt->mutex};
        QVERIFY(receipt->ready);
        QCOMPARE(receipt->state, WalletQmlController::WalletOperationState::WalletOperationSucceeded);
    }
    boundary->reject_cleanup_above.reset();
    QVERIFY(graph.until([&] { return original.isNull(); }));
    QCOMPARE(state.remove_calls.load(), 1);
    QCOMPARE(survivor_state.remove_calls.load(), 0);
    delayed = {};
    notification = {};
}

void WalletQmlControllerTests::manualControllerAdmissionSurvivesReentrantOwnerLoss_data()
{
    QTest::addColumn<bool>("after_borrow");
    QTest::newRow("after-cleanup-reservation") << false;
    QTest::newRow("after-original-borrow") << true;
}
void WalletQmlControllerTests::manualControllerAdmissionSurvivesReentrantOwnerLoss()
{
    QFETCH(bool, after_borrow);
    MockNode node;
    FakeWalletLoader loader;
    ConfigureExpectedControllerInitialization(node, loader);
    std::shared_ptr<ControllerBoundaryScheduler> boundary;
    ManualControllerGraph graph{node, execution::Limits::Bounded(), [&](auto target) {
        boundary = std::make_shared<ControllerBoundaryScheduler>(std::move(target));
        return boundary;
    }};
    graph.controller->initialize();
    QVERIFY(graph.until([&] { return graph.controller->initialized(); }));
    const QPointer<WalletQmlController> observed{graph.controller.get()};
    const auto retire = [&] { graph.controller.reset(); };
    if (after_borrow) boundary->after_borrow = retire;
    else boundary->after_reserve = retire;
    observed->setSelectedWallet("never-loaded", "sqlite");
    QVERIFY(!observed);
    QCOMPARE(loader.load_wallet_calls, 0);
    QVERIFY(graph.until([&] { return graph.physical_completions == 1 && graph.native.scheduler->pending() == 0; }));
}

void WalletQmlControllerTests::manualInjectedShutdownOwnsAcceptedOutcomeAfterObserverLoss_data()
{
    QTest::addColumn<bool>("destroy_observer");
    QTest::newRow("quiet-controller-stays-live") << false;
    QTest::newRow("controller-destroyed") << true;
}
void WalletQmlControllerTests::manualInjectedShutdownOwnsAcceptedOutcomeAfterObserverLoss()
{
    QFETCH(bool, destroy_observer);
    MockNode node;
    FakeWalletLoader loader;
    FakeWallet::State state;
    ConfigureExpectedControllerInitialization(node, loader);
    loader.load_wallet_fn = [&](const auto&, auto&) -> util::Result<std::unique_ptr<interfaces::Wallet>> {
        return std::unique_ptr<interfaces::Wallet>{std::make_unique<FakeWallet>("accepted", &state)};
    };
    ManualControllerGraph graph{node};
    graph.controller->initialize();
    QVERIFY(graph.until([&] { return graph.controller->initialized(); }));
    graph.controller->setSelectedWallet("accepted", "sqlite");
    const auto receipt = graph.controller->m_command_receipt;
    const QPointer<WalletQmlController> observed{graph.controller.get()};
    QSignalSpy loaded{graph.controller.get(), &WalletQmlController::walletLoadSucceeded};
    QCOMPARE(loader.load_wallet_calls, 0);
    graph.controller->beginShutdown();
    QVERIFY(!graph.drained);
    QCOMPARE(graph.physical_completions, 0);
    if (destroy_observer) graph.controller.reset();
    QVERIFY(graph.until([&] { return graph.physical_completions == 1 && graph.native.scheduler->pending() == 0; }));
    QCOMPARE(loader.load_wallet_calls, 1);
    QCOMPARE(loaded.size(), 0);
    {
        std::lock_guard lock{receipt->mutex};
        QVERIFY(receipt->ready);
        QCOMPARE(receipt->state, WalletQmlController::WalletOperationState::WalletOperationSucceeded);
        QCOMPARE(receipt->name, QStringLiteral("accepted"));
    }
    QCOMPARE(bool(observed), !destroy_observer);
    if (observed) QVERIFY(!observed->isWalletOpen("accepted"));
    QCOMPARE(state.private_accesses.load(), 0);
}

void WalletQmlControllerTests::acceptedCreationDoesNotRetargetLaterSelection()
{
    FakeWallet::State original_state, created_state;
    MockNode node; FakeWalletLoader loader;
    loader.get_wallets_fn = [&] {
        std::vector<std::unique_ptr<interfaces::Wallet>> wallets;
        wallets.push_back(std::make_unique<FakeWallet>("original", &original_state)); return wallets;
    };
    ConfigureExpectedControllerInitialization(node, loader);
    auto entered = std::make_shared<QSemaphore>(), release = std::make_shared<QSemaphore>();
    DrainingWalletController controller{node}; controller.initialize(); QTRY_VERIFY(controller.initialized());
    auto* original = controller.selectedWallet(); QVERIFY(original);
    loader.create_wallet_fn = [&, entered, release](const std::string& name, const SecureString&, uint64_t, std::vector<bilingual_str>&) {
        entered->release(); release->acquire();
        return util::Result<std::unique_ptr<interfaces::Wallet>>{std::make_unique<FakeWallet>(name, &created_state)};
    };
    const auto unblock = qScopeGuard([release] { release->release(); });
    QSignalSpy created{&controller, &WalletQmlController::walletCreateSucceeded};
    controller.createSingleSigWallet("created", "");
    QVERIFY(entered->tryAcquire(1, 5000));
    const auto reads = loader.list_wallet_dir_calls;
    QVERIFY(controller.selectWallet(original)); // Explicitly reaffirm the later selection.
    QCOMPARE(loader.list_wallet_dir_calls, reads);
    release->release();
    QTRY_COMPARE(created.size(), 1);
    QCOMPARE(controller.walletOperationState(), WalletQmlController::WalletOperationState::WalletOperationSucceeded);
    QCOMPARE(controller.walletOperationName(), QStringLiteral("created"));
    QCOMPARE(loader.create_wallet_calls, 1);
    QCOMPARE(controller.selectedWallet(), original);
    auto* actual = created[0][0].value<WalletQmlModel*>(); QVERIFY(actual); QCOMPARE(actual->name(), QStringLiteral("created"));
    QVERIFY(controller.rowForWallet(actual) >= 0); QCOMPARE(created_state.private_accesses.load(), 0);
}
void WalletQmlControllerTests::openWalletRowsPreserveIndependentViews()
{
    FakeWallet::State alpha_state, beta_state;
    MockNode node; FakeWalletLoader loader;
    loader.get_wallets_fn = [&] {
        std::vector<std::unique_ptr<interfaces::Wallet>> wallets;
        wallets.push_back(std::make_unique<FakeWallet>("alpha", &alpha_state));
        wallets.push_back(std::make_unique<FakeWallet>("beta", &beta_state)); return wallets;
    };
    ConfigureExpectedControllerInitialization(node, loader);
    DrainingWalletController controller{node}; controller.initialize(); QTRY_VERIFY(controller.initialized());
    auto* rows = controller.openWalletModel(); QCOMPARE(rows->rowCount(), 2);
    QSignalSpy resets{rows, &QAbstractItemModel::modelReset};
    const QPersistentModelIndex alpha{rows->index(controller.rowForWallet(controller.walletForName("alpha")), 0)};
    auto* alpha_wallet = controller.walletForName("alpha");
    QCOMPARE(alpha.data(Qt::UserRole + 1).value<WalletQmlModel*>(), alpha_wallet);
    controller.closeWallet("beta");
    QTRY_COMPARE(rows->rowCount(), 1);
    QVERIFY(alpha.isValid()); QCOMPARE(alpha.data(Qt::UserRole + 1).value<WalletQmlModel*>(), alpha_wallet);
    QCOMPARE(controller.rowForWallet(alpha_wallet), 0); QCOMPARE(resets.size(), 0);
    QTRY_COMPARE(beta_state.remove_calls.load(), 1);
}

void WalletQmlControllerTests::openWalletTreeRemovalPreservesRowsAndPersistentIdentities()
{
    MockNode node;FakeWalletLoader loader;
    loader.get_wallets_fn=[] {
        std::vector<std::unique_ptr<interfaces::Wallet>> wallets;wallets.reserve(17);
        for(int index=0;index<17;++index)wallets.push_back(std::make_unique<FakeWallet>("tree-"+util::ToString(index),nullptr));
        return wallets;
    };
    ConfigureExpectedControllerInitialization(node,loader);
    DrainingWalletController controller{node};controller.initialize();QTRY_VERIFY(controller.initialized());
    auto* rows=controller.openWalletModel();QTRY_COMPARE(rows->rowCount(),17);
    std::vector<WalletQmlModel*> expected;
    std::vector<QPersistentModelIndex> persistent;expected.reserve(17);persistent.reserve(17);
    for(int row=0;row<rows->rowCount();++row){
        expected.push_back(rows->data(rows->index(row,0),Qt::UserRole+1).value<WalletQmlModel*>());
        persistent.emplace_back(rows->index(row,0));
    }
    const auto original=expected;std::set<WalletQmlModel*> removed;
    QSignalSpy resets{rows,&QAbstractItemModel::modelReset};
    for(int index:{7,3,11,0,16,8,4}){
        const auto name=QStringLiteral("tree-%1").arg(index);auto* wallet=controller.walletForName(name);QVERIFY(wallet);
        removed.insert(wallet);const auto found=std::find(expected.begin(),expected.end(),wallet);QVERIFY(found!=expected.end());expected.erase(found);
        controller.closeWallet(name);QTRY_COMPARE(rows->rowCount(),int(expected.size()));
        for(int row=0;row<rows->rowCount();++row){
            QCOMPARE(rows->data(rows->index(row,0),Qt::UserRole+1).value<WalletQmlModel*>(),expected[row]);
            QCOMPARE(controller.rowForWallet(expected[row]),row);
        }
        for(size_t row=0;row<original.size();++row){
            if(removed.contains(original[row]))QVERIFY(!persistent[row].isValid());
            else{QVERIFY(persistent[row].isValid());QCOMPARE(persistent[row].data(Qt::UserRole+1).value<WalletQmlModel*>(),original[row]);}
        }
    }
    QCOMPARE(resets.size(),0);
}

void WalletQmlControllerTests::provisioningChecksOwnManualPhases_data()
{
    QTest::addColumn<int>("scenario");
    QTest::newRow("matched") << 0;
    QTest::newRow("newer-draft") << 1;
    QTest::newRow("rejected") << 2;
    QTest::newRow("closed") << 3;
}
void WalletQmlControllerTests::provisioningChecksOwnManualPhases()
{
    QFETCH(int, scenario);
    ManualExecutionEnvironment environment;
    MockNode node; FakeWalletLoader loader;
    node.wallet_loader_fn = [&]() -> interfaces::WalletLoader& { return loader; };
    loader.wallet_dir_entries = {{"taken", "sqlite"}};
    WalletProvisioningCheck check{node, {environment.scheduler, [&](auto turn) { environment.post(std::move(turn)); }}};
    check.setXpub(VALID_XPUB);
    environment.drain(); environment.publishAll();
    QVERIFY(check.ready()); QVERIFY(check.xpubValid());
    const auto before = loader.list_wallet_dir_calls;
    if (scenario == 2) environment.rejectNext(true);
    check.setName("taken");
    QCOMPARE(loader.list_wallet_dir_calls, before); QVERIFY(check.pending()); QVERIFY(!check.ready());
    environment.drain();
    QVERIFY(check.pending()); QVERIFY(!check.xpubValid());
    if (scenario == 1) { check.setName("different"); check.setXpub("garbage"); }
    if (scenario == 3) {
        QSignalSpy changes{&check, &WalletProvisioningCheck::changed};
        check.closeAdmission(); QCOMPARE(changes.size(), 0);
        environment.publishAll(); QCOMPARE(changes.size(), 0);
    } else {
        environment.drain(); environment.publishAll();
        if (scenario == 2) { QVERIFY(!check.ready()); QVERIFY(!check.error().isEmpty()); QVERIFY(!check.xpubValid()); }
        else if (scenario == 1) { QVERIFY(check.ready()); QVERIFY(check.nameError().isEmpty()); QVERIFY(!check.xpubValid()); QCOMPARE(check.name(), QStringLiteral("different")); }
        else { QVERIFY(check.ready()); QCOMPARE(check.nameError(), QStringLiteral("A wallet with this name already exists")); QVERIFY(check.xpubValid()); }
    }
    check.closeAdmission(); environment.drain(); environment.publishAll(); QVERIFY(environment.quiescent());
}

void WalletQmlControllerTests::validateXpubAcceptsValidKey()
{
    MockNode node;
    DrainingWalletController controller(node);

    QVERIFY(CheckedXpub(controller, VALID_XPUB));
}

void WalletQmlControllerTests::validateXpubRejectsGarbage()
{
    MockNode node;
    DrainingWalletController controller(node);

    QVERIFY(!CheckedXpub(controller, "not-an-xpub"));
}

void WalletQmlControllerTests::validateXpubRejectsEmpty()
{
    MockNode node;
    DrainingWalletController controller(node);

    QVERIFY(!CheckedXpub(controller, ""));
}

void WalletQmlControllerTests::validateXpubTrimsWhitespace()
{
    MockNode node;
    DrainingWalletController controller(node);

    QVERIFY(CheckedXpub(controller, "  " + VALID_XPUB + "  "));
}

void WalletQmlControllerTests::createWatchOnlyInvalidXpubSetsError()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    ConfigureExpectedControllerInitialization(node, loader);

    DrainingWalletController controller(node);
    controller.initialize();
    QTRY_VERIFY(controller.initialized());

    QSignalSpy error_spy(&controller, &WalletQmlController::walletLoadErrorChanged);
    controller.createWatchOnlyWallet("test_wallet", "garbage");
    QVERIFY(AwaitWalletOperation(controller));

    QCOMPARE(controller.walletLoadError(), QString{"Invalid extended public key."});
    QVERIFY(!controller.isWalletLoaded());
    QCOMPARE(error_spy.count(), 1);
}

void WalletQmlControllerTests::createWatchOnlyCleansUpWhenDescriptorImportFails()
{
    FakeWallet::State wallet_state;
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    ConfigureExpectedControllerInitialization(node, loader);

    DrainingWalletController controller(node);
    controller.initialize();
    QTRY_VERIFY(controller.initialized());
    QVERIFY(loader.captured_load_wallet_fn);

    loader.create_wallet_fn = [&](const std::string& name,
                                  const SecureString&,
                                  uint64_t,
                                  std::vector<bilingual_str>&) {
        // Mimic the real WalletLoader: NotifyWalletLoaded fires synchronously
        // with the new wallet before createWallet returns. The controller's
        // defer-adoption intercept should stash this wallet instead of
        // publishing a model for it.
        loader.captured_load_wallet_fn(std::make_unique<FakeWallet>(name, &wallet_state));
        return util::Result<std::unique_ptr<interfaces::Wallet>>{
            std::make_unique<FakeWallet>(name, &wallet_state)};
    };

    QSignalSpy create_spy(&controller, &WalletQmlController::walletCreateSucceeded);
    QSignalSpy error_spy(&controller, &WalletQmlController::walletLoadErrorChanged);

    // The public descriptor import reports failure, so reject the incomplete wallet,
    // call remove() on it, and surface a load error without registering a model.
    controller.createWatchOnlyWallet("watch_wallet", VALID_XPUB);
    QVERIFY(AwaitWalletOperation(controller));

    QTRY_COMPARE_WITH_TIMEOUT(error_spy.count(), 1, 5000);
    QVERIFY(!controller.walletLoadError().isEmpty());
    QCOMPARE(controller.walletOperationState(), WalletQmlController::WalletOperationState::WalletOperationIncomplete);
    QCOMPARE(controller.walletOperationName(), QStringLiteral("watch_wallet"));
    QCOMPARE(wallet_state.remove_calls.load(), 1);
    QCOMPARE(create_spy.count(), 0);
    QVERIFY(!controller.isWalletLoaded());
    QVERIFY(!controller.walletLoadInProgress());
}

void WalletQmlControllerTests::createWatchOnlyUsesPublicDescriptorImport_data()
{
    QTest::addColumn<int>("failure");
    QTest::newRow("both-imported") << -1;
    QTest::newRow("external-fails") << 0;
    QTest::newRow("internal-fails") << 1;
    QTest::newRow("missing-result") << 2;
}

void WalletQmlControllerTests::createWatchOnlyUsesPublicDescriptorImport()
{
    QFETCH(int, failure);
    FakeWallet::State state;
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    ConfigureExpectedControllerInitialization(node, loader);
    DrainingWalletController controller(node);
    controller.initialize();
    QTRY_VERIFY(controller.initialized());
    std::vector<wallet::ImportDescriptorRequest> captured;
    std::atomic_bool off_gui{false};
    state.import_fn = [&](auto& requests) {
        off_gui = QThread::currentThread() != qApp->thread();
        { std::lock_guard lock{state.mutex}; captured = requests; }
        std::vector<wallet::ImportResult> results(failure == 2 ? 1 : 2);
        if (failure == 0 || failure == 1) results[failure] = wallet::ImportResult{wallet::WalletErrorCode::GenericError, "descriptor rejected"};
        return results;
    };
    loader.create_wallet_fn = [&](const std::string& name, const SecureString&, uint64_t, std::vector<bilingual_str>&) -> util::Result<std::unique_ptr<interfaces::Wallet>> {
        auto result = std::make_unique<FakeWallet>(name, &state);
        result->private_keys_disabled = true;
        return std::unique_ptr<interfaces::Wallet>{std::move(result)};
    };
    QSignalSpy created{&controller, &WalletQmlController::walletCreateSucceeded};
    controller.createWatchOnlyWallet("public_watch", VALID_XPUB);
    QVERIFY(AwaitWalletOperation(controller));
    QTRY_VERIFY(!controller.walletLoadInProgress());
    QCOMPARE(state.imports.load(), 1);
    QCOMPARE(state.private_accesses.load(), 0);
    QVERIFY(off_gui.load());
    {
        std::lock_guard lock{state.mutex};
        QCOMPARE(captured.size(), size_t{2});
        for (size_t index = 0; index < captured.size(); ++index) {
            const auto& request = captured[index];
            QVERIFY(request.active);
            QVERIFY(request.internal.has_value());
            QCOMPARE(*request.internal, index == 1);
            QVERIFY(!request.timestamp);
            QCOMPARE(request.next_index, std::optional<int64_t>{0});
            QVERIFY(request.descriptor.find(index ? "/1/*)#" : "/0/*)#") != std::string::npos);
            QCOMPARE(request.descriptor.substr(request.descriptor.size() - 8), GetDescriptorChecksum(request.descriptor));
        }
    }
    QCOMPARE(created.count(), failure < 0 ? 1 : 0);
    QCOMPARE(state.remove_calls.load(), failure < 0 ? 0 : 1);
    QCOMPARE(controller.walletLoadError().isEmpty(), failure < 0);
}

void WalletQmlControllerTests::externalSignerCreationRequiresConfiguredPath()
{
    MockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    node.get_persistent_setting_fn = [](const std::string&) { return common::SettingsValue{}; };
    node.list_external_signers_fn = [] { return MakeSigners({"Ledger Nano X"}); };
    node.ExpectExactly(node.calls.listExternalSigners, 1);

    DrainingWalletController controller(node);
    controller.refreshExternalSignerStatus();
    QTRY_VERIFY(!controller.externalSignerPending());

    QVERIFY(!controller.canCreateExternalSignerWallet());
    QCOMPARE(controller.externalSignerName(), QString("Ledger Nano X"));
    QCOMPARE(controller.suggestedExternalSignerWalletName(), QString("Ledger_Nano_X"));
}

void WalletQmlControllerTests::externalSignerCreationRequiresExactlyOneSigner()
{
    MockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    node.get_persistent_setting_fn = [](const std::string& name) {
        return name == "signer" ? common::SettingsValue{std::string{"/usr/bin/hwi"}} : common::SettingsValue{};
    };
    node.list_external_signers_fn = [] { return MakeSigners({"Signer A", "Signer B"}); };
    node.ExpectExactly(node.calls.listExternalSigners, 1);

    DrainingWalletController controller(node);
    controller.refreshExternalSignerStatus();
    QTRY_VERIFY(!controller.externalSignerPending());

    QVERIFY(!controller.canCreateExternalSignerWallet());
    QVERIFY(controller.externalSignerName().isEmpty());
    QCOMPARE(controller.externalSignerError(), QString("More than one external signer was found. Connect only one device."));
}

void WalletQmlControllerTests::externalSignerSuggestionUsesSignerName()
{
    MockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    node.get_persistent_setting_fn = [](const std::string& name) {
        return name == "signer" ? common::SettingsValue{std::string{"/usr/bin/hwi"}} : common::SettingsValue{};
    };
    node.list_external_signers_fn = [] { return MakeSigners({"Coldcard Mk4"}); };
    node.ExpectExactly(node.calls.listExternalSigners, 1);

    DrainingWalletController controller(node);
    controller.refreshExternalSignerStatus();
    QTRY_VERIFY(!controller.externalSignerPending());

    QVERIFY(controller.canCreateExternalSignerWallet());
    QCOMPARE(controller.externalSignerName(), QString("Coldcard Mk4"));
    QCOMPARE(controller.suggestedExternalSignerWalletName(), QString("Coldcard_Mk4"));
}

void WalletQmlControllerTests::signerDiscoveryUsesDefaultNativeWorker()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    const auto gui = QThread::currentThread();
    std::atomic_bool setting_native{false}, force_native{false}, discovery_native{false};
    std::atomic<int> unexpected_settings{0};
    node.get_persistent_setting_fn = [&](const std::string& name) {
        setting_native = QThread::currentThread() != gui;
        if (name != "signer") ++unexpected_settings;
        return common::SettingsValue{std::string{"native-signer-command"}};
    };
    node.force_setting_fn = [&](const std::string&, const common::SettingsValue&) {
        force_native = QThread::currentThread() != gui;
    };
    node.list_external_signers_fn = [&] {
        discovery_native = QThread::currentThread() != gui;
        return MakeSigners({"Native signer"});
    };
    node.ExpectExactly(node.calls.getPersistentSetting, 1);
    node.ExpectExactly(node.calls.forceSetting, 1);
    node.ExpectExactly(node.calls.listExternalSigners, 1);
    DrainingWalletController controller{node};
    controller.refreshExternalSignerStatus();
    QTRY_VERIFY(!controller.externalSignerPending());
    QVERIFY(setting_native.load());
    QVERIFY(force_native.load());
    QVERIFY(discovery_native.load());
    QCOMPARE(unexpected_settings.load(), 0);
    QVERIFY(controller.canCreateExternalSignerWallet());
    QCOMPARE(controller.externalSignerName(), QStringLiteral("Native signer"));
    QCOMPARE(controller.suggestedExternalSignerWalletName(), QStringLiteral("Native_signer"));
}

void WalletQmlControllerTests::signerDiscoveryKeepsNewerIntentAcrossHeldPublication()
{
    ManualRoutedExecution env;
    std::deque<std::function<void()>> gui;
    MockNode node;
    node.get_persistent_setting_fn = [](const std::string&) { return common::SettingsValue{std::string{"/usr/bin/hwi"}}; };
    int reads{0};
    node.list_external_signers_fn = [&] { return MakeSigners({++reads == 1 ? "First" : "Latest"}); };
    DrainingWalletController controller(node);
    controller.setExternalSignerServices({env.scheduler, [&](std::function<void()> callback) { gui.push_back(std::move(callback)); }});
    const auto publish = [&] { auto callback = std::move(gui.front()); gui.pop_front(); callback(); };
    controller.refreshExternalSignerStatus();
    QVERIFY(controller.externalSignerPending());
    QVERIFY(!controller.canCreateExternalSignerWallet());
    QCOMPARE(reads, 0);
    QVERIFY(env.step(execution::Lane::Core));
    QCOMPARE(reads, 1);
    QVERIFY(gui.empty());
    controller.refreshExternalSignerStatus();
    QVERIFY(env.step(execution::Lane::Domain));
    QCOMPARE(gui.size(), size_t{1});
    publish();
    QVERIFY(controller.externalSignerPending());
    QVERIFY(controller.externalSignerName().isEmpty());
    QVERIFY(env.step(execution::Lane::Core));
    QCOMPARE(reads, 2);
    QVERIFY(env.step(execution::Lane::Domain));
    QCOMPARE(gui.size(), size_t{1});
    publish();
    QVERIFY(!controller.externalSignerPending());
    QCOMPARE(controller.externalSignerName(), QStringLiteral("Latest"));
    QVERIFY(controller.canCreateExternalSignerWallet());
    env.drain();
}

void WalletQmlControllerTests::signerDiscoveryRejectsPublicationAfterQuietClose()
{
    ManualRoutedExecution env;
    std::deque<std::function<void()>> gui;
    MockNode node;
    node.list_external_signers_fn = [] { return MakeSigners({"Late"}); };
    DrainingWalletController controller(node);
    controller.setExternalSignerServices({env.scheduler, [&](std::function<void()> callback) { gui.push_back(std::move(callback)); }});
    controller.refreshExternalSignerStatus();
    QVERIFY(env.step(execution::Lane::Core));
    QVERIFY(env.step(execution::Lane::Domain));
    QCOMPARE(gui.size(), size_t{1});
    controller.closeAdmission();
    QSignalSpy changed{&controller, &WalletQmlController::externalSignerStatusChanged};
    gui.front()(); gui.pop_front();
    QCOMPARE(changed.count(), 0);
    QVERIFY(controller.externalSignerName().isEmpty());
    QVERIFY(!controller.canCreateExternalSignerWallet());
    env.drain();
}

void WalletQmlControllerTests::initializedControllerSignalsMigrationForLegacyWallet()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    loader.wallet_dir_entries = {{"legacy_wallet", "bdb"}};
    ConfigureExpectedControllerInitialization(node, loader);

    DrainingWalletController controller(node);
    controller.initialize();
    QTRY_VERIFY(controller.initialized());

    QSignalSpy migration_spy(&controller, &WalletQmlController::walletMigrationRequired);

    controller.setSelectedWallet("legacy_wallet", "bdb");
    QVERIFY(AwaitWalletOperation(controller));
    QCOMPARE(migration_spy.count(), 1);
    QCOMPARE(migration_spy.takeFirst().at(0).toString(), QString{"legacy_wallet"});
    QCOMPARE(loader.load_wallet_calls, 0);
    QVERIFY(!controller.walletLoadInProgress());
    QVERIFY(controller.walletLoadError().isEmpty());
}

void WalletQmlControllerTests::createWalletBeforeInitializationReturnsFalseAndSetsError()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    DrainingWalletController controller(node);

    controller.createSingleSigWallet("test_wallet", "secret");
    QVERIFY(AwaitWalletOperation(controller));
    QCOMPARE(controller.walletCreateError(), QString{NOT_INITIALIZED_ERROR});
    QVERIFY(!controller.isWalletLoaded());
    QVERIFY(!controller.walletLoadInProgress());
}

void WalletQmlControllerTests::importWalletBeforeInitializationSetsLoadError()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    DrainingWalletController controller(node);

    controller.importWallet("/tmp/test_wallet.dat");
    QVERIFY(AwaitWalletOperation(controller));
    QCOMPARE(controller.walletLoadError(), QString{NOT_INITIALIZED_ERROR});
}

void WalletQmlControllerTests::migrateWalletBeforeInitializationSetsMigrationError()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    DrainingWalletController controller(node);

    controller.migrateWallet("legacy_wallet", "secret");
    QVERIFY(AwaitWalletOperation(controller));
    QCOMPARE(controller.walletMigrationError(), QString{NOT_INITIALIZED_ERROR});
}

void WalletQmlControllerTests::selectWalletBeforeInitializationSetsLoadError()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    DrainingWalletController controller(node);

    controller.setSelectedWallet("test_wallet");
    QVERIFY(AwaitWalletOperation(controller));
    QCOMPARE(controller.walletLoadError(), QString{NOT_INITIALIZED_ERROR});
}

void WalletQmlControllerTests::initializedControllerPropagatesCreateErrors()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    ConfigureExpectedControllerInitialization(node, loader);

    DrainingWalletController controller(node);
    controller.initialize();
    QTRY_VERIFY(controller.initialized());

    QVERIFY(controller.initialized());

    bool saw_expected_passphrase{false};
    bool saw_expected_flags{false};
    loader.create_wallet_fn = [&](const std::string&,
                                  const SecureString& passphrase,
                                  uint64_t wallet_creation_flags,
                                  std::vector<bilingual_str>&) {
        saw_expected_passphrase = (passphrase == SecureString{"secret"});
        saw_expected_flags = (wallet_creation_flags == wallet::WALLET_FLAG_DESCRIPTORS);
        return util::Result<std::unique_ptr<interfaces::Wallet>>{
            util::Error{Untranslated("Wallet creation failed.")}};
    };

    QSignalSpy error_spy(&controller, &WalletQmlController::walletCreateErrorChanged);
    controller.createSingleSigWallet("test_wallet", "secret");
    QVERIFY(AwaitWalletOperation(controller));
    QCOMPARE(error_spy.count(), 1);
    QVERIFY(saw_expected_passphrase);
    QVERIFY(saw_expected_flags);
    QCOMPARE(loader.create_wallet_calls, 1);
    QCOMPARE(controller.walletCreateError(), QString{"Wallet creation failed."});
    QVERIFY(!controller.isWalletLoaded());
    QVERIFY(!controller.walletLoadInProgress());
}

void WalletQmlControllerTests::capturedSecretSurvivesQueueDelayAndQuietClose_data()
{
    QTest::addColumn<bool>("migration");QTest::newRow("create")<<false;QTest::newRow("migrate")<<true;
}
void WalletQmlControllerTests::capturedSecretSurvivesQueueDelayAndQuietClose()
{
    QFETCH(bool,migration);StrictMockNode node;[[maybe_unused]] auto verify_node=node.VerifyOnExit();FakeWalletLoader loader;ConfigureExpectedControllerInitialization(node,loader);loader.wallet_dir_entries={{"legacy_wallet","bdb"}};
    DrainingWalletController controller{node};controller.initialize();QTRY_VERIFY(controller.initialized());
    struct Probe{QSemaphore entered,release;std::atomic_bool exact{false};std::atomic<QThread*> native{nullptr};};const auto probe=std::make_shared<Probe>();
    const auto unblock=qScopeGuard([probe]{probe->release.release();});
    QVERIFY(execution::Submit(*controller.backgroundScheduler(),[probe]{probe->entered.release();probe->release.acquire();},[]{}));
    QVERIFY(probe->entered.tryAcquire(1,5000));
    const std::string expected(40'000,'s');
    loader.create_wallet_fn=[probe,expected](const auto&,const SecureString& password,uint64_t,auto&)->util::Result<std::unique_ptr<interfaces::Wallet>>{probe->exact=std::string{password.begin(),password.end()}==expected;probe->native=QThread::currentThread();return util::Error{Untranslated("Captured creation reached Native")};};
    loader.migrate_wallet_fn=[probe,expected](const auto&,const SecureString& password,bool)->util::Result<interfaces::WalletMigrationResult>{probe->exact=std::string{password.begin(),password.end()}==expected;probe->native=QThread::currentThread();return util::Error{Untranslated("Captured migration reached Native")};};
    QString source=QString::fromStdString(expected);if(migration)controller.migrateWallet("legacy_wallet",source);else controller.createSingleSigWallet("captured-wallet",source);
    auto original=controller.m_command;QVERIFY(original);source.fill(QLatin1Char('z'));controller.closeAdmission();QCOMPARE(loader.create_wallet_calls+loader.migrate_wallet_calls,0);
    probe->release.release();QTRY_VERIFY(original->result().has_value());QVERIFY(original->result()->value);QVERIFY(probe->exact.load());QVERIFY(probe->native.load());QVERIFY(probe->native.load()!=QThread::currentThread());
    QCOMPARE(loader.create_wallet_calls+loader.migrate_wallet_calls,1);QVERIFY(original->result()->value->error.contains(QStringLiteral("reached Native")));original.reset();
}
void WalletQmlControllerTests::capturedSecretTransportRefusalDoesNotCallNativeLoader_data()
{
    QTest::addColumn<int>("input");QTest::newRow("secret-peak")<<0;QTest::newRow("raw-name")<<1;QTest::newRow("raw-migration-path")<<2;
}
void WalletQmlControllerTests::capturedSecretTransportRefusalDoesNotCallNativeLoader()
{
    QFETCH(int,input);StrictMockNode node;[[maybe_unused]] auto verify_node=node.VerifyOnExit();FakeWalletLoader loader;ConfigureExpectedControllerInitialization(node,loader);
    auto limits=execution::Limits::Bounded();limits.ordinary.payload_bytes=200'000;DrainingWalletController controller{node,limits};controller.initialize();QTRY_VERIFY(controller.initialized());const auto directory_reads=loader.list_wallet_dir_calls;
    if(input==0)controller.createSingleSigWallet("transport-refused",QString(40'000,QLatin1Char('p')));
    if(input==1)controller.createSingleSigWallet(QString(150'000,QLatin1Char('n')),"secret");
    if(input==2)controller.migrateWallet(QString(150'000,QLatin1Char('p')),"secret");
    QVERIFY(AwaitWalletOperation(controller));QCOMPARE(loader.create_wallet_calls,0);QCOMPARE(loader.migrate_wallet_calls,0);QCOMPARE(loader.list_wallet_dir_calls,directory_reads);
    QVERIFY(input==2?!controller.walletMigrationError().isEmpty():!controller.walletCreateError().isEmpty());QCOMPARE(controller.walletOperationState(),WalletQmlController::WalletOperationState::WalletOperationFailed);
}
void WalletQmlControllerTests::initializedControllerForwardsMigrationPassphrase_data()
{
    QTest::addColumn<QString>("original_password");QTest::newRow("native-small-literal")<<QStringLiteral("secret");QTest::newRow("valid-40000-character-literal")<<QString(40'000,QLatin1Char('m'));
}
void WalletQmlControllerTests::initializedControllerForwardsMigrationPassphrase()
{
    QFETCH(QString,original_password);const auto expected_password=original_password.toUtf8().toStdString();std::atomic<QThread*> worker{nullptr};
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    loader.wallet_dir_entries = {{"legacy_wallet", "bdb"}};
    ConfigureExpectedControllerInitialization(node, loader);

    DrainingWalletController controller(node);
    controller.initialize();
    QTRY_VERIFY(controller.initialized());

    QSignalSpy failed_spy(&controller, &WalletQmlController::walletMigrationFailed);

    bool saw_expected_name{false};
    bool saw_expected_passphrase{false};
    loader.migrate_wallet_fn = [&](const std::string& name, const SecureString& passphrase, bool) {
        saw_expected_name = (name == "legacy_wallet");
        saw_expected_passphrase = (std::string{passphrase.begin(),passphrase.end()} == expected_password);worker=QThread::currentThread();
        return util::Result<interfaces::WalletMigrationResult>{
            util::Error{Untranslated("Migration failed.")}};
    };

    controller.migrateWallet("legacy_wallet", original_password);
    original_password.fill(QLatin1Char('z'));
    QVERIFY(AwaitWalletOperation(controller));
    QTRY_VERIFY(!failed_spy.empty());
    QVERIFY(worker.load());QVERIFY(worker.load()!=QThread::currentThread());
    QVERIFY(saw_expected_name);
    QVERIFY(saw_expected_passphrase);
    QCOMPARE(loader.migrate_wallet_calls, 1);
    QCOMPARE(controller.walletMigrationError(), QString{"Migration failed."});
    QVERIFY(!controller.walletMigrationInProgress());
}

void WalletQmlControllerTests::initializedControllerForwardsUtf8CreatePassphrase_data()
{
    QTest::addColumn<QString>("original_password");QTest::newRow("utf8-literal")<<QString::fromUtf8("pässwörd-₿");QTest::newRow("valid-40000-character-literal")<<QString(40'000,QLatin1Char('c'));
}
void WalletQmlControllerTests::initializedControllerForwardsUtf8CreatePassphrase()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    ConfigureExpectedControllerInitialization(node, loader);

    DrainingWalletController controller(node);
    controller.initialize();
    QTRY_VERIFY(controller.initialized());

    QFETCH(QString,original_password);const std::string expected_passphrase{original_password.toUtf8().toStdString()};std::atomic<QThread*> worker{nullptr};

    bool saw_expected_passphrase{false};
    loader.create_wallet_fn = [&](const std::string&,
                                  const SecureString& passphrase,
                                  uint64_t,
                                  std::vector<bilingual_str>&) {
        saw_expected_passphrase = (std::string{passphrase.begin(), passphrase.end()} == expected_passphrase);worker=QThread::currentThread();
        return util::Result<std::unique_ptr<interfaces::Wallet>>{
            util::Error{Untranslated("Wallet creation failed.")}};
    };

    QSignalSpy error_spy(&controller, &WalletQmlController::walletCreateErrorChanged);
    controller.createSingleSigWallet("test_wallet", original_password);
    original_password.fill(QLatin1Char('z'));
    QVERIFY(AwaitWalletOperation(controller));
    QTRY_VERIFY(!error_spy.empty());
    QVERIFY(worker.load());QVERIFY(worker.load()!=QThread::currentThread());
    QVERIFY(saw_expected_passphrase);
    QCOMPARE(loader.create_wallet_calls, 1);
    QCOMPARE(controller.walletCreateError(), QString{"Wallet creation failed."});
}

void WalletQmlControllerTests::initializedControllerEmitsWalletCreateSucceeded()
{
    FakeWallet::State wallet_state;
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    ConfigureExpectedControllerInitialization(node, loader);

    DrainingWalletController controller(node);
    controller.initialize();
    QTRY_VERIFY(controller.initialized());
    QVERIFY(loader.captured_load_wallet_fn);

    loader.create_wallet_fn = [&](const std::string& name,
                                  const SecureString&,
                                  uint64_t,
                                  std::vector<bilingual_str>&) {
        return util::Result<std::unique_ptr<interfaces::Wallet>>{
            std::make_unique<FakeWallet>(name, &wallet_state)};
    };

    QSignalSpy create_spy(&controller, &WalletQmlController::walletCreateSucceeded);
    QSignalSpy create_calls_spy(&controller, &WalletQmlController::walletLoadInProgressChanged);
    controller.createSingleSigWallet("created_wallet", "");
    QVERIFY(AwaitWalletOperation(controller));

    QTRY_COMPARE_WITH_TIMEOUT(create_spy.count(), 1, 5000);
    QVERIFY(controller.isWalletLoaded());
    QVERIFY(!controller.walletLoadInProgress());
    QCOMPARE(controller.walletCreateError(), QString{});
    auto* created_model = create_spy.at(0).at(0).value<WalletQmlModel*>();
    QVERIFY(created_model);
    QCOMPARE(created_model->name(), QString{"created_wallet"});
    QVERIFY(controller.openWallets().contains(created_model));
}

void WalletQmlControllerTests::initializedControllerDoesNotCompleteCreateForUnrelatedLoadNotification()
{
    FakeWallet::State created_state;
    FakeWallet::State unrelated_state;
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    ConfigureExpectedControllerInitialization(node, loader);

    DrainingWalletController controller(node);
    controller.initialize();
    QTRY_VERIFY(controller.initialized());
    QVERIFY(loader.captured_load_wallet_fn);

    QSemaphore create_started;
    QSemaphore allow_create_return;
    loader.create_wallet_fn = [&](const std::string& name,
                                  const SecureString&,
                                  uint64_t,
                                  std::vector<bilingual_str>&) {
        create_started.release();
        allow_create_return.acquire();
        return util::Result<std::unique_ptr<interfaces::Wallet>>{
            std::make_unique<FakeWallet>(name, &created_state)};
    };

    QSignalSpy create_spy(&controller, &WalletQmlController::walletCreateSucceeded);

    controller.createSingleSigWallet("created_wallet", "");
    QVERIFY(create_started.tryAcquire(1, 5000));
    QVERIFY(controller.walletLoadInProgress());

    loader.captured_load_wallet_fn(std::make_unique<FakeWallet>("unrelated_wallet", &unrelated_state));
    QCoreApplication::processEvents();

    QCOMPARE(create_spy.count(), 0);
    QVERIFY(controller.walletLoadInProgress());

    allow_create_return.release();
    QTRY_COMPARE_WITH_TIMEOUT(create_spy.count(), 1, 5000);
    QVERIFY(!controller.walletLoadInProgress());
    QCOMPARE(controller.selectedWallet()->name(), QString{"created_wallet"});
}

void WalletQmlControllerTests::initializedControllerDoesNotCompleteLoadForUnrelatedLoadNotification()
{
    FakeWallet::State loaded_state;
    FakeWallet::State unrelated_state;
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    loader.wallet_dir_entries = {{"target_wallet", "sqlite"}};
    ConfigureExpectedControllerInitialization(node, loader);

    DrainingWalletController controller(node);
    controller.initialize();
    QTRY_VERIFY(controller.initialized());
    QVERIFY(loader.captured_load_wallet_fn);

    QSemaphore load_started;
    QSemaphore allow_load_return;
    loader.load_wallet_fn = [&](const std::string& name,
                                std::vector<bilingual_str>&) {
        load_started.release();
        allow_load_return.acquire();
        return util::Result<std::unique_ptr<interfaces::Wallet>>{
            std::make_unique<FakeWallet>(name, &loaded_state)};
    };

    QSignalSpy load_spy(&controller, &WalletQmlController::walletLoadSucceeded);

    controller.setSelectedWallet("target_wallet", "sqlite");
    QVERIFY(load_started.tryAcquire(1, 5000));
    QVERIFY(controller.walletLoadInProgress());

    loader.captured_load_wallet_fn(std::make_unique<FakeWallet>("unrelated_wallet", &unrelated_state));
    QCoreApplication::processEvents();

    QCOMPARE(load_spy.count(), 0);
    QVERIFY(controller.walletLoadInProgress());

    allow_load_return.release();
    QTRY_COMPARE_WITH_TIMEOUT(load_spy.count(), 1, 5000);
    QVERIFY(!controller.walletLoadInProgress());
    QCOMPARE(controller.selectedWallet()->name(), QString{"target_wallet"});
}

void WalletQmlControllerTests::initializedControllerReportsCreateWarnings()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    ConfigureExpectedControllerInitialization(node, loader);

    DrainingWalletController controller(node);
    controller.initialize();
    QTRY_VERIFY(controller.initialized());

    loader.create_wallet_fn = [](const std::string&,
                                 const SecureString&,
                                 uint64_t,
                                 std::vector<bilingual_str>& warnings) {
        warnings.push_back(Untranslated("Disk space is low."));
        return util::Result<std::unique_ptr<interfaces::Wallet>>{
            util::Error{Untranslated("Wallet creation failed.")}};
    };

    QSignalSpy warnings_spy(&controller, &WalletQmlController::walletLoadWarningsChanged);
    controller.createSingleSigWallet("test_wallet", "secret");
    QVERIFY(AwaitWalletOperation(controller));
    QCOMPARE(warnings_spy.count(), 1);
    QCOMPARE(controller.walletLoadWarnings(), QString{"Disk space is low."});
    QCOMPARE(controller.walletCreateError(), QString{"Wallet creation failed."});
}

void WalletQmlControllerTests::initializedControllerIgnoresDuplicateCreateWhileInProgress()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    ConfigureExpectedControllerInitialization(node, loader);
    struct CreateGate { QSemaphore entered, release; };
    const auto gate = std::make_shared<CreateGate>();
    DrainingWalletController controller(node);
    const auto unblock = qScopeGuard([gate] { gate->release.release(); });
    controller.initialize();
    QTRY_VERIFY(controller.initialized());
    loader.create_wallet_fn = [gate](const std::string&, const SecureString&, uint64_t,
                                    std::vector<bilingual_str>&) {
        gate->entered.release();
        gate->release.acquire();
        return util::Result<std::unique_ptr<interfaces::Wallet>>{
            util::Error{Untranslated("Wallet creation failed.")}};
    };
    QSignalSpy error_spy(&controller, &WalletQmlController::walletCreateErrorChanged);
    controller.createSingleSigWallet("test_wallet", "secret");
    const auto original = controller.walletOperationId();
    QVERIFY(QTest::qWaitFor([&] { return gate->entered.tryAcquire(1); }, 5000));
    QCOMPARE(controller.walletOperationState(), WalletQmlController::WalletOperationState::WalletOperationPending);
    QCOMPARE(loader.create_wallet_calls, 1);
    QCOMPARE(error_spy.count(), 0);
    controller.createSingleSigWallet("test_wallet", "secret");
    QCOMPARE(controller.walletOperationId(), original);
    QCOMPARE(controller.walletOperationState(), WalletQmlController::WalletOperationState::WalletOperationPending);
    QCOMPARE(loader.create_wallet_calls, 1);
    QCOMPARE(error_spy.count(), 0);
    gate->release.release();
    QVERIFY(AwaitWalletOperation(controller));
    QCOMPARE(controller.walletOperationId(), original);
    QCOMPARE(controller.walletOperationState(), WalletQmlController::WalletOperationState::WalletOperationFailed);
    QCOMPARE(error_spy.count(), 1);
    QCOMPARE(controller.walletCreateError(), QString{"Wallet creation failed."});
    QCOMPARE(loader.create_wallet_calls, 1);
}

void WalletQmlControllerTests::watchOnlyCreateBeforeInitializationSetsLoadError()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    DrainingWalletController controller(node);

    controller.createWatchOnlyWallet("watch_only", VALID_XPUB);
    QVERIFY(AwaitWalletOperation(controller));

    QCOMPARE(controller.walletLoadError(), QString{NOT_INITIALIZED_ERROR});
    QVERIFY(!controller.walletLoadInProgress());
}

void WalletQmlControllerTests::watchOnlyCreateWhileWalletLoadInProgressIsIgnored()
{
    FakeWallet::State wallet_state;
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    ConfigureExpectedControllerInitialization(node, loader);

    DrainingWalletController controller(node);
    controller.initialize();
    QTRY_VERIFY(controller.initialized());

    QSemaphore create_started;
    QSemaphore allow_create_return;
    loader.create_wallet_fn = [&](const std::string& name,
                                  const SecureString&,
                                  uint64_t,
                                  std::vector<bilingual_str>&) {
        create_started.release();
        allow_create_return.acquire();
        return util::Result<std::unique_ptr<interfaces::Wallet>>{
            std::make_unique<FakeWallet>(name, &wallet_state)};
    };

    controller.createSingleSigWallet("regular_wallet", "");
    QVERIFY(create_started.tryAcquire(1, 5000));
    QVERIFY(controller.walletLoadInProgress());

    controller.createWatchOnlyWallet("watch_only", VALID_XPUB);

    QCOMPARE(loader.create_wallet_calls, 1);
    QVERIFY(controller.walletLoadInProgress());

    allow_create_return.release();
    QTRY_VERIFY_WITH_TIMEOUT(!controller.walletLoadInProgress(), 5000);
}

void WalletQmlControllerTests::watchOnlyCreateFailureAfterCreateDoesNotPublishWallet()
{
    FakeWallet::State returned_state;
    FakeWallet::State notified_state;
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    ConfigureExpectedControllerInitialization(node, loader);

    DrainingWalletController controller(node);
    controller.initialize();
    QTRY_VERIFY(controller.initialized());
    QVERIFY(loader.captured_load_wallet_fn);

    loader.create_wallet_fn = [&](const std::string& name,
                                  const SecureString&,
                                  uint64_t,
                                  std::vector<bilingual_str>&) {
        loader.captured_load_wallet_fn(std::make_unique<FakeWallet>(name, &notified_state));
        return util::Result<std::unique_ptr<interfaces::Wallet>>{
            std::make_unique<FakeWallet>(name, &returned_state)};
    };

    QSignalSpy create_spy(&controller, &WalletQmlController::walletCreateSucceeded);
    QSignalSpy error_spy(&controller, &WalletQmlController::walletLoadErrorChanged);

    controller.createWatchOnlyWallet("watch_only", VALID_XPUB);
    QVERIFY(AwaitWalletOperation(controller));

    QTRY_COMPARE_WITH_TIMEOUT(error_spy.count(), 1, 5000);
    QCOMPARE(create_spy.count(), 0);
    QVERIFY(!controller.isWalletLoaded());
    QVERIFY(!controller.walletLoadInProgress());
    QVERIFY(!controller.walletLoadError().isEmpty());
    QCOMPARE(controller.selectedWallet()->name(), QString{});
    QCOMPARE(returned_state.remove_calls.load(), 1);
}

void WalletQmlControllerTests::initializedControllerRequestsPassphraseBeforeEncryptedMigration()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    loader.wallet_dir_entries = {{"legacy_wallet", "bdb"}};
    loader.is_encrypted_fn = [](const std::string& name) {
        return name == "legacy_wallet";
    };
    ConfigureExpectedControllerInitialization(node, loader);

    DrainingWalletController controller(node);
    controller.initialize();
    QTRY_VERIFY(controller.initialized());

    QSignalSpy passphrase_spy(&controller, &WalletQmlController::walletMigrationPassphraseRequired);

    controller.migrateWallet("legacy_wallet", "");
    QVERIFY(AwaitWalletOperation(controller));
    QCOMPARE(passphrase_spy.count(), 1);
    QCOMPARE(passphrase_spy.takeFirst().at(0).toString(), QString{"legacy_wallet"});
    QCOMPARE(loader.is_encrypted_calls, 1);
    QCOMPARE(loader.migrate_wallet_calls, 0);
    QVERIFY(!controller.walletMigrationInProgress());
    QVERIFY(controller.walletMigrationError().isEmpty());
}

void WalletQmlControllerTests::initializedControllerMigratesUnencryptedWalletWithoutPassphrase()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    loader.wallet_dir_entries = {{"legacy_wallet", "bdb"}};
    loader.is_encrypted_fn = [](const std::string&) {
        return false;
    };
    ConfigureExpectedControllerInitialization(node, loader);

    DrainingWalletController controller(node);
    controller.initialize();
    QTRY_VERIFY(controller.initialized());

    QSignalSpy failed_spy(&controller, &WalletQmlController::walletMigrationFailed);

    bool saw_empty_passphrase{false};
    loader.migrate_wallet_fn = [&](const std::string& name, const SecureString& passphrase, bool) {
        saw_empty_passphrase = (name == "legacy_wallet" && passphrase.empty());
        return util::Result<interfaces::WalletMigrationResult>{
            util::Error{Untranslated("Migration failed.")}};
    };

    controller.migrateWallet("legacy_wallet", "");
    QVERIFY(AwaitWalletOperation(controller));
    QCOMPARE(failed_spy.count(), 1);
    QVERIFY(saw_empty_passphrase);
    QCOMPARE(loader.is_encrypted_calls, 1);
    QCOMPARE(loader.migrate_wallet_calls, 1);
    QCOMPARE(controller.walletMigrationError(), QString{"Migration failed."});
    QVERIFY(!controller.walletMigrationInProgress());
}

void WalletQmlControllerTests::initializedControllerClosesSelectedWalletAndSelectsRemainingLoadedWallet()
{
    FakeWallet::State alpha_state;
    FakeWallet::State beta_state;
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    loader.get_wallets_fn = [&]() {
        std::vector<std::unique_ptr<interfaces::Wallet>> wallets;
        wallets.emplace_back(std::make_unique<FakeWallet>("alpha_wallet", &alpha_state));
        wallets.emplace_back(std::make_unique<FakeWallet>("beta_wallet", &beta_state));
        return wallets;
    };
    ConfigureExpectedControllerInitialization(node, loader);

    DrainingWalletController controller(node);
    controller.initialize();
    QTRY_VERIFY(controller.initialized());

    QCOMPARE(controller.selectedWallet()->name(), QString{"alpha_wallet"});
    QVERIFY(controller.isWalletLoaded());
    QVERIFY(controller.isWalletOpen("alpha_wallet"));
    QVERIFY(controller.isWalletOpen("beta_wallet"));
    QCOMPARE(controller.openWallets().size(), 2);
    WalletQmlModel* beta_model = qobject_cast<WalletQmlModel*>(controller.openWallets().at(1));
    QVERIFY(beta_model);

    QSignalSpy selected_spy(&controller, &WalletQmlController::selectedWalletChanged);
    QSignalSpy load_state_spy(&controller, &WalletQmlController::walletLoadStateChanged);
    QStringList signal_order;
    QObject::connect(&controller, &WalletQmlController::selectedWalletChanged, [&]() {
        signal_order.append("selectedWalletChanged");
    });
    QObject::connect(&controller, &WalletQmlController::walletLoadStateChanged, [&]() {
        signal_order.append("walletLoadStateChanged");
    });
    // Views are keyed by the open wallets, so the next wallet must be
    // selected before the closed one leaves the list.
    QObject::connect(&controller, &WalletQmlController::openWalletsChanged, [&]() {
        signal_order.append("openWalletsChanged");
        QCOMPARE(controller.selectedWallet(), beta_model);
    });

    controller.closeWallet("alpha_wallet");

    QTRY_COMPARE(alpha_state.remove_calls.load(), 1);
    QCOMPARE(beta_state.remove_calls.load(), 0);
    QCOMPARE(selected_spy.count(), 1);
    QCOMPARE(load_state_spy.count(), 1);
    QCOMPARE(load_state_spy.at(0).at(0).toString(), QString{"alpha_wallet"});
    QCOMPARE(load_state_spy.at(0).at(1).toBool(), false);
    QCOMPARE(signal_order, QStringList({"selectedWalletChanged", "openWalletsChanged", "walletLoadStateChanged"}));
    QCOMPARE(controller.selectedWallet()->name(), QString{"beta_wallet"});
    QCOMPARE(controller.openWallets(), QList<QObject*>({beta_model}));
    QVERIFY(controller.isWalletLoaded());
    QVERIFY(!controller.isWalletOpen("alpha_wallet"));
    QVERIFY(controller.isWalletOpen("beta_wallet"));
}

void WalletQmlControllerTests::initializedControllerClosesNonSelectedWalletWithoutChangingSelection()
{
    FakeWallet::State alpha_state;
    FakeWallet::State beta_state;
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    loader.get_wallets_fn = [&]() {
        std::vector<std::unique_ptr<interfaces::Wallet>> wallets;
        wallets.emplace_back(std::make_unique<FakeWallet>("alpha_wallet", &alpha_state));
        wallets.emplace_back(std::make_unique<FakeWallet>("beta_wallet", &beta_state));
        return wallets;
    };
    ConfigureExpectedControllerInitialization(node, loader);

    DrainingWalletController controller(node);
    controller.initialize();
    QTRY_VERIFY(controller.initialized());

    QSignalSpy selected_spy(&controller, &WalletQmlController::selectedWalletChanged);
    QSignalSpy load_state_spy(&controller, &WalletQmlController::walletLoadStateChanged);

    controller.closeWallet("beta_wallet");

    QCOMPARE(alpha_state.remove_calls.load(), 0);
    QTRY_COMPARE(beta_state.remove_calls.load(), 1);
    QCOMPARE(selected_spy.count(), 0);
    QCOMPARE(load_state_spy.count(), 1);
    QCOMPARE(load_state_spy.at(0).at(0).toString(), QString{"beta_wallet"});
    QCOMPARE(load_state_spy.at(0).at(1).toBool(), false);
    QCOMPARE(controller.selectedWallet()->name(), QString{"alpha_wallet"});
    QVERIFY(controller.isWalletLoaded());
    QVERIFY(controller.isWalletOpen("alpha_wallet"));
    QVERIFY(!controller.isWalletOpen("beta_wallet"));
}

void WalletQmlControllerTests::reopeningRetiringWalletReportsLoadErrorAndSucceedsAfterDrain()
{
    FakeWallet::State state;
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    loader.wallet_dir_entries = {{"alpha_wallet", "sqlite"}};
    loader.get_wallets_fn = [&] {
        std::vector<std::unique_ptr<interfaces::Wallet>> wallets;
        wallets.emplace_back(std::make_unique<FakeWallet>("alpha_wallet", &state));
        return wallets;
    };
    struct LoadGate { QSemaphore entered, release; };
    const auto load_gate = std::make_shared<LoadGate>();
    loader.load_wallet_fn = [&, load_gate](const std::string& name, std::vector<bilingual_str>&) -> util::Result<std::unique_ptr<interfaces::Wallet>> {
        load_gate->entered.release();
        load_gate->release.acquire();
        return std::unique_ptr<interfaces::Wallet>{std::make_unique<FakeWallet>(name, &state)};
    };
    ConfigureExpectedControllerInitialization(node, loader);
    QSemaphore entered, release;
    DrainingWalletController controller{node};
    WalletListModel list{node};
    list.listWalletDir();
    QTRY_VERIFY(!list.projectionPending());
    QVERIFY(list.readError().isEmpty());
    connect(&controller, &WalletQmlController::walletLoadStateChanged, &list, &WalletListModel::setWalletLoadState);
    controller.initialize();
    QTRY_VERIFY(controller.initialized());
    const auto row = list.index(0);
    QTRY_COMPARE(list.data(row, WalletListModel::LoadStateRole).toInt(), static_cast<int>(WalletListModel::LoadState::Open));

    const auto executor = controller.selectedWallet()->backendExecutor();
    QSignalSpy retired{controller.selectedWallet(), &WalletQmlModel::shutdownFinished};
    executor->submit(this, [&] { entered.release(); release.acquire(); }, [] {});
    const auto release_on_exit = qScopeGuard([&] { release.release(); load_gate->release.release(); });
    QTRY_VERIFY(entered.available() > 0);
    controller.closeWallet("alpha_wallet");
    QVERIFY(!controller.isWalletOpen("alpha_wallet"));
    QTRY_COMPARE(list.data(row, WalletListModel::LoadStateRole).toInt(), static_cast<int>(WalletListModel::LoadState::Closed));

    for (int attempt = 0; attempt < 2; ++attempt) {
        controller.setSelectedWallet("alpha_wallet", "sqlite");
        QCOMPARE(loader.load_wallet_calls, 0);
        QTRY_COMPARE(list.data(row, WalletListModel::LoadStateRole).toInt(), static_cast<int>(WalletListModel::LoadState::LoadError));
        QCOMPARE(list.data(row, WalletListModel::ErrorMessageRole).toString(), QStringLiteral("This wallet is still closing."));
        QCOMPARE(controller.walletLoadError(), list.data(row, WalletListModel::ErrorMessageRole).toString());
    }

    release.release();
    QTRY_COMPARE(retired.size(), 1);
    QVERIFY(executor->isDrained());
    QSignalSpy loaded{&controller, &WalletQmlController::walletLoadSucceeded};
    controller.setSelectedWallet("alpha_wallet", "sqlite");
    QVERIFY(QTest::qWaitFor([&] { return load_gate->entered.tryAcquire(1); }, 5000));
    QTRY_COMPARE(list.data(row, WalletListModel::LoadStateRole).toInt(), static_cast<int>(WalletListModel::LoadState::Loading));
    QCOMPARE(loaded.size(), 0);
    QCOMPARE(loader.load_wallet_calls, 1);
    QVERIFY(controller.walletLoadInProgress());
    load_gate->release.release();
    QTRY_COMPARE(loaded.size(), 1);
    QCOMPARE(loader.load_wallet_calls, 1);
    QVERIFY(controller.isWalletOpen("alpha_wallet"));
    QTRY_COMPARE(list.data(row, WalletListModel::LoadStateRole).toInt(), static_cast<int>(WalletListModel::LoadState::Open));
    QVERIFY(list.data(row, WalletListModel::ErrorMessageRole).toString().isEmpty());
    QVERIFY(controller.walletLoadError().isEmpty());
}

void WalletQmlControllerTests::initializedControllerEmitsWalletLoadStateChanged()
{
    FakeWallet::State alpha_state;
    FakeWallet::State beta_state;
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    loader.get_wallets_fn = [&]() {
        std::vector<std::unique_ptr<interfaces::Wallet>> wallets;
        wallets.emplace_back(std::make_unique<FakeWallet>("alpha_wallet", &alpha_state));
        wallets.emplace_back(std::make_unique<FakeWallet>("beta_wallet", &beta_state));
        return wallets;
    };
    ConfigureExpectedControllerInitialization(node, loader);

    DrainingWalletController controller(node);
    QSignalSpy load_state_spy(&controller, &WalletQmlController::walletLoadStateChanged);

    controller.initialize();
    QTRY_VERIFY(controller.initialized());

    QCOMPARE(load_state_spy.count(), 2);
    QCOMPARE(load_state_spy.at(0).at(0).toString(), QString{"alpha_wallet"});
    QCOMPARE(load_state_spy.at(0).at(1).toBool(), true);
    QCOMPARE(load_state_spy.at(1).at(0).toString(), QString{"beta_wallet"});
    QCOMPARE(load_state_spy.at(1).at(1).toBool(), true);
    QCOMPARE(loader.list_wallet_dir_calls, 0);
}

void WalletQmlControllerTests::initializedControllerHandlesExternalWalletUnload()
{
    FakeWallet::State alpha_state;
    FakeWallet::State beta_state;
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    loader.get_wallets_fn = [&]() {
        std::vector<std::unique_ptr<interfaces::Wallet>> wallets;
        wallets.emplace_back(std::make_unique<FakeWallet>("alpha_wallet", &alpha_state));
        wallets.emplace_back(std::make_unique<FakeWallet>("beta_wallet", &beta_state));
        return wallets;
    };
    ConfigureExpectedControllerInitialization(node, loader);

    DrainingWalletController controller(node);
    controller.initialize();
    QTRY_VERIFY(controller.initialized());

    QTRY_VERIFY(alpha_state.hasUnload());
    QSignalSpy selected_spy(&controller, &WalletQmlController::selectedWalletChanged);
    QSignalSpy load_state_spy(&controller, &WalletQmlController::walletLoadStateChanged);
    WalletQmlModel* alpha_model = controller.selectedWallet();
    QVERIFY(alpha_model->available());
    QSignalSpy available_spy(alpha_model, &WalletQmlModel::availableChanged);

    alpha_state.notifyUnload();

    // Actions are refused from the moment the backend reports the unload,
    // before the GUI thread hears about it or drops the wallet.
    QVERIFY(!alpha_model->available());
    QVERIFY(controller.isWalletOpen("alpha_wallet"));
    QVERIFY(!alpha_model->backupWallet(QStringLiteral("/nonexistent/alpha.bak")));
    QCOMPARE(alpha_model->settingsError(), QString{"This wallet is closing."});
    QTRY_COMPARE(available_spy.count(), 1);

    QTRY_COMPARE(load_state_spy.count(), 1);
    QCOMPARE(load_state_spy.at(0).at(0).toString(), QString{"alpha_wallet"});
    QCOMPARE(load_state_spy.at(0).at(1).toBool(), false);
    QCOMPARE(selected_spy.count(), 1);
    QCOMPARE(controller.selectedWallet()->name(), QString{"beta_wallet"});
    QVERIFY(!controller.isWalletOpen("alpha_wallet"));
    QVERIFY(controller.isWalletOpen("beta_wallet"));
    QVERIFY(controller.isWalletLoaded());
    QCOMPARE(alpha_state.remove_calls.load(), 0);
    QCOMPARE(beta_state.remove_calls.load(), 0);
}

void WalletQmlControllerTests::initializedControllerUnloadWalletsClearsSelectionAndOpenWallets()
{
    FakeWallet::State alpha_state;
    FakeWallet::State beta_state;
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    loader.get_wallets_fn = [&]() {
        std::vector<std::unique_ptr<interfaces::Wallet>> wallets;
        wallets.emplace_back(std::make_unique<FakeWallet>("alpha_wallet", &alpha_state));
        wallets.emplace_back(std::make_unique<FakeWallet>("beta_wallet", &beta_state));
        return wallets;
    };
    ConfigureExpectedControllerInitialization(node, loader);

    DrainingWalletController controller(node);
    controller.initialize();
    QTRY_VERIFY(controller.initialized());

    QSignalSpy drained{&controller, &WalletQmlController::walletsDrained};
    QSignalSpy selected_spy(&controller, &WalletQmlController::selectedWalletChanged);
    QSignalSpy load_state_spy(&controller, &WalletQmlController::walletLoadStateChanged);

    controller.unloadWallets();

    QCOMPARE(selected_spy.count(), 1);
    QTRY_COMPARE(load_state_spy.count(), 2);
    QCOMPARE(load_state_spy.at(0).at(0).toString(), QString{"alpha_wallet"});
    QCOMPARE(load_state_spy.at(0).at(1).toBool(), false);
    QCOMPARE(load_state_spy.at(1).at(0).toString(), QString{"beta_wallet"});
    QCOMPARE(load_state_spy.at(1).at(1).toBool(), false);
    QCOMPARE(controller.selectedWallet()->name(), QString{});
    QVERIFY(!controller.isWalletOpen("alpha_wallet"));
    QVERIFY(!controller.isWalletOpen("beta_wallet"));
    QCOMPARE(alpha_state.remove_calls.load(), 0);
    QCOMPARE(beta_state.remove_calls.load(), 0);
    QTRY_COMPARE(drained.count(), 1);
}

void WalletQmlControllerTests::initializedControllerEmitsLoadingThenLoadErrorOnFailedLoad()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    loader.wallet_dir_entries = {{"alpha_wallet", "sqlite"}};
    ConfigureExpectedControllerInitialization(node, loader);

    DrainingWalletController controller(node);
    controller.initialize();
    QTRY_VERIFY(controller.initialized());

    QSignalSpy load_state_spy(&controller, &WalletQmlController::walletLoadStateChanged);
    QSignalSpy load_error_spy(&controller, &WalletQmlController::walletLoadErrorChanged);
    controller.setSelectedWallet("alpha_wallet", "sqlite");

    // FakeWalletLoader::loadWallet() defaults to returning an error, so
    // startWalletLoad must emit Loading synchronously and LoadError once the
    // worker completes.
    QTRY_COMPARE_WITH_TIMEOUT(load_state_spy.count(), 2, 5000);
    QCOMPARE(load_state_spy.at(0).at(0).toString(), QString{"alpha_wallet"});
    QCOMPARE(load_state_spy.at(0).at(1).toInt(),
             static_cast<int>(WalletListModel::LoadState::Loading));
    QCOMPARE(load_state_spy.at(0).at(2).toString(), QString{});

    QCOMPARE(load_state_spy.at(1).at(0).toString(), QString{"alpha_wallet"});
    QCOMPARE(load_state_spy.at(1).at(1).toInt(),
             static_cast<int>(WalletListModel::LoadState::LoadError));
    QVERIFY(!load_state_spy.at(1).at(2).toString().isEmpty());

    QTRY_VERIFY(load_error_spy.count() >= 1);
    QVERIFY(!controller.walletLoadInProgress());
}

void WalletQmlControllerTests::publishOpenWalletsInfoEmitsWalletInfoChangedForEachOpenWallet()
{
    FakeWallet::State alpha_state;
    FakeWallet::State beta_state;
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    alpha_state.balance = 13900000000LL;
    beta_state.balance = 1;
    loader.get_wallets_fn = [&]() {
        std::vector<std::unique_ptr<interfaces::Wallet>> wallets;
        wallets.emplace_back(std::make_unique<FakeWallet>("alpha_wallet", &alpha_state));
        wallets.emplace_back(std::make_unique<FakeWallet>("beta_wallet", &beta_state));
        return wallets;
    };
    ConfigureExpectedControllerInitialization(node, loader);

    DrainingWalletController controller(node);
    controller.initialize();
    QTRY_VERIFY(controller.initialized());

    controller.setSelectedWallet("alpha_wallet", "sqlite");
    QVERIFY(AwaitWalletOperation(controller));
    QTRY_COMPARE(controller.selectedWallet()->displayedBalanceSatoshi(), qint64{13900000000LL});
    controller.setSelectedWallet("beta_wallet", "sqlite");
    QVERIFY(AwaitWalletOperation(controller));
    QTRY_COMPARE(controller.selectedWallet()->displayedBalanceSatoshi(), qint64{1});
    QSignalSpy info_spy(&controller, &WalletQmlController::walletInfoChanged);
    const auto metadata_reads = alpha_state.name_reads.load() + beta_state.name_reads.load();
    controller.publishOpenWalletsInfo();
    QCOMPARE(alpha_state.name_reads.load() + beta_state.name_reads.load(), metadata_reads);

    QCOMPARE(info_spy.count(), 2);
    QStringList names;
    names << info_spy.at(0).at(0).toString() << info_spy.at(1).at(0).toString();
    names.sort();
    QCOMPARE(names, QStringList({"alpha_wallet", "beta_wallet"}));
    const auto verify_raw_balances = [&info_spy]() {
        QCOMPARE(info_spy.count(), 2);
        for (const auto& args : info_spy) {
            QCOMPARE(args.at(1).metaType().id(), QMetaType::LongLong);
            QCOMPARE(args.at(1).toLongLong(), args.at(0).toString() == "alpha_wallet" ? 13900000000LL : 1LL);
            QVERIFY(args.at(3).toBool());
        }
    };
    verify_raw_balances();
    controller.setSelectedWallet("alpha_wallet", "sqlite");
    QVERIFY(AwaitWalletOperation(controller));
    controller.selectedWallet()->setDisplayUnit(3);
    controller.setSelectedWallet("beta_wallet", "sqlite");
    QVERIFY(AwaitWalletOperation(controller));
    info_spy.clear();
    controller.publishOpenWalletsInfo();
    verify_raw_balances();
}

void WalletQmlControllerTests::walletNameAvailabilityErrorRejectsEmptyAndWhitespace()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    ConfigureExpectedControllerInitialization(node, loader);

    DrainingWalletController controller(node);
    controller.initialize();
    QTRY_VERIFY(controller.initialized());

    QCOMPARE(CheckedWalletName(controller, ""), QString{"Enter a wallet name."});
    QCOMPARE(CheckedWalletName(controller, "   "), QString{"Enter a wallet name."});
    QCOMPARE(CheckedWalletName(controller, "\t\n"), QString{"Enter a wallet name."});
}

void WalletQmlControllerTests::walletNameAvailabilityErrorRejectsExistingNameWithSurroundingWhitespace()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    loader.wallet_dir_entries = {{"alpha_wallet", "sqlite"}};
    ConfigureExpectedControllerInitialization(node, loader);

    DrainingWalletController controller(node);
    controller.initialize();
    QTRY_VERIFY(controller.initialized());

    QCOMPARE(CheckedWalletName(controller, "alpha_wallet"),
             QString{"A wallet with this name already exists"});
    QCOMPARE(CheckedWalletName(controller, "  alpha_wallet  "),
             QString{"A wallet with this name already exists"});
    QCOMPARE(CheckedWalletName(controller, "ALPHA_WALLET"),
             QString{"A wallet with this name already exists"});
}

void WalletQmlControllerTests::walletNameAvailabilityErrorReturnsEmptyForAvailableName()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    loader.wallet_dir_entries = {{"alpha_wallet", "sqlite"}};
    ConfigureExpectedControllerInitialization(node, loader);

    DrainingWalletController controller(node);
    controller.initialize();
    QTRY_VERIFY(controller.initialized());

    QCOMPARE(CheckedWalletName(controller, "brand_new_wallet"), QString{});
    QVERIFY(loader.directory_thread.load() != QThread::currentThread());
}

void WalletQmlControllerTests::openWalletLocationOpensWalletDirectory()
{
    FakeWallet::State wallet_state;
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    QTemporaryDir wallet_dir;
    QVERIFY(wallet_dir.isValid());
    QVERIFY(QDir{wallet_dir.path()}.mkpath("created_wallet"));
    loader.wallet_dir = wallet_dir.path().toStdString();
    loader.get_wallets_fn = [&]() {
        std::vector<std::unique_ptr<interfaces::Wallet>> wallets;
        wallets.emplace_back(std::make_unique<FakeWallet>("created_wallet", &wallet_state));
        return wallets;
    };
    ConfigureExpectedControllerInitialization(node, loader);

    DrainingWalletController controller(node);
    controller.initialize();
    QTRY_VERIFY(controller.initialized());

    QString opened_path;
    controller.setOpenLocalPathFnForTesting([&](const QString& path) {
        opened_path = path;
        return true;
    });

    QVERIFY(AwaitWalletLocation(controller, controller.selectedWallet()));
    QVERIFY(loader.directory_path_thread.load() != nullptr);
    QVERIFY(loader.directory_path_thread.load() != QThread::currentThread());
    QCOMPARE(opened_path, QFileInfo{wallet_dir.filePath("created_wallet")}.absoluteFilePath());
    QVERIFY(controller.walletLocationOpenError().isEmpty());
}

void WalletQmlControllerTests::openWalletLocationReportsOpenFailure()
{
    FakeWallet::State wallet_state;
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    QTemporaryDir wallet_dir;
    QVERIFY(wallet_dir.isValid());
    QVERIFY(QDir{wallet_dir.path()}.mkpath("created_wallet"));
    loader.wallet_dir = wallet_dir.path().toStdString();
    loader.get_wallets_fn = [&]() {
        std::vector<std::unique_ptr<interfaces::Wallet>> wallets;
        wallets.emplace_back(std::make_unique<FakeWallet>("created_wallet", &wallet_state));
        return wallets;
    };
    ConfigureExpectedControllerInitialization(node, loader);

    DrainingWalletController controller(node);
    controller.initialize();
    QTRY_VERIFY(controller.initialized());

    QString opened_path;
    controller.setOpenLocalPathFnForTesting([&](const QString& path) {
        opened_path = path;
        return false;
    });

    QVERIFY(!AwaitWalletLocation(controller, controller.selectedWallet()));
    QCOMPARE(opened_path, QFileInfo{wallet_dir.filePath("created_wallet")}.absoluteFilePath());
    QCOMPARE(controller.walletLocationOpenError(), QString{"Could not open wallet file location."});
}

void WalletQmlControllerTests::openWalletLocationReportsMissingWalletPath()
{
    FakeWallet::State wallet_state;
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    QTemporaryDir wallet_dir;
    QVERIFY(wallet_dir.isValid());
    loader.wallet_dir = wallet_dir.path().toStdString();
    loader.get_wallets_fn = [&]() {
        std::vector<std::unique_ptr<interfaces::Wallet>> wallets;
        wallets.emplace_back(std::make_unique<FakeWallet>("missing_wallet", &wallet_state));
        return wallets;
    };
    ConfigureExpectedControllerInitialization(node, loader);

    DrainingWalletController controller(node);
    controller.initialize();
    QTRY_VERIFY(controller.initialized());

    bool opened{false};
    controller.setOpenLocalPathFnForTesting([&](const QString&) {
        opened = true;
        return true;
    });

    QVERIFY(!AwaitWalletLocation(controller, controller.selectedWallet()));
    QVERIFY(!opened);
    QCOMPARE(controller.walletLocationOpenError(), QString{"Wallet file not found: %1"}.arg(QFileInfo{wallet_dir.filePath("missing_wallet")}.absoluteFilePath()));
}

void WalletQmlControllerTests::openWalletLocationReportsMissingWallet()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    DrainingWalletController controller(node);

    QSignalSpy errors{&controller, &WalletQmlController::walletLocationOpenErrorChanged};
    node.ExpectNoCalls(node.calls.walletLoader);
    QVERIFY(!AwaitWalletLocation(controller, nullptr));
    QCOMPARE(errors.count(), 1);
    QVERIFY(!controller.walletLocationPending());
    QCOMPARE(controller.walletLocationOpenError(), QString{"No wallet file is available to view."});
}

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(WalletQmlControllerTests)
#else
QTEST_MAIN(WalletQmlControllerTests)
#endif

#include <test_walletqmlcontroller.moc>
