// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/applicationrouter.h>
#include <qml/bitcoinqmlapplication.h>
#include <qml/test/integration_test_registry.h>
#include <qml/test/wallet_test_fixture.h>
#include <qml/wallet/walletmanager.h>
#include <QQmlApplicationEngine>
#include <QPointer>
#include <QSignalSpy>
#include <QTest>

class WalletActivityIntegrationTests : public QObject
{
    Q_OBJECT
public:
    explicit WalletActivityIntegrationTests(BitcoinQmlApplication& app) : m_app{app} {}
private Q_SLOTS:
    void routesResolveOriginalSessionAndKeepCppOwnership()
    {
        WalletTestFixture fixture{m_app.node()};
        auto backend{fixture.create()};
        auto& manager{*m_app.walletManager()};
        const auto published = [&manager](const QString& name) {
            auto* catalog = manager.catalog();
            for (int row = 0; row < catalog->rowCount(); ++row) {
                const auto index = catalog->index(row);
                if (catalog->data(index, WalletListModel::NameRole).toString() == name)
                    return catalog->data(index, WalletListModel::LoadedRole).toBool();
            }
            return false;
        };
        const QString name{QString::fromStdString(backend->getWalletName())};
        // Earlier cases can leave queued unload notifications and persisted
        // catalog rows. Wait for this backend, not any selected wallet/row.
        QTRY_VERIFY_WITH_TIMEOUT(published(name), 10'000);
        manager.selectWallet(name);
        QTRY_VERIFY_WITH_TIMEOUT(manager.selectedWallet() && manager.selectedWallet()->overview()->name() == name, 10'000);
        QPointer<WalletViewModel> original{manager.selectedWallet()};
        const QString id{original->sessionId()};
        QTRY_VERIFY_WITH_TIMEOUT(original->receive()->available() && !original->receive()->busy(), 10'000);
        QVERIFY(original->receive()->save());
        QTRY_VERIFY_WITH_TIMEOUT(!original->receive()->busy() && original->activity()->count() == 1, 10'000);
        const QString row_key{original->activity()->rows().front().value("rowKey").toString()};
        QSignalSpy warnings{&m_app.engine(), &QQmlEngine::warnings};
        const QVariantMap params{{"sessionId", id}};
        for (const auto& route : {"wallet-activity", "wallet-transaction", "wallet-receive", "wallet-addresses", "wallet-address", "wallet-message"}) {
            auto route_params{params};
            if (QString::fromLatin1(route) == "wallet-transaction") route_params.insert("rowKey", row_key);
            QVERIFY(m_app.router().navigate(QString::fromLatin1(route), route_params));
            auto* host{m_app.engine().rootObjects().constFirst()->findChild<QObject*>("applicationPageHost")};
            QVERIFY(host);
            QTRY_VERIFY_WITH_TIMEOUT(host->property("item").value<QObject*>(), 5'000);
            QCOMPARE(host->property("status").toInt(), 1); // Loader.Ready, not a retained failed page.
            QCoreApplication::processEvents();
            QCOMPARE(host->property("item").value<QObject*>()->property("wallet").value<QObject*>(), original.data());
        }
        QVERIFY(m_app.router().navigate("node"));
        m_app.engine().collectGarbage();
        QVERIFY(original); // A Q_INVOKABLE return must not transfer manager ownership.
        QCOMPARE(manager.walletBySession(id), original.data());
        auto second{fixture.create()};
        const QString second_name{QString::fromStdString(second->getWalletName())};
        QTRY_VERIFY_WITH_TIMEOUT(published(second_name), 10'000);
        manager.selectWallet(second_name);
        QTRY_VERIFY_WITH_TIMEOUT(manager.selectedWallet() && manager.selectedWallet()->overview()->name() == second_name, 10'000);
        QVERIFY(m_app.router().navigate("wallet-activity", params));
        backend->remove();
        backend.reset();
        QTRY_VERIFY_WITH_TIMEOUT(!manager.walletBySession(id), 10'000);
        QCOMPARE(manager.selectedWallet()->overview()->name(), second_name);
        QVERIFY(m_app.router().navigate("node"));
        m_app.engine().collectGarbage();
        QCOMPARE(warnings.count(), 0);
    }
    void cleanup() { QVERIFY(m_app.router().navigate("node")); }
private:
    BitcoinQmlApplication& m_app;
};
BITCOINQML_REGISTER_WALLET_INTEGRATION_TEST(WalletActivityIntegrationTests)
#include <test_wallet_activity_integration.moc>
