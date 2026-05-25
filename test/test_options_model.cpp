// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <QFile>
#include <QDir>
#include <QSettings>
#include <QTemporaryDir>
#include <test/gmocktestfixture.h>
#include <test/mocks/mocknode.h>
#include <qml/models/options_model.h>
#include <qml/bitcoinunits.h>
#include <net_processing.h>
#include <common/args.h>
#include <common/settings.h>
#include <init.h>
#include <util/translation.h>

#ifndef BITCOINQML_NO_TEST_MAIN
const TranslateFn G_TRANSLATION_FUN{nullptr};
#endif

class OptionsModelTests : public GmockTestFixture
{
    Q_OBJECT

private Q_SLOTS:
    void proxyDisabledRemovesKey();
    void torDisabledRemovesKey();
    void proxyEnabledWritesAddress();
    void onboardWritesProxy();
    void proxyDirtySetWhenOnboarded();
    void proxyDirtyNotSetDuringOnboarding();
    void proxyDirtyResetWhenReverted();
    void proxyDirtyNotSetAfterOnboard();
    void onboardWritesMarkerWithoutResettingSettings();
    void onboardPreservesUnmodifiedExistingSettings();
    void selectingExistingDataDirLoadsCurrentSettings();
    void selectingDataDirLoadsBitcoinConfSettings();
    void onboardAfterSelectingExistingDataDirPreservesLoadedSettings();
    void connectionDirtyTracksRestartRequiredSettings();
    void storageDirtyTracksRestartRequiredSettings();
    void developerDirtyTracksRestartRequiredSettings();
    void dataDirValidationRejectsFilePath();
    void displayUnitMigratesLegacySatValueToQtKey();
    void mempoolSizeLoadedFromSettings();
    void mempoolSizeWritesSetting();
    void mempoolSizeDoesNotRewriteUnchangedSetting();
    void externalSignerPathWritesSigner();
    void externalSignerPathClearedRemovesKey();
    void onboardWritesExternalSignerPath();
    void walletSettingsDirtyTracksExternalSignerPath();
    void walletSettingsDirtyNotSetDuringOnboarding();
    void signerPathLoadedFromSettings();
    void signerPathWritesSetting();
    void signerDirtySetWhenOnboarded();
    void signerDirtyNotSetDuringOnboarding();
    void signerDirtyResetWhenReverted();
    void signerDirtyNotSetAfterOnboard();
    void externalSignerPathValidationRejectsMissingPath();
    void externalSignerPathValidationAcceptsExecutablePath();
};

// Convenience: set up a NiceMock whose getPersistentSetting returns null for
// all keys by default, but returns a given address for the specified key.
static common::SettingsValue MakeAddress(const std::string& addr)
{
    return common::SettingsValue{addr};
}

static common::SettingsValue MakeInt(int value)
{
    return common::SettingsValue{value};
}

static void WriteSettingsJson(const QString& datadir, const QByteArray& json)
{
    QDir dir;
    QVERIFY(dir.mkpath(datadir + QStringLiteral("/regtest")));
    QFile settings_file(datadir + QStringLiteral("/regtest/settings.json"));
    QVERIFY(settings_file.open(QIODevice::WriteOnly | QIODevice::Text));
    QVERIFY(settings_file.write(json) == json.size());
    settings_file.close();
}

static void WriteBitcoinConf(const QString& datadir, const QByteArray& config)
{
    QDir dir;
    QVERIFY(dir.mkpath(datadir));
    QFile conf_file(datadir + QStringLiteral("/bitcoin.conf"));
    QVERIFY(conf_file.open(QIODevice::WriteOnly | QIODevice::Text));
    QVERIFY(conf_file.write(config) == config.size());
    conf_file.close();
}

