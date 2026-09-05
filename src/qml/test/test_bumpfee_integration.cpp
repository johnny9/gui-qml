// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/bitcoinqmlapplication.h>
#include <qml/wallet/bumpfeemodel.h>
#include <qml/wallet/psbtdocument.h>
#include <qml/wallet/transactionhistorymodel.h>
#include <qml/wallet/walletsession.h>
#include <qml/test/integration_test_registry.h>
#include <qml/test/psbt_test_fixture.h>
#include <qml/test/wallet_test_fixture.h>
#include <node/types.h>
#include <wallet/coincontrol.h>
#include <wallet/wallet.h>
#include <QTest>

class BumpFeeIntegrationTests : public QObject
{
    Q_OBJECT
    BitcoinQmlApplication& m_app;
    Txid initialTransaction(interfaces::Wallet& wallet, CAmount amount = COIN)
    {
        QString error;
        auto document = DecodePsbtDocument(FundedPsbt(m_app.node(), wallet, true, false, amount), error);
        if (!document) throw std::runtime_error("Invalid funded fixture");
        CMutableTransaction tx;
        if (!FinalizeAndExtractPSBT(document->current, tx)) throw std::runtime_error("Incomplete funded fixture");
        const auto transaction = MakeTransactionRef(tx);
        std::string reject;
        if (m_app.node().broadcastTransaction(transaction, wallet.getDefaultMaxTxFee(), reject) != node::TransactionError::OK)
            throw std::runtime_error(reject);
        return transaction->GetHash();
    }
public:
    explicit BumpFeeIntegrationTests(BitcoinQmlApplication& app) : m_app(app) {}
private Q_SLOTS:
    void encryptedWalletReviewsSignsAndRecordsCoreRelationships()
    {
        WalletTestFixture fixture(m_app.node());
        const SecureString password{"synthetic-bump-password"};
        auto wallet = fixture.create(password);
        fixture.fund(*wallet);
        QTRY_VERIFY(wallet->getBalance() > 0);
        QVERIFY(wallet->unlock(password));
        const auto original = initialTransaction(*wallet);
        QVERIFY(wallet->lock());
        QTRY_VERIFY(wallet->transactionCanBeBumped(original));
        WalletOperationExecutor executor;
        WalletSession session(wallet, 631, executor);
        BumpFeeModel model(session, m_app.node());
        TransactionHistoryModel history(session);
        model.inspect("not-a-transaction-id");
        QVERIFY(!model.eligible());
        QVERIFY(!model.error().isEmpty());
        model.inspect(QString::fromStdString(original.GetHex()));
        QTRY_VERIFY(!model.busy());
        QVERIFY(model.eligible());
        QVERIFY(!model.canSubmit());
        model.prepare("0");
        QVERIFY(!model.review()->hasReview());
        QVERIFY(!model.error().isEmpty());
        model.prepare("10");
        QTRY_VERIFY(!model.busy());
        QVERIFY2(model.canSubmit(), qPrintable(model.error()));
        const auto snapshot = *model.review()->snapshot();
        QVERIFY(snapshot.previous_fee);
        QVERIFY(snapshot.fee > *snapshot.previous_fee);
        QVERIFY(!model.feeIncrease().isEmpty());
        QVERIFY(wallet->isLocked());
        model.submit("wrong-synthetic-password");
        QTRY_VERIFY(!model.busy());
        QVERIFY(!model.accepted());
        QVERIFY(wallet->isLocked());
        QVERIFY(model.canSubmit());
        model.submit(QString::fromStdString(std::string(password.begin(), password.end())));
        model.submit("ignored-double-click");
        QTRY_VERIFY(!model.busy());
        QVERIFY2(model.accepted(), qPrintable(model.error()));
        QVERIFY(wallet->isLocked());
        QVERIFY(!model.canSubmit());
        const auto replacement = Txid::FromHex(model.replacementTransactionId().toStdString());
        QVERIFY(replacement);
        QCOMPARE(wallet->getWalletTx(original).replaced_by_txid, replacement);
        QCOMPARE(wallet->getWalletTx(*replacement).replaces_txid, std::optional<Txid>{original});
        QTRY_VERIFY(!history.keyForTransaction(model.replacementTransactionId()).isEmpty());
        const auto old_details = history.details(history.keyForTransaction(QString::fromStdString(original.GetHex())));
        QCOMPARE(old_details.value("replacedByTxid").toString(), model.replacementTransactionId());
        QCOMPARE(old_details.value("status").toString(), QStringLiteral("Replaced"));
        const auto new_details = history.details(history.keyForTransaction(model.replacementTransactionId()));
        QCOMPARE(new_details.value("replacesTxid").toString(), QString::fromStdString(original.GetHex()));
        QCOMPARE(new_details.value("feeSat").toLongLong(), snapshot.fee);
    }

