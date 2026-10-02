// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <chainparams.h>
#include <common/settings.h>
#include <common/run_command.h>
#include <univalue.h>
#include <interfaces/handler.h>
#include <interfaces/wallet.h>
#include <outputtype.h>
#include <qml/walletqmlcontroller.h>
#include <qml/models/walletlistmodel.h>
#include <scheduler.h>
#include <test/mocks/mocknode.h>
#include <util/translation.h>
#include <wallet/types.h>
#include <wallet/walletutil.h>

#include <QDir>
#include <QFileInfo>
#include <QScopeGuard>
#include <QSemaphore>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>

#include <atomic>
#include <mutex>

#ifndef BITCOINQML_NO_TEST_MAIN
const TranslateFn G_TRANSLATION_FUN{nullptr};
#endif

namespace {
constexpr auto NOT_INITIALIZED_ERROR{"Wallets are still loading. Try again in a moment."};

std::unique_ptr<interfaces::Handler> MakeNoopHandler()
{
    return interfaces::MakeCleanupHandler([] {});
}

// Test scopes mirror the application's asynchronous shutdown contract.
class DrainingWalletController : public WalletQmlController
{
public:
    using WalletQmlController::WalletQmlController;
    ~DrainingWalletController() override
    {
        beginShutdown();
        if (!QTest::qWaitFor([this] { return isShutdownComplete(); }, 10000)) {
            qFatal("The wallet fixture did not drain before backend teardown");
        }
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
};

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
    std::atomic<int> create_wallet_calls{0};
    std::atomic<int> migrate_wallet_calls{0};
    std::atomic<int> is_encrypted_calls{0};
    std::atomic<int> handle_load_wallet_calls{0};
    std::atomic<int> get_wallets_calls{0};
    std::atomic<int> list_wallet_dir_calls{0};
    std::atomic<int> load_wallet_calls{0};
    std::string wallet_dir;

    std::function<util::Result<std::unique_ptr<interfaces::Wallet>>(const std::string&, const SecureString&, uint64_t, std::vector<bilingual_str>&)>
        create_wallet_fn = [](const std::string&, const SecureString&, uint64_t, std::vector<bilingual_str>&) {
            return util::Error{Untranslated("Unexpected createWallet call")};
        };
    std::function<util::Result<std::unique_ptr<interfaces::Wallet>>(const std::string&, std::vector<bilingual_str>&)>
        load_wallet_fn = [](const std::string&, std::vector<bilingual_str>&) {
            return util::Error{Untranslated("Unexpected loadWallet call")};
        };
    std::function<util::Result<interfaces::WalletMigrationResult>(const std::string&, const SecureString&)>
        migrate_wallet_fn = [](const std::string&, const SecureString&) {
            return util::Error{Untranslated("Unexpected migrateWallet call")};
        };
    std::function<bool(const std::string&)> is_encrypted_fn = [](const std::string&) {
        return false;
    };
    std::function<std::vector<std::unique_ptr<interfaces::Wallet>>()> get_wallets_fn = [] {
        return std::vector<std::unique_ptr<interfaces::Wallet>>{};
    };
    std::vector<std::pair<std::string, std::string>> wallet_dir_entries;
    LoadWalletFn captured_load_wallet_fn;

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
    std::string getWalletDir() override { return wallet_dir; }
    util::Result<std::unique_ptr<interfaces::Wallet>> restoreWallet(const fs::path&, const std::string&, std::vector<bilingual_str>&, bool) override
    {
        return util::Error{Untranslated("Unexpected restoreWallet call")};
    }
    util::Result<interfaces::WalletMigrationResult> migrateWallet(const std::string& name, const SecureString& passphrase) override
    {
        ++migrate_wallet_calls;
        return migrate_wallet_fn(name, passphrase);
    }
    bool isEncrypted(const std::string& name) override
    {
        ++is_encrypted_calls;
        return is_encrypted_fn(name);
    }
    std::vector<std::pair<std::string, std::string>> listWalletDir() override
    {
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
        return MakeNoopHandler();
    }
};

class FakeWallet : public interfaces::Wallet
{
public:
    struct State {
        std::atomic<int> remove_calls{0};
        CAmount balance{0};
        std::function<void()> before_remove;
        std::mutex mutex;
        interfaces::Wallet::UnloadFn unload_fn;

        interfaces::Wallet::UnloadFn unloadCallback()
        {
            std::lock_guard lock(mutex);
            return unload_fn;
        }
    };

    explicit FakeWallet(std::string wallet_name, State* state)
        : m_wallet_name(std::move(wallet_name)), m_state(state) {}

    bool private_keys_disabled{false};
    bool external_signer{false};

