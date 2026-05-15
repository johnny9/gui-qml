// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <common/args.h>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QTemporaryDir>
#include <QUrl>
#include <test/gmocktestfixture.h>
#include <test/mocks/mocknode.h>
#include <qml/models/options_model.h>
#include <qml/guiconstants.h>
#include <net_processing.h>
#include <common/settings.h>
#include <qml/models/settings_keys.h>
#include <util/fs.h>
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
    void existingCoreDataDirRejectsOnboard();
    void customDataDirArgsCreatesDirectoryAndSetsArg();
    void customDataDirArgsRejectsFilePath();
    void setDataDirDefaultClearsCustomDataDir();
    void onboardPersistsCustomDataDir();
    void onboardClearsPersistedDataDirForDefault();
};

// Convenience: set up a NiceMock whose getPersistentSetting returns null for
// all keys by default, but returns a given address for the specified key.
static common::SettingsValue MakeAddress(const std::string& addr)
{
    return common::SettingsValue{addr};
}

static QSettings DataDirSettings()
{
    return QSettings(QSettings::UserScope, QAPP_ORG_NAME, QAPP_APP_NAME_DEFAULT);
}

static void SetupDataDirTestArgs(ArgsManager& args)
{
    args.ForceSetArg("-noconf", "1");
    args.ForceSetArg("-settings", "");
    args.ForceSetArg("-regtest", "1");
}

static void SetDataDirArg(ArgsManager& args, const QString& data_dir)
{
    args.ForceSetArg("-datadir", fs::PathToString(fs::PathFromString(data_dir.toStdString())));
}

class ScopedHomeDir
{
public:
    explicit ScopedHomeDir(const QString& path)
        : m_had_home{qEnvironmentVariableIsSet("HOME")}
        , m_old_home{qgetenv("HOME")}
    {
        qputenv("HOME", QFile::encodeName(path));
    }

    ~ScopedHomeDir()
    {
        if (m_had_home) {
            qputenv("HOME", m_old_home);
        } else {
            qunsetenv("HOME");
        }
    }

private:
    const bool m_had_home;
    const QByteArray m_old_home;
};

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
    ON_CALL(node, resetSettings()).WillByDefault(Return());

    // Construct as not-yet-onboarded.
    ArgsManager args;
    SetupDataDirTestArgs(args);
    OptionsQmlModel model(node, /*is_onboarded=*/false, args);
    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    QVERIFY(model.setCustomDataDirArgs(QUrl::fromLocalFile(temp_dir.filePath("fresh-data")).toString()));
    model.setProxyEnabled(true);
    model.setProxyAddress("10.0.0.1:9050");

    EXPECT_CALL(node, updateRwSetting(_, _)).Times(::testing::AnyNumber());
    // onboard() must write the proxy address to disk.
    EXPECT_CALL(node, updateRwSetting(std::string{"proxy"},
        Truly([](const common::SettingsValue& v) {
            return v.isStr() && v.get_str() == "10.0.0.1:9050";
        })));

    QVERIFY2(model.onboard(), qPrintable(model.dataDirError()));
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
    ON_CALL(node, resetSettings()).WillByDefault(Return());

    // Configure proxy during onboarding.
    ArgsManager args;
    SetupDataDirTestArgs(args);
    OptionsQmlModel model(node, /*is_onboarded=*/false, args);
    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    QVERIFY(model.setCustomDataDirArgs(QUrl::fromLocalFile(temp_dir.filePath("fresh-data")).toString()));
    model.setProxyEnabled(true);
    model.setProxyAddress("127.0.0.1:9050");
    QVERIFY(!model.proxySettingsDirty());

    // After onboard() the node starts with those settings applied —
    // no restart is needed, so dirty must be false.
    QVERIFY2(model.onboard(), qPrintable(model.dataDirError()));
    QVERIFY(!model.proxySettingsDirty());
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
    ON_CALL(node, resetSettings()).WillByDefault(Return());

    ArgsManager args;
    SetupDataDirTestArgs(args);
    OptionsQmlModel model(node, /*is_onboarded=*/false, args);
    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    QVERIFY(model.setCustomDataDirArgs(QUrl::fromLocalFile(temp_dir.filePath("fresh-data")).toString()));
    model.setExternalSignerPath("/usr/local/bin/hwi");

    EXPECT_CALL(node, updateRwSetting(::testing::_, ::testing::_)).Times(::testing::AnyNumber());
    EXPECT_CALL(node, updateRwSetting(std::string{"signer"},
        Truly([](const common::SettingsValue& v) {
            return v.isStr() && v.get_str() == "/usr/local/bin/hwi";
        })));

    QVERIFY2(model.onboard(), qPrintable(model.dataDirError()));
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
    ON_CALL(node, resetSettings()).WillByDefault(Return());

    ArgsManager args;
    SetupDataDirTestArgs(args);
    OptionsQmlModel model(node, /*is_onboarded=*/false, args);
    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    QVERIFY(model.setCustomDataDirArgs(QUrl::fromLocalFile(temp_dir.filePath("fresh-data")).toString()));
    model.setExternalSignerPath("/opt/hwi/ledger.py");
    QVERIFY(!model.walletSettingsDirty());

    EXPECT_CALL(node, updateRwSetting(::testing::_, ::testing::_)).Times(::testing::AnyNumber());
    EXPECT_CALL(node, updateRwSetting(std::string{"signer"},
        Truly([](const common::SettingsValue& v) {
            return v.isStr() && v.get_str() == "/opt/hwi/ledger.py";
        })));

    QVERIFY2(model.onboard(), qPrintable(model.dataDirError()));
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

