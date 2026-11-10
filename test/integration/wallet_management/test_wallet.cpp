// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/integration/runner/test_cases.h>
#include <test/integration/fixture/test_wallet.h>
#include <util/moneystr.h>
#include <wallet/db.h>
#include <wallet/wallet.h>

#include <QApplication>
#include <QEvent>
#include <QFileDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QScopeGuard>
#include <QTimer>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QSignalSpy>
#include <QTemporaryDir>

namespace {
class RefuseNativeOverwrite final : public QObject
{
public:
    int prompts{0};
    bool eventFilter(QObject* object, QEvent* event) override
    {
        auto* message = qobject_cast<QMessageBox*>(object);
        if (message && event->type() == QEvent::Show) {
            ++prompts;
            QTimer::singleShot(0, message, [message] { message->done(QMessageBox::No); });
        }
        return false;
    }
};
QFileDialog* VisibleBackupChooser()
{
    for (auto* widget : QApplication::topLevelWidgets()) {
        if (auto* dialog = qobject_cast<QFileDialog*>(widget); dialog && dialog->isVisible()) return dialog;
    }
    return nullptr;
}
QByteArray BackupBytes(const QString& path)
{
    QFile file{path};
    if (!file.open(QIODevice::ReadOnly)) throw std::runtime_error{"Cannot read backup oracle"};
    return file.readAll();
}
// The same source runs against the exact parent for red evidence. That parent
// has a synchronous backup API and no operation handle. Only this test harness
// uses property discovery; production QML binds the typed operation interface.
QObject* BackupOperationFor(WalletQmlModel* wallet)
{
    return wallet->property("backupOperation").value<QObject*>();
}
} // namespace

class WalletIntegrationTests : public QObject
{
    Q_OBJECT
    BitcoinQmlApplication& m_app;

    bool openSelector()
    {
        auto* popup = qmltest::Find(m_app, "walletSelectPopup");
        if (popup && popup->property("opened").toBool()) return true;
        return qmltest::Click(m_app, "walletBadge") && qmltest::WaitForVisible(m_app, "walletSelectPopup");
    }

    bool selectWallet(const QString& name)
    {
        return openSelector() && qmltest::Click(m_app, qPrintable("walletSelectItem_" + name)) &&
               qmltest::WaitForWallet(m_app, name);
    }