void OptionsModelTests::proxyDisabledRemovesKey()
{
    using ::testing::_;
    using ::testing::NiceMock;
    using ::testing::Return;
    using ::testing::Truly;

    NiceMock<MockNode> node;
    ON_CALL(node, getPersistentSetting(_)).WillByDefault(Return(common::SettingsValue{}));
    // Simulate a previously-saved proxy address so that m_proxy_enabled=true on construction.
    ON_CALL(node, getPersistentSetting(std::string{"proxy"}))
        .WillByDefault(Return(MakeAddress("127.0.0.1:9050")));

    OptionsQmlModel model(node, /*is_onboarded=*/true);
    QVERIFY(model.proxyEnabled());

    // When proxy is disabled, updateRwSetting must be called with a null (not
    // empty-string) SettingsValue so that the key is erased from settings.json.
    EXPECT_CALL(node, updateRwSetting(std::string{"proxy"},
        Truly([](const common::SettingsValue& v) { return v.isNull(); })));

    model.setProxyEnabled(false);
    QVERIFY(!model.proxyEnabled());
}

void OptionsModelTests::torDisabledRemovesKey()
{
    using ::testing::_;
    using ::testing::NiceMock;
    using ::testing::Return;
    using ::testing::Truly;

    NiceMock<MockNode> node;
    ON_CALL(node, getPersistentSetting(_)).WillByDefault(Return(common::SettingsValue{}));
    ON_CALL(node, getPersistentSetting(std::string{"onion"}))
        .WillByDefault(Return(MakeAddress("127.0.0.1:9150")));

    OptionsQmlModel model(node, /*is_onboarded=*/true);
    QVERIFY(model.torEnabled());

    EXPECT_CALL(node, updateRwSetting(std::string{"onion"},
        Truly([](const common::SettingsValue& v) { return v.isNull(); })));

    model.setTorEnabled(false);
    QVERIFY(!model.torEnabled());
}

void OptionsModelTests::proxyEnabledWritesAddress()
{
    using ::testing::_;
    using ::testing::Eq;
    using ::testing::NiceMock;
    using ::testing::Return;
    using ::testing::Truly;

    NiceMock<MockNode> node;
    ON_CALL(node, getPersistentSetting(_)).WillByDefault(Return(common::SettingsValue{}));

    // Construct with no saved proxy — m_proxy_enabled=false, m_proxy_address="".
    OptionsQmlModel model(node, /*is_onboarded=*/true);
    QVERIFY(!model.proxyEnabled());

    // Pre-load an address into the model (as QML does before toggling the switch).
    model.setProxyAddress("10.0.0.1:9050");

    // Enabling proxy must write the address string to settings.
    EXPECT_CALL(node, updateRwSetting(std::string{"proxy"},
        Truly([](const common::SettingsValue& v) {
            return v.isStr() && v.get_str() == "10.0.0.1:9050";
        })));

    model.setProxyEnabled(true);
    QVERIFY(model.proxyEnabled());
}

void OptionsModelTests::onboardWritesProxy()
{
    using ::testing::_;
    using ::testing::NiceMock;
    using ::testing::Return;
    using ::testing::Truly;

    NiceMock<MockNode> node;
    ON_CALL(node, getPersistentSetting(_)).WillByDefault(Return(common::SettingsValue{}));

    // Construct as not-yet-onboarded.
    ArgsManager args;
    OptionsQmlModel model(node, /*is_onboarded=*/false, args, /*initialize_config_on_onboard=*/false);
    model.setProxyEnabled(true);
    model.setProxyAddress("10.0.0.1:9050");

    EXPECT_CALL(node, updateRwSetting(_, _)).Times(::testing::AnyNumber());
    // onboard() must write the proxy address to disk.
    EXPECT_CALL(node, updateRwSetting(std::string{"proxy"},
        Truly([](const common::SettingsValue& v) {
            return v.isStr() && v.get_str() == "10.0.0.1:9050";
        })));

    QVERIFY(model.onboard());
}

void OptionsModelTests::proxyDirtySetWhenOnboarded()
{
    using ::testing::_;
    using ::testing::NiceMock;
    using ::testing::Return;

    NiceMock<MockNode> node;
    ON_CALL(node, getPersistentSetting(_)).WillByDefault(Return(common::SettingsValue{}));

    OptionsQmlModel model(node, /*is_onboarded=*/true);
    QVERIFY(!model.proxySettingsDirty());

    model.setProxyEnabled(true);
    QVERIFY(model.proxySettingsDirty());
}