void OptionsModelTests::existingCoreDataDirRejectsOnboard()
{
    using ::testing::_;
    using ::testing::NiceMock;
    using ::testing::Return;

    NiceMock<MockNode> node;
    ON_CALL(node, getPersistentSetting(_)).WillByDefault(Return(common::SettingsValue{}));
    EXPECT_CALL(node, resetSettings()).Times(0);

    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    const QString existing_dir = temp_dir.filePath("existing-core");
    QVERIFY(QDir().mkpath(existing_dir));

    QFile conf(existing_dir + "/bitcoin.conf");
    QVERIFY(conf.open(QIODevice::WriteOnly | QIODevice::Text));
    conf.write("regtest=1\n");
    conf.close();

    ArgsManager args;
    SetupDataDirTestArgs(args);
    SetDataDirArg(args, existing_dir);
    OptionsQmlModel model(node, /*is_onboarded=*/false, args);

    QVERIFY(!model.validateDataDirSelection());
    QVERIFY(model.dataDirError().contains("already contains Bitcoin Core data"));
    QVERIFY(!model.onboard());
}

void OptionsModelTests::customDataDirArgsCreatesDirectoryAndSetsArg()
{
    using ::testing::_;
    using ::testing::NiceMock;
    using ::testing::Return;

    NiceMock<MockNode> node;
    ON_CALL(node, getPersistentSetting(_)).WillByDefault(Return(common::SettingsValue{}));

    ArgsManager args;
    SetupDataDirTestArgs(args);
    OptionsQmlModel model(node, /*is_onboarded=*/false, args, /*initialize_config_on_onboard=*/true);

    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    const QString custom_dir = temp_dir.filePath("custom-data");

    QVERIFY(model.setCustomDataDirArgs(QUrl::fromLocalFile(custom_dir).toString()));
    QCOMPARE(model.dataDir(), custom_dir);
    QCOMPARE(model.getCustomDataDirString(), custom_dir);
    QVERIFY(model.dataDirError().isEmpty());
    QVERIFY(!QFileInfo::exists(custom_dir));

    QVERIFY2(model.onboard(), qPrintable(model.dataDirError()));
    QVERIFY(QFileInfo::exists(custom_dir));
    QVERIFY(QFileInfo::exists(custom_dir + "/regtest/wallets"));
    QCOMPARE(QString::fromStdString(args.GetDataDirBase().utf8string()), custom_dir);
}

