// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <interfaces/handler.h>
#include <interfaces/wallet.h>
#include <qml/models/walletlistmodel.h>
#include <scheduler.h>
#include <test/mocks/mocknode.h>
#include <util/translation.h>
#include <wallet/types.h>

#include <QSettings>
#include <QLocale>
#include <QThread>
#include <test/mocks/manualexecutionenvironment.h>
#include <atomic>

namespace {
class FakeWalletLoader : public interfaces::WalletLoader
{
public:
    std::vector<std::pair<std::string, std::string>> wallet_dir_entries;
    std::atomic<int> reads{0};
    std::atomic<QThread*> read_thread{nullptr};
    bool fail_read{false};

    void registerRpcs() override {}
    bool verify() override { return true; }
    bool load() override { return true; }
    void start(CScheduler&) override {}
    void stop() override {}
    void setMockTime(int64_t) override {}
    void schedulerMockForward(std::chrono::seconds) override {}
    util::Result<std::unique_ptr<interfaces::Wallet>> createWallet(const std::string&, const SecureString&, uint64_t, std::vector<bilingual_str>&) override
    {
        return util::Error{Untranslated("Unexpected createWallet call")};
    }
    util::Result<std::unique_ptr<interfaces::Wallet>> loadWallet(const std::string&, std::vector<bilingual_str>&) override
    {
        return util::Error{Untranslated("Unexpected loadWallet call")};
    }
    std::string getWalletDir() override { return {}; }
    util::Result<std::unique_ptr<interfaces::Wallet>> restoreWallet(const fs::path&, const std::string&, std::vector<bilingual_str>&, bool) override
    {
        return util::Error{Untranslated("Unexpected restoreWallet call")};
    }
    util::Result<interfaces::WalletMigrationResult> migrateWallet(const std::string&, const SecureString&, bool) override
    {
        return util::Error{Untranslated("Unexpected migrateWallet call")};
    }
    bool isEncrypted(const std::string&) override { return false; }
    std::vector<std::pair<std::string, std::string>> listWalletDir() override
    {
        ++reads; read_thread.store(QThread::currentThread());
        if (fail_read) throw std::runtime_error("directory failed");
        return wallet_dir_entries;
    }
    std::vector<std::unique_ptr<interfaces::Wallet>> getWallets() override { return {}; }
    std::unique_ptr<interfaces::Handler> handleLoadWallet(LoadWalletFn) override
    {
        return interfaces::MakeCleanupHandler([] {});
    }
};

void ConfigureExpectedWalletLoader(MockNode& node, FakeWalletLoader& loader)
{
    node.wallet_loader_fn = [&loader]() -> interfaces::WalletLoader& { return loader; };
    node.ExpectAtLeast(node.calls.walletLoader, 1);
}
bool AwaitCatalog(WalletListModel& model)
{
    return QTest::qWaitFor([&] { return !model.projectionPending(); }, 5000) && model.readError().isEmpty();
}
} // namespace

class WalletListModelTests : public QObject
{
    Q_OBJECT

private:
    QLocale m_previous_locale;

private Q_SLOTS:
    void init();
    void cleanup();
    void listWalletDirMapsNameAndLoadStateRoles();
    void listWalletDirRemovesMissingEntries();
    void listWalletDirSortsCaseInsensitivelyAndPreservesDuplicateRows();
    void displayNameRoleUsesStoredAlias();
    void setWalletLoadStateUpdatesLoadStateRole();
    void setWalletLoadStateSortsLoadedRowsFirst();
    void setWalletLoadStateBeforeListWalletDirSeedsInitialRows();
    void setWalletLoadStateAddsNewLoadedWalletAfterInitialList();
    void setWalletLoadStateRemovesOpenOnlyWalletOnUnload();
    void setWalletLoadStateLoadingExposesLoadingRoleAndClearsOnOpen();
    void setWalletLoadStateLoadErrorExposesErrorMessageRoleAndClearsOnClosed();
    void setWalletInfoUpdatesBalanceAndKeySchemeRolesForRowOnly();
    void listWalletDirPreservesBalanceAndKeySchemeAcrossRebuilds();
    void walletDirLoadedFlipsAfterFirstList();
    void balancesFollowDisplayUnitAcrossWalletRefreshes();
    void balancesUseGermanLocale();
    void manualCatalogOwnsPhasesAndRetainsPriorRows_data();
    void manualCatalogOwnsPhasesAndRetainsPriorRows();
};