void OptionsModelTests::proxyDirtyNotSetDuringOnboarding()
{
    using ::testing::_;
    using ::testing::NiceMock;
    using ::testing::Return;

    NiceMock<MockNode> node;
    ON_CALL(node, getPersistentSetting(_)).WillByDefault(Return(common::SettingsValue{}));

    // During onboarding the node has not started yet, so no restart is needed.
    OptionsQmlModel model(node, /*is_onboarded=*/false);
    model.setProxyEnabled(true);
    model.setProxyAddress("127.0.0.1:9050");
    QVERIFY(!model.proxySettingsDirty());
}

void OptionsModelTests::proxyDirtyResetWhenReverted()
{
    using ::testing::_;
    using ::testing::NiceMock;
    using ::testing::Return;

    NiceMock<MockNode> node;
    ON_CALL(node, getPersistentSetting(_)).WillByDefault(Return(common::SettingsValue{}));

    // Start onboarded with proxy disabled (no saved proxy).
    OptionsQmlModel model(node, /*is_onboarded=*/true);
    QVERIFY(!model.proxySettingsDirty());

    // Simulate what ProxySettings.qml does: set address before enabling.
    model.setProxyAddress("127.0.0.1:9050");
    // Address changed but proxy is still disabled — should NOT be dirty since
    // the address is irrelevant when proxy is off.
    QVERIFY(!model.proxySettingsDirty());

    // Enable proxy — now dirty (enabled differs from initial disabled).
    model.setProxyEnabled(true);
    QVERIFY(model.proxySettingsDirty());

    // Revert enable state — dirty should clear even though address is populated,
    // because the address is ignored when proxy is disabled.
    model.setProxyEnabled(false);
    QVERIFY(!model.proxySettingsDirty());
}

void OptionsModelTests::proxyDirtyNotSetAfterOnboard()
{
    using ::testing::_;
    using ::testing::NiceMock;
    using ::testing::Return;

    NiceMock<MockNode> node;
    ON_CALL(node, getPersistentSetting(_)).WillByDefault(Return(common::SettingsValue{}));

    // Configure proxy during onboarding.
    ArgsManager args;
    OptionsQmlModel model(node, /*is_onboarded=*/false, args, /*initialize_config_on_onboard=*/false);
    model.setProxyEnabled(true);
    model.setProxyAddress("127.0.0.1:9050");
    QVERIFY(!model.proxySettingsDirty());

    // After onboard() the node starts with those settings applied —
    // no restart is needed, so dirty must be false.
    QVERIFY(model.onboard());
    QVERIFY(!model.proxySettingsDirty());
}

void OptionsModelTests::onboardWritesMarkerWithoutResettingSettings()
{
    using ::testing::_;
    using ::testing::NiceMock;
    using ::testing::Return;
    using ::testing::Truly;

    NiceMock<MockNode> node;
    ON_CALL(node, getPersistentSetting(_)).WillByDefault(Return(common::SettingsValue{}));

    ArgsManager args;
    OptionsQmlModel model(node, /*is_onboarded=*/false, args, /*initialize_config_on_onboard=*/false);

    EXPECT_CALL(node, resetSettings()).Times(0);
    EXPECT_CALL(node, updateRwSetting(std::string{"qml_onboarded"},
        Truly([](const common::SettingsValue& v) {
            return v.isBool() && v.get_bool();
        })));

    QVERIFY(model.onboard());
}

void OptionsModelTests::onboardPreservesUnmodifiedExistingSettings()
{
    using ::testing::_;
    using ::testing::NiceMock;
    using ::testing::Return;
    using ::testing::Truly;

    NiceMock<MockNode> node;
    ON_CALL(node, getPersistentSetting(_)).WillByDefault(Return(common::SettingsValue{}));
    ON_CALL(node, getPersistentSetting(std::string{"listen"}))
        .WillByDefault(Return(common::SettingsValue{false}));
    ON_CALL(node, getPersistentSetting(std::string{"server"}))
        .WillByDefault(Return(common::SettingsValue{true}));
    ON_CALL(node, getPersistentSetting(std::string{"dbcache"}))
        .WillByDefault(Return(MakeInt(900)));

    ArgsManager args;
    OptionsQmlModel model(node, /*is_onboarded=*/false, args, /*initialize_config_on_onboard=*/false);

    EXPECT_CALL(node, updateRwSetting(std::string{"listen"}, _)).Times(0);
    EXPECT_CALL(node, updateRwSetting(std::string{"server"}, _)).Times(0);
    EXPECT_CALL(node, updateRwSetting(std::string{"dbcache"}, _)).Times(0);
    EXPECT_CALL(node, updateRwSetting(std::string{"qml_onboarded"},
        Truly([](const common::SettingsValue& v) {
            return v.isBool() && v.get_bool();
        })));

    QVERIFY(model.onboard());
}

