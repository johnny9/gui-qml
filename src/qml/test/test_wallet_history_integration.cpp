// Copyright (c) 2024-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/bitcoinqmlapplication.h>
#include <qml/test/integration_test_registry.h>
#include <qml/test/wallet_test_fixture.h>
#include <qml/wallet/transactionhistorymodel.h>
#include <qml/wallet/walletsession.h>
#include <key_io.h>
#include <univalue.h>
#include <wallet/wallet.h>
#include <QSignalSpy>
#include <QSemaphore>
#include <QTest>

class WalletHistoryIntegrationTests : public QObject
{
    Q_OBJECT
public:
    explicit WalletHistoryIntegrationTests(BitcoinQmlApplication& app) : m_node{app.node()} {}
private Q_SLOTS:
    void initialProcessedTipDoesNotInventAnActivityChange()
    {
        WalletTestFixture fixture{m_node};
        WalletOperationExecutor executor;
        auto backend{fixture.create()};
        const auto destination{backend->getNewDestination(OutputType::BECH32, "baseline fixture")};
        QVERIFY(destination);
        UniValue mine{UniValue::VARR};
        mine.push_back(1);
        mine.push_back(EncodeDestination(*destination));
        m_node.executeRpc("generatetoaddress", mine, "");
        QTRY_COMPARE_WITH_TIMEOUT(backend->getWalletTxs().size(), size_t{1}, 5'000);
        WalletSession session{backend, 4003, executor};
        session.m_tip_timer.stop();
        QSignalSpy activity{&session, &WalletSession::activityChanged};
        QSignalSpy changed{&session, &WalletSession::changed};
        TransactionHistoryModel history{session};
        QVERIFY(drainReads(session)); // Baseline and initial history snapshot precede this barrier.
        QCOMPARE(activity.count(), 0);
        QCOMPARE(changed.count(), 0);
        QCOMPARE(history.count(), 1);
        // Initial TRY_LOCK can lose to the application's other wallet reads.
        // Control this case's clock and establish a successful baseline before
        // testing same-tip silence; the separate case covers failed baselines.
        QVERIFY(QTest::qWaitFor([&] {
            if (!session.m_processed_tip.IsNull()) return true;
            session.pollProcessedTip(true);
            return drainReads(session) && !session.m_processed_tip.IsNull();
        }, 5'000));
        QCOMPARE(activity.count(), 0);
        QCOMPARE(changed.count(), 0);
        QVERIFY(QMetaObject::invokeMethod(&session, "pollProcessedTip", Q_ARG(bool, false)));
        QVERIFY(drainReads(session));
        QCOMPARE(activity.count(), 0);
        QCOMPARE(changed.count(), 0);

        UniValue aging{UniValue::VARR};
        aging.push_back(1);
        aging.push_back("bcrt1qqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqq3xueyj");
        const auto blocks{m_node.executeRpc("generatetoaddress", aging, "")};
        const auto expected_tip = uint256::FromHex(blocks[0].get_str());
        QVERIFY(expected_tip);
        QTRY_VERIFY_WITH_TIMEOUT(processedTip(*backend) == expected_tip, 5'000);
        QVERIFY(QTest::qWaitFor([&] {
            session.pollProcessedTip();
            return drainReads(session) && activity.count() > 0;
        }, 5'000));
        QTRY_COMPARE_WITH_TIMEOUT(history.records().front().confirmations, 2, 5'000);
    }

    void failedInitialTipReadDoesNotHideTheNextSuccessfulPoll()
    {
        WalletTestFixture fixture{m_node};
        WalletOperationExecutor executor;
        auto backend{fixture.create()};
        auto* context = m_node.walletLoader().context();
        QVERIFY(context);
        const auto core_wallet = wallet::GetWallet(*context, backend->getWalletName());
        QVERIFY(core_wallet);
        std::unique_ptr<WalletSession> session;
        std::unique_ptr<QSignalSpy> activity;
        bool initial_read_finished{false};
        {
            // The worker uses TRY_LOCK, so its constructor baseline must fail
            // while this lock is held. Do not dispatch GUI events under the
            // wallet lock: queued wallet-manager callbacks can take other locks.
            LOCK(core_wallet->cs_wallet);
            session = std::make_unique<WalletSession>(backend, 4004, executor);
            session->m_tip_timer.stop();
            activity = std::make_unique<QSignalSpy>(session.get(), &WalletSession::activityChanged);
            const auto barrier = std::make_shared<QSemaphore>();
            QVERIFY(executor.submit([barrier] {
                barrier->release();
                return WalletOperationResult{};
            }, [](WalletOperationResult) {}));
            initial_read_finished = barrier->tryAcquire(1, 5'000);
        }
        QVERIFY(initial_read_finished);
        QVERIFY(drainReads(*session));
        // Releasing our lock does not guarantee the next TRY_LOCK succeeds:
        // the application-owned wallet models may be reading concurrently.
        // Observe retries only after their worker/read-completion barrier.
        QVERIFY(QTest::qWaitFor([&] {
            if (!activity->isEmpty()) return true;
            if (!QMetaObject::invokeMethod(session.get(), "pollProcessedTip", Q_ARG(bool, false))) return false;
            return drainReads(*session) && !activity->isEmpty();
        }, 5'000));
        QCOMPARE(activity->count(), 1); // Consumers may have read data after the failed baseline.
    }

    void realWalletNotificationsAndReloadAgree()
    {
        WalletTestFixture fixture{m_node};
        WalletOperationExecutor executor;
        auto backend{fixture.create()};
        WalletSession session{backend, 4001, executor};
        TransactionHistoryModel history{session};
        const auto destination{backend->getNewDestination(OutputType::BECH32, "history fixture")};
        QVERIFY(destination);
        UniValue params{UniValue::VARR};
        params.push_back(1);
        params.push_back(EncodeDestination(*destination));
        m_node.executeRpc("generatetoaddress", params, "");
        QTRY_COMPARE_WITH_TIMEOUT(history.count(), 1, 10'000);
        const auto before{history.records().front()};
        QCOMPARE(before.kind, TransactionRecord::Generated);
        QCOMPARE(before.address, QString::fromStdString(EncodeDestination(*destination)));
        QVERIFY(before.credit > 0);
        QCOMPARE(before.label, QStringLiteral("history fixture"));
        QSignalSpy reloaded{&history, &TransactionHistoryModel::recordsChanged};
        history.reload();
        QTRY_VERIFY_WITH_TIMEOUT(!reloaded.isEmpty(), 10'000);
        QCOMPARE(history.records().front().key(), before.key());
        QCOMPARE(history.records().front().credit, before.credit);
        QVERIFY(!session.actionBusy());
        UniValue aging_params{UniValue::VARR};
        aging_params.push_back(2);
        aging_params.push_back("bcrt1qqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqq3xueyj");
        m_node.executeRpc("generatetoaddress", aging_params, "");
        QTRY_VERIFY_WITH_TIMEOUT(history.records().front().confirmations >= 3, 10'000);
        session.invalidate();
        const auto count{history.count()};
        history.reload();
        QCOMPARE(history.count(), count);
    }
private:
    static bool drainReads(WalletSession& session)
    {
        const auto finished = std::make_shared<bool>(false);
        if (!session.runRead([](interfaces::Wallet&) { return WalletOperationResult{}; },
                             [finished](WalletOperationResult) { *finished = true; })) return false;
        return QTest::qWaitFor([finished] { return *finished; }, 5'000);
    }
    static std::optional<uint256> processedTip(interfaces::Wallet& wallet)
    {
        interfaces::WalletBalances ignored;
        uint256 tip;
        return wallet.tryGetBalances(ignored, tip) ? std::optional{tip} : std::nullopt;
    }
    interfaces::Node& m_node;
};
BITCOINQML_REGISTER_WALLET_INTEGRATION_TEST(WalletHistoryIntegrationTests)
#include <test_wallet_history_integration.moc>