    void confirmedOriginalInvalidatesTheReview()
    {
        WalletTestFixture fixture(m_app.node());
        auto wallet = fixture.create();
        fixture.fund(*wallet);
        QTRY_VERIFY(wallet->getBalance() > 0);
        const auto original = initialTransaction(*wallet);
        QTRY_VERIFY(wallet->transactionCanBeBumped(original));
        WalletOperationExecutor executor;
        WalletSession session(wallet, 632, executor);
        BumpFeeModel model(session, m_app.node());
        model.inspect(QString::fromStdString(original.GetHex()));
        QTRY_VERIFY(!model.busy());
        model.prepare("10");
        QTRY_VERIFY(!model.busy());
        QVERIFY(model.canSubmit());
        const auto address = wallet->getNewDestination(OutputType::BECH32, "confirmation-race");
        QVERIFY(address);
        UniValue args(UniValue::VARR);
        args.push_back(1);
        args.push_back(EncodeDestination(*address));
        m_app.node().executeRpc("generatetoaddress", args, "");
        QTRY_VERIFY(!wallet->transactionCanBeBumped(original));
        model.submit();
        QTRY_VERIFY(!model.busy());
        QVERIFY(!model.accepted());
        QVERIFY(!model.canSubmit());
        QVERIFY(!model.review()->hasReview());
        QVERIFY(!wallet->getWalletTx(original).replaced_by_txid);
    }