    bool walletAction(const QString& name, const char* action)
    {
        return openSelector() && qmltest::Click(m_app, qPrintable("walletSelectActions_" + name)) &&
               qmltest::Click(m_app, qPrintable(QString{"walletSelect%1_%2"}.arg(QString::fromLatin1(action), name)));
    }

public:
    explicit WalletIntegrationTests(BitcoinQmlApplication& app) : m_app{app} {}

private Q_SLOTS:
    void namesRejectLoadedClosedAndAliasedWallets()
    {
        auto* controller = qmltest::Model<WalletQmlController>(m_app, "walletController");
        QVERIFY(controller);
        QVERIFY(qmltest::CreateWallet(m_app, "closed"));
        controller->closeWallet("closed");
        QTRY_VERIFY(!controller->isWalletOpen("closed"));
        QVERIFY(qmltest::CreateWallet(m_app, "loaded"));
        QVERIFY(qmltest::CreateWallet(m_app, "alias_target"));
        QVERIFY(controller->setWalletDisplayName("alias_target", "Savings"));
        QVERIFY(qmltest::Click(m_app, "walletBadge"));
        QVERIFY(qmltest::Click(m_app, "walletSelectAddWalletButton"));
        QVERIFY(qmltest::Click(m_app, "walletTypeRegular"));
        QVERIFY(qmltest::SetText(m_app, "createWalletPasswordInput", "test wallet password"));
        QVERIFY(qmltest::SetText(m_app, "createWalletPasswordRepeatInput", "test wallet password"));
        QVERIFY(qmltest::Click(m_app, "createWalletPasswordConfirmToggle"));
        for (const auto* name : {"closed", "loaded", "Savings", "savings"}) {
            QVERIFY2(controller->walletNameAvailabilityError(name).contains("already exists"), name);
            QVERIFY(qmltest::SetText(m_app, "createWalletNameInput", name));
            QVERIFY(qmltest::Click(m_app, "createWalletFormCreateButton"));
            auto* error = qmltest::Find(m_app, "createWalletFormErrorText");
            QVERIFY(error);
            QTRY_VERIFY(error->property("text").toString().contains("already exists"));
        }
        QCOMPARE(controller->walletNameAvailabilityError("My new wallet"), QString{});
        QSignalSpy created{controller, &WalletQmlController::walletCreateSucceeded};
        QVERIFY(qmltest::SetText(m_app, "createWalletNameInput", "My new wallet"));
        QVERIFY(qmltest::Click(m_app, "createWalletFormCreateButton"));
        QTRY_COMPARE_WITH_TIMEOUT(created.count(), 1, 30'000);
        QVERIFY(qmltest::WaitForVisible(m_app, "walletCreationReadyPage"));
        QVERIFY(qmltest::Click(m_app, "createWalletReadyDoneButton"));
        auto* wallet = qmltest::WaitForWallet(m_app, "My new wallet");
        QVERIFY(wallet);
        QVERIFY(wallet->isEncrypted());
        QVERIFY(wallet->isLocked());
        const auto info = qmltest::Rpc(m_app, "getwalletinfo", {}, wallet->name());
        QVERIFY(info.find_value("private_keys_enabled").get_bool());
        QCOMPARE(info.find_value("unlocked_until").getInt<int64_t>(), 0);
    }

    void closingAndReloadingReplacesTheModel()
    {
        auto* controller = qmltest::Model<WalletQmlController>(m_app, "walletController");
        QVERIFY(controller);
        QPointer<WalletQmlModel> original{qmltest::CreateWallet(m_app, "reload")};
        QVERIFY(original);
        QSignalSpy selected{controller, &WalletQmlController::selectedWalletChanged};
        controller->closeWallet("reload");
        QTRY_VERIFY(!controller->isWalletLoaded());
        QTRY_VERIFY(original.isNull());
        QVERIFY(!selected.isEmpty());
        QCOMPARE(qmltest::Rpc(m_app, "listwallets").size(), 0U);
        controller->setSelectedWallet("reload");
        QVERIFY(qmltest::WaitForWallet(m_app, "reload"));
        QCOMPARE(qmltest::Rpc(m_app, "listwallets").size(), 1U);
        auto* list = qmltest::Model<WalletListModel>(m_app, "walletListModel");
        QVERIFY(list);
        QTRY_COMPARE(list->rowCount(), 1);
        QCOMPARE(list->data(list->index(0), WalletListModel::LoadStateRole).toInt(), static_cast<int>(WalletListModel::LoadState::Open));
    }

    void passwordChangesRelockAndPreserveTheWallet()
    {
        auto* wallet = qmltest::CreateWallet(m_app);
        QVERIFY(wallet);
        QVERIFY(wallet->encryptWallet("old password"));
        QTRY_VERIFY(wallet->isLocked());
        QVERIFY(!wallet->changeWalletPassphrase("wrong password", "new password"));
        QVERIFY(!wallet->settingsError().isEmpty());
        QVERIFY(wallet->isLocked());
        QVERIFY(wallet->changeWalletPassphrase("old password", "new password"));
        QVERIFY(wallet->settingsError().isEmpty());
        QVERIFY(wallet->isLocked());
        qmltest::Rpc(m_app, "walletpassphrase", qmltest::Json("[\"new password\", 10]"), wallet->name());
        QTRY_VERIFY(!wallet->isLocked());
        qmltest::Rpc(m_app, "walletlock", {}, wallet->name());
        QTRY_VERIFY(wallet->isLocked());
    }

    void settingsExposeReadOnlyNameAndBackUpToTheChosenDirectory()
    {
        QVERIFY(qmltest::CreateWallet(m_app, "settings"));
        QVERIFY(qmltest::SelectSettings(m_app, "wallet"));
        QVERIFY(qmltest::WaitForVisible(m_app, "walletSettingsPage"));
        QCOMPARE(qmltest::Find(m_app, "walletNameRowValue")->property("text").toString(), QStringLiteral("settings"));
        QVERIFY(!qmltest::Find(m_app, "walletNameInput"));

        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        auto* path = qmltest::Find(m_app, "walletSettingsBackupPathField");
        QVERIFY(path);
        QVERIFY(path->setProperty("text", directory.path()));
        QVERIFY(qmltest::Click(m_app, "walletBackupRow"));
        QTRY_VERIFY(QFileInfo{directory.filePath("settings.bak")}.size() > 0);
        QCOMPARE(qmltest::Find(m_app, "walletSettingsPage")->property("errorText").toString(), QString{});
    }

    void wrongCurrentPasswordPreservesTheFieldAndTheWallet()
    {
        auto* wallet = qmltest::CreateWallet(m_app);
        QVERIFY(wallet);
        QVERIFY(wallet->encryptWallet("old password"));
        QTRY_VERIFY(wallet->isLocked());
        QVERIFY(qmltest::SelectSettings(m_app, "wallet"));
        QVERIFY(qmltest::Click(m_app, "walletPasswordRow"));
        QVERIFY(qmltest::WaitForVisible(m_app, "walletPasswordSettingsPage"));
        QVERIFY(qmltest::SetText(m_app, "walletPasswordCurrentField", "wrong password"));
        QVERIFY(qmltest::SetText(m_app, "walletPasswordNewField", "new integration password"));
        QVERIFY(qmltest::SetText(m_app, "walletPasswordConfirmField", "new integration password"));
        QVERIFY(qmltest::Click(m_app, "walletPasswordSaveButton"));
        QTRY_VERIFY(qmltest::Find(m_app, "walletPasswordErrorText")->property("text").toString().contains("incorrect", Qt::CaseInsensitive));
        QCOMPARE(qmltest::Find(m_app, "walletPasswordCurrentField")->property("text").toString(), QStringLiteral("wrong password"));
        QVERIFY(wallet->isLocked());
    }

    void walletChangesUnwindPasswordSettings_data()
    {
        QTest::addColumn<bool>("close_wallet");
        QTest::newRow("close-selected-wallet") << true;
        QTest::newRow("select-another-wallet") << false;
    }

    void walletChangesUnwindPasswordSettings()
    {
        QFETCH(bool, close_wallet);
        auto* alpha = qmltest::CreateWallet(m_app, "alpha");
        QVERIFY(alpha);
        QVERIFY(alpha->encryptWallet("alpha password"));
        QVERIFY(qmltest::CreateWallet(m_app, "beta"));
        QVERIFY(selectWallet("alpha"));
        QVERIFY(qmltest::SelectSettings(m_app, "wallet"));
        QVERIFY(qmltest::Click(m_app, "walletPasswordRow"));
        QVERIFY(qmltest::WaitForVisible(m_app, "walletPasswordSettingsPage"));

        if (close_wallet) {
            QVERIFY(walletAction("alpha", "Close"));
            QVERIFY(qmltest::Click(m_app, "walletCloseConfirmationConfirmButton"));
            QTRY_COMPARE(qmltest::Rpc(m_app, "listwallets").size(), 1U);
        } else {
            QVERIFY(selectWallet("beta"));
        }
        QVERIFY(qmltest::WaitForVisible(m_app, "walletSettingsPage"));
        QVERIFY(!qmltest::Find(m_app, "walletPasswordSettingsPage") ||
                !qmltest::Find(m_app, "walletPasswordSettingsPage")->property("visible").toBool());
        QVERIFY(selectWallet(close_wallet ? "alpha" : "beta"));
        QVERIFY(qmltest::WaitForVisible(m_app, "walletNameRow"));
    }

    void selectorActionsTargetTheirOwnRowAndRequireCloseConfirmation()
    {
        QVERIFY(qmltest::CreateWallet(m_app, "alpha"));
        qmltest::Rpc(m_app, "createwallet", qmltest::Json("{\"wallet_name\":\"beta\",\"disable_private_keys\":true}"));
        QVERIFY(qmltest::WaitForWallet(m_app, "beta"));
        auto* controller = qmltest::Model<WalletQmlController>(m_app, "walletController");
        QVERIFY(controller);
        QSignalSpy loading{controller, &WalletQmlController::walletLoadInProgressChanged};
        QVERIFY(selectWallet("alpha"));
        QVERIFY(loading.isEmpty());
        QCOMPARE(qmltest::Rpc(m_app, "listwallets").size(), 2U);

        QVERIFY(openSelector());
        QCOMPARE(qmltest::Find(m_app, "walletSelectStatus_alpha")->property("text").toString(), QStringLiteral("0.00000000 BTC"));
        QVERIFY(qmltest::Click(m_app, "walletSelectActions_beta"));
        QCOMPARE(qmltest::Find(m_app, "walletBadge")->property("text").toString(), QStringLiteral("alpha"));
        QVERIFY(qmltest::Click(m_app, "walletSelectSettings_beta"));
        QVERIFY(qmltest::WaitForWallet(m_app, "beta"));
        QVERIFY(qmltest::Find(m_app, "desktopWalletSettingsTabButton")->property("checked").toBool());

        QVERIFY(walletAction("beta", "Close"));
        QCOMPARE(qmltest::Rpc(m_app, "listwallets").size(), 2U);
        QVERIFY(qmltest::Click(m_app, "walletCloseConfirmationCancelButton"));
        QCOMPARE(qmltest::Rpc(m_app, "listwallets").size(), 2U);
        QVERIFY(walletAction("beta", "Close"));
        QVERIFY(qmltest::Click(m_app, "walletCloseConfirmationConfirmButton"));
        QTRY_COMPARE(qmltest::Rpc(m_app, "listwallets").size(), 1U);
        QVERIFY(qmltest::WaitForWallet(m_app, "alpha"));
        QVERIFY(openSelector());
        auto* row = qmltest::Find(m_app, "walletSelectItem_beta");
        QVERIFY(row);
        QTRY_COMPARE(row->property("loadState").toInt(), static_cast<int>(WalletListModel::LoadState::Closed));
        QCOMPARE(row->property("iconSource").toString(), QStringLiteral("image://images/wallet"));
        QVERIFY(!qmltest::Find(m_app, "walletSelectActionsMenu_beta")->property("visible").toBool());
    }

    void selectorLoadFailureRemainsVisibleAfterDismissingTheAlert()
    {
        QPointer<WalletQmlModel> wallet{qmltest::CreateWallet(m_app, "broken")};
        QVERIFY(wallet);
        const auto database_path = QString::fromStdString(wallet->wallet()->wallet()->GetDatabase().Filename());
        QFile database{database_path};
        QVERIFY(database.open(QIODevice::ReadOnly));
        const auto header = database.read(32);
        database.close();
        auto* controller = qmltest::Model<WalletQmlController>(m_app, "walletController");
        QVERIFY(controller);
        auto* wallets = qmltest::Model<WalletListModel>(m_app, "walletListModel");
        QVERIFY(wallets);
        wallets->listWalletDir();
        QTRY_VERIFY(wallets->walletDirLoaded());
        controller->closeWallet("broken");
        QTRY_VERIFY(wallet.isNull());
        QTRY_COMPARE(qmltest::Rpc(m_app, "listwallets").size(), 0U);
        QVERIFY(openSelector());

        // Keep SQLite's format signature so directory discovery still lists it,
        // while opening the truncated database produces a real load error.
        QVERIFY(database.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(database.write(header), header.size());
        database.close();
        QVERIFY(qmltest::Click(m_app, "walletSelectItem_broken"));
        QVERIFY(qmltest::WaitForVisible(m_app, "walletLoadErrorPopup"));
        QVERIFY(!qmltest::Find(m_app, "walletLoadErrorPopupText")->property("text").toString().isEmpty());
        auto* row = qmltest::Find(m_app, "walletSelectItem_broken");
        QVERIFY(row);
        QTRY_COMPARE(row->property("loadState").toInt(), static_cast<int>(WalletListModel::LoadState::LoadError));
        QVERIFY(qmltest::Find(m_app, "walletSelectStatus_broken")->property("text").toString().contains("failed", Qt::CaseInsensitive));
        QVERIFY(qmltest::Click(m_app, "walletLoadErrorPopupDismissButton"));
        QTRY_VERIFY(!qmltest::Find(m_app, "walletLoadErrorPopup")->property("visible").toBool());
        QCOMPARE(row->property("loadState").toInt(), static_cast<int>(WalletListModel::LoadState::LoadError));
    }

    void walletTypeSelectorRejectsInvalidBackups()
    {
        QVERIFY(qmltest::Click(m_app, "walletBadge"));
        QVERIFY(qmltest::WaitForVisible(m_app, "walletCreationTypePage"));
        QVERIFY(!qmltest::Find(m_app, "walletTypeMultiKey"));
        QVERIFY(!qmltest::Find(m_app, "walletTypeCustom"));
        auto* controller = qmltest::Model<WalletQmlController>(m_app, "walletController");
        QVERIFY(controller);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QFile invalid{directory.filePath("invalid.bak")};
        QVERIFY(invalid.open(QIODevice::WriteOnly));
        QVERIFY(invalid.write("not a wallet backup") > 0);
        invalid.close();
        auto* wizard = qmltest::Find(m_app, "createWalletWizard");
        QVERIFY(wizard);
        QVERIFY(QMetaObject::invokeMethod(wizard, "importFile", Q_ARG(QVariant, invalid.fileName())));
        QVERIFY(qmltest::WaitForVisible(m_app, "walletImportErrorPage"));
        QVERIFY(!controller->walletLoadError().isEmpty());
        QCOMPARE(qmltest::Find(m_app, "importWalletErrorTitle")->property("text").toString(), QStringLiteral("This wallet type is not supported"));
        QVERIFY(qmltest::Find(m_app, "importWalletErrorHelpText")->property("text").toString().contains("Data is not in recognized format."));
        QVERIFY(!QFileInfo::exists(QString::fromStdString(m_app.node().walletLoader().getWalletDir()) + "/invalid"));
        QCOMPARE(qmltest::Rpc(m_app, "listwallets").size(), 0U);
    }

    void backupRefusalPreservesSelectedAndFinalFiles_data()
    {
        QTest::addColumn<bool>("selected_exists");
        QTest::newRow("dotted-new-name-existing-final-backup") << false;
        QTest::newRow("selected-and-final-sentinels") << true;
    }

    void backupRefusalPreservesSelectedAndFinalFiles()
    {
        QFETCH(bool, selected_exists);
        const bool previous = QCoreApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
        QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true);
        const auto restore = qScopeGuard([previous] { QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs, previous); });
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto selected = directory.filePath("archive.v1");
        const QString final{selected + ".bak"};
        auto* original = qmltest::CreateWallet(m_app, "backup_original");
        QVERIFY(original);
        const auto original_address = qmltest::NewAddress(m_app, original->name());
        QVERIFY(original->backupWallet(final));
        QTRY_VERIFY(QFileInfo{final}.size() > 0);
        if (auto* operation = BackupOperationFor(original)) QTRY_VERIFY(!operation->property("pending").toBool());
        const auto final_before = BackupBytes(final);
        QVERIFY(!final_before.isEmpty());
        QByteArray selected_before;
        if (selected_exists) {
            QFile sentinel{selected}; QVERIFY(sentinel.open(QIODevice::WriteOnly));
            selected_before = "selected path sentinel - retain every byte";
            QCOMPARE(sentinel.write(selected_before), qint64{selected_before.size()}); sentinel.close();
        }
        auto* replacement = qmltest::CreateWallet(m_app, "backup_replacement");
        QVERIFY(replacement);
        const auto replacement_address = qmltest::NewAddress(m_app, replacement->name());
        RefuseNativeOverwrite refuse;
        qApp->installEventFilter(&refuse);
        const auto filter_cleanup = qScopeGuard([&refuse] { qApp->removeEventFilter(&refuse); });
        QVERIFY(qmltest::Click(m_app, "desktopWalletSettingsTabButton"));
        QVERIFY(qmltest::Click(m_app, "settingsSidebar_wallet"));
        QVERIFY(qmltest::WaitForVisible(m_app, "walletSettingsPage"));
        QVERIFY(qmltest::Click(m_app, "walletBackupRow"));
        QTRY_VERIFY(VisibleBackupChooser());
        auto* chooser = VisibleBackupChooser();
        auto* name_field = chooser->findChild<QLineEdit*>("fileNameEdit");
        QVERIFY(name_field); // Actual Widgets fallback, not a fabricated accepted event.
        chooser->selectNameFilter("All files (*)");
        chooser->setDirectory(directory.path());
        name_field->clear();
        QTest::keyClicks(name_field, "archive.v1");
        QSignalSpy accepted{chooser, &QFileDialog::accepted};
        qInfo() << "Fallback typed name" << name_field->text() << "selected files before Return" << chooser->selectedFiles();
        QTest::keyClick(name_field, Qt::Key_Return);
        QTRY_VERIFY(accepted.count() == 1 || refuse.prompts > 0);
        qInfo() << "Fallback after Return selected files" << chooser->selectedFiles()
                << "bound current wallet" << replacement->name()
                << "QML error" << qmltest::Find(m_app, "walletSettingsPage")->property("errorText");
        int app_refusals{0};
        if (refuse.prompts > 0) {
            chooser->reject();
        } else if (auto* operation = BackupOperationFor(replacement)) {
            QTRY_VERIFY(operation->property("confirmationRequired").toBool() || !operation->property("pending").toBool());
            if (operation->property("confirmationRequired").toBool()) {
                QCOMPARE(operation->property("finalPath").toString(), final);
                QVERIFY(qmltest::WaitForVisible(m_app, "walletBackupOverwritePopup"));
                QVERIFY(qmltest::Click(m_app, "walletBackupOverwriteRejectButton"));
                ++app_refusals;
                QTRY_VERIFY(!operation->property("pending").toBool());
            }
        }
        const auto final_after = BackupBytes(final);
        const bool selected_preserved = selected_exists ? BackupBytes(selected) == selected_before : !QFileInfo::exists(selected);
        // Independently restore the resulting file before checking bytes, so
        // the red run records whose real keys replaced the disposable backup.
        UniValue params{UniValue::VARR}; params.push_back("backup_restored"); params.push_back(final.toStdString());
        qmltest::Rpc(m_app, "restorewallet", params);
        auto owns = [&](const QString& address) {
            UniValue request{UniValue::VARR}; request.push_back(address.toStdString());
            return qmltest::Rpc(m_app, "getaddressinfo", request, "backup_restored").find_value("ismine").get_bool();
        };
        const bool owns_original = owns(original_address), owns_replacement = owns(replacement_address);
        qInfo() << "Backup refusal oracle native prompts" << refuse.prompts << "app refusals" << app_refusals
                << "selected bytes preserved" << selected_preserved << "final bytes preserved" << (final_after == final_before)
                << "restored original/replacement ownership" << owns_original << owns_replacement;
        QVERIFY(selected_preserved);
        QCOMPARE(final_after, final_before);
        QVERIFY(owns_original); QVERIFY(!owns_replacement);
        QCOMPARE(refuse.prompts + app_refusals, 1);
    }