void WalletListModelTests::init()
{
    m_previous_locale = QLocale();
    // Use decimal points unless a test explicitly selects another locale.
    QLocale::setDefault(QLocale::c());
    QSettings settings;
    settings.remove("walletDisplayNames");
    settings.sync();
}

void WalletListModelTests::cleanup()
{
    QLocale::setDefault(m_previous_locale);
}

void WalletListModelTests::listWalletDirMapsNameAndLoadStateRoles()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    loader.wallet_dir_entries = {
        {"alpha_wallet", "sqlite"},
        {"beta_wallet", "sqlite"},
    };
    ConfigureExpectedWalletLoader(node, loader);

    WalletListModel model{node, nullptr};
    model.listWalletDir();
    QVERIFY(AwaitCatalog(model));

    QVERIFY(loader.read_thread.load() != QThread::currentThread());
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.roleNames().value(WalletListModel::NameRole), QByteArray{"name"});
    QCOMPARE(model.roleNames().value(WalletListModel::LoadStateRole), QByteArray{"loadState"});

    const QModelIndex first = model.index(0, 0);
    const QModelIndex second = model.index(1, 0);
    QCOMPARE(model.data(first, WalletListModel::NameRole).toString(), QString{"alpha_wallet"});
    QCOMPARE(model.data(second, WalletListModel::NameRole).toString(), QString{"beta_wallet"});
    QCOMPARE(model.data(first, WalletListModel::LoadStateRole).toInt(), static_cast<int>(WalletListModel::LoadState::Closed));
    QCOMPARE(model.data(second, WalletListModel::LoadStateRole).toInt(), static_cast<int>(WalletListModel::LoadState::Closed));
}

void WalletListModelTests::listWalletDirRemovesMissingEntries()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    loader.wallet_dir_entries = {
        {"alpha_wallet", "sqlite"},
        {"beta_wallet", "sqlite"},
    };
    ConfigureExpectedWalletLoader(node, loader);

    WalletListModel model{node, nullptr};
    model.listWalletDir();
    QVERIFY(AwaitCatalog(model));
    QCOMPARE(model.rowCount(), 2);

    loader.wallet_dir_entries = {
        {"beta_wallet", "sqlite"},
    };
    model.listWalletDir();
    QVERIFY(AwaitCatalog(model));

    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0, 0), WalletListModel::NameRole).toString(), QString{"beta_wallet"});
}

void WalletListModelTests::listWalletDirSortsCaseInsensitivelyAndPreservesDuplicateRows()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    loader.wallet_dir_entries = {
        {"zulu_wallet", "sqlite"},
        {"alpha_wallet", "sqlite"},
        {"Alpha_wallet", "sqlite"},
        {"alpha_wallet", "bdb"},
        {"bravo_wallet", "sqlite"},
    };
    ConfigureExpectedWalletLoader(node, loader);

    WalletListModel model{node, nullptr};
    model.listWalletDir();
    QVERIFY(AwaitCatalog(model));

    QCOMPARE(model.rowCount(), 5);
    QCOMPARE(model.data(model.index(0, 0), WalletListModel::NameRole).toString(), QString{"Alpha_wallet"});
    QCOMPARE(model.data(model.index(0, 0), WalletListModel::FormatRole).toString(), QString{"sqlite"});
    QCOMPARE(model.data(model.index(1, 0), WalletListModel::NameRole).toString(), QString{"alpha_wallet"});
    QCOMPARE(model.data(model.index(1, 0), WalletListModel::FormatRole).toString(), QString{"bdb"});
    QCOMPARE(model.data(model.index(2, 0), WalletListModel::NameRole).toString(), QString{"alpha_wallet"});
    QCOMPARE(model.data(model.index(2, 0), WalletListModel::FormatRole).toString(), QString{"sqlite"});
    QCOMPARE(model.data(model.index(3, 0), WalletListModel::NameRole).toString(), QString{"bravo_wallet"});
    QCOMPARE(model.data(model.index(4, 0), WalletListModel::NameRole).toString(), QString{"zulu_wallet"});
}