    bool encryptWallet(const SecureString&) override { return true; }
    bool isCrypted() override { return false; }
    bool lock() override { return true; }
    bool unlock(const SecureString&) override { return true; }
    bool isLocked() override { return false; }
    bool changeWalletPassphrase(const SecureString&, const SecureString&) override { return true; }
    void abortRescan() override {}
    bool backupWallet(const std::string&) override { return true; }
    std::string getWalletName() override { return m_wallet_name; }
    util::Result<CTxDestination> getNewDestination(const OutputType, const std::string&) override
    {
        return CTxDestination{CNoDestination{}};
    }
    bool getPubKey(const CScript&, const CKeyID&, CPubKey&) override { return false; }
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
    void commitTransaction(CTransactionRef, interfaces::WalletValueMap, interfaces::WalletOrderForm) override {}
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
    interfaces::WalletTx getWalletTxDetails(const Txid&, interfaces::WalletTxStatus&, interfaces::WalletOrderForm&, bool&, int&) override { return {}; }
    std::optional<common::PSBTError> fillPSBT(const common::PSBTFillOptions&, size_t*, PartiallySignedTransaction&, bool&) override
    {
        return std::nullopt;
    }
    interfaces::WalletBalances getBalances() override { return {}; }
    bool tryGetBalances(interfaces::WalletBalances&, uint256&) override { return false; }
    CAmount getBalance() override { return m_state ? m_state->balance : 0; }
    CAmount getAvailableBalance(const wallet::CCoinControl&) override { return 0; }
    bool txinIsMine(const CTxIn&) override { return false; }
    bool txoutIsMine(const CTxOut&) override { return false; }
    CAmount getDebit(const CTxIn&) override { return 0; }
    CAmount getCredit(const CTxOut&) override { return 0; }
    CoinsList listCoins() override { return {}; }
    std::vector<interfaces::WalletTxOut> getCoins(const std::vector<COutPoint>&) override { return {}; }
    CAmount getRequiredFee(unsigned int) override { return 0; }
    CAmount getMinimumFee(unsigned int, const wallet::CCoinControl&, int*, FeeReason*) override { return 0; }
    unsigned int getConfirmTarget() override { return 6; }
    bool hdEnabled() override { return true; }
    bool canGetAddresses() override { return true; }
    bool privateKeysDisabled() override { return private_keys_disabled; }
    bool taprootEnabled() override { return true; }
    bool hasExternalSigner() override { return external_signer; }
    OutputType getDefaultAddressType() override { return OutputType::BECH32; }
    CAmount getDefaultMaxTxFee() override { return COIN; }
    void remove() override
    {
        if (m_state) {
            ++m_state->remove_calls;
            if (m_state->before_remove) m_state->before_remove();
        }
    }
    std::unique_ptr<interfaces::Handler> handleUnload(UnloadFn fn) override
    {
        if (m_state) {
            {
                std::lock_guard lock(m_state->mutex);
                m_state->unload_fn = std::move(fn);
            }
            return interfaces::MakeCleanupHandler([state = m_state] {
                std::lock_guard lock(state->mutex);
                state->unload_fn = nullptr;
            });
        }
        return MakeNoopHandler();
    }
    std::unique_ptr<interfaces::Handler> handleShowProgress(ShowProgressFn) override { return MakeNoopHandler(); }
    std::unique_ptr<interfaces::Handler> handleStatusChanged(StatusChangedFn) override { return MakeNoopHandler(); }
    std::unique_ptr<interfaces::Handler> handleAddressBookChanged(AddressBookChangedFn) override { return MakeNoopHandler(); }
    std::unique_ptr<interfaces::Handler> handleTransactionChanged(TransactionChangedFn) override { return MakeNoopHandler(); }
    std::unique_ptr<interfaces::Handler> handleCanGetAddressesChanged(CanGetAddressesChangedFn) override { return MakeNoopHandler(); }

private:
    std::string m_wallet_name;
    State* m_state;
};

void ConfigureExpectedControllerInitialization(MockNode& node, FakeWalletLoader& loader)
{
    node.wallet_loader_fn = [&loader]() -> interfaces::WalletLoader& { return loader; };
    node.get_persistent_setting_fn = [](const std::string&) { return common::SettingsValue{}; };
    node.force_setting_fn = [](const std::string&, const common::SettingsValue&) {};
    node.list_external_signers_fn = [] { return std::vector<std::unique_ptr<interfaces::ExternalSigner>>{}; };
    node.get_dust_relay_fee_fn = [] { return CFeeRate{3000}; };
    node.ExpectAtLeast(node.calls.walletLoader, 2);
    node.ExpectAtLeast(node.calls.getPersistentSetting, 1);
    node.ExpectExactly(node.calls.forceSetting, 1);
    node.ExpectExactly(node.calls.listExternalSigners, 1);
}

const QString VALID_XPUB{
    "xpub661MyMwAqRbcFtXgS5sYJABqqG9YLmC4Q1Rdap9gSE8NqtwybGhePY2gZ29ESFjqJoCu1Rupje8YtGqsefD265TMg7usUDFdp6W1EGMcet8"};
} // namespace

class WalletQmlControllerTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void shutdownDrainsWalletQueueAndPlaceholder();
    void shutdownRetiresQueuedLoadNotificationOffGui();
    void shutdownReleasesNotificationsBeforeMigrationRollback();
    void shutdownWaitsForNotificationRetirement();
    void validateXpubAcceptsValidKey();
    void validateXpubRejectsGarbage();
    void validateXpubRejectsEmpty();
    void validateXpubTrimsWhitespace();
    void createWatchOnlyInvalidXpubSetsError();
    void createWatchOnlyCleansUpWhenDescriptorImportFails();
    void externalSignerDiscoveryWaitsForSubprocessOffGui()
    {
        const auto python = QStandardPaths::findExecutable("python3");
        if (python.isEmpty()) QSKIP("Python is required for the external signer process fixture.");
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto ready_path = dir.filePath("ready");
        const auto release_path = dir.filePath("release");
        QFile script(dir.filePath("signer.py"));
        QVERIFY(script.open(QIODevice::WriteOnly));
        script.write("import pathlib, sys, time\npathlib.Path(sys.argv[1]).touch()\nend = time.monotonic() + 10\nwhile not pathlib.Path(sys.argv[2]).exists() and time.monotonic() < end:\n    time.sleep(0.01)\nprint('{\"name\":\"Gated signer\"}')\n");
        script.close();
        MockNode node;
        node.list_external_signers_fn = [python, path = script.fileName(), ready_path, release_path] {
            const auto result = RunCommandParseJSON({python.toStdString(), path.toStdString(), ready_path.toStdString(), release_path.toStdString()});
            std::vector<std::unique_ptr<interfaces::ExternalSigner>> signers;
            signers.emplace_back(std::make_unique<FakeExternalSigner>(result["name"].get_str()));
            return signers;
        };
        DrainingWalletController controller(node);
        controller.refreshExternalSignerStatus();
        QTRY_VERIFY(QFileInfo::exists(ready_path));
        bool gui_callback{false};
        QTimer::singleShot(0, &controller, [&] { gui_callback = true; });
        QTRY_VERIFY(gui_callback);
        QVERIFY(controller.externalSignerPending());
        QFile release(release_path);
        QVERIFY(release.open(QIODevice::WriteOnly));
        release.close();
        QTRY_VERIFY(!controller.externalSignerPending());
        QCOMPARE(controller.externalSignerName(), QString{"Gated signer"});
    }

    void externalSignerDiscoveryDrainsBeforeShutdown()
    {
        QSemaphore entered, release;
        MockNode node;
        node.list_external_signers_fn = [&] {
            entered.release();
            release.acquire();
            return MakeSigners({"Gated signer"});
        };
        DrainingWalletController controller(node);
        const auto unblock = qScopeGuard([&] { release.release(); });
        QSignalSpy drained{&controller, &WalletQmlController::walletsDrained};
        controller.refreshExternalSignerStatus();
        QTRY_VERIFY(entered.available() > 0);
        controller.beginShutdown();
        QTest::qWait(25);
        QVERIFY(drained.empty());
        release.release();
        QTRY_COMPARE(drained.count(), 1);
    }

    void externalSignerCreationRequiresConfiguredPath();
    void externalSignerCreationRequiresExactlyOneSigner();
    void externalSignerSuggestionUsesSignerName();
    void initializeCoalescesConcurrentRequests();
    void emptyWalletJobsDrainBeforeControllerShutdown();
    void closeBlocksReopenUntilBackendRemovalFinishes();
    void initializedControllerSignalsMigrationForLegacyWallet();
    void createWalletBeforeInitializationReturnsFalseAndSetsError();
    void importWalletBeforeInitializationSetsLoadError();
    void migrateWalletBeforeInitializationSetsMigrationError();
    void selectWalletBeforeInitializationSetsLoadError();
    void initializedControllerPropagatesCreateErrors();
    void initializedControllerForwardsMigrationPassphrase();
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
    void initializedControllerEmitsWalletLoadStateChanged();
    void initializedControllerHandlesExternalWalletUnload();
    void initializedControllerUnloadWalletsClearsSelectionAndOpenWallets();
    void initializedControllerEmitsLoadingThenLoadErrorOnFailedLoad();
    void publishOpenWalletsInfoEmitsWalletInfoChangedForEachOpenWallet();
    void walletNameAvailabilityErrorRejectsEmptyAndWhitespace();
    void walletNameAvailabilityErrorRejectsExistingNameWithSurroundingWhitespace();
    void walletNameAvailabilityErrorReturnsEmptyForAvailableName();
    void openSelectedWalletLocationOpensWalletDirectory();
    void openSelectedWalletLocationReportsOpenFailure();
    void openSelectedWalletLocationReportsMissingWalletPath();
    void openSelectedWalletLocationReportsMissingSelectedWallet();
};

