// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/bitcoinqmlapplication.h>
#include <qml/applicationrouter.h>
#include <qml/wallet/bumpfeemodel.h>
#include <qml/wallet/psbtmodel.h>
#include <qml/wallet/walletsendmodel.h>
#include <qml/wallet/walletsession.h>
#include <qml/wallet/walletmanager.h>
#include <qml/test/integration_test_registry.h>
#include <qml/test/psbt_test_fixture.h>
#include <qml/test/wallet_test_fixture.h>
#include <QSemaphore>
#include <QSignalSpy>
#include <QTest>
#include <QQmlApplicationEngine>
#include <atomic>

class TransactionWorkflowTests : public QObject
{
    Q_OBJECT
    BitcoinQmlApplication& m_app;
    QString m_original_route;
    QVariantMap m_original_parameters;
public:
    explicit TransactionWorkflowTests(BitcoinQmlApplication& app) : m_app(app) {}
private Q_SLOTS:
    void init()
    {
        m_original_route = m_app.router().currentRoute();
        m_original_parameters = m_app.router().currentParameters();
    }
    void cleanup()
    {
        QVERIFY(m_app.router().navigate(m_original_route, m_original_parameters));
    }

    void selectedWalletChangesDoNotRetargetAnOpenPsbtPage()
    {
        WalletTestFixture fixture(m_app.node());
        auto original = fixture.create();
        auto other = fixture.create();
        auto& manager = *m_app.walletManager();
        auto listed = [&](const std::string& name) {
            for (int row = 0; row < manager.catalog()->rowCount(); ++row) {
                const auto index = manager.catalog()->index(row);
                if (manager.catalog()->data(index, WalletListModel::NameRole).toString() == QString::fromStdString(name))
                    return manager.catalog()->data(index, WalletListModel::LoadedRole).toBool();
            }
            return false;
        };
        QTRY_VERIFY(listed(original->getWalletName()) && listed(other->getWalletName()) && !manager.busy());
        manager.selectWallet(QString::fromStdString(original->getWalletName()));
        QTRY_VERIFY(manager.selectedWallet() && manager.selectedWallet()->overview()->name() == QString::fromStdString(original->getWalletName()));
        auto* original_model = manager.selectedWallet()->psbt();
        const auto session_id = manager.selectedWallet()->sessionId();
        QVERIFY(m_app.router().navigate("wallet-psbt", {{"sessionId", session_id}}));
        auto* root = m_app.engine().rootObjects().constFirst();
        QTRY_VERIFY(root->findChild<QObject*>("walletPsbtPage"));
        auto* page = root->findChild<QObject*>("walletPsbtPage");
        QCOMPARE(page->property("psbt").value<QObject*>(), original_model);
        manager.selectWallet(QString::fromStdString(other->getWalletName()));
        QTRY_VERIFY(manager.selectedWallet() && manager.selectedWallet()->overview()->name() == QString::fromStdString(other->getWalletName()));
        // Updating destination availability can rebuild the page. Its primitive
        // route parameters must still resolve the original model, not selection.
        QTRY_VERIFY(root->findChild<QObject*>("walletPsbtPage") &&
                    root->findChild<QObject*>("walletPsbtPage")->property("psbt").value<QObject*>() == original_model);
        QCOMPARE(m_app.router().currentParameters().value("sessionId").toString(), session_id);
        QVERIFY(manager.selectedWallet()->psbt() != original_model);
        QVERIFY(m_app.router().navigate("wallets"));
    }