void WalletListModelTests::displayNameRoleUsesStoredAlias()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    loader.wallet_dir_entries = {
        {"alpha_wallet", "sqlite"},
    };
    ConfigureExpectedWalletLoader(node, loader);

    QSettings settings;
    settings.setValue("walletDisplayNames/alpha_wallet", "Personal");
    settings.sync();

    WalletListModel model{node, nullptr};
    model.listWalletDir();
    QVERIFY(AwaitCatalog(model));

    QCOMPARE(model.data(model.index(0, 0), WalletListModel::DisplayNameRole).toString(), QString{"Personal"});
}

void WalletListModelTests::setWalletLoadStateUpdatesLoadStateRole()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    loader.wallet_dir_entries = {
        {"alpha_wallet", "sqlite"},
        {"beta_wallet", "sqlite"},
    };
    ConfigureExpectedWalletLoader(node, loader);

    WalletListModel model{node, nullptr};
    model.listWalletDir();
    QVERIFY(AwaitCatalog(model));

    QSignalSpy data_changed_spy(&model, &QAbstractItemModel::dataChanged);

    model.setWalletLoadState("alpha_wallet", WalletListModel::LoadState::Open);
    QVERIFY(AwaitCatalog(model));

    QCOMPARE(data_changed_spy.count(), 1);
    QCOMPARE(model.data(model.index(0, 0), WalletListModel::NameRole).toString(), QString{"alpha_wallet"});
    QCOMPARE(model.data(model.index(0, 0), WalletListModel::LoadStateRole).toInt(), static_cast<int>(WalletListModel::LoadState::Open));
    QCOMPARE(model.data(model.index(1, 0), WalletListModel::NameRole).toString(), QString{"beta_wallet"});
    QCOMPARE(model.data(model.index(1, 0), WalletListModel::LoadStateRole).toInt(), static_cast<int>(WalletListModel::LoadState::Closed));

    model.setWalletLoadState("alpha_wallet", WalletListModel::LoadState::Closed);
    QVERIFY(AwaitCatalog(model));

    QCOMPARE(data_changed_spy.count(), 2);
    QCOMPARE(model.data(model.index(0, 0), WalletListModel::LoadStateRole).toInt(), static_cast<int>(WalletListModel::LoadState::Closed));
}

void WalletListModelTests::setWalletLoadStateSortsLoadedRowsFirst()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    loader.wallet_dir_entries = {
        {"zulu_wallet", "sqlite"},
        {"alpha_wallet", "sqlite"},
        {"bravo_wallet", "sqlite"},
        {"charlie_wallet", "sqlite"},
    };
    ConfigureExpectedWalletLoader(node, loader);

    WalletListModel model{node, nullptr};
    model.listWalletDir();
    QVERIFY(AwaitCatalog(model));

    QCOMPARE(model.data(model.index(0, 0), WalletListModel::NameRole).toString(), QString{"alpha_wallet"});
    QCOMPARE(model.data(model.index(1, 0), WalletListModel::NameRole).toString(), QString{"bravo_wallet"});
    QCOMPARE(model.data(model.index(2, 0), WalletListModel::NameRole).toString(), QString{"charlie_wallet"});
    QCOMPARE(model.data(model.index(3, 0), WalletListModel::NameRole).toString(), QString{"zulu_wallet"});

    QSignalSpy model_reset_spy(&model, &QAbstractItemModel::modelReset);

    model.setWalletLoadState("zulu_wallet", WalletListModel::LoadState::Open);
    QVERIFY(AwaitCatalog(model));
    model.setWalletLoadState("bravo_wallet", WalletListModel::LoadState::Open);
    QVERIFY(AwaitCatalog(model));

    QCOMPARE(model_reset_spy.count(), 2);
    QCOMPARE(model.data(model.index(0, 0), WalletListModel::NameRole).toString(), QString{"bravo_wallet"});
    QCOMPARE(model.data(model.index(0, 0), WalletListModel::LoadStateRole).toInt(), static_cast<int>(WalletListModel::LoadState::Open));
    QCOMPARE(model.data(model.index(1, 0), WalletListModel::NameRole).toString(), QString{"zulu_wallet"});
    QCOMPARE(model.data(model.index(1, 0), WalletListModel::LoadStateRole).toInt(), static_cast<int>(WalletListModel::LoadState::Open));
    QCOMPARE(model.data(model.index(2, 0), WalletListModel::NameRole).toString(), QString{"alpha_wallet"});
    QCOMPARE(model.data(model.index(2, 0), WalletListModel::LoadStateRole).toInt(), static_cast<int>(WalletListModel::LoadState::Closed));
    QCOMPARE(model.data(model.index(3, 0), WalletListModel::NameRole).toString(), QString{"charlie_wallet"});
    QCOMPARE(model.data(model.index(3, 0), WalletListModel::LoadStateRole).toInt(), static_cast<int>(WalletListModel::LoadState::Closed));
}