void WalletQmlControllerTests::initTestCase()
{
    SelectParams(ChainType::MAIN);
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
    loader.migrate_wallet_fn = [&, probe, gui_thread](const std::string&, const SecureString&) -> util::Result<interfaces::WalletMigrationResult> {
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

void WalletQmlControllerTests::validateXpubAcceptsValidKey()
{
    MockNode node;
    DrainingWalletController controller(node);

    QVERIFY(controller.validateXpub(VALID_XPUB));
}

void WalletQmlControllerTests::validateXpubRejectsGarbage()
{
    MockNode node;
    DrainingWalletController controller(node);

    QVERIFY(!controller.validateXpub("not-an-xpub"));
}

void WalletQmlControllerTests::validateXpubRejectsEmpty()
{
    MockNode node;
    DrainingWalletController controller(node);

    QVERIFY(!controller.validateXpub(""));
}

void WalletQmlControllerTests::validateXpubTrimsWhitespace()
{
    MockNode node;
    DrainingWalletController controller(node);

    QVERIFY(controller.validateXpub("  " + VALID_XPUB + "  "));
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

    QCOMPARE(controller.walletLoadError(), QString{"Invalid extended public key."});
    QVERIFY(!controller.isWalletLoaded());
    QCOMPARE(error_spy.count(), 1);
}

void WalletQmlControllerTests::createWatchOnlyCleansUpWhenDescriptorImportFails()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    ConfigureExpectedControllerInitialization(node, loader);

    FakeWallet::State wallet_state;
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

    // FakeWallet::wallet() returns nullptr, so descriptor import adds zero
    // ScriptPubKeyMans and the controller must reject the half-created wallet,
    // call remove() on it, and surface a load error without registering a model.
    controller.createWatchOnlyWallet("watch_wallet", VALID_XPUB);

    QTRY_COMPARE_WITH_TIMEOUT(error_spy.count(), 1, 5000);
    QVERIFY(!controller.walletLoadError().isEmpty());
    QCOMPARE(wallet_state.remove_calls.load(), 1);
    QCOMPARE(create_spy.count(), 0);
    QVERIFY(!controller.isWalletLoaded());
    QVERIFY(!controller.walletLoadInProgress());
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

void WalletQmlControllerTests::emptyWalletJobsDrainBeforeControllerShutdown()
{
    MockNode node;
    QSemaphore entered;
    QSemaphore release;
    DrainingWalletController controller(node);
    const auto unblock = qScopeGuard([&] { release.release(); });
    bool completion{false};
    QVERIFY(controller.selectedWallet()->backendExecutor()->submit(&controller, [&] {
        entered.release();
        release.tryAcquire(1, 5000);
    }, [&] { completion = true; }));
    QTRY_VERIFY(entered.available() > 0);
    QSignalSpy drained(&controller, &WalletQmlController::walletsDrained);
    controller.beginShutdown();
    bool heartbeat{false};
    QTimer::singleShot(0, &controller, [&] { heartbeat = true; });
    QTRY_VERIFY(heartbeat);
    QVERIFY(!controller.isShutdownComplete());
    QCOMPARE(drained.count(), 0);
    release.release();
    QTRY_VERIFY(controller.isShutdownComplete());
    QCOMPARE(drained.count(), 1);
    QVERIFY(!completion);
}

void WalletQmlControllerTests::initializeCoalescesConcurrentRequests()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    QSemaphore entered;
    QSemaphore release;
    const auto unblock = qScopeGuard([&] { release.release(2); });
    std::atomic<bool> off_gui{false};
    loader.get_wallets_fn = [&] {
        off_gui = QThread::currentThread() != QCoreApplication::instance()->thread();
        entered.release();
        release.tryAcquire(1, 5000);
        return std::vector<std::unique_ptr<interfaces::Wallet>>{};
    };
    ConfigureExpectedControllerInitialization(node, loader);
    DrainingWalletController controller(node);
    QSignalSpy initialized(&controller, &WalletQmlController::initializedChanged);
    controller.initialize();
    QTRY_VERIFY(entered.available() > 0);
    controller.initialize();
    bool heartbeat{false};
    QTimer::singleShot(0, &controller, [&] { heartbeat = true; });
    QTRY_VERIFY(heartbeat);
    QVERIFY(off_gui.load());
    QVERIFY(!controller.initialized());
    QCOMPARE(initialized.count(), 0);
    QCOMPARE(loader.handle_load_wallet_calls.load(), 1);
    release.release(2);
    QTRY_VERIFY(controller.initialized());
    QCOMPARE(initialized.count(), 1);
    controller.beginShutdown();
    QTRY_VERIFY(controller.isShutdownComplete());
    QCOMPARE(loader.handle_load_wallet_calls.load(), 1);
    QCOMPARE(loader.get_wallets_calls.load(), 1);
}

void WalletQmlControllerTests::closeBlocksReopenUntilBackendRemovalFinishes()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    FakeWallet::State original;
    FakeWallet::State reopened;
    QSemaphore entered;
    QSemaphore release;
    const auto unblock = qScopeGuard([&] { release.release(); });
    std::atomic<bool> off_gui{false};
    original.before_remove = [&] {
        off_gui = QThread::currentThread() != QCoreApplication::instance()->thread();
        entered.release();
        release.tryAcquire(1, 5000);
    };
    loader.wallet_dir_entries = {{"alpha_wallet", "sqlite"}};
    loader.get_wallets_fn = [&] {
        std::vector<std::unique_ptr<interfaces::Wallet>> wallets;
        wallets.emplace_back(std::make_unique<FakeWallet>("alpha_wallet", &original));
        return wallets;
    };
    loader.load_wallet_fn = [&](const std::string& name, std::vector<bilingual_str>&) -> util::Result<std::unique_ptr<interfaces::Wallet>> {
        return util::Result<std::unique_ptr<interfaces::Wallet>>{std::make_unique<FakeWallet>(name, &reopened)};
    };
    ConfigureExpectedControllerInitialization(node, loader);
    DrainingWalletController controller(node);
    controller.initialize();
    QTRY_VERIFY(controller.initialized());
    QSignalSpy states(&controller, &WalletQmlController::walletLoadStateChanged);
    controller.closeWallet("alpha_wallet");
    QTRY_VERIFY(entered.available() > 0);
    QVERIFY(off_gui.load());
    QCOMPARE(states.count(), 1);
    QCOMPARE(states.front().at(1).value<WalletListModel::LoadState>(), WalletListModel::LoadState::Closing);
    controller.setSelectedWallet("alpha_wallet");
    QVERIFY(controller.walletLoadError().contains("still closing"));
    QVERIFY(!controller.walletLoadInProgress());
    QCOMPARE(loader.load_wallet_calls.load(), 0);
    bool heartbeat{false};
    QTimer::singleShot(0, &controller, [&] { heartbeat = true; });
    QTRY_VERIFY(heartbeat);
    QCOMPARE(states.count(), 1);
    release.release();
    QTRY_COMPARE(states.count(), 2);
    QCOMPARE(states.back().at(1).value<WalletListModel::LoadState>(), WalletListModel::LoadState::Closed);
    controller.setSelectedWallet("alpha_wallet");
    QTRY_VERIFY(!controller.walletLoadInProgress());
    QVERIFY(controller.walletLoadError().isEmpty());
    QVERIFY(controller.isWalletOpen("alpha_wallet"));
    QCOMPARE(loader.load_wallet_calls.load(), 1);
    QCOMPARE(original.remove_calls.load(), 1);
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
    QTRY_COMPARE(migration_spy.count(), 1);
    QCOMPARE(migration_spy.takeFirst().at(0).toString(), QString{"legacy_wallet"});
    QCOMPARE(loader.load_wallet_calls.load(), 0);
    QVERIFY(!controller.walletLoadInProgress());
    QVERIFY(controller.walletLoadError().isEmpty());
}

