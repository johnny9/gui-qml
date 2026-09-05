// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/bitcoinqmlapplication.h>
#include <qml/wallet/walletsendmodel.h>
#include <qml/wallet/walletsession.h>
#include <qml/wallet/walletpassphrase.h>
#include <qml/test/integration_test_registry.h>
#include <qml/test/wallet_test_fixture.h>
#include <key_io.h>
#include <univalue.h>
#include <QSemaphore>
#include <QTest>
#include <algorithm>
#include <atomic>

namespace {
/** Releases on every assertion exit, while worker-owned state stays alive. */
class PrepareBarrier
{
    struct State { QSemaphore release; std::atomic<bool> entered{false}; };
    std::shared_ptr<State> m_state{std::make_shared<State>()};
public:
    ~PrepareBarrier() { release(); }
    bool enqueue(WalletOperationExecutor& executor)
    {
        return executor.submit([state = m_state] {
            state->entered = true;
            state->release.acquire();
            return WalletOperationResult{};
        }, [](WalletOperationResult) {});
    }
    bool entered() const { return m_state->entered.load(); }
    void release() { m_state->release.release(); }
};
} // namespace

class SendPreparationIntegrationTests : public QObject
{
    Q_OBJECT
    BitcoinQmlApplication& m_app;
    bool initialStateReady(const WalletSession& session, WalletSendModel& send) const
    {
        // Funding and a failed constructor TRY_LOCK can still produce a later
        // activity notification. Settle that initial state before testing one
        // preparation; genuine activity must continue to invalidate reviews.
        return session.m_processed_tip == m_app.node().getBestBlockHash() &&
            !session.m_tip_read_pending && !send.coins()->busy();
    }
public:
    explicit SendPreparationIntegrationTests(BitcoinQmlApplication& app) : m_app(app) {}
private Q_SLOTS:
    void fixedAndMaximumRecipientsUseOnlySelectedInputs()
    {
        WalletTestFixture fixture(m_app.node());
        auto backend = fixture.create();
        fixture.fund(*backend, 9 * COIN, 3);
        WalletOperationExecutor executor;
        WalletSession session(backend, 702, executor);
        WalletSendModel send(session, m_app.node().getDustRelayFee());
        QTRY_VERIFY_WITH_TIMEOUT(initialStateReady(session, send), 10'000);
        auto& coins = *send.coins();
        QTRY_COMPARE_WITH_TIMEOUT(coins.rowCount(), 3, 10'000);
        for (int row = 0; row < 2; ++row)
            coins.select(coins.data(coins.index(row), CoinSelectionModel::CoinKeyRole).toString(), true);
        QCOMPARE(coins.selectedCount(), 2);
        QVERIFY(coins.manual());
        const auto selected = coins.selected();
        CAmount selected_value{0};
        for (const auto& coin : backend->getCoins(selected)) selected_value += coin.txout.nValue;
        QCOMPARE(selected_value, 6 * COIN);

        const QString fixed = QString::fromStdString(EncodeDestination(WitnessV0KeyHash(uint160{})));
        const QString maximum = QString::fromStdString(EncodeDestination(PKHash(uint160{})));
        send.recipients()->setAddress(0, fixed);
        send.recipients()->setAmount(0, "1");
        send.recipients()->add();
        send.recipients()->setAddress(1, maximum);
        send.recipients()->setMaximum(1, true);
        QVERIFY(send.snapshot().selected_only);
        const auto before = backend->getWalletTxs().size();
        QVERIFY(send.prepare(""));
        QTRY_VERIFY_WITH_TIMEOUT(send.review()->hasReview(), 10'000);
        const auto reviewed = *send.review()->snapshot();
        QVERIFY(reviewed.fee > 0);
        QCOMPARE(reviewed.transaction->vin.size(), selected.size());
        for (const auto& input : reviewed.transaction->vin)
            QVERIFY(std::ranges::find(selected, input.prevout) != selected.end());
        QCOMPARE(reviewed.transaction->vout.size(), size_t{2});
        QCOMPARE(reviewed.outputs.size(), size_t{2});
        CAmount fixed_value{0}, maximum_value{0};
        for (size_t i = 0; i < reviewed.outputs.size(); ++i) {
            const auto& output = reviewed.outputs[i];
            QCOMPARE(output.amount, reviewed.transaction->vout[i].nValue);
            QCOMPARE(GetScriptForDestination(DecodeDestination(output.address.toStdString())), reviewed.transaction->vout[i].scriptPubKey);
            QVERIFY(!output.change);
            if (output.address == fixed) fixed_value += output.amount;
            else if (output.address == maximum) maximum_value += output.amount;
            else QFAIL("Maximum spend unexpectedly created another output");
        }
        QCOMPARE(fixed_value, COIN);
        QCOMPARE(maximum_value, selected_value - COIN - reviewed.fee);
        QCOMPARE(reviewed.transaction->GetValueOut() + reviewed.fee, selected_value);
        QCOMPARE(send.snapshot().recipients[0].amount, COIN);
        QVERIFY(send.snapshot().recipients[1].maximum);
        QCOMPARE(backend->getWalletTxs().size(), before); // Preparation does not consume any coin.
        QCOMPARE(backend->getBalances().balance, 9 * COIN);

        // An unchanged processed tip is not a wallet edit. In particular, the
        // first poll after initial child reads must not clear a fresh review.
        QVERIFY(QMetaObject::invokeMethod(&session, "pollProcessedTip", Q_ARG(bool, false)));
        const auto polled = std::make_shared<std::atomic<bool>>(false);
        QVERIFY(session.runRead([](interfaces::Wallet&) { return WalletOperationResult{}; },
                                [polled](WalletOperationResult) { *polled = true; }));
        QTRY_VERIFY_WITH_TIMEOUT(polled->load(), 5'000);
        QVERIFY(send.review()->hasReview());
        QCOMPARE(send.review()->snapshot()->transaction->GetWitnessHash(), reviewed.transaction->GetWitnessHash());
    }

    void pendingPreparationCannotReviveAnUnavailableSession_data()
    {
        QTest::addColumn<bool>("unload");
        QTest::newRow("session-invalidated") << false;
        QTest::newRow("core-wallet-unloaded") << true;
    }

    void pendingPreparationCannotReviveAnUnavailableSession()
    {
        QFETCH(bool, unload);
        WalletTestFixture fixture(m_app.node());
        const auto secret = WalletPassphrase("synthetic-late-prepare");
        auto backend = fixture.create(secret);
        fixture.fund(*backend);
        WalletOperationExecutor executor;
        WalletSession session(backend, 703, executor);
        WalletSendModel send(session, m_app.node().getDustRelayFee());
        QTRY_VERIFY_WITH_TIMEOUT(initialStateReady(session, send), 10'000);
        send.recipients()->setAddress(0, QString::fromStdString(EncodeDestination(WitnessV0KeyHash(uint160{}))));
        send.recipients()->setAmount(0, "1");
        const auto before = backend->getWalletTxs().size();
        PrepareBarrier barrier;
        QVERIFY(barrier.enqueue(executor));
        QTRY_VERIFY_WITH_TIMEOUT(barrier.entered(), 5'000);
        QVERIFY(send.prepare("synthetic-late-prepare"));
        QVERIFY(session.actionBusy());
        QVERIFY(!send.review()->hasReview());
        const auto generation = session.generation();
        if (unload) backend->remove();
        else session.invalidate();
        QTRY_VERIFY_WITH_TIMEOUT(!session.available(), 5'000);
        QVERIFY(session.generation() > generation);
        QVERIFY(!send.canPrepare());

        // This completion is queued after the accepted preparation, proving
        // the blocked Core action and its GUI completion both ran before checking.
        const auto completed = std::make_shared<std::atomic<bool>>(false);
        QVERIFY(executor.submit([] { return WalletOperationResult{}; },
                                [completed](WalletOperationResult) { *completed = true; }));
        barrier.release();
        QTRY_VERIFY_WITH_TIMEOUT(completed->load(), 10'000);
        QVERIFY(!session.actionBusy());
        QVERIFY(!send.review()->hasReview());
        QVERIFY(!send.canPrepare());
        QVERIFY(backend->isLocked());
        QCOMPARE(backend->getWalletTxs().size(), before);
    }

    void exactReviewScopedUnlockAndRevisionInvalidation()
    {
        WalletTestFixture fixture(m_app.node());
        const auto secret = WalletPassphrase("synthetic-send-test");
        auto backend = fixture.create(secret);
        QVERIFY(backend->isLocked());
        fixture.fund(*backend);
        WalletOperationExecutor executor;
        WalletSession session(backend, 701, executor);
        WalletSendModel send(session, m_app.node().getDustRelayFee());
        QTRY_VERIFY_WITH_TIMEOUT(initialStateReady(session, send), 10'000);
        const auto first = WitnessV0KeyHash(uint160{});
        const auto second = PKHash(uint160{});
        send.recipients()->setAddress(0, QString::fromStdString(EncodeDestination(first)));
        send.recipients()->setAmount(0, "1");
        send.recipients()->setSubtractFee(0, true);
        send.recipients()->add();
        send.recipients()->setAddress(1, QString::fromStdString(EncodeDestination(second)));
        send.recipients()->setAmount(1, "2");
        QVERIFY(send.canPrepare());
        QVERIFY(send.needsPassphrase());
        const auto wallet_tx_count = backend->getWalletTxs().size();
        QVERIFY(send.prepare("incorrect"));
        QVERIFY(!send.prepare("synthetic-send-test"));
        QTRY_VERIFY_WITH_TIMEOUT(!send.busy(), 10'000);
        QVERIFY(!send.review()->hasReview());
        QVERIFY(send.error().contains("password"));
        QVERIFY(backend->isLocked());
        QCOMPARE(backend->getWalletTxs().size(), wallet_tx_count);

        QVERIFY(send.prepare("synthetic-send-test"));
        QTRY_VERIFY_WITH_TIMEOUT(send.review()->hasReview(), 10'000);
        QVERIFY(backend->isLocked()); // The review never holds an unlock context.
        const auto reviewed = *send.review()->snapshot();
        QVERIFY(reviewed.fee > 0);
        QVERIFY(std::ranges::any_of(reviewed.transaction->vin, [](const auto& input) { return !input.scriptWitness.IsNull(); }));
        CAmount inputs{0};
        std::vector<COutPoint> outpoints;
        for (const auto& input : reviewed.transaction->vin) outpoints.push_back(input.prevout);
        for (const auto& output : backend->getCoins(outpoints)) inputs += output.txout.nValue;
        QCOMPARE(reviewed.fee, inputs - reviewed.transaction->GetValueOut());
        QCOMPARE(reviewed.outputs.size(), reviewed.transaction->vout.size());
        for (size_t i = 0; i < reviewed.outputs.size(); ++i) {
            QCOMPARE(reviewed.outputs[i].amount, reviewed.transaction->vout[i].nValue);
            QCOMPARE(GetScriptForDestination(DecodeDestination(reviewed.outputs[i].address.toStdString())), reviewed.transaction->vout[i].scriptPubKey);
            if (reviewed.outputs[i].address == QString::fromStdString(EncodeDestination(first)))
                QCOMPARE(reviewed.outputs[i].amount, COIN - reviewed.fee);
            if (reviewed.outputs[i].address == QString::fromStdString(EncodeDestination(second)))
                QCOMPARE(reviewed.outputs[i].amount, 2 * COIN);
        }
        QCOMPARE(backend->getWalletTxs().size(), wallet_tx_count); // Preparation does not commit.
        const auto revision = send.revision();
        send.review()->setDisplayUnit(3);
        send.fees()->setDisplayUnit(3);
        QCOMPARE(send.revision(), revision);
        QCOMPARE(send.review()->snapshot()->transaction, reviewed.transaction);
        send.fees()->setTarget(6);
        QVERIFY(!send.review()->hasReview());

        // Completion cannot publish a transaction prepared from an older form.
        QVERIFY(send.prepare("synthetic-send-test"));
        send.recipients()->setAmount(1, "3");
        QTRY_VERIFY_WITH_TIMEOUT(!send.busy(), 10'000);
        QVERIFY(!send.review()->hasReview());
        QVERIFY(backend->isLocked());
        QCOMPARE(send.snapshot().recipients[1].amount, 3 * COIN);

        // Preserve an originally unlocked state too.
        QVERIFY(backend->unlock(secret));
        QCoreApplication::processEvents();
        QVERIFY(send.prepare(""));
        QTRY_VERIFY_WITH_TIMEOUT(send.review()->hasReview(), 10'000);
        QVERIFY(!backend->isLocked());
        backend->lock();
        QTRY_VERIFY(!send.review()->hasReview());
        send.recipients()->setAmount(1, "21000000");
        QVERIFY(!send.canPrepare()); // Aggregate money-range validation.
        send.recipients()->setAmount(1, "1000");
        QVERIFY(send.prepare("synthetic-send-test"));
        QTRY_VERIFY_WITH_TIMEOUT(!send.busy(), 10'000);
        QVERIFY(!send.review()->hasReview());
        QVERIFY(!send.error().isEmpty());
        QVERIFY(backend->isLocked());
        session.invalidate();
        QVERIFY(!send.canPrepare());
        QVERIFY(!send.review()->hasReview());
    }
};

BITCOINQML_REGISTER_WALLET_INTEGRATION_TEST(SendPreparationIntegrationTests)
#include <test_sendprepare_integration.moc>