void WalletListModelTests::setWalletLoadStateBeforeListWalletDirSeedsInitialRows()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    loader.wallet_dir_entries = {
        {"alpha_wallet", "sqlite"},
        {"beta_wallet", "sqlite"},
    };
    ConfigureExpectedWalletLoader(node, loader);

    WalletListModel model{node, nullptr};
    model.setWalletLoadState("beta_wallet", WalletListModel::LoadState::Open);
    QVERIFY(AwaitCatalog(model));
    QCOMPARE(model.rowCount(), 0);

    model.listWalletDir();
    QVERIFY(AwaitCatalog(model));

    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.data(model.index(0, 0), WalletListModel::NameRole).toString(), QString{"beta_wallet"});
    QCOMPARE(model.data(model.index(0, 0), WalletListModel::LoadStateRole).toInt(), static_cast<int>(WalletListModel::LoadState::Open));
    QCOMPARE(model.data(model.index(1, 0), WalletListModel::NameRole).toString(), QString{"alpha_wallet"});
    QCOMPARE(model.data(model.index(1, 0), WalletListModel::LoadStateRole).toInt(), static_cast<int>(WalletListModel::LoadState::Closed));
}

void WalletListModelTests::setWalletLoadStateAddsNewLoadedWalletAfterInitialList()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    loader.wallet_dir_entries = {
        {"alpha_wallet", "sqlite"},
    };
    ConfigureExpectedWalletLoader(node, loader);

    WalletListModel model{node, nullptr};
    model.listWalletDir();
    QVERIFY(AwaitCatalog(model));

    QSignalSpy model_reset_spy(&model, &QAbstractItemModel::modelReset);
    model.setWalletLoadState("created_wallet", WalletListModel::LoadState::Open);
    QVERIFY(AwaitCatalog(model));

    QCOMPARE(model_reset_spy.count(), 1);
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.data(model.index(0, 0), WalletListModel::NameRole).toString(), QString{"created_wallet"});
    QCOMPARE(model.data(model.index(0, 0), WalletListModel::FormatRole).toString(), QString{});
    QCOMPARE(model.data(model.index(0, 0), WalletListModel::LoadStateRole).toInt(), static_cast<int>(WalletListModel::LoadState::Open));
    QCOMPARE(model.data(model.index(1, 0), WalletListModel::NameRole).toString(), QString{"alpha_wallet"});
    QCOMPARE(model.data(model.index(1, 0), WalletListModel::LoadStateRole).toInt(), static_cast<int>(WalletListModel::LoadState::Closed));
}