void WalletQmlControllerTests::createWalletBeforeInitializationReturnsFalseAndSetsError()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    DrainingWalletController controller(node);

    controller.createSingleSigWallet("test_wallet", "secret");
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
    QCOMPARE(controller.walletLoadError(), QString{NOT_INITIALIZED_ERROR});
}

void WalletQmlControllerTests::migrateWalletBeforeInitializationSetsMigrationError()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    DrainingWalletController controller(node);

    controller.migrateWallet("legacy_wallet", "secret");
    QCOMPARE(controller.walletMigrationError(), QString{NOT_INITIALIZED_ERROR});
}

void WalletQmlControllerTests::selectWalletBeforeInitializationSetsLoadError()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    DrainingWalletController controller(node);

    controller.setSelectedWallet("test_wallet");
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
    QVERIFY(error_spy.wait(5000));
    QVERIFY(saw_expected_passphrase);
    QVERIFY(saw_expected_flags);
    QCOMPARE(loader.create_wallet_calls.load(), 1);
    QCOMPARE(controller.walletCreateError(), QString{"Wallet creation failed."});
    QVERIFY(!controller.isWalletLoaded());
    QVERIFY(!controller.walletLoadInProgress());
}

void WalletQmlControllerTests::initializedControllerForwardsMigrationPassphrase()
{
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
    loader.migrate_wallet_fn = [&](const std::string& name, const SecureString& passphrase) {
        saw_expected_name = (name == "legacy_wallet");
        saw_expected_passphrase = (passphrase == SecureString{"secret"});
        return util::Result<interfaces::WalletMigrationResult>{
            util::Error{Untranslated("Migration failed.")}};
    };

    controller.migrateWallet("legacy_wallet", "secret");
    QVERIFY(failed_spy.wait(5000));
    QVERIFY(saw_expected_name);
    QVERIFY(saw_expected_passphrase);
    QCOMPARE(loader.migrate_wallet_calls.load(), 1);
    QCOMPARE(controller.walletMigrationError(), QString{"Migration failed."});
    QVERIFY(!controller.walletMigrationInProgress());
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

    const QString passphrase{QString::fromUtf8("pässwörd-₿")};
    const std::string expected_passphrase{passphrase.toUtf8().toStdString()};

    bool saw_expected_passphrase{false};
    loader.create_wallet_fn = [&](const std::string&,
                                  const SecureString& passphrase,
                                  uint64_t,
                                  std::vector<bilingual_str>&) {
        saw_expected_passphrase = (std::string{passphrase.begin(), passphrase.end()} == expected_passphrase);
        return util::Result<std::unique_ptr<interfaces::Wallet>>{
            util::Error{Untranslated("Wallet creation failed.")}};
    };

    QSignalSpy error_spy(&controller, &WalletQmlController::walletCreateErrorChanged);
    controller.createSingleSigWallet("test_wallet", passphrase);
    QVERIFY(error_spy.wait(5000));
    QVERIFY(saw_expected_passphrase);
    QCOMPARE(loader.create_wallet_calls.load(), 1);
    QCOMPARE(controller.walletCreateError(), QString{"Wallet creation failed."});
}

