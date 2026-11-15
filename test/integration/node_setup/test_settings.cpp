// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/integration/runner/test_cases.h>
#include <test/integration/fixture/test_util.h>
#include <qml/models/core_settings_model.h>
#include <qml/models/settingseditormodel.h>
#include <qml/models/desktopwindowbehaviormodel.h>

#include <QSettings>
#include <QDesktopServices>
#include <QScopeGuard>
#include <QUrl>

#include <tuple>
#include <map>

class SettingsIntegrationTests : public QObject
{
    Q_OBJECT
    BitcoinQmlApplication& m_app;
    QList<QUrl> m_opened_urls;

    QObject* openProxySettings()
    {
        if (!qmltest::SelectSettings(m_app, "connection")) return nullptr;
        auto* page=qmltest::WaitForVisible(m_app,"connectionSettingsPage");
        auto* editor=settingsEditor();
        if(!editor||!QTest::qWaitFor([&]{return editor->fresh();}))return nullptr;
        return page;
    }
    SettingsEditorModel* settingsEditor()
    {
        auto* view=qmltest::Find(m_app,"settingsView");
        return view?view->property("settingsEditor").value<SettingsEditorModel*>():nullptr;
    }

    QString persistentProxy(const std::string& key)
    {
        const auto value = qmltest::Call(m_app, &interfaces::Node::getPersistentSetting, key);
        return value.isStr() ? QString::fromStdString(value.get_str()) : QString{};
    }

public:
    explicit SettingsIntegrationTests(BitcoinQmlApplication& app) : m_app{app} {}

public Q_SLOTS:
    void recordOpenedUrl(const QUrl& url) { m_opened_urls.append(url); }

private Q_SLOTS:
    void aboutLinkConfirmationCancelsAndReopensWithoutNavigation()
    {
        // Catch accidental launches without opening a real browser.
        QDesktopServices::setUrlHandler(QStringLiteral("https"), this, "recordOpenedUrl");
        const auto restore_handler = qScopeGuard([] { QDesktopServices::unsetUrlHandler(QStringLiteral("https")); });
        QVERIFY(qmltest::SelectSettings(m_app, "about"));
        QPointer<QObject> about{qmltest::WaitForVisible(m_app, "aboutSettingsPage")};
        QVERIFY(about);
        QPointer<QObject> popup;
        for (const auto& [row, link] : {
                 std::pair{"aboutWebsiteRow", "https://bitcoincore.org"},
                 std::pair{"aboutSourceCodeRow", "https://github.com/bitcoin/bitcoin"}}) {
            QVERIFY(qmltest::Click(m_app, row));
            auto* opened = qmltest::WaitForVisible(m_app, "aboutExternalLinkPopup");
            QVERIFY(opened);
            if (popup) QCOMPARE(opened, popup.data());
            popup = opened;
            QTRY_VERIFY(popup->property("opened").toBool());
            QCOMPARE(popup->property("link").toUrl(), QUrl{QString::fromLatin1(link)});
            QVERIFY(qmltest::WaitForVisible(m_app, "externalLinkCancel"));
            QVERIFY(qmltest::WaitForVisible(m_app, "externalLinkConfirm"));
            QVERIFY(qmltest::Click(m_app, "externalLinkCancel"));
            QTRY_VERIFY(!popup->property("visible").toBool());
            QVERIFY(about && about->property("visible").toBool());
            QVERIFY(m_opened_urls.isEmpty());
        }
    }

    void displayControlsUpdateUnitsAndTranslations()
    {
        QVERIFY(qmltest::SelectSettings(m_app, "display"));
        QVERIFY(qmltest::WaitForVisible(m_app, "displaySettingsPage"));
        for (const auto* row : {"displayThemeRow", "displayBlockStatusSizeRow", "displayMoneyFontRow",
                                "displayUnitRow", "displayLanguageRow", "displayTransactionUrlsRow"}) {
            auto* object = qmltest::Find(m_app, row);
            QVERIFY2(object, row);
            QCOMPARE(object->property("description").toString(), QString{});
        }
        QVERIFY(qmltest::Click(m_app, "displayUnitPickerButton"));
        QVERIFY(qmltest::Click(m_app, "displayUnitSAT"));
        auto* picker = qmltest::Find(m_app, "displayUnitPicker");
        QVERIFY(picker);
        QTRY_COMPARE(picker->property("currentValue").toInt(), 3);
        QCOMPARE(picker->property("currentText").toString(), QStringLiteral("sat"));
        QVERIFY(qmltest::Click(m_app, "displayLanguageRow"));
        QVERIFY(qmltest::SetText(m_app, "languageSearch", QString::fromUtf8("español")));
        QVERIFY(qmltest::Click(m_app, "language_es"));
        QVERIFY(qmltest::WaitForVisible(m_app, "displayLanguageRow"));
        QTRY_COMPARE(qmltest::Find(m_app, "displayLanguageRow")->property("title").toString(), QStringLiteral("Idioma"));
        QCOMPARE(qmltest::Find(m_app, "displayUnitRow")->property("title").toString(), QString::fromUtf8("Unidad de visualización"));
        QVERIFY(qmltest::Click(m_app, "displayLanguageRow"));
        QVERIFY(qmltest::SetText(m_app, "languageSearch", ""));
        QVERIFY(qmltest::Click(m_app, "language_"));
        QVERIFY(qmltest::WaitForVisible(m_app, "displayLanguageRow"));
        QTRY_COMPARE(qmltest::Find(m_app, "displayLanguageRow")->property("title").toString(), QStringLiteral("Language"));
    }