void WalletListModelTests::setWalletLoadStateRemovesOpenOnlyWalletOnUnload()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    ConfigureExpectedWalletLoader(node, loader);

    WalletListModel model{node, nullptr};
    model.listWalletDir();
    QVERIFY(AwaitCatalog(model));
    QCOMPARE(model.rowCount(), 0);

    QSignalSpy wallet_list_changed_spy(&model, &WalletListModel::walletListChanged);
    QSignalSpy model_reset_spy(&model, &QAbstractItemModel::modelReset);

    model.setWalletLoadState("external_wallet", WalletListModel::LoadState::Open);
    QVERIFY(AwaitCatalog(model));
    QCOMPARE(model_reset_spy.count(), 1);
    QCOMPARE(wallet_list_changed_spy.count(), 1);
    QCOMPARE(wallet_list_changed_spy.at(0).at(0).toBool(), true);
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0, 0), WalletListModel::NameRole).toString(), QString{"external_wallet"});
    QCOMPARE(model.data(model.index(0, 0), WalletListModel::FormatRole).toString(), QString{});
    QCOMPARE(model.data(model.index(0, 0), WalletListModel::LoadStateRole).toInt(), static_cast<int>(WalletListModel::LoadState::Open));

    model.setWalletLoadState("external_wallet", WalletListModel::LoadState::Closed);
    QVERIFY(AwaitCatalog(model));
    QCOMPARE(model_reset_spy.count(), 2);
    QCOMPARE(wallet_list_changed_spy.count(), 2);
    QCOMPARE(wallet_list_changed_spy.at(1).at(0).toBool(), false);
    QCOMPARE(model.rowCount(), 0);
}

void WalletListModelTests::setWalletLoadStateLoadingExposesLoadingRoleAndClearsOnOpen()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    loader.wallet_dir_entries = {
        {"alpha_wallet", "sqlite"},
        {"beta_wallet", "sqlite"},
    };
    ConfigureExpectedWalletLoader(node, loader);

    WalletListModel model{node, nullptr};
    model.listWalletDir();
    QVERIFY(AwaitCatalog(model));

    model.setWalletLoadState("alpha_wallet", WalletListModel::LoadState::Loading);
    QVERIFY(AwaitCatalog(model));

    QCOMPARE(model.data(model.index(0, 0), WalletListModel::LoadStateRole).toInt(),
             static_cast<int>(WalletListModel::LoadState::Loading));
    QCOMPARE(model.data(model.index(0, 0), WalletListModel::ErrorMessageRole).toString(), QString{});
    QCOMPARE(model.data(model.index(1, 0), WalletListModel::LoadStateRole).toInt(),
             static_cast<int>(WalletListModel::LoadState::Closed));

    model.setWalletLoadState("alpha_wallet", WalletListModel::LoadState::Open);
    QVERIFY(AwaitCatalog(model));

    QCOMPARE(model.data(model.index(0, 0), WalletListModel::LoadStateRole).toInt(),
             static_cast<int>(WalletListModel::LoadState::Open));
}

void WalletListModelTests::setWalletLoadStateLoadErrorExposesErrorMessageRoleAndClearsOnClosed()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    loader.wallet_dir_entries = {
        {"alpha_wallet", "sqlite"},
    };
    ConfigureExpectedWalletLoader(node, loader);

    WalletListModel model{node, nullptr};
    model.listWalletDir();
    QVERIFY(AwaitCatalog(model));

    model.setWalletLoadState("alpha_wallet",
                             WalletListModel::LoadState::LoadError,
                             "Disk is full");
    QVERIFY(AwaitCatalog(model));

    QCOMPARE(model.data(model.index(0, 0), WalletListModel::LoadStateRole).toInt(),
             static_cast<int>(WalletListModel::LoadState::LoadError));
    QCOMPARE(model.data(model.index(0, 0), WalletListModel::ErrorMessageRole).toString(),
             QString{"Disk is full"});

    model.setWalletLoadState("alpha_wallet", WalletListModel::LoadState::Closed);
    QVERIFY(AwaitCatalog(model));

    QCOMPARE(model.data(model.index(0, 0), WalletListModel::LoadStateRole).toInt(),
             static_cast<int>(WalletListModel::LoadState::Closed));
    QCOMPARE(model.data(model.index(0, 0), WalletListModel::ErrorMessageRole).toString(), QString{});
}