void OptionsModelTests::selectingExistingDataDirLoadsCurrentSettings()
{
    using ::testing::_;
    using ::testing::NiceMock;

    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    WriteSettingsJson(temp_dir.path(), R"({
        "listen": false,
        "natpmp": true,
        "prune": 4768,
        "server": true,
        "proxy": "10.0.0.1:9050",
        "onion": "127.0.0.1:9150"
    })");

    ArgsManager args;
    args.SelectConfigNetwork("regtest");

    NiceMock<MockNode> node;
    ON_CALL(node, getPersistentSetting(_))
        .WillByDefault([&args](const std::string& name) {
            return args.GetPersistentSetting(name);
        });

    OptionsQmlModel model(node, /*is_onboarded=*/false, args, /*initialize_config_on_onboard=*/false);
    QVERIFY(model.setCustomDataDirArgs(temp_dir.path()));

    QVERIFY(model.existingCoreProfile());
    QVERIFY(!model.listen());
    QVERIFY(model.natpmp());
    QVERIFY(model.prune());
    QCOMPARE(model.pruneSizeGB(), 5);
    QVERIFY(model.server());
    QVERIFY(model.proxyEnabled());
    QCOMPARE(model.proxyAddress(), QStringLiteral("10.0.0.1:9050"));
    QVERIFY(model.torEnabled());
    QCOMPARE(model.torAddress(), QStringLiteral("127.0.0.1:9150"));
}

void OptionsModelTests::selectingDataDirLoadsBitcoinConfSettings()
{
    using ::testing::_;
    using ::testing::NiceMock;

    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    WriteBitcoinConf(temp_dir.path(), QByteArray{
        "regtest=1\n"
        "[regtest]\n"
        "listen=0\n"
        "natpmp=1\n"
        "prune=4768\n"
        "server=1\n"
        "proxy=10.0.0.1:9050\n"
        "onion=127.0.0.1:9150\n"
    });

    ArgsManager args;
    SetupServerArgs(args);
    args.SelectConfigNetwork("regtest");

    NiceMock<MockNode> node;
    ON_CALL(node, getPersistentSetting(_))
        .WillByDefault([&args](const std::string& name) {
            return args.GetPersistentSetting(name);
        });

    OptionsQmlModel model(node, /*is_onboarded=*/false, args, /*initialize_config_on_onboard=*/false);
    QVERIFY(model.setCustomDataDirArgs(temp_dir.path()));

    QVERIFY(!model.existingCoreProfile());
    QVERIFY(!model.listen());
    QVERIFY(model.natpmp());
    QVERIFY(model.prune());
    QCOMPARE(model.pruneSizeGB(), 5);
    QVERIFY(model.server());
    QVERIFY(model.proxyEnabled());
    QCOMPARE(model.proxyAddress(), QStringLiteral("10.0.0.1:9050"));
    QVERIFY(model.torEnabled());
    QCOMPARE(model.torAddress(), QStringLiteral("127.0.0.1:9150"));
}

void OptionsModelTests::onboardAfterSelectingExistingDataDirPreservesLoadedSettings()
{
    using ::testing::_;
    using ::testing::NiceMock;
    using ::testing::Truly;

    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    WriteSettingsJson(temp_dir.path(), R"({
        "listen": false,
        "natpmp": true,
        "prune": 4768,
        "server": true,
        "proxy": "10.0.0.1:9050"
    })");

    ArgsManager args;
    args.SelectConfigNetwork("regtest");

    NiceMock<MockNode> node;
    ON_CALL(node, getPersistentSetting(_))
        .WillByDefault([&args](const std::string& name) {
            return args.GetPersistentSetting(name);
        });

    OptionsQmlModel model(node, /*is_onboarded=*/false, args, /*initialize_config_on_onboard=*/false);
    QVERIFY(model.setCustomDataDirArgs(temp_dir.path()));

    EXPECT_CALL(node, updateRwSetting(std::string{"listen"}, _)).Times(0);
    EXPECT_CALL(node, updateRwSetting(std::string{"natpmp"}, _)).Times(0);
    EXPECT_CALL(node, updateRwSetting(std::string{"prune"}, _)).Times(0);
    EXPECT_CALL(node, updateRwSetting(std::string{"server"}, _)).Times(0);
    EXPECT_CALL(node, updateRwSetting(std::string{"proxy"}, _)).Times(0);
    EXPECT_CALL(node, updateRwSetting(std::string{"qml_onboarded"},
        Truly([](const common::SettingsValue& v) {
            return v.isBool() && v.get_bool();
        })));

    QVERIFY(model.onboard());
}

