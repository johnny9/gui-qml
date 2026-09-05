// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/bitcoinqmlapplication.h>
#include <qml/test/integration_test_registry.h>
#include <qml/test/qt_test_registry.h>
#include <qml/wallet/walletcreationmodel.h>
#include <qml/wallet/walletmanager.h>
#include <interfaces/node.h>
#include <interfaces/wallet.h>

#include <QSignalSpy>
#include <QTest>
#include <QUuid>

class WalletNameTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void syntax()
    {
        for (const QString& invalid : {QString{}, QStringLiteral("  "), QStringLiteral(".."), QStringLiteral("a/b"), QStringLiteral("a\\b")})
            QVERIFY(!WalletCreationModel::nameSyntaxError(invalid).isEmpty());
        QVERIFY(WalletCreationModel::nameSyntaxError(QStringLiteral("My wallet")).isEmpty());
        QVERIFY(WalletCreationModel::nameSyntaxError(QString::fromUtf8("Épargne")).isEmpty());
    }
};
BITCOINQML_REGISTER_QT_TEST(WalletNameTests)

class WalletCreationTests : public QObject
{
    Q_OBJECT
public:
    explicit WalletCreationTests(BitcoinQmlApplication& app) : m_app(app) {}
private Q_SLOTS:
    void create_data()
    {
        QTest::addColumn<QString>("password");
        QTest::newRow("unencrypted") << QString{};
        QTest::newRow("encrypted") << QStringLiteral("test-only-secret");
    }
    void create()
    {
        QFETCH(QString, password);
        auto& manager = *m_app.walletManager();
        auto& creation = *manager.creation();
        const QString name = QStringLiteral("create-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
        m_created = name;
        QSignalSpy success(&creation, &WalletCreationModel::succeeded);
        QVERIFY(!creation.create(name, QStringLiteral("one"), QStringLiteral("two")));
        QCOMPARE(creation.resultCode(), int{WalletOperationResult::InvalidInput});
        QVERIFY(creation.create(name, password, password));
        QTRY_VERIFY_WITH_TIMEOUT(!creation.busy(), 20'000);
        QCOMPARE(creation.error(), QString{});
        QCOMPARE(success.size(), 1);
        QCOMPARE(manager.selectedWallet()->overview()->name(), name);
        QCOMPARE(manager.selectedWallet()->overview()->encrypted(), !password.isEmpty());
        QVERIFY(!creation.nameError(name.toUpper()).isEmpty());
        QVERIFY(!creation.create(name, {}, {}));
        QCOMPARE(success.size(), 1);
        int matches{0};
        for (int row = 0; row < manager.catalog()->rowCount(); ++row) {
            if (manager.catalog()->data(manager.catalog()->index(row), WalletListModel::NameRole).toString() == name) ++matches;
        }
        QCOMPARE(matches, 1);
    }
    void cleanup()
    {
        if (m_created.isEmpty()) return;
        auto& manager = *m_app.walletManager();
        QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(), 20'000);
        manager.closeWallet(m_created);
        QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(), 20'000);
        m_created.clear();
    }
private:
    BitcoinQmlApplication& m_app;
    QString m_created;
};
BITCOINQML_REGISTER_INTEGRATION_TEST(WalletCreationTests)

#include <test_wallet_creation.moc>
