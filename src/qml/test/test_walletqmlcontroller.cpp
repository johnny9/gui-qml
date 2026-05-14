// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <gmock/gmock.h>

#include <common/settings.h>
#include <interfaces/handler.h>
#include <interfaces/wallet.h>
#include <qml/walletqmlcontroller.h>
#include <scheduler.h>
#include <test/mocks/mocknode.h>
#include <util/translation.h>

#ifndef BITCOINQML_NO_TEST_MAIN
const TranslateFn G_TRANSLATION_FUN{nullptr};
#endif

namespace {
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

std::unique_ptr<interfaces::Handler> MakeNoopHandler()
{
    return interfaces::MakeCleanupHandler([] {});
}

class FakeWalletLoader : public interfaces::WalletLoader
{
public:
    int handle_load_wallet_calls{0};
    int get_wallets_calls{0};
    int list_wallet_dir_calls{0};
    int load_wallet_calls{0};

    std::function<std::vector<std::unique_ptr<interfaces::Wallet>>()> get_wallets_fn = [] {
        return std::vector<std::unique_ptr<interfaces::Wallet>>{};
    };
    std::vector<std::pair<std::string, std::string>> wallet_dir_entries;

    void registerRpcs() override {}
    bool verify() override { return true; }
    bool load() override { return true; }
    void start(CScheduler&) override {}
    void stop() override {}
    void setMockTime(int64_t) override {}
    void schedulerMockForward(std::chrono::seconds) override {}
    util::Result<std::unique_ptr<interfaces::Wallet>> createWallet(
        const std::string&,
        const SecureString&,
        uint64_t,
        std::vector<bilingual_str>&) override
    {
        return util::Error{Untranslated("Unexpected createWallet call")};
    }
    util::Result<std::unique_ptr<interfaces::Wallet>> loadWallet(const std::string&, std::vector<bilingual_str>&) override
    {
        ++load_wallet_calls;
        return util::Error{Untranslated("Unexpected loadWallet call")};
    }
    std::string getWalletDir() override { return {}; }
    util::Result<std::unique_ptr<interfaces::Wallet>> restoreWallet(const fs::path&, const std::string&, std::vector<bilingual_str>&) override
    {
        return util::Error{Untranslated("Unexpected restoreWallet call")};
    }
    util::Result<interfaces::WalletMigrationResult> migrateWallet(const std::string&, const SecureString&) override
    {
        return util::Error{Untranslated("Unexpected migrateWallet call")};
    }
    bool isEncrypted(const std::string&) override { return false; }
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
    std::unique_ptr<interfaces::Handler> handleLoadWallet(LoadWalletFn) override
    {
        ++handle_load_wallet_calls;
        return MakeNoopHandler();
    }
};

void ExpectControllerInitialization(MockNode& node, FakeWalletLoader& loader)
{
    using ::testing::_;
    using ::testing::AtLeast;
    using ::testing::Invoke;
    using ::testing::Return;
    using ::testing::ReturnRef;

    ON_CALL(node, walletLoader()).WillByDefault(ReturnRef(loader));
    EXPECT_CALL(node, walletLoader()).Times(AtLeast(3)).WillRepeatedly(ReturnRef(loader));
    EXPECT_CALL(node, getPersistentSetting(_)).WillOnce(Return(common::SettingsValue{}));
    EXPECT_CALL(node, forceSetting(_, _)).Times(1);
    EXPECT_CALL(node, listExternalSigners()).WillOnce(Invoke([] {
        return std::vector<std::unique_ptr<interfaces::ExternalSigner>>{};
    }));
}
} // namespace

class WalletQmlControllerTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void externalSignerCreationRequiresConfiguredPath();
    void externalSignerCreationRequiresExactlyOneSigner();
    void externalSignerSuggestionUsesSignerName();
    void initializedControllerSignalsMigrationForLegacyWallet();
};