void WalletListModelTests::setWalletInfoUpdatesBalanceAndKeySchemeRolesForRowOnly()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    loader.wallet_dir_entries = {
        {"alpha_wallet", "sqlite"},
        {"beta_wallet", "sqlite"},
    };
    ConfigureExpectedWalletLoader(node, loader);

    WalletListModel model{node, nullptr};
    model.listWalletDir();
    QVERIFY(AwaitCatalog(model));

    QSignalSpy data_changed_spy(&model, &QAbstractItemModel::dataChanged);
    model.setWalletInfo("alpha_wallet", 167930, /*keySchemeKind=*/2);
    QVERIFY(AwaitCatalog(model));

    QCOMPARE(data_changed_spy.count(), 1);
    const QList<QVariant> args = data_changed_spy.takeFirst();
    const QModelIndex top_left = args.at(0).value<QModelIndex>();
    const QModelIndex bottom_right = args.at(1).value<QModelIndex>();
    QCOMPARE(top_left.row(), 0);
    QCOMPARE(bottom_right.row(), 0);
    const QVector<int> roles = args.at(2).value<QVector<int>>();
    QVERIFY(roles.contains(WalletListModel::BalanceRole));
    QVERIFY(roles.contains(WalletListModel::BalanceKnownRole));
    QVERIFY(roles.contains(WalletListModel::KeySchemeKindRole));

    QCOMPARE(model.data(model.index(0, 0), WalletListModel::BalanceRole).toString(), QString{"0.00167930"});
    QCOMPARE(model.data(model.index(0, 0), WalletListModel::KeySchemeKindRole).toInt(), 2);
    QCOMPARE(model.data(model.index(1, 0), WalletListModel::BalanceRole).toString(), QString{});
    QVERIFY(!model.data(model.index(1, 0), WalletListModel::BalanceKnownRole).toBool());
    QVERIFY(model.data(model.index(0, 0), WalletListModel::BalanceKnownRole).toBool());
    QCOMPARE(model.data(model.index(1, 0), WalletListModel::KeySchemeKindRole).toInt(), -1);

    // No-op when nothing changed.
    model.setWalletInfo("alpha_wallet", 167930, /*keySchemeKind=*/2);
    QVERIFY(AwaitCatalog(model));
    QCOMPARE(data_changed_spy.count(), 0);
    // Opening metadata can arrive before its first observed balance. Preserve
    // the key type while distinguishing absence from a known zero.
    model.setWalletInfo("alpha_wallet", 0, /*keySchemeKind=*/2, /*balanceKnown=*/false);
    QVERIFY(AwaitCatalog(model));
    QCOMPARE(data_changed_spy.count(), 1);
    QVERIFY(!model.data(model.index(0, 0), WalletListModel::BalanceKnownRole).toBool());
    QCOMPARE(model.data(model.index(0, 0), WalletListModel::BalanceRole).toString(), QString{});
    QCOMPARE(model.data(model.index(0, 0), WalletListModel::KeySchemeKindRole).toInt(), 2);
}

void WalletListModelTests::listWalletDirPreservesBalanceAndKeySchemeAcrossRebuilds()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    loader.wallet_dir_entries = {
        {"alpha_wallet", "sqlite"},
    };
    ConfigureExpectedWalletLoader(node, loader);

    WalletListModel model{node, nullptr};
    model.listWalletDir();
    QVERIFY(AwaitCatalog(model));
    model.setWalletInfo("alpha_wallet", 123000000, /*keySchemeKind=*/1);
    QVERIFY(AwaitCatalog(model));

    // Rebuild with the same wallet still present plus a new one.
    loader.wallet_dir_entries = {
        {"alpha_wallet", "sqlite"},
        {"beta_wallet", "sqlite"},
    };
    model.listWalletDir();
    QVERIFY(AwaitCatalog(model));

    const int alpha_row = model.data(model.index(0, 0), WalletListModel::NameRole).toString() == "alpha_wallet" ? 0 : 1;
    QCOMPARE(model.data(model.index(alpha_row, 0), WalletListModel::BalanceRole).toString(), QString{"1.23000000"});
    QCOMPARE(model.data(model.index(alpha_row, 0), WalletListModel::KeySchemeKindRole).toInt(), 1);

    const int beta_row = 1 - alpha_row;
    QCOMPARE(model.data(model.index(beta_row, 0), WalletListModel::BalanceRole).toString(), QString{});
    QCOMPARE(model.data(model.index(beta_row, 0), WalletListModel::KeySchemeKindRole).toInt(), -1);
}

