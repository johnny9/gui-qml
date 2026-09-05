// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/bitcoinqmlapplication.h>
#include <qml/test/wallet_test_fixture.h>
#include <qml/test/integration_test_registry.h>
#include <qml/wallet/walletmanager.h>
#include <qml/wallet/walletsession.h>

#include <QSignalSpy>
#include <QQmlApplicationEngine>
#include <QTest>

class WalletCatalogTests : public QObject
{
    Q_OBJECT
public:
    explicit WalletCatalogTests(BitcoinQmlApplication& app) : m_app(app) {}
private Q_SLOTS:
    void overviewPageLoads()
    {
        auto* root = m_app.engine().rootObjects().constFirst();
        auto* runner = root->findChild<QObject*>(QStringLiteral("nodeRunner"));
        QVERIFY(runner);
        QVERIFY(QMetaObject::invokeMethod(runner, "walletsClicked"));
        QTRY_VERIFY_WITH_TIMEOUT(root->findChild<QObject*>(QStringLiteral("walletOverviewPage")), 2'000);
        auto* create = root->findChild<QObject*>(QStringLiteral("createWalletButton"));
        QVERIFY(create);
        QVERIFY(QMetaObject::invokeMethod(create, "clicked"));
        auto* dialog = root->findChild<QObject*>(QStringLiteral("createWalletDialog"));
        QVERIFY(dialog);
        QTRY_VERIFY_WITH_TIMEOUT(dialog->property("opened").toBool(), 2'000);
        QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
    }
    void externalLoadSelectionAndClose()
    {
        WalletTestFixture fixture(m_app.node());
        auto& manager = *m_app.walletManager();
        QVERIFY(manager.initialized());
        auto first = fixture.create();
        auto second = fixture.create();
        const QString first_name = QString::fromStdString(first->getWalletName());
        const QString second_name = QString::fromStdString(second->getWalletName());
        QTRY_VERIFY_WITH_TIMEOUT(manager.selectedWallet(), 10'000);
        QTRY_VERIFY_WITH_TIMEOUT(([&manager, &second_name] {
            for (int i = 0; i < manager.catalog()->rowCount(); ++i) {
                const auto index = manager.catalog()->index(i);
                if (manager.catalog()->data(index, WalletListModel::NameRole).toString() == second_name &&
                    manager.catalog()->data(index, WalletListModel::LoadedRole).toBool()) return true;
            }
            return false;
        }()), 10'000);
        manager.selectWallet(first_name);
        QCOMPARE(manager.selectedWallet()->overview()->name(), first_name);
        const auto first_id = manager.selectedWallet()->session().id();
        manager.selectWallet(second_name);
        QVERIFY(manager.selectedWallet()->session().id() != first_id);
        first.reset();
        manager.closeWallet(first_name);
        QCOMPARE(manager.selectedWallet()->overview()->name(), second_name);
        QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(), 10'000);
        manager.selectWallet(first_name);
        QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(), 10'000);
        QCOMPARE(manager.selectedWallet()->overview()->name(), first_name);
        QVERIFY(manager.selectedWallet()->session().id() != first_id);
        second->remove();
        second.reset();
        QCoreApplication::processEvents();
        QCOMPARE(manager.selectedWallet()->overview()->name(), first_name);
        manager.closeWallet(first_name);
        QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(), 10'000);
    }
    void loadThenUnloadBeforeDelivery()
    {
        WalletTestFixture fixture(m_app.node());
        auto wallet = fixture.create();
        const QString name = QString::fromStdString(wallet->getWalletName());
        wallet->remove();
        wallet.reset();
        QCoreApplication::processEvents();
        auto& manager = *m_app.walletManager();
        QVERIFY(!manager.selectedWallet() || manager.selectedWallet()->overview()->name() != name);
        for (int row = 0; row < manager.catalog()->rowCount(); ++row) {
            const auto index = manager.catalog()->index(row);
            if (manager.catalog()->data(index, WalletListModel::NameRole).toString() == name)
                QVERIFY(!manager.catalog()->data(index, WalletListModel::LoadedRole).toBool());
        }
    }
private:
    BitcoinQmlApplication& m_app;
};

BITCOINQML_REGISTER_WALLET_INTEGRATION_TEST(WalletCatalogTests)

#include <test_wallet_catalog.moc>