void OptionsModelTests::connectionDirtyTracksRestartRequiredSettings()
{
    using ::testing::_;
    using ::testing::NiceMock;
    using ::testing::Return;

    NiceMock<MockNode> node;
    ON_CALL(node, getPersistentSetting(_)).WillByDefault(Return(common::SettingsValue{}));

    OptionsQmlModel model(node, /*is_onboarded=*/true);
    QVERIFY(!model.connectionSettingsDirty());

    model.setListen(!model.listen());
    QVERIFY(model.connectionSettingsDirty());

    model.setListen(!model.listen());
    QVERIFY(!model.connectionSettingsDirty());
}

void OptionsModelTests::storageDirtyTracksRestartRequiredSettings()
{
    using ::testing::_;
    using ::testing::NiceMock;
    using ::testing::Return;

    NiceMock<MockNode> node;
    ON_CALL(node, getPersistentSetting(_)).WillByDefault(Return(common::SettingsValue{}));

    OptionsQmlModel model(node, /*is_onboarded=*/true);
    QVERIFY(!model.storageSettingsDirty());

    model.setPrune(!model.prune());
    QVERIFY(model.storageSettingsDirty());

    model.setPrune(!model.prune());
    QVERIFY(!model.storageSettingsDirty());
}

void OptionsModelTests::developerDirtyTracksRestartRequiredSettings()
{
    using ::testing::_;
    using ::testing::NiceMock;
    using ::testing::Return;

    NiceMock<MockNode> node;
    ON_CALL(node, getPersistentSetting(_)).WillByDefault(Return(common::SettingsValue{}));

    OptionsQmlModel model(node, /*is_onboarded=*/true);
    QVERIFY(!model.developerSettingsDirty());

    model.setScriptThreads(model.scriptThreads() + 1);
    QVERIFY(model.developerSettingsDirty());

    model.setScriptThreads(model.scriptThreads() - 1);
    QVERIFY(!model.developerSettingsDirty());
}