void WalletListModelTests::balancesFollowDisplayUnitAcrossWalletRefreshes()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    loader.wallet_dir_entries = {{"Charlie", "sqlite"}, {"Other", "sqlite"}};
    ConfigureExpectedWalletLoader(node, loader);
    WalletListModel model{node};
    model.listWalletDir();
    QVERIFY(AwaitCatalog(model));
    model.setWalletInfo("Charlie", 13900000000LL, 0);
    QVERIFY(AwaitCatalog(model));
    model.setWalletInfo("Other", 1, 0);
    QVERIFY(AwaitCatalog(model));
    const auto amount = [&model](int row) {
        return model.data(model.index(row, 0), WalletListModel::BalanceRole).toString().remove(QLocale().groupSeparator());
    };
    QCOMPARE(amount(0), QStringLiteral("139.00000000"));
    QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
    model.setDisplayUnit(3);
    QVERIFY(AwaitCatalog(model));
    QCOMPARE(changed.count(), 2);
    QCOMPARE(changed.at(0).at(0).value<QModelIndex>().row(), 0);
    QCOMPARE(changed.at(1).at(0).value<QModelIndex>().row(), 1);
    QCOMPARE(amount(0), QStringLiteral("13900000000"));
    QCOMPARE(amount(1), QStringLiteral("1"));
    // Switching wallets republishes metadata and reopening the selector rebuilds rows.
    model.setWalletInfo("Charlie", 13900000000LL, 0);
    QVERIFY(AwaitCatalog(model));
    model.listWalletDir();
    QVERIFY(AwaitCatalog(model));
    QCOMPARE(amount(0), QStringLiteral("13900000000"));
    model.setDisplayUnit(1);
    QVERIFY(AwaitCatalog(model));
    QCOMPARE(amount(0), QStringLiteral("139000.00000"));
    model.setDisplayUnit(2);
    QVERIFY(AwaitCatalog(model));
    QCOMPARE(amount(0), QStringLiteral("139000000.00"));
    model.setDisplayUnit(0);
    QVERIFY(AwaitCatalog(model));
    QCOMPARE(amount(0), QStringLiteral("139.00000000"));
    QCOMPARE(amount(1), QStringLiteral("0.00000001"));
}

void WalletListModelTests::balancesUseGermanLocale()
{
    QLocale::setDefault(QLocale{"de_DE"});
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    loader.wallet_dir_entries = {{"alpha_wallet", "sqlite"}};
    ConfigureExpectedWalletLoader(node, loader);
    WalletListModel model{node};
    model.listWalletDir();
    QVERIFY(AwaitCatalog(model));
    model.setWalletInfo("alpha_wallet", 123456789, 0);
    QVERIFY(AwaitCatalog(model));
    const auto amount = [&model] {
        return model.data(model.index(0, 0), WalletListModel::BalanceRole).toString();
    };

    QCOMPARE(amount(), QStringLiteral("1,23456789"));
    model.setDisplayUnit(1);
    QVERIFY(AwaitCatalog(model));
    QCOMPARE(amount(), QStringLiteral("1.234,56789"));
    model.setDisplayUnit(2);
    QVERIFY(AwaitCatalog(model));
    QCOMPARE(amount(), QStringLiteral("1.234.567,89"));
    model.setDisplayUnit(3);
    QVERIFY(AwaitCatalog(model));
    QCOMPARE(amount(), QStringLiteral("123.456.789"));
    model.setDisplayUnit(0);
    QVERIFY(AwaitCatalog(model));
    model.listWalletDir();
    QVERIFY(AwaitCatalog(model));
    QCOMPARE(amount(), QStringLiteral("1,23456789"));
}