    void externalReplacementCannotSubmitAnOlderCandidate()
    {
        WalletTestFixture fixture(m_app.node());
        auto wallet = fixture.create();
        fixture.fund(*wallet);
        QTRY_VERIFY(wallet->getBalance() > 0);
        const auto original = initialTransaction(*wallet);
        QTRY_VERIFY(wallet->transactionCanBeBumped(original));
        WalletOperationExecutor executor;
        WalletSession session(wallet, 633, executor);
        BumpFeeModel model(session, m_app.node());
        model.inspect(QString::fromStdString(original.GetHex()));
        QTRY_VERIFY(!model.busy());
        model.prepare("10");
        QTRY_VERIFY(!model.busy());
        QVERIFY(model.canSubmit());
        wallet::CCoinControl control;
        control.m_feerate = CFeeRate{20'000};
        std::vector<bilingual_str> errors;
        CAmount old_fee{0}, new_fee{0};
        CMutableTransaction external;
        QVERIFY(wallet->createBumpTransaction(original, control, errors, old_fee, new_fee, external));
        QVERIFY(wallet->signBumpTransaction(external));
        Txid external_id;
        QVERIFY(wallet->commitBumpTransaction(original, std::move(external), errors, external_id));
        model.submit();
        QTRY_VERIFY(!model.busy());
        QVERIFY(!model.accepted());
        QVERIFY(!model.canSubmit());
        QCOMPARE(wallet->getWalletTx(original).replaced_by_txid, std::optional<Txid>{external_id});
    }

    void walletBroadcastPolicyIsNotOverridden()
    {
        WalletTestFixture fixture(m_app.node());
        auto wallet = fixture.create();
        fixture.fund(*wallet);
        QTRY_VERIFY(wallet->getBalance() > 0);
        const auto original = initialTransaction(*wallet);
        QTRY_VERIFY(wallet->transactionCanBeBumped(original));
        WalletOperationExecutor executor;
        WalletSession session(wallet, 634, executor);
        BumpFeeModel model(session, m_app.node());
        model.inspect(QString::fromStdString(original.GetHex()));
        QTRY_VERIFY(!model.busy());
        model.prepare("10");
        QTRY_VERIFY(!model.busy());
        QVERIFY(model.canSubmit());
        QVERIFY(wallet->wallet());
        wallet->wallet()->SetBroadcastTransactions(false);
        model.submit();
        QTRY_VERIFY(!model.busy());
        QVERIFY(!model.accepted());
        QVERIFY(model.error().contains("recorded in the wallet"));
        QVERIFY(!model.canSubmit());
        const auto replacement = Txid::FromHex(model.replacementTransactionId().toStdString());
        QVERIFY(replacement);
        QVERIFY(wallet->getTx(*replacement));
        interfaces::WalletTxStatus status{};
        std::vector<std::string> messages, requests;
        bool in_mempool{false};
        int blocks{0};
        wallet->getWalletTxDetails(*replacement, status, messages, requests, in_mempool, blocks);
        QVERIFY(!in_mempool);
    }

    void anAdditionalInputLockedAfterReviewRequiresPreparationAgain()
    {
        WalletTestFixture fixture(m_app.node());
        auto wallet = fixture.create();
        fixture.fund(*wallet, 20 * COIN, 2);
        QTRY_VERIFY(wallet->getBalance() >= 20 * COIN);
        std::vector<COutPoint> available;
        for (const auto& [address, coins] : wallet->listCoins())
            for (const auto& [outpoint, output] : coins) available.push_back(outpoint);
        QCOMPARE(available.size(), size_t{2});
        // Force the original to use exactly one UTXO, independently of Core's
        // change-avoidance selection preferences. The bump may then add the other.
        QVERIFY(wallet->lockCoin(available.back(), false));
        const auto original = initialTransaction(*wallet, 10 * COIN - 5'000);
        QVERIFY(wallet->unlockCoin(available.back()));
        QTRY_VERIFY(wallet->transactionCanBeBumped(original));
        QCOMPARE(wallet->getTx(original)->vin.size(), size_t{1});
        WalletOperationExecutor executor;
        WalletSession session(wallet, 635, executor);
        BumpFeeModel model(session, m_app.node());
        model.inspect(QString::fromStdString(original.GetHex()));
        QTRY_VERIFY(!model.busy());
        model.prepare("100");
        QTRY_VERIFY(!model.busy());
        QVERIFY2(model.canSubmit(), qPrintable(model.error()));
        const auto original_tx = wallet->getTx(original);
        const auto& inputs = model.review()->snapshot()->transaction->vin;
        const auto additional = std::ranges::find_if(inputs, [&](const auto& input) {
            return std::ranges::none_of(original_tx->vin, [&](const auto& old) { return input.prevout == old.prevout; });
        });
        QVERIFY(additional != inputs.end());
        QVERIFY(wallet->lockCoin(additional->prevout, false));
        const auto mempool = m_app.node().getMempoolSize();
        model.submit();
        QTRY_VERIFY(!model.busy());
        QVERIFY(!model.accepted());
        QVERIFY(!model.review()->hasReview());
        QVERIFY(model.error().contains("locked"));
        QVERIFY(!wallet->getWalletTx(original).replaced_by_txid);
        QCOMPARE(m_app.node().getMempoolSize(), mempool);
    }
};

BITCOINQML_REGISTER_WALLET_INTEGRATION_TEST(BumpFeeIntegrationTests)
#include <test_bumpfee_integration.moc>
