// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/bitcoinqmlapplication.h>
#include <qml/test/integration_test_registry.h>
#include <qml/test/wallet_test_fixture.h>
#include <qml/wallet/walletmanager.h>
#include <qml/wallet/walletpassphrase.h>
#include <qml/wallet/walletsession.h>

#include <QFileInfo>
#include <QSignalSpy>
#include <QTest>
#include <bitcoin-build-config.h>

class WalletSecurityTests : public QObject
{
    Q_OBJECT
public:
    explicit WalletSecurityTests(BitcoinQmlApplication& app) : m_app(app) {}
private Q_SLOTS:
    void existingNonstandardCapabilities_data()
    {
        QTest::addColumn<QString>("filename");
        QTest::addColumn<bool>("external_signer");
#ifdef ENABLE_EXTERNAL_SIGNER
        QTest::newRow("external-signer-without-device") << QStringLiteral("external-signer.bak") << true;
#endif
        QTest::newRow("multikey-watchonly") << QStringLiteral("multikey-watchonly.bak") << false;
    }
    void existingNonstandardCapabilities()
    {
        QFETCH(QString, filename);
        QFETCH(bool, external_signer);
        WalletTestFixture fixture(m_app.node());
        const auto backup = fixture.copyCanonicalBackup(filename);
        auto& manager = *m_app.walletManager();
        m_restored = QStringLiteral("capability-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
        auto& import_model = *manager.importModel();
        QVERIFY(import_model.restore(backup, m_restored));
        QTRY_VERIFY_WITH_TIMEOUT(!import_model.busy(), 20'000);
        QCOMPARE(import_model.error(), QString{});
        auto* view = manager.selectedWallet();
        QVERIFY(view);
        QCOMPARE(view->overview()->name(), m_restored);
        QVERIFY(view->overview()->available());
        QCOMPARE(view->session().wallet().hasExternalSigner(), external_signer);
        QVERIFY(view->session().wallet().privateKeysDisabled());
        QCOMPARE(view->overview()->canReceive(), external_signer);
        QCOMPARE(view->overview()->keyScheme(), external_signer ? QStringLiteral("External signer") : QStringLiteral("Watch-only descriptors"));
        QVERIFY(!view->overview()->localSigning());
        QVERIFY(!view->security()->supported());
        QVERIFY(!view->security()->encrypt(QStringLiteral("secret"), QStringLiteral("secret")));
        QVERIFY(!view->security()->error().isEmpty());
        QVERIFY(view->storage()->backup(fixture.backupPath()));
        QTRY_VERIFY_WITH_TIMEOUT(!view->storage()->busy(), 10'000);
        QCOMPARE(view->storage()->error(), QString{});
        QVERIFY(QFileInfo::exists(fixture.backupPath()));
    }
    void cleanup()
    {
        if (m_restored.isEmpty()) return;
        auto& manager = *m_app.walletManager();
        QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(), 20'000);
        manager.closeWallet(m_restored);
        QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(), 20'000);
        m_restored.clear();
    }
    void watchOnlyCapabilities()
    {
        WalletTestFixture fixture(m_app.node());
        auto backend = fixture.create({}, wallet::WALLET_FLAG_DESCRIPTORS | wallet::WALLET_FLAG_DISABLE_PRIVATE_KEYS);
        const QString name = QString::fromStdString(backend->getWalletName());
        auto& manager = *m_app.walletManager();
        QTRY_VERIFY_WITH_TIMEOUT(manager.selectedWallet() && manager.selectedWallet()->overview()->name() == name, 10'000);
        auto* view = manager.selectedWallet();
        QVERIFY(!view->overview()->localSigning());
        QVERIFY(!view->security()->supported());
        QVERIFY(!view->security()->encrypt(QStringLiteral("secret"), QStringLiteral("secret")));
        QVERIFY(view->storage()->backup(fixture.backupPath()));
        QTRY_VERIFY_WITH_TIMEOUT(!view->storage()->busy(), 10'000);
        QCOMPARE(view->storage()->error(), QString{});
        backend.reset();
        manager.closeWallet(name);
        QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(), 10'000);
    }
    void securityAndStorage()
    {
        WalletTestFixture fixture(m_app.node());
        auto backend = fixture.create();
        const QString name = QString::fromStdString(backend->getWalletName());
        auto& manager = *m_app.walletManager();
        QTRY_VERIFY_WITH_TIMEOUT(manager.selectedWallet() && manager.selectedWallet()->overview()->name() == name, 10'000);
        auto* view = manager.selectedWallet();
        auto* security = view->security();
        auto* storage = view->storage();
        QVERIFY(security->supported());
        QVERIFY(!security->encrypted());
        QVERIFY(!security->encrypt(QStringLiteral("one"), QStringLiteral("two")));
        QVERIFY(storage->setAlias(QStringLiteral("Savings")));
        QCOMPARE(view->overview()->displayName(), QStringLiteral("Savings"));
        QCOMPARE(view->overview()->name(), name);
        QVERIFY(QFileInfo::exists(storage->location()));
        QVERIFY(security->encrypt(QStringLiteral("test-passphrase"), QStringLiteral("test-passphrase")));
        QVERIFY(!storage->backup(fixture.backupPath()));
        QTRY_VERIFY_WITH_TIMEOUT(!security->busy(), 20'000);
        QCOMPARE(security->error(), QString{});
        QVERIFY(security->encrypted());
        QVERIFY(security->locked());
        QVERIFY(security->changePassphrase(QStringLiteral("wrong"), QStringLiteral("new-secret"), QStringLiteral("new-secret")));
        QTRY_VERIFY_WITH_TIMEOUT(!security->busy(), 20'000);
        QVERIFY(!security->error().isEmpty());
        QVERIFY(security->changePassphrase(QStringLiteral("test-passphrase"), QStringLiteral("new-secret"), QStringLiteral("new-secret")));
        QTRY_VERIFY_WITH_TIMEOUT(!security->busy(), 20'000);
        QCOMPARE(security->error(), QString{});
        QVERIFY(backend->unlock(WalletPassphrase(QStringLiteral("new-secret"))));
        QVERIFY(backend->lock());
        QVERIFY(storage->backup(fixture.backupPath()));
        QTRY_VERIFY_WITH_TIMEOUT(!storage->busy(), 10'000);
        QCOMPARE(storage->error(), QString{});
        QVERIFY(QFileInfo::exists(fixture.backupPath()));
        QVERIFY(!storage->backup(fixture.backupPath()));
        backend.reset();
        manager.closeWallet(name);
        QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(), 10'000);
        manager.selectWallet(name);
        QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(), 10'000);
        QCOMPARE(manager.selectedWallet()->overview()->displayName(), QStringLiteral("Savings"));
        manager.closeWallet(name);
        QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(), 10'000);
    }
private:
    BitcoinQmlApplication& m_app;
    QString m_restored;
};
BITCOINQML_REGISTER_INTEGRATION_TEST(WalletSecurityTests)

#include <test_wallet_security.moc>