void WalletQmlControllerTests::initializedControllerEmitsWalletCreateSucceeded()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    ConfigureExpectedControllerInitialization(node, loader);

    FakeWallet::State wallet_state;
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

    QTRY_COMPARE_WITH_TIMEOUT(create_spy.count(), 1, 5000);
    QVERIFY(controller.isWalletLoaded());
    QVERIFY(!controller.walletLoadInProgress());
    QCOMPARE(controller.walletCreateError(), QString{});
}

void WalletQmlControllerTests::initializedControllerDoesNotCompleteCreateForUnrelatedLoadNotification()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    ConfigureExpectedControllerInitialization(node, loader);

    FakeWallet::State created_state;
    FakeWallet::State unrelated_state;
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
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    loader.wallet_dir_entries = {{"target_wallet", "sqlite"}};
    ConfigureExpectedControllerInitialization(node, loader);

    FakeWallet::State loaded_state;
    FakeWallet::State unrelated_state;
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
    QVERIFY(warnings_spy.wait(5000));
    QCOMPARE(controller.walletLoadWarnings(), QString{"Disk space is low."});
    QCOMPARE(controller.walletCreateError(), QString{"Wallet creation failed."});
}

void WalletQmlControllerTests::initializedControllerIgnoresDuplicateCreateWhileInProgress()
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
                                 std::vector<bilingual_str>&) {
        return util::Result<std::unique_ptr<interfaces::Wallet>>{
            util::Error{Untranslated("Wallet creation failed.")}};
    };

    QSignalSpy error_spy(&controller, &WalletQmlController::walletCreateErrorChanged);
    controller.createSingleSigWallet("test_wallet", "secret");
    // Second call while the first is in flight must be dropped without
    // touching the loader. The flag is set synchronously before the worker
    // posts, so this check happens before the first call completes.
    controller.createSingleSigWallet("test_wallet", "secret");
    QVERIFY(error_spy.wait(5000));
    QCOMPARE(loader.create_wallet_calls.load(), 1);
}

void WalletQmlControllerTests::watchOnlyCreateBeforeInitializationSetsLoadError()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    DrainingWalletController controller(node);

    controller.createWatchOnlyWallet("watch_only", VALID_XPUB);

    QCOMPARE(controller.walletLoadError(), QString{NOT_INITIALIZED_ERROR});
    QVERIFY(!controller.walletLoadInProgress());
}

void WalletQmlControllerTests::watchOnlyCreateWhileWalletLoadInProgressIsIgnored()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    ConfigureExpectedControllerInitialization(node, loader);

    FakeWallet::State wallet_state;
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

    QCOMPARE(loader.create_wallet_calls.load(), 1);
    QVERIFY(controller.walletLoadInProgress());

    allow_create_return.release();
    QTRY_VERIFY_WITH_TIMEOUT(!controller.walletLoadInProgress(), 5000);
}

void WalletQmlControllerTests::watchOnlyCreateFailureAfterCreateDoesNotPublishWallet()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    ConfigureExpectedControllerInitialization(node, loader);

    FakeWallet::State returned_state;
    FakeWallet::State notified_state;
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
    QTRY_COMPARE(passphrase_spy.count(), 1);
    QCOMPARE(passphrase_spy.takeFirst().at(0).toString(), QString{"legacy_wallet"});
    QCOMPARE(loader.is_encrypted_calls.load(), 1);
    QCOMPARE(loader.migrate_wallet_calls.load(), 0);
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
    loader.migrate_wallet_fn = [&](const std::string& name, const SecureString& passphrase) {
        saw_empty_passphrase = (name == "legacy_wallet" && passphrase.empty());
        return util::Result<interfaces::WalletMigrationResult>{
            util::Error{Untranslated("Migration failed.")}};
    };

    controller.migrateWallet("legacy_wallet", "");
    QVERIFY(failed_spy.wait(5000));
    QVERIFY(saw_empty_passphrase);
    QCOMPARE(loader.is_encrypted_calls.load(), 1);
    QCOMPARE(loader.migrate_wallet_calls.load(), 1);
    QCOMPARE(controller.walletMigrationError(), QString{"Migration failed."});
    QVERIFY(!controller.walletMigrationInProgress());
}