    void proxyValidationKeepsInvalidDraftsOutOfCoreSettings_data()
    {
        QTest::addColumn<bool>("tor");
        QTest::newRow("default-proxy") << false;
        QTest::newRow("tor-proxy") << true;
    }

    void proxyValidationKeepsInvalidDraftsOutOfCoreSettings()
    {
        QFETCH(bool, tor);
        QVERIFY(openProxySettings());
        auto* editor=settingsEditor();QVERIFY(editor);
        const auto key=tor?"onion":"proxy";
        const auto address=tor?"onionSettingInput":"proxySettingInput";
        const auto original=persistentProxy(key);
        QVERIFY(!editor->restartRequired());
        QVERIFY(qmltest::SetText(m_app,address,"999.999.999.999:9050"));
        QTRY_VERIFY(editor->dirty());
        QVERIFY(qmltest::Click(m_app,"settingsSaveBatchButton"));
        QTRY_VERIFY(!editor->busy()&&!editor->error().isEmpty());
        QVERIFY(editor->dirty());QCOMPARE(persistentProxy(key),original);
        QVERIFY(!editor->lastSavePersisted());QVERIFY(!editor->restartRequired());
        QCOMPARE(editor->entry(QString::fromLatin1(key))->text(),QStringLiteral("999.999.999.999:9050"));
        QVERIFY(qmltest::SetText(m_app,address,"127.0.0.1:9050"));
        QTRY_VERIFY(editor->canSave());QCOMPARE(persistentProxy(key),original);
        editor->discard();QTRY_VERIFY(!editor->dirty());
        QCOMPARE(persistentProxy(key),original);
    }

    void cancellingBackPreservesProxyDraftAndConfirmingDiscardsIt()
    {
        QVERIFY(openProxySettings());auto* editor=settingsEditor();QVERIFY(editor);
        const auto original=persistentProxy("proxy");
        QVERIFY(qmltest::SetText(m_app,"proxySettingInput","10.0.0.5:9050"));
        QTRY_VERIFY(editor->dirty());
        QVERIFY(qmltest::SelectSettings(m_app,"storage"));
        QTRY_VERIFY(editor->closeQuestion());
        QVERIFY(qmltest::Click(m_app,"settingsCloseKeep"));
        QTRY_VERIFY(!editor->closeQuestion());
        QCOMPARE(qmltest::Find(m_app,"settingsView")->property("selectedSectionId").toString(),QStringLiteral("connection"));
        QCOMPARE(editor->entry(QStringLiteral("proxy"))->text(),QStringLiteral("10.0.0.5:9050"));
        QVERIFY(qmltest::SelectSettings(m_app,"storage"));QTRY_VERIFY(editor->closeQuestion());
        QVERIFY(qmltest::Click(m_app,"settingsCloseDiscard"));
        QTRY_COMPARE(qmltest::Find(m_app,"settingsView")->property("selectedSectionId").toString(),QStringLiteral("storage"));
        QVERIFY(openProxySettings());QTRY_VERIFY(!settingsEditor()->dirty());
        QCOMPARE(settingsEditor()->entry(QStringLiteral("proxy"))->text(),original);
        QCOMPARE(persistentProxy("proxy"),original);
    }