void WalletListModelTests::walletDirLoadedFlipsAfterFirstList()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    ConfigureExpectedWalletLoader(node, loader);

    WalletListModel model{node, nullptr};
    QSignalSpy wallet_dir_loaded_spy(&model, &WalletListModel::walletDirLoadedChanged);

    QCOMPARE(model.walletDirLoaded(), false);
    model.listWalletDir();
    QVERIFY(AwaitCatalog(model));

    QCOMPARE(model.walletDirLoaded(), true);
    QCOMPARE(wallet_dir_loaded_spy.count(), 1);

    model.listWalletDir();
    QVERIFY(AwaitCatalog(model));
    QCOMPARE(wallet_dir_loaded_spy.count(), 1);
}

void WalletListModelTests::manualCatalogOwnsPhasesAndRetainsPriorRows_data()
{
    QTest::addColumn<int>("scenario");
    QTest::newRow("success") << 0;
    QTest::newRow("rejection") << 1;
    QTest::newRow("failure") << 2;
    QTest::newRow("quiet-close") << 3;
}
void WalletListModelTests::manualCatalogOwnsPhasesAndRetainsPriorRows()
{
    QFETCH(int, scenario);
    ManualExecutionEnvironment environment;
    MockNode node; FakeWalletLoader loader;
    loader.wallet_dir_entries = {{"beta", "sqlite"}, {"alpha", "sqlite"}};
    node.wallet_loader_fn = [&]() -> interfaces::WalletLoader& { return loader; };
    WalletListModel model{node, {environment.scheduler, [&](auto turn) { environment.post(std::move(turn)); }}};
    QSignalSpy reset{&model, &QAbstractItemModel::modelReset};
    QSignalSpy changed{&model, &QAbstractItemModel::dataChanged};
    model.listWalletDir(); QCOMPARE(loader.reads.load(), 0); QCOMPARE(model.rowCount(), 0);
    QVERIFY(environment.step()); QCOMPARE(loader.reads.load(), 1); QCOMPARE(model.rowCount(), 0);
    QVERIFY(environment.step()); QCOMPARE(model.rowCount(), 0); QVERIFY(model.projectionPending());
    QVERIFY(environment.publishNext()); QCOMPARE(model.rowCount(), 2); QVERIFY(!model.projectionPending());
    QCOMPARE(model.data(model.index(0), WalletListModel::NameRole).toString(), QStringLiteral("alpha"));
    const auto reads = loader.reads.load(); const auto resets = reset.size();
    const QPersistentModelIndex stable{model.index(0)};
    if (scenario == 1) environment.rejectNext(true);
    if (scenario == 2) loader.fail_read = true;
    if (scenario == 1 || scenario == 2) model.listWalletDir();
    else model.setWalletInfo("alpha", 123, 1);
    QCOMPARE(loader.reads.load(), reads);
    environment.drain();
    if (scenario == 3) {
        const auto notices = changed.size(); model.closeAdmission();
        QCOMPARE(reset.size(), resets); QCOMPARE(changed.size(), notices);
        environment.publishAll(); QCOMPARE(reset.size(), resets); QCOMPARE(changed.size(), notices);
    } else {
        environment.publishAll();
        QCOMPARE(model.rowCount(), 2); QCOMPARE(reset.size(), resets); QVERIFY(stable.isValid());
        QCOMPARE(stable.data(WalletListModel::NameRole).toString(), QStringLiteral("alpha"));
        if (scenario == 0) { QCOMPARE(changed.size(), 1); QCOMPARE(stable.data(WalletListModel::BalanceSatoshiRole).toLongLong(), qint64{123}); }
        else { QVERIFY(!model.readError().isEmpty()); QCOMPARE(stable.data(WalletListModel::BalanceKnownRole).toBool(), false); }
    }
    model.closeAdmission(); environment.drain(); environment.publishAll(); QVERIFY(environment.quiescent());
}

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(WalletListModelTests)
#else
QTEST_MAIN(WalletListModelTests)
#endif

#include <test_walletlistmodel.moc>