void WalletQmlControllerTests::initializedControllerClosesSelectedWalletAndSelectsRemainingLoadedWallet()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    FakeWallet::State alpha_state;
    FakeWallet::State beta_state;
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

    QSignalSpy selected_spy(&controller, &WalletQmlController::selectedWalletChanged);
    QSignalSpy load_state_spy(&controller, &WalletQmlController::walletLoadStateChanged);
    QStringList signal_order;
    QObject signal_context;
    QObject::connect(&controller, &WalletQmlController::selectedWalletChanged, &signal_context, [&]() {
        signal_order.append("selectedWalletChanged");
    });
    QObject::connect(&controller, &WalletQmlController::walletLoadStateChanged, &signal_context, [&]() {
        signal_order.append("walletLoadStateChanged");
    });

    controller.closeWallet("alpha_wallet");

    QTRY_COMPARE(alpha_state.remove_calls.load(), 1);
    QCOMPARE(beta_state.remove_calls.load(), 0);
    QCOMPARE(selected_spy.count(), 1);
    QTRY_COMPARE(load_state_spy.count(), 2);
    QCOMPARE(load_state_spy.at(0).at(0).toString(), QString{"alpha_wallet"});
    QCOMPARE(load_state_spy.at(0).at(1).toInt(), static_cast<int>(WalletListModel::LoadState::Closing));
    QCOMPARE(load_state_spy.at(1).at(1).toInt(), static_cast<int>(WalletListModel::LoadState::Closed));
    QCOMPARE(signal_order, QStringList({"selectedWalletChanged", "walletLoadStateChanged", "walletLoadStateChanged"}));
    QCOMPARE(controller.selectedWallet()->name(), QString{"beta_wallet"});
    QVERIFY(controller.isWalletLoaded());
    QVERIFY(!controller.isWalletOpen("alpha_wallet"));
    QVERIFY(controller.isWalletOpen("beta_wallet"));
}

void WalletQmlControllerTests::initializedControllerClosesNonSelectedWalletWithoutChangingSelection()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    FakeWallet::State alpha_state;
    FakeWallet::State beta_state;
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
    QTRY_COMPARE(load_state_spy.count(), 2);
    QCOMPARE(load_state_spy.at(0).at(0).toString(), QString{"beta_wallet"});
    QCOMPARE(load_state_spy.at(0).at(1).toInt(), static_cast<int>(WalletListModel::LoadState::Closing));
    QCOMPARE(load_state_spy.at(1).at(1).toInt(), static_cast<int>(WalletListModel::LoadState::Closed));
    QCOMPARE(controller.selectedWallet()->name(), QString{"alpha_wallet"});
    QVERIFY(controller.isWalletLoaded());
    QVERIFY(controller.isWalletOpen("alpha_wallet"));
    QVERIFY(!controller.isWalletOpen("beta_wallet"));
}

void WalletQmlControllerTests::initializedControllerEmitsWalletLoadStateChanged()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    FakeWallet::State alpha_state;
    FakeWallet::State beta_state;
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
    QCOMPARE(loader.list_wallet_dir_calls.load(), 1);
}

void WalletQmlControllerTests::initializedControllerHandlesExternalWalletUnload()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    FakeWallet::State alpha_state;
    FakeWallet::State beta_state;
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

    QTRY_VERIFY(alpha_state.unloadCallback());
    QSignalSpy selected_spy(&controller, &WalletQmlController::selectedWalletChanged);
    QSignalSpy load_state_spy(&controller, &WalletQmlController::walletLoadStateChanged);

    alpha_state.unloadCallback()();

    QTRY_COMPARE(load_state_spy.count(), 2);
    QCOMPARE(load_state_spy.at(0).at(0).toString(), QString{"alpha_wallet"});
    QCOMPARE(load_state_spy.at(0).at(1).toInt(), static_cast<int>(WalletListModel::LoadState::Closing));
    QCOMPARE(load_state_spy.at(1).at(1).toInt(), static_cast<int>(WalletListModel::LoadState::Closed));
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
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    FakeWallet::State alpha_state;
    FakeWallet::State beta_state;
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

    controller.unloadWallets();

    QCOMPARE(selected_spy.count(), 2);
    QCOMPARE(load_state_spy.count(), 2);
    QCOMPARE(load_state_spy.at(0).at(0).toString(), QString{"alpha_wallet"});
    QCOMPARE(load_state_spy.at(0).at(1).toInt(), static_cast<int>(WalletListModel::LoadState::Closing));
    QCOMPARE(load_state_spy.at(1).at(0).toString(), QString{"beta_wallet"});
    QCOMPARE(load_state_spy.at(1).at(1).toInt(), static_cast<int>(WalletListModel::LoadState::Closing));
    QCOMPARE(controller.selectedWallet()->name(), QString{});
    QVERIFY(!controller.isWalletOpen("alpha_wallet"));
    QVERIFY(!controller.isWalletOpen("beta_wallet"));
    QCOMPARE(alpha_state.remove_calls.load(), 0);
    QCOMPARE(beta_state.remove_calls.load(), 0);
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

    QVERIFY(load_error_spy.count() >= 1);
    QVERIFY(!controller.walletLoadInProgress());
}