void OptionsModelTests::customDataDirArgsRejectsFilePath()
{
    using ::testing::_;
    using ::testing::NiceMock;
    using ::testing::Return;

    NiceMock<MockNode> node;
    ON_CALL(node, getPersistentSetting(_)).WillByDefault(Return(common::SettingsValue{}));

    QTemporaryDir home_dir;
    QVERIFY(home_dir.isValid());
    ScopedHomeDir scoped_home{home_dir.path()};

    ArgsManager args;
    SetupDataDirTestArgs(args);
    OptionsQmlModel model(node, /*is_onboarded=*/false, args);

    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    const QString file_path = temp_dir.filePath("not-a-directory");
    QFile file(file_path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("not a directory\n");
    file.close();

    QVERIFY(!model.setCustomDataDirArgs(QUrl::fromLocalFile(file_path).toString()));
    QCOMPARE(model.dataDir(), model.getDefaultDataDirString());
    QCOMPARE(model.dataDirError(), QString("The selected data directory path is not a directory."));
}

void OptionsModelTests::setDataDirDefaultClearsCustomDataDir()
{
    using ::testing::_;
    using ::testing::NiceMock;
    using ::testing::Return;

    NiceMock<MockNode> node;
    ON_CALL(node, getPersistentSetting(_)).WillByDefault(Return(common::SettingsValue{}));

    QTemporaryDir home_dir;
    QVERIFY(home_dir.isValid());
    ScopedHomeDir scoped_home{home_dir.path()};

    ArgsManager args;
    SetupDataDirTestArgs(args);
    OptionsQmlModel model(node, /*is_onboarded=*/false, args);

    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    const QString custom_dir = temp_dir.filePath("custom-data");

    QVERIFY(model.setCustomDataDirArgs(QUrl::fromLocalFile(custom_dir).toString()));
    model.setDataDir(model.getDefaultDataDirString());
    QCOMPARE(model.dataDir(), model.getDefaultDataDirString());
    QVERIFY(model.getCustomDataDirString().isEmpty());
    QVERIFY(model.dataDirError().isEmpty());
}

void OptionsModelTests::onboardPersistsCustomDataDir()
{
    using ::testing::_;
    using ::testing::NiceMock;
    using ::testing::Return;

    QSettings settings = DataDirSettings();
    settings.remove(SettingsKeys::DATA_DIR);

    NiceMock<MockNode> node;
    ON_CALL(node, getPersistentSetting(_)).WillByDefault(Return(common::SettingsValue{}));
    ON_CALL(node, resetSettings()).WillByDefault(Return());

    ArgsManager args;
    SetupDataDirTestArgs(args);
    OptionsQmlModel model(node, /*is_onboarded=*/false, args);

    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    const QString custom_dir = temp_dir.filePath("custom-data");
    QVERIFY(model.setCustomDataDirArgs(QUrl::fromLocalFile(custom_dir).toString()));

    QVERIFY(model.onboard());
    QCOMPARE(settings.value(SettingsKeys::DATA_DIR).toString(), custom_dir);

    settings.remove(SettingsKeys::DATA_DIR);
}

void OptionsModelTests::onboardClearsPersistedDataDirForDefault()
{
    using ::testing::_;
    using ::testing::NiceMock;
    using ::testing::Return;

    QSettings settings = DataDirSettings();
    settings.setValue(SettingsKeys::DATA_DIR, QStringLiteral("/tmp/old-custom-data"));

    NiceMock<MockNode> node;
    ON_CALL(node, getPersistentSetting(_)).WillByDefault(Return(common::SettingsValue{}));
    ON_CALL(node, resetSettings()).WillByDefault(Return());

    ArgsManager args;
    SetupDataDirTestArgs(args);
    QTemporaryDir home_dir;
    QVERIFY(home_dir.isValid());
    ScopedHomeDir scoped_home{home_dir.path()};
    OptionsQmlModel model(node, /*is_onboarded=*/false, args);

    QVERIFY(model.onboard());
    QVERIFY(!settings.contains(SettingsKeys::DATA_DIR));
}

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(OptionsModelTests)
#else
QTEST_MAIN(OptionsModelTests)
#endif
#include "test_options_model.moc"