void WalletQmlControllerTests::externalSignerCreationRequiresConfiguredPath()
{
    using ::testing::_;
    using ::testing::Invoke;
    using ::testing::NiceMock;
    using ::testing::Return;

    NiceMock<MockNode> node;
    ON_CALL(node, getPersistentSetting(_)).WillByDefault(Return(common::SettingsValue{}));
    EXPECT_CALL(node, listExternalSigners())
        .WillOnce(Invoke([] { return MakeSigners({"Ledger Nano X"}); }));

    WalletQmlController controller(node);
    controller.refreshExternalSignerStatus();

    QVERIFY(!controller.canCreateExternalSignerWallet());
    QCOMPARE(controller.externalSignerName(), QString("Ledger Nano X"));
    QCOMPARE(controller.suggestedExternalSignerWalletName(), QString("Ledger_Nano_X"));
}

void WalletQmlControllerTests::externalSignerCreationRequiresExactlyOneSigner()
{
    using ::testing::_;
    using ::testing::Invoke;
    using ::testing::NiceMock;
    using ::testing::Return;

    NiceMock<MockNode> node;
    ON_CALL(node, getPersistentSetting(_)).WillByDefault(Return(common::SettingsValue{}));
    ON_CALL(node, getPersistentSetting(std::string{"signer"}))
        .WillByDefault(Return(common::SettingsValue{std::string{"/usr/bin/hwi"}}));
    EXPECT_CALL(node, listExternalSigners())
        .WillOnce(Invoke([] { return MakeSigners({"Signer A", "Signer B"}); }));

    WalletQmlController controller(node);
    controller.refreshExternalSignerStatus();

    QVERIFY(!controller.canCreateExternalSignerWallet());
    QVERIFY(controller.externalSignerName().isEmpty());
    QCOMPARE(controller.externalSignerError(), QString("More than one external signer was found. Connect only one device."));
}

void WalletQmlControllerTests::externalSignerSuggestionUsesSignerName()
{
    using ::testing::_;
    using ::testing::Invoke;
    using ::testing::NiceMock;
    using ::testing::Return;

    NiceMock<MockNode> node;
    ON_CALL(node, getPersistentSetting(_)).WillByDefault(Return(common::SettingsValue{}));
    ON_CALL(node, getPersistentSetting(std::string{"signer"}))
        .WillByDefault(Return(common::SettingsValue{std::string{"/usr/bin/hwi"}}));
    EXPECT_CALL(node, listExternalSigners())
        .WillOnce(Invoke([] { return MakeSigners({"Coldcard Mk4"}); }));

    WalletQmlController controller(node);
    controller.refreshExternalSignerStatus();

    QVERIFY(controller.canCreateExternalSignerWallet());
    QCOMPARE(controller.externalSignerName(), QString("Coldcard Mk4"));
    QCOMPARE(controller.suggestedExternalSignerWalletName(), QString("Coldcard_Mk4"));
}

void WalletQmlControllerTests::initializedControllerSignalsMigrationForLegacyWallet()
{
    using ::testing::StrictMock;

    StrictMock<MockNode> node;
    FakeWalletLoader loader;
    loader.wallet_dir_entries = {{"legacy_wallet", "bdb"}};
    ExpectControllerInitialization(node, loader);

    WalletQmlController controller(node);
    controller.initialize();

    QSignalSpy migration_spy(&controller, &WalletQmlController::walletMigrationRequired);

    controller.setSelectedWallet("legacy_wallet", "bdb");
    QCOMPARE(migration_spy.count(), 1);
    QCOMPARE(migration_spy.takeFirst().at(0).toString(), QString{"legacy_wallet"});
    QCOMPARE(loader.load_wallet_calls, 0);
    QVERIFY(!controller.walletLoadInProgress());
    QVERIFY(controller.walletLoadError().isEmpty());
}

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(WalletQmlControllerTests)
#else
QTEST_MAIN(WalletQmlControllerTests)
#endif
#include "test_walletqmlcontroller.moc"