void WalletQmlControllerTests::publishOpenWalletsInfoEmitsWalletInfoChangedForEachOpenWallet()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    FakeWallet::State alpha_state;
    FakeWallet::State beta_state;
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
    QTRY_VERIFY(controller.selectedWallet()->walletStateReady());
    controller.setSelectedWallet("beta_wallet", "sqlite");
    QTRY_VERIFY(controller.selectedWallet()->walletStateReady());
    QSignalSpy info_spy(&controller, &WalletQmlController::walletInfoChanged);
    controller.publishOpenWalletsInfo();

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
        }
    };
    verify_raw_balances();
    controller.setSelectedWallet("alpha_wallet", "sqlite");
    controller.selectedWallet()->setDisplayUnit(3);
    controller.setSelectedWallet("beta_wallet", "sqlite");
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

    QCOMPARE(controller.walletNameAvailabilityError(""), QString{"Enter a wallet name."});
    QCOMPARE(controller.walletNameAvailabilityError("   "), QString{"Enter a wallet name."});
    QCOMPARE(controller.walletNameAvailabilityError("\t\n"), QString{"Enter a wallet name."});
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

    QCOMPARE(controller.walletNameAvailabilityError("alpha_wallet"),
             QString{"A wallet with this name already exists"});
    QCOMPARE(controller.walletNameAvailabilityError("  alpha_wallet  "),
             QString{"A wallet with this name already exists"});
    QCOMPARE(controller.walletNameAvailabilityError("ALPHA_WALLET"),
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

    QCOMPARE(controller.walletNameAvailabilityError("brand_new_wallet"), QString{});
}

void WalletQmlControllerTests::openSelectedWalletLocationOpensWalletDirectory()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    QTemporaryDir wallet_dir;
    QVERIFY(wallet_dir.isValid());
    QVERIFY(QDir{wallet_dir.path()}.mkpath("created_wallet"));
    loader.wallet_dir = wallet_dir.path().toStdString();
    FakeWallet::State wallet_state;
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

    QVERIFY(controller.openSelectedWalletLocation());
    QTRY_COMPARE(opened_path, QFileInfo{wallet_dir.filePath("created_wallet")}.absoluteFilePath());
    QVERIFY(controller.walletLocationOpenError().isEmpty());
}

void WalletQmlControllerTests::openSelectedWalletLocationReportsOpenFailure()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    QTemporaryDir wallet_dir;
    QVERIFY(wallet_dir.isValid());
    QVERIFY(QDir{wallet_dir.path()}.mkpath("created_wallet"));
    loader.wallet_dir = wallet_dir.path().toStdString();
    FakeWallet::State wallet_state;
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

    QVERIFY(controller.openSelectedWalletLocation());
    QTRY_COMPARE(opened_path, QFileInfo{wallet_dir.filePath("created_wallet")}.absoluteFilePath());
    QTRY_COMPARE(controller.walletLocationOpenError(), QString{"Could not open wallet file location."});
}

void WalletQmlControllerTests::openSelectedWalletLocationReportsMissingWalletPath()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    QTemporaryDir wallet_dir;
    QVERIFY(wallet_dir.isValid());
    loader.wallet_dir = wallet_dir.path().toStdString();
    FakeWallet::State wallet_state;
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

    QVERIFY(controller.openSelectedWalletLocation());
    QTRY_VERIFY(!controller.walletLocationOpenError().isEmpty());
    QVERIFY(!opened);
    QTRY_COMPARE(controller.walletLocationOpenError(), QString{"Wallet file not found: %1"}.arg(QFileInfo{wallet_dir.filePath("missing_wallet")}.absoluteFilePath()));
}

void WalletQmlControllerTests::openSelectedWalletLocationReportsMissingSelectedWallet()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    DrainingWalletController controller(node);

    QVERIFY(!controller.openSelectedWalletLocation());
    QTRY_COMPARE(controller.walletLocationOpenError(), QString{"No wallet file is available to view."});
}

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(WalletQmlControllerTests)
#else
QTEST_MAIN(WalletQmlControllerTests)
#endif

#include "test_walletqmlcontroller.moc"