void OptionsModelTests::dataDirValidationRejectsFilePath()
{
    using ::testing::_;
    using ::testing::NiceMock;
    using ::testing::Return;

    NiceMock<MockNode> node;
    ON_CALL(node, getPersistentSetting(_)).WillByDefault(Return(common::SettingsValue{}));

    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    const QString file_path = temp_dir.filePath("not-a-directory");
    QFile file(file_path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write("not a directory\n");
    file.close();

    ArgsManager args;
    OptionsQmlModel model(node, /*is_onboarded=*/false, args, /*initialize_config_on_onboard=*/false);
    QVERIFY(!model.setCustomDataDirArgs(file_path));
    QVERIFY(!model.dataDirError().isEmpty());
}

void OptionsModelTests::displayUnitMigratesLegacySatValueToQtKey()
{
    using ::testing::_;
    using ::testing::NiceMock;
    using ::testing::Return;

    QSettings settings;
    const bool had_display = settings.contains(SettingsKeys::DISPLAY_UNIT);
    const QVariant old_display = settings.value(SettingsKeys::DISPLAY_UNIT);
    const bool had_legacy = settings.contains(SettingsKeys::LEGACY_DISPLAY_UNIT);
    const QVariant old_legacy = settings.value(SettingsKeys::LEGACY_DISPLAY_UNIT);

    settings.remove(SettingsKeys::DISPLAY_UNIT);
    settings.setValue(SettingsKeys::LEGACY_DISPLAY_UNIT, 1);

    NiceMock<MockNode> node;
    ON_CALL(node, getPersistentSetting(_)).WillByDefault(Return(common::SettingsValue{}));

    ArgsManager args;
    OptionsQmlModel model(node, /*is_onboarded=*/true, args, /*initialize_config_on_onboard=*/false);
    QCOMPARE(model.displayUnit(), static_cast<int>(QmlBitcoinUnits::Unit::SAT));
    QCOMPARE(settings.value(SettingsKeys::DISPLAY_UNIT).toInt(), static_cast<int>(QmlBitcoinUnits::Unit::SAT));

    settings.remove(SettingsKeys::DISPLAY_UNIT);
    settings.remove(SettingsKeys::LEGACY_DISPLAY_UNIT);
    if (had_display) settings.setValue(SettingsKeys::DISPLAY_UNIT, old_display);
    if (had_legacy) settings.setValue(SettingsKeys::LEGACY_DISPLAY_UNIT, old_legacy);
}

void OptionsModelTests::mempoolSizeLoadedFromSettings()
{
    using ::testing::_;
    using ::testing::NiceMock;
    using ::testing::Return;

    NiceMock<MockNode> node;
    ON_CALL(node, getPersistentSetting(_)).WillByDefault(Return(common::SettingsValue{}));
    ON_CALL(node, getPersistentSetting(std::string{"maxmempool"}))
        .WillByDefault(Return(MakeInt(456)));

    OptionsQmlModel model(node, /*is_onboarded=*/true);
    QCOMPARE(model.maxMempoolSizeMB(), 456);
}

void OptionsModelTests::mempoolSizeWritesSetting()
{
    using ::testing::_;
    using ::testing::NiceMock;
    using ::testing::Return;
    using ::testing::Truly;

    NiceMock<MockNode> node;
    ON_CALL(node, getPersistentSetting(_)).WillByDefault(Return(common::SettingsValue{}));

    OptionsQmlModel model(node, /*is_onboarded=*/true);

    EXPECT_CALL(node, updateRwSetting(std::string{"maxmempool"},
        Truly([](const common::SettingsValue& v) {
            return v.isNum() && v.getInt<int64_t>() == 456;
        })));

    model.setMaxMempoolSizeMB(456);
    QCOMPARE(model.maxMempoolSizeMB(), 456);
}

void OptionsModelTests::mempoolSizeDoesNotRewriteUnchangedSetting()
{
    using ::testing::_;
    using ::testing::NiceMock;
    using ::testing::Return;

    NiceMock<MockNode> node;
    ON_CALL(node, getPersistentSetting(_)).WillByDefault(Return(common::SettingsValue{}));
    ON_CALL(node, getPersistentSetting(std::string{"maxmempool"}))
        .WillByDefault(Return(MakeInt(456)));

    OptionsQmlModel model(node, /*is_onboarded=*/true);

    EXPECT_CALL(node, updateRwSetting(std::string{"maxmempool"}, _)).Times(0);

    model.setMaxMempoolSizeMB(456);
    QCOMPARE(model.maxMempoolSizeMB(), 456);
}

void OptionsModelTests::externalSignerPathWritesSigner()
{
    using ::testing::_;
    using ::testing::NiceMock;
    using ::testing::Return;
    using ::testing::Truly;

    NiceMock<MockNode> node;
    ON_CALL(node, getPersistentSetting(_)).WillByDefault(Return(common::SettingsValue{}));

    OptionsQmlModel model(node, /*is_onboarded=*/true);

    EXPECT_CALL(node, updateRwSetting(std::string{"signer"},
        Truly([](const common::SettingsValue& v) {
            return v.isStr() && v.get_str() == "/usr/local/bin/hwi";
        })));

    model.setExternalSignerPath("/usr/local/bin/hwi");
    QCOMPARE(model.externalSignerPath(), QString("/usr/local/bin/hwi"));
}

void OptionsModelTests::externalSignerPathClearedRemovesKey()
{
    using ::testing::_;
    using ::testing::NiceMock;
    using ::testing::Return;
    using ::testing::Truly;

    NiceMock<MockNode> node;
    ON_CALL(node, getPersistentSetting(_)).WillByDefault(Return(common::SettingsValue{}));
    ON_CALL(node, getPersistentSetting(std::string{"signer"}))
        .WillByDefault(Return(MakeAddress("/usr/local/bin/hwi")));

    OptionsQmlModel model(node, /*is_onboarded=*/true);
    QCOMPARE(model.externalSignerPath(), QString("/usr/local/bin/hwi"));

    EXPECT_CALL(node, updateRwSetting(std::string{"signer"},
        Truly([](const common::SettingsValue& v) { return v.isNull(); })));

    model.setExternalSignerPath("");
    QVERIFY(model.externalSignerPath().isEmpty());
}

void OptionsModelTests::onboardWritesExternalSignerPath()
{
    using ::testing::_;
    using ::testing::NiceMock;
    using ::testing::Return;
    using ::testing::Truly;

    NiceMock<MockNode> node;
    ON_CALL(node, getPersistentSetting(_)).WillByDefault(Return(common::SettingsValue{}));

    ArgsManager args;
    OptionsQmlModel model(node, /*is_onboarded=*/false, args, /*initialize_config_on_onboard=*/false);
    model.setExternalSignerPath("/usr/local/bin/hwi");

    EXPECT_CALL(node, updateRwSetting(::testing::_, ::testing::_)).Times(::testing::AnyNumber());
    EXPECT_CALL(node, updateRwSetting(std::string{"signer"},
        Truly([](const common::SettingsValue& v) {
            return v.isStr() && v.get_str() == "/usr/local/bin/hwi";
        })));

    QVERIFY(model.onboard());
}

void OptionsModelTests::walletSettingsDirtyTracksExternalSignerPath()
{
    using ::testing::_;
    using ::testing::NiceMock;
    using ::testing::Return;

    NiceMock<MockNode> node;
    ON_CALL(node, getPersistentSetting(_)).WillByDefault(Return(common::SettingsValue{}));

    OptionsQmlModel model(node, /*is_onboarded=*/true);
    QVERIFY(!model.walletSettingsDirty());

    model.setExternalSignerPath("/usr/local/bin/hwi");
    QVERIFY(model.walletSettingsDirty());

    model.setExternalSignerPath("");
    QVERIFY(!model.walletSettingsDirty());
}

void OptionsModelTests::walletSettingsDirtyNotSetDuringOnboarding()
{
    using ::testing::_;
    using ::testing::NiceMock;
    using ::testing::Return;

    NiceMock<MockNode> node;
    ON_CALL(node, getPersistentSetting(_)).WillByDefault(Return(common::SettingsValue{}));

    OptionsQmlModel model(node, /*is_onboarded=*/false);
    model.setExternalSignerPath("/usr/local/bin/hwi");
    QVERIFY(!model.walletSettingsDirty());
}

void OptionsModelTests::signerPathLoadedFromSettings()
{
    using ::testing::_;
    using ::testing::NiceMock;
    using ::testing::Return;

    NiceMock<MockNode> node;
    ON_CALL(node, getPersistentSetting(_)).WillByDefault(Return(common::SettingsValue{}));
    ON_CALL(node, getPersistentSetting(std::string{"signer"}))
        .WillByDefault(Return(MakeAddress("/opt/hwi/ledger.py")));

    OptionsQmlModel model(node, /*is_onboarded=*/true);
    QCOMPARE(model.externalSignerPath(), QString("/opt/hwi/ledger.py"));
}

void OptionsModelTests::signerPathWritesSetting()
{
    using ::testing::_;
    using ::testing::NiceMock;
    using ::testing::Return;
    using ::testing::Truly;

    NiceMock<MockNode> node;
    ON_CALL(node, getPersistentSetting(_)).WillByDefault(Return(common::SettingsValue{}));

    OptionsQmlModel model(node, /*is_onboarded=*/true);
    QVERIFY(model.externalSignerPath().isEmpty());

    EXPECT_CALL(node, updateRwSetting(std::string{"signer"},
        Truly([](const common::SettingsValue& v) {
            return v.isStr() && v.get_str() == "/opt/hwi/ledger.py";
        })));

    model.setExternalSignerPath("/opt/hwi/ledger.py");
    QCOMPARE(model.externalSignerPath(), QString("/opt/hwi/ledger.py"));
}

void OptionsModelTests::signerDirtySetWhenOnboarded()
{
    using ::testing::_;
    using ::testing::NiceMock;
    using ::testing::Return;

    NiceMock<MockNode> node;
    ON_CALL(node, getPersistentSetting(_)).WillByDefault(Return(common::SettingsValue{}));

    OptionsQmlModel model(node, /*is_onboarded=*/true);
    QVERIFY(!model.walletSettingsDirty());

    model.setExternalSignerPath("/opt/hwi/ledger.py");
    QVERIFY(model.walletSettingsDirty());
}

void OptionsModelTests::signerDirtyNotSetDuringOnboarding()
{
    using ::testing::_;
    using ::testing::NiceMock;
    using ::testing::Return;

    NiceMock<MockNode> node;
    ON_CALL(node, getPersistentSetting(_)).WillByDefault(Return(common::SettingsValue{}));

    OptionsQmlModel model(node, /*is_onboarded=*/false);
    model.setExternalSignerPath("/opt/hwi/ledger.py");
    QVERIFY(!model.walletSettingsDirty());
}

void OptionsModelTests::signerDirtyResetWhenReverted()
{
    using ::testing::_;
    using ::testing::NiceMock;
    using ::testing::Return;

    NiceMock<MockNode> node;
    ON_CALL(node, getPersistentSetting(_)).WillByDefault(Return(common::SettingsValue{}));

    OptionsQmlModel model(node, /*is_onboarded=*/true);
    QVERIFY(!model.walletSettingsDirty());

    model.setExternalSignerPath("/opt/hwi/ledger.py");
    QVERIFY(model.walletSettingsDirty());

    model.setExternalSignerPath("");
    QVERIFY(!model.walletSettingsDirty());
}

void OptionsModelTests::signerDirtyNotSetAfterOnboard()
{
    using ::testing::_;
    using ::testing::NiceMock;
    using ::testing::Return;
    using ::testing::Truly;

    NiceMock<MockNode> node;
    ON_CALL(node, getPersistentSetting(_)).WillByDefault(Return(common::SettingsValue{}));

    ArgsManager args;
    OptionsQmlModel model(node, /*is_onboarded=*/false, args, /*initialize_config_on_onboard=*/false);
    model.setExternalSignerPath("/opt/hwi/ledger.py");
    QVERIFY(!model.walletSettingsDirty());

    EXPECT_CALL(node, updateRwSetting(::testing::_, ::testing::_)).Times(::testing::AnyNumber());
    EXPECT_CALL(node, updateRwSetting(std::string{"signer"},
        Truly([](const common::SettingsValue& v) {
            return v.isStr() && v.get_str() == "/opt/hwi/ledger.py";
        })));

    QVERIFY(model.onboard());
    QVERIFY(!model.walletSettingsDirty());
}

void OptionsModelTests::externalSignerPathValidationRejectsMissingPath()
{
    using ::testing::_;
    using ::testing::NiceMock;
    using ::testing::Return;

    NiceMock<MockNode> node;
    ON_CALL(node, getPersistentSetting(_)).WillByDefault(Return(common::SettingsValue{}));

    OptionsQmlModel model(node, /*is_onboarded=*/true);
    QCOMPARE(model.externalSignerPathValidationError("/definitely/not/a/real/signer"),
        QString("The configured signer path does not exist."));
}

void OptionsModelTests::externalSignerPathValidationAcceptsExecutablePath()
{
    using ::testing::_;
    using ::testing::NiceMock;
    using ::testing::Return;

    NiceMock<MockNode> node;
    ON_CALL(node, getPersistentSetting(_)).WillByDefault(Return(common::SettingsValue{}));

    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());

    const QString script_path = temp_dir.filePath("fake-signer");
    QFile script(script_path);
    QVERIFY(script.open(QIODevice::WriteOnly | QIODevice::Text));
    script.write("#!/bin/sh\nexit 0\n");
    script.close();
    QVERIFY(script.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));

    OptionsQmlModel model(node, /*is_onboarded=*/true);
    QVERIFY(model.externalSignerPathValidationError(script_path).isEmpty());
}

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(OptionsModelTests)
#else
QTEST_MAIN(OptionsModelTests)
#endif
#include "test_options_model.moc"