    void readOnlyNetworkProxyKeepsOtherSettingsWritable()
    {
        const auto routes=[&]{std::map<std::string,std::string> result;const auto info=qmltest::Call(m_app,&interfaces::Node::executeRpc,"getnetworkinfo",UniValue{UniValue::VARR},"");for(const auto& network:info.find_value("networks").getValues())result.emplace(network.find_value("name").get_str(),network.find_value("proxy").get_str());return result;};
        const auto original=routes();QCOMPARE(original.at("ipv4"),std::string{"127.0.0.1:9050"});QCOMPARE(original.at("onion"),std::string{});
        QVERIFY(openProxySettings());auto* editor=settingsEditor();QVERIFY(editor);QVERIFY(!editor->entry(QStringLiteral("proxy"))->editable());
        const auto running=editor->entry(QStringLiteral("maxmempool"))->effectiveText();
        QVERIFY(qmltest::SelectSettings(m_app,"mempool"));QVERIFY(qmltest::WaitForVisible(m_app,"maxmempoolSettingInput"));QVERIFY(qmltest::SetText(m_app,"maxmempoolSettingInput","400"));QTRY_VERIFY(editor->canSave());
        QVERIFY(qmltest::Click(m_app,"settingsSaveBatchButton"));QTRY_VERIFY(editor->lastSavePersisted()&&!editor->busy());
        const auto saved=qmltest::Call(m_app,&interfaces::Node::getPersistentSetting,"maxmempool");QVERIFY(saved.isNum());QCOMPARE(saved.getInt<int64_t>(),400);QVERIFY(qmltest::Call(m_app,&interfaces::Node::getPersistentSetting,"proxy").isNull());
        QVERIFY(routes()==original);QCOMPARE(editor->entry(QStringLiteral("maxmempool"))->effectiveText(),running);QCOMPARE(editor->entry(QStringLiteral("maxmempool"))->nextStartText(),QStringLiteral("400"));QVERIFY(editor->restartRequired());
    }

    void savingProxyDraftCommitsBothEntries()
    {
        QVERIFY(openProxySettings());auto* editor=settingsEditor();QVERIFY(editor);
        for(const auto& [address,value]:{std::pair{"proxySettingInput","10.0.0.1:9050"},std::pair{"onionSettingInput","127.0.0.1:9150"}})QVERIFY(qmltest::SetText(m_app,address,value));
        QTRY_VERIFY(editor->canSave());
        QVERIFY(qmltest::Click(m_app,"settingsSaveBatchButton"));
        QTRY_VERIFY(editor->lastSavePersisted()&&!editor->busy());
        QTRY_COMPARE(persistentProxy("proxy"),QStringLiteral("10.0.0.1:9050"));
        QTRY_COMPARE(persistentProxy("onion"),QStringLiteral("127.0.0.1:9150"));
        const auto result=editor->saveResult();QVERIFY(result.outcomeKnown);QCOMPARE(result.outcome,SettingsSaveResult::Saved);
        QVERIFY(result.actualValues.contains(QStringLiteral("proxy = 10.0.0.1:9050")));
        QVERIFY(result.actualValues.contains(QStringLiteral("onion = 127.0.0.1:9150")));
        QVERIFY(!editor->dirty());QVERIFY(editor->restartRequired());
    }

    void windowBehaviorControlsUpdateTheLiveModelAndRetainNavigation()
    {
        auto* behavior = qmltest::Model<DesktopWindowBehaviorModel>(m_app, "desktopWindowBehaviorModel");
        QVERIFY(behavior);
        QTRY_VERIFY(behavior->settingsReady());
        QVERIFY(qmltest::SelectSettings(m_app, "window-behavior"));
        QVERIFY(qmltest::WaitForVisible(m_app, "windowBehaviorSettingsPage"));
        QVERIFY(qmltest::Find(m_app, "showTrayIconSwitch")->property("checked").toBool());
        QVERIFY(!qmltest::Find(m_app, "minimizeToTraySwitch")->property("checked").toBool());
        QVERIFY(!qmltest::Find(m_app, "minimizeOnCloseSwitch")->property("checked").toBool());
        QVERIFY(qmltest::Click(m_app, "showTrayIconSwitch"));
        QTRY_VERIFY(!behavior->showTrayIcon());
        QVERIFY(qmltest::Click(m_app, "showTrayIconSwitch"));
        QTRY_VERIFY(behavior->showTrayIcon());
        QVERIFY(qmltest::Click(m_app, "minimizeOnCloseSwitch"));
        QTRY_VERIFY(behavior->minimizeOnClose());
        QVERIFY(qmltest::SelectSettings(m_app, "about"));
        QVERIFY(qmltest::WaitForVisible(m_app, "aboutSettingsPage"));
        QVERIFY(qmltest::SelectSettings(m_app, "window-behavior"));
        QVERIFY(qmltest::WaitForVisible(m_app, "windowBehaviorSettingsPage"));
        QVERIFY(qmltest::Find(m_app, "minimizeOnCloseSwitch")->property("checked").toBool());
        // Leave normal window-close semantics for the fixture's shutdown.
        QVERIFY(qmltest::Click(m_app, "minimizeOnCloseSwitch"));
    }
};

std::unique_ptr<QObject> CreateSettingsTests(BitcoinQmlApplication& app, WalletMode)
{
    return std::make_unique<SettingsIntegrationTests>(app);
}

QStringList SettingsTestArguments(const QString& selector)
{
    return selector==QStringLiteral("readOnlyNetworkProxyKeepsOtherSettingsWritable")?QStringList{QStringLiteral("-proxy=127.0.0.1:9050=ipv4")}:QStringList{};
}

#include <test_settings.moc>