    void newBackupDestinationIsTheApprovedPath_data()
    {
        QTest::addColumn<bool>("rewritten");
        QTest::newRow("known-suffix-new-file") << false;
        QTest::newRow("dotted-name-new-final-file") << true;
    }

    void newBackupDestinationIsTheApprovedPath()
    {
        QFETCH(bool, rewritten);
        const bool previous = QCoreApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
        QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true);
        const auto restore = qScopeGuard([previous] { QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs, previous); });
        QTemporaryDir directory; QVERIFY(directory.isValid());
        auto* wallet = qmltest::CreateWallet(m_app, "approved_source"); QVERIFY(wallet);
        const auto address = qmltest::NewAddress(m_app, wallet->name());
        const QString name = rewritten ? "new.archive" : "new.bak";
        const QString selected = directory.filePath(name);
        const QString final = rewritten ? selected + ".bak" : selected;
        QVERIFY(qmltest::Click(m_app, "desktopWalletSettingsTabButton"));
        QVERIFY(qmltest::Click(m_app, "settingsSidebar_wallet"));
        QVERIFY(qmltest::WaitForVisible(m_app, "walletSettingsPage"));
        QVERIFY(qmltest::Click(m_app, "walletBackupRow"));
        QTRY_VERIFY(VisibleBackupChooser()); auto* chooser = VisibleBackupChooser();
        chooser->selectNameFilter("All files (*)"); chooser->setDirectory(directory.path());
        auto* field = chooser->findChild<QLineEdit*>("fileNameEdit"); QVERIFY(field);
        field->clear(); QTest::keyClicks(field, name); QTest::keyClick(field, Qt::Key_Return);
        QTRY_VERIFY(wallet->backupOperation()); auto* operation = wallet->backupOperation();
        if (rewritten) {
            QTRY_VERIFY(operation->confirmationRequired());
            QCOMPARE(operation->finalPath(), final);
            QVERIFY(!QFileInfo::exists(selected)); QVERIFY(!QFileInfo::exists(final));
            QVERIFY(qmltest::WaitForVisible(m_app, "walletBackupOverwritePopup"));
            QVERIFY(qmltest::Click(m_app, "walletBackupOverwriteApproveButton"));
        }
        QTRY_COMPARE(operation->phase(), BackupFileOperation::Phase::Succeeded);
        QCOMPARE(operation->finalPath(), final); QVERIFY(QFileInfo{final}.size() > 0);
        if (rewritten) QVERIFY(!QFileInfo::exists(selected));
        UniValue restore_params{UniValue::VARR}; restore_params.push_back("approved_restored"); restore_params.push_back(final.toStdString());
        qmltest::Rpc(m_app, "restorewallet", restore_params);
        UniValue info{UniValue::VARR}; info.push_back(address.toStdString());
        QVERIFY(qmltest::Rpc(m_app, "getaddressinfo", info, "approved_restored").find_value("ismine").get_bool());
    }

    void backupRestoresAddressesAndRejectsInvalidFiles()
    {
        auto* wallet = qmltest::CreateWallet(m_app, "original");
        QVERIFY(wallet);
        const auto address = qmltest::NewAddress(m_app, wallet->name());
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto backup = directory.filePath(QString::fromUtf8("wallet backup ü.dat"));
        QVERIFY(wallet->backupWallet(backup));
        QTRY_COMPARE(wallet->backupOperation()->phase(), BackupFileOperation::Phase::Succeeded);
        QVERIFY(QFileInfo{backup}.size() > 0);
        auto* controller = qmltest::Model<WalletQmlController>(m_app, "walletController");
        QVERIFY(controller);
        controller->closeWallet("original");
        QTRY_VERIFY(!controller->isWalletLoaded());
        QSignalSpy imported{controller, &WalletQmlController::walletImportSucceeded};
        controller->importWallet(backup);
        QTRY_COMPARE_WITH_TIMEOUT(imported.count(), 1, 30'000);
        QVERIFY(controller->isWalletLoaded());
        UniValue params{UniValue::VARR};
        params.push_back(address.toStdString());
        QVERIFY(qmltest::Rpc(m_app, "getaddressinfo", params, controller->selectedWallet()->name()).find_value("ismine").get_bool());
        controller->importWallet(directory.filePath("missing.dat"));
        QTRY_VERIFY(!controller->walletLoadError().isEmpty());
        QCOMPARE(imported.count(), 1);
        QVERIFY(controller->isWalletLoaded());
    }

    void earlierBackupCoverageAfterEncryption_data()
    {
        QTest::addColumn<bool>("spend");
        QTest::newRow("later-receive") << false;
        QTest::newRow("actual-change") << true;
    }

    void earlierBackupCoverageAfterEncryption()
    {
        QFETCH(bool, spend);
        auto* miner = qmltest::CreateWallet(m_app, "recovery_miner");
        QVERIFY(miner);
        QVERIFY(qmltest::FundWallet(m_app, *miner));
        QPointer<WalletQmlModel> live{qmltest::CreateWallet(m_app, "recovery_live")};
        QVERIFY(live);
        const auto original_address = qmltest::NewAddress(m_app, live->name());
        const auto mining_address = qmltest::NewAddress(m_app, miner->name());
        auto mine = [&] {
            UniValue params{UniValue::VARR}; params.push_back(1); params.push_back(mining_address.toStdString());
            qmltest::Rpc(m_app, "generatetoaddress", params);
        };
        auto pay = [&](const QString& from, const QString& address, const char* amount) {
            UniValue params{UniValue::VOBJ};
            params.pushKV("address", address.toStdString());
            params.pushKV("amount", UniValue{UniValue::VNUM, amount});
            params.pushKV("fee_rate", 2);
            return qmltest::Rpc(m_app, "sendtoaddress", params, from).get_str();
        };
        auto balance = [&](const QString& name) {
            return ParseMoney(qmltest::Rpc(m_app, "getbalance", {}, name).getValStr());
        };
        auto owns = [&](const QString& name, const QString& address) {
            UniValue params{UniValue::VARR}; params.push_back(address.toStdString());
            return qmltest::Rpc(m_app, "getaddressinfo", params, name).find_value("ismine").get_bool();
        };
        pay(miner->name(), original_address, "1.0"); mine();
        QCOMPARE(balance(live->name()), std::optional<CAmount>{100'000'000});
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto old_backup = directory.filePath("before-encryption.bak");
        const auto new_backup = directory.filePath("after-encryption.bak");
        QVERIFY(live->backupWallet(old_backup));
        QTRY_COMPARE(live->backupOperation()->phase(), BackupFileOperation::Phase::Succeeded);
        QFile old_file{old_backup}; QVERIFY(old_file.open(QIODevice::ReadOnly));
        const auto old_bytes = old_file.readAll(); old_file.close(); QVERIFY(!old_bytes.isEmpty());
        QVERIFY(live->encryptWallet("recovery password"));
        QVERIFY(live->isLocked());
        const auto later_address = qmltest::NewAddress(m_app, live->name());
        QString actual_change;
        if (spend) {
            qmltest::Rpc(m_app, "walletpassphrase", qmltest::Json("[\"recovery password\",60]"), live->name());
            const auto txid = pay(live->name(), mining_address, "0.25");
            qmltest::Rpc(m_app, "walletlock", {}, live->name());
            UniValue tx_params{UniValue::VARR}; tx_params.push_back(txid);
            const auto transaction = qmltest::Rpc(m_app, "gettransaction", tx_params, live->name());
            UniValue decode{UniValue::VARR}; decode.push_back(transaction.find_value("hex").get_str());
            const auto decoded = qmltest::Rpc(m_app, "decoderawtransaction", decode);
            for (const auto& output : decoded.find_value("vout").getValues()) {
                const auto address = QString::fromStdString(output.find_value("scriptPubKey").find_value("address").get_str());
                if (owns(live->name(), address)) {
                    QVERIFY(actual_change.isEmpty()); actual_change = address;
                }
            }
            QVERIFY(!actual_change.isEmpty());
        } else {
            pay(miner->name(), later_address, "0.5");
        }
        mine();
        const auto live_balance = balance(live->name());
        QVERIFY(live_balance && *live_balance > 0);
        if (!spend) QCOMPARE(*live_balance, CAmount{150'000'000});
        else QVERIFY(*live_balance < 100'000'000);
        QVERIFY(live->backupWallet(new_backup));
        QTRY_COMPARE(live->backupOperation()->phase(), BackupFileOperation::Phase::Succeeded);
        auto restore = [&](const QString& name, const QString& path) {
            UniValue params{UniValue::VARR}; params.push_back(name.toStdString()); params.push_back(path.toStdString());
            return qmltest::Rpc(m_app, "restorewallet", params).find_value("name").get_str();
        };
        QCOMPARE(restore("recovery_old", old_backup), std::string{"recovery_old"});
        QCOMPARE(restore("recovery_fresh", new_backup), std::string{"recovery_fresh"});
        QVERIFY(owns("recovery_old", original_address));
        QVERIFY(!owns("recovery_old", later_address));
        QVERIFY(owns("recovery_fresh", later_address));
        if (spend) {
            QVERIFY(!owns("recovery_old", actual_change));
            QVERIFY(owns("recovery_fresh", actual_change));
        }
        QCOMPARE(balance("recovery_old"), std::optional<CAmount>{spend ? 0 : 100'000'000});
        QCOMPARE(balance("recovery_fresh"), live_balance);
        QCOMPARE(balance("recovery_live"), live_balance); // The live wallet has not lost these funds.
        QVERIFY(live);
        auto* controller = qmltest::Model<WalletQmlController>(m_app, "walletController");
        QVERIFY(controller);
        controller->setSelectedWallet("recovery_live");
        QCOMPARE(qmltest::WaitForWallet(m_app, "recovery_live"), live.data());
        QVERIFY(old_file.open(QIODevice::ReadOnly)); QCOMPARE(old_file.readAll(), old_bytes);
        qInfo() << "Controlled recovery" << (spend ? "actual change" : "later receive")
                << "old backup" << *balance("recovery_old") << "fresh backup and preserved live wallet" << *live_balance;
    }

    void encryptedBackupRestoresLockedAndRetainsItsSigningKeys()
    {
        auto* wallet = qmltest::CreateWallet(m_app, "encrypted_source");
        QVERIFY(wallet);
        const auto address = qmltest::NewAddress(m_app, wallet->name(), "legacy");
        QVERIFY(wallet->encryptWallet("backup password"));
        QTRY_VERIFY(wallet->isLocked());
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto backup = directory.filePath("encrypted_restore.bak");
        QVERIFY(wallet->backupWallet(backup));
        QTRY_COMPARE(wallet->backupOperation()->phase(), BackupFileOperation::Phase::Succeeded);
        auto* controller = qmltest::Model<WalletQmlController>(m_app, "walletController");
        QVERIFY(controller);
        controller->closeWallet(wallet->name());
        QTRY_VERIFY(!controller->isWalletLoaded());
        QTRY_COMPARE(qmltest::Rpc(m_app, "listwallets").size(), 0U);
        controller->importWallet(backup);
        auto* restored = qmltest::WaitForWallet(m_app, "encrypted_restore");
        QVERIFY(restored);
        QVERIFY(restored->isEncrypted());
        QVERIFY(restored->isLocked());
        auto* signer = restored->signVerifyMessageModel();
        QVERIFY(signer->signMessageWithPassphrase(address, "restored wallet", "backup password"));
        QVERIFY(signer->verifyMessage(address, "restored wallet", signer->signature()));
        QVERIFY(restored->isLocked());
    }
};

std::unique_ptr<QObject> CreateWalletTests(BitcoinQmlApplication& app, WalletMode)
{
    return std::make_unique<WalletIntegrationTests>(app);
}

#include <test_wallet.moc>
