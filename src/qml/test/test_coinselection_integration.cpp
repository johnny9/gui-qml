// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/bitcoinqmlapplication.h>
#include <qml/applicationrouter.h>
#include <qml/wallet/walletmanager.h>
#include <qml/wallet/walletsendmodel.h>
#include <qml/wallet/walletsession.h>
#include <qml/wallet/sendpreview.h>
#include <qml/test/integration_test_registry.h>
#include <qml/test/wallet_test_fixture.h>
#include <key_io.h>
#include <qml/bitcoinunits.h>
#include <univalue.h>
#include <QAbstractItemModelTester>
#include <QQmlApplicationEngine>
#include <QSignalSpy>
#include <QPointer>
#include <QTest>

class CoinSelectionIntegrationTests : public QObject
{
    Q_OBJECT
    BitcoinQmlApplication& m_app;
public:
    explicit CoinSelectionIntegrationTests(BitcoinQmlApplication& app) : m_app(app) {}
private Q_SLOTS:
    void coinControlsLoadForBoundWallet()
    {
        WalletTestFixture fixture(m_app.node());
        auto backend = fixture.create();
        auto& manager = *m_app.walletManager();
        const QString name = QString::fromStdString(backend->getWalletName());
        QTRY_VERIFY_WITH_TIMEOUT(manager.selectedWallet() && manager.selectedWallet()->overview()->name() == name, 10'000);
        QSignalSpy warnings(&m_app.engine(), &QQmlEngine::warnings);
        QVERIFY(m_app.router().navigate("wallet/send", {{"sessionId", manager.selectedWallet()->sessionId()}}));
        auto* root = m_app.engine().rootObjects().constFirst();
        QTRY_VERIFY_WITH_TIMEOUT(root->findChild<QObject*>("sendManualCoins"), 5'000);
        QCOMPARE(warnings.count(), 0);
        QVERIFY(manager.selectedWallet()->send()->coins()->active());
        QPointer<WalletViewModel> owned = manager.selectedWallet();
        QVERIFY(m_app.router().navigate("node"));
        m_app.engine().collectGarbage();
        QVERIFY(owned); // Returning a facade to QML must not transfer ownership.
        QVERIFY(!manager.selectedWallet()->send()->coins()->active());
    }

    void selectedOnlyLocksAndExternalSpendsAgreeWithCore()
    {
        WalletTestFixture fixture(m_app.node());
        auto backend = fixture.create();
        fixture.fund(*backend, 20 * COIN, 2);
        WalletOperationExecutor executor;
        WalletSession session(backend, 601, executor);
        WalletSendModel send(session, m_app.node().getDustRelayFee());
        auto& coins = *send.coins();
        QAbstractItemModelTester consistent(&coins, QAbstractItemModelTester::FailureReportingMode::QtTest);
        QTRY_VERIFY_WITH_TIMEOUT(coins.rowCount() >= 2, 10'000);
        const QString key = coins.data(coins.index(0), CoinSelectionModel::CoinKeyRole).toString();
        coins.select(key, true);
        QCOMPARE(coins.selectedCount(), 1);
        QVERIFY(coins.manual());
        const auto outpoint = coins.selected()[0];
        send.recipients()->setAddress(0, QString::fromStdString(EncodeDestination(WitnessV0KeyHash(uint160{}))));
        send.recipients()->setMaximum(0, true);
        QTRY_VERIFY_WITH_TIMEOUT(send.fees()->affordable(), 10'000);
        const auto draft = send.snapshot();
        const auto control = CoinControlForDraft(draft, true);
        QVERIFY(!control.m_allow_other_inputs);
        const auto outputs = backend->getCoins({outpoint});
        QCOMPARE(coins.data(coins.index(0), CoinSelectionModel::AmountRole).toString(),
                 QString(QmlBitcoinUnits::format(QmlBitcoinUnits::Unit::BTC, outputs[0].txout.nValue) + " BTC"));
        QCOMPARE(backend->getAvailableBalance(control), outputs[0].txout.nValue);
        const auto tx = backend->createTransaction(RecipientsForDraft(draft, *backend), control, false, std::nullopt);
        QVERIFY(tx);
        QCOMPARE(tx->tx->vin.size(), size_t{1});
        QCOMPARE(tx->tx->vin[0].prevout, outpoint);

        // An external lock invalidates selection without broadening its policy.
        QVERIFY(backend->lockCoin(outpoint, false));
        coins.refresh();
        QTRY_COMPARE(coins.selectedCount(), 0);
        QVERIFY(coins.manual());
        QTRY_VERIFY_WITH_TIMEOUT(!send.fees()->pending(), 10'000);
        QVERIFY(!send.fees()->affordable());
        coins.select(key, true);
        QCOMPARE(coins.selectedCount(), 0);
        coins.setLocked(key, false);
        QTRY_VERIFY(!coins.busy());
        QVERIFY(!backend->isLockedCoin(outpoint));
        coins.select(key, true);
        QCOMPARE(coins.selectedCount(), 1);
        coins.setLocked(key, true);
        coins.setLocked(key, false); // Repeated actions cannot race the first lock.
        QTRY_VERIFY(!coins.busy());
        QVERIFY(backend->isLockedCoin(outpoint));
        QCOMPARE(coins.selectedCount(), 0);
        coins.setLocked(key, false);
        QTRY_VERIFY(!coins.busy());
        coins.select(key, true);
        QCOMPARE(coins.selectedCount(), 1);

        // A transaction created outside this UI removes the selected input.
        auto spend = backend->createTransaction(RecipientsForDraft(send.snapshot(), *backend), CoinControlForDraft(send.snapshot(), false), true, std::nullopt);
        QVERIFY(spend);
        backend->commitTransaction(spend->tx, {});
        coins.refresh();
        QTRY_COMPARE_WITH_TIMEOUT(coins.selectedCount(), 0, 10'000);
        QVERIFY(coins.manual());
        coins.setManual(false);
        QVERIFY(!send.snapshot().selected_only);
        session.invalidate();
        QCOMPARE(coins.rowCount(), 0);
        QVERIFY(!coins.active());
    }
};

BITCOINQML_REGISTER_WALLET_INTEGRATION_TEST(CoinSelectionIntegrationTests)
#include <test_coinselection_integration.moc>