    void allActionsShareTheOriginalSessionsExclusiveGuard()
    {
        WalletTestFixture fixture(m_app.node());
        auto backend = fixture.create();
        fixture.fund(*backend);
        QTRY_VERIFY(backend->getBalance() > 0);
        WalletOperationExecutor executor;
        WalletSession session(backend, 641, executor);
        WalletSendModel send(session, m_app.node().getDustRelayFee());
        PsbtModel psbt(session, m_app.node());
        BumpFeeModel bump(session, m_app.node());
        psbt.importData(FundedPsbt(m_app.node(), *backend));
        QTRY_VERIFY(!psbt.busy() && !send.coins()->busy());
        QVERIFY(psbt.canSign());
        send.recipients()->setAddress(0, QString::fromStdString(EncodeDestination(WitnessV0KeyHash(uint160{}))));
        send.recipients()->setAmount(0, "1");
        QVERIFY(send.canPrepare());
        const auto revision = psbt.revision();
        const auto bytes = psbt.document()->serialized();
        QSemaphore release;
        std::atomic<bool> entered{false};
        QVERIFY(session.runAction([&](interfaces::Wallet&) {
            entered = true;
            release.acquire();
            return WalletOperationResult{};
        }, [](WalletOperationResult) {}));
        const bool started = QTest::qWaitFor([&] { return entered.load(); }, 5'000);
        const bool all_busy = send.busy() && psbt.busy() && bump.busy();
        const bool send_queued = send.prepare("");
        psbt.sign("");
        psbt.submit();
        bump.inspect(QString(64, QLatin1Char('a')));
        bump.prepare("10");
        bump.submit("");
        release.release(); // Release before assertions can return from this case.
        QVERIFY(started);
        QVERIFY(all_busy);
        QVERIFY(!send_queued);
        QTRY_VERIFY(!session.actionBusy());
        QCOMPARE(psbt.revision(), revision);
        QCOMPARE(psbt.document()->serialized(), bytes);
        QVERIFY(!psbt.complete());
        QVERIFY(!send.review()->hasReview());
        QVERIFY(!bump.review()->hasReview());
        QVERIFY(!bump.accepted());
    }

    void unloadAndShutdownDiscardQueuedResultsAcrossEveryWorkflow()
    {
        WalletTestFixture fixture(m_app.node());
        auto backend = fixture.create();
        fixture.fund(*backend);
        QTRY_VERIFY(backend->getBalance() > 0);
        const auto bytes = FundedPsbt(m_app.node(), *backend);
        WalletOperationExecutor executor;
        WalletSession session(backend, 642, executor);
        WalletSendModel send(session, m_app.node().getDustRelayFee());
        PsbtModel psbt(session, m_app.node());
        BumpFeeModel bump(session, m_app.node());
        QTRY_VERIFY(!send.coins()->busy());
        send.recipients()->setAddress(0, QString::fromStdString(EncodeDestination(WitnessV0KeyHash(uint160{}))));
        send.recipients()->setAmount(0, "1");
        QVERIFY(send.canPrepare());
        QSemaphore release;
        std::atomic<bool> entered{false};
        QVERIFY(executor.submit([&] {
            entered = true;
            release.acquire();
            return WalletOperationResult{};
        }, [](WalletOperationResult) {}));
        const bool started = QTest::qWaitFor([&] { return entered.load(); }, 5'000);
        psbt.importData(bytes);
        bump.inspect(QString(64, QLatin1Char('a')));
        const bool preparing = send.prepare("");
        backend->remove();
        session.invalidate();
        QSignalSpy drained(&executor, &WalletOperationExecutor::drained);
        executor.drain();
        const bool accepted_after_drain = executor.submit([] { return WalletOperationResult{}; }, [](WalletOperationResult) {});
        release.release();
        QVERIFY(started);
        QVERIFY(preparing);
        QVERIFY(!accepted_after_drain);
        QTRY_COMPARE_WITH_TIMEOUT(drained.size(), 1, 10'000);
        QVERIFY(!session.available());
        QVERIFY(!session.actionBusy());
        QVERIFY(!send.available());
        QVERIFY(!send.review()->hasReview());
        QVERIFY(!psbt.available());
        QVERIFY(!psbt.loaded());
        QVERIFY(!psbt.review()->hasReview());
        QVERIFY(!psbt.canSign());
        QVERIFY(!psbt.canSubmit());
        QVERIFY(!bump.eligible());
        QVERIFY(!bump.review()->hasReview());
        QVERIFY(!bump.canSubmit());
    }

    void clearingAQueuedImportCannotReviveAnOldDocument()
    {
        WalletTestFixture fixture(m_app.node());
        auto backend = fixture.create();
        fixture.fund(*backend);
        QTRY_VERIFY(backend->getBalance() > 0);
        const auto bytes = FundedPsbt(m_app.node(), *backend);
        WalletOperationExecutor executor;
        WalletSession session(backend, 643, executor);
        PsbtModel psbt(session, m_app.node());
        QSemaphore release;
        std::atomic<bool> entered{false};
        QVERIFY(executor.submit([&] {
            entered = true;
            release.acquire();
            return WalletOperationResult{};
        }, [](WalletOperationResult) {}));
        const bool started = QTest::qWaitFor([&] { return entered.load(); }, 5'000);
        psbt.importData(bytes);
        psbt.clear();
        const auto revision = psbt.revision();
        QSignalSpy drained(&executor, &WalletOperationExecutor::drained);
        executor.drain();
        release.release();
        QVERIFY(started);
        QTRY_COMPARE_WITH_TIMEOUT(drained.size(), 1, 10'000);
        QCOMPARE(psbt.revision(), revision);
        QVERIFY(!psbt.loaded());
        QVERIFY(!psbt.review()->hasReview());
        QVERIFY(!psbt.canSign());
        QVERIFY(!psbt.canSubmit());
    }
};

BITCOINQML_REGISTER_WALLET_INTEGRATION_TEST(TransactionWorkflowTests)
#include <test_transaction_workflows.moc>
