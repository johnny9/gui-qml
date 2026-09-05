// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/bitcoinqmlapplication.h>
#include <qml/wallet/psbtmodel.h>
#include <qml/wallet/walletsession.h>
#include <qml/test/integration_test_registry.h>
#include <qml/test/psbt_test_fixture.h>
#include <qml/test/wallet_test_fixture.h>
#include <node/types.h>
#include <streams.h>
#include <QTest>

class PsbtSubmitIntegrationTests : public QObject
{
    Q_OBJECT
    BitcoinQmlApplication& m_app;
    static QByteArray serialize(const PartiallySignedTransaction& psbt)
    {
        DataStream stream;
        stream << psbt;
        return QByteArray::fromStdString(stream.str());
    }
public:
    explicit PsbtSubmitIntegrationTests(BitcoinQmlApplication& app) : m_app(app) {}
private Q_SLOTS:
    void negativeFeesNeverOfferSubmission()
    {
        WalletTestFixture fixture(m_app.node());
        auto source = fixture.create();
        fixture.fund(*source);
        QTRY_VERIFY(source->getBalance() > 0);
        QString error;
        const auto original = DecodePsbtDocument(FundedPsbt(m_app.node(), *source), error);
        QVERIFY(original);
        CAmount input_value{0};
        for (const auto& input : original->current.inputs) {
            CTxOut utxo;
            QVERIFY(input.GetUTXO(utxo));
            input_value += utxo.nValue;
        }
        auto transaction = *original->current.GetUnsignedTx();
        transaction.vout.resize(1);
        transaction.vout[0].nValue = input_value + 1;
        PartiallySignedTransaction negative_fee(transaction);
        negative_fee.inputs = original->current.inputs;
        bool complete{false};
        QVERIFY(!source->fillPSBT({.sign = true}, nullptr, negative_fee, complete));
        QVERIFY(complete);
        WalletOperationExecutor executor;
        WalletSession session(source, 624, executor);
        PsbtModel model(session, m_app.node());
        model.importData(serialize(negative_fee));
        QTRY_VERIFY(!model.busy());
        QVERIFY(model.complete());
        QVERIFY(model.review()->hasReview());
        QCOMPARE(model.review()->snapshot()->fee, CAmount{-1});
        QVERIFY(!model.canSubmit());
        QVERIFY(model.status().contains("Submission is unavailable"));
        const auto revision = model.revision();
        const auto mempool = m_app.node().getMempoolSize();
        model.submit();
        QVERIFY(!model.busy());
        QCOMPARE(model.revision(), revision);
        QCOMPARE(m_app.node().getMempoolSize(), mempool);
    }

    void completeForeignTransactionRequiresExplicitSubmission()
    {
        WalletTestFixture fixture(m_app.node());
        auto source = fixture.create();
        auto observer = fixture.create({}, wallet::WALLET_FLAG_DESCRIPTORS | wallet::WALLET_FLAG_DISABLE_PRIVATE_KEYS);
        fixture.fund(*source);
        QTRY_VERIFY(source->getBalance() > 0);
        const auto bytes = FundedPsbt(m_app.node(), *source, true);
        WalletOperationExecutor executor;
        WalletSession session(observer, 621, executor);
        PsbtModel model(session, m_app.node());
        model.importData(bytes);
        QTRY_VERIFY(!model.busy());
        QVERIFY(model.canSubmit());
        QVERIFY(!model.canSign());
        const auto transaction = model.review()->snapshot()->transaction;
        QVERIFY(!source->getTx(transaction->GetHash()));
        const auto mempool = m_app.node().getMempoolSize();
        model.submit();
        model.submit(); // A second click must not queue another action.
        QTRY_VERIFY(!model.busy());
        QVERIFY2(model.accepted(), qPrintable(model.error()));
        QVERIFY(!model.canSubmit());
        QVERIFY(!model.review()->hasReview());
        QTRY_VERIFY(source->getTx(transaction->GetHash()));
        QCOMPARE(m_app.node().getMempoolSize(), mempool + 1);
        QCOMPARE(model.document()->serialized(), bytes); // Submit did not re-sign.
        model.importData(bytes);
        QTRY_VERIFY(!model.busy());
        QVERIFY(model.known());
        QVERIFY(!model.canSubmit());
    }

    void coreRejectsANonFinalTransactionWithoutClaimingSuccess()
    {
        WalletTestFixture fixture(m_app.node());
        auto source = fixture.create();
        fixture.fund(*source);
        QTRY_VERIFY(source->getBalance() > 0);
        QString error;
        auto original = DecodePsbtDocument(FundedPsbt(m_app.node(), *source), error);
        QVERIFY(original);
        auto transaction = *original->current.GetUnsignedTx();
        transaction.nLockTime = 1'000'000;
        PartiallySignedTransaction non_final(transaction);
        non_final.inputs = original->current.inputs;
        non_final.outputs = original->current.outputs;
        bool complete{false};
        QVERIFY(!source->fillPSBT({.sign = true}, nullptr, non_final, complete));
        QVERIFY(complete);
        WalletOperationExecutor executor;
        WalletSession session(source, 622, executor);
        PsbtModel model(session, m_app.node());
        model.importData(serialize(non_final));
        QTRY_VERIFY(!model.busy());
        QVERIFY2(model.canSubmit(), qPrintable(model.error()));
        const auto mempool = m_app.node().getMempoolSize();
        model.submit();
        QTRY_VERIFY(!model.busy());
        QVERIFY(!model.accepted());
        QVERIFY(model.error().contains("Core rejected"));
        QVERIFY(!model.canSubmit());
        QCOMPARE(m_app.node().getMempoolSize(), mempool);
    }

    void spentInputsCannotPassAnOldReview()
    {
        WalletTestFixture fixture(m_app.node());
        auto source = fixture.create();
        fixture.fund(*source);
        QTRY_VERIFY(source->getBalance() > 0);
        QString error;
        auto original = DecodePsbtDocument(FundedPsbt(m_app.node(), *source), error);
        QVERIFY(original);
        auto reviewed = original->current;
        bool complete{false};
        QVERIFY(!source->fillPSBT({.sign = true}, nullptr, reviewed, complete));
        QVERIFY(complete);
        WalletOperationExecutor executor;
        WalletSession session(source, 623, executor);
        PsbtModel model(session, m_app.node());
        model.importData(serialize(reviewed));
        QTRY_VERIFY(!model.busy());
        QVERIFY(model.canSubmit());
        auto transaction = *original->current.GetUnsignedTx();
        transaction.vout[0].scriptPubKey = GetScriptForDestination(PKHash(uint160{}));
        PartiallySignedTransaction competing(transaction);
        competing.inputs = original->current.inputs;
        QVERIFY(!source->fillPSBT({.sign = true}, nullptr, competing, complete));
        QVERIFY(complete);
        QVERIFY(FinalizeAndExtractPSBT(competing, transaction));
        std::string reject;
        QCOMPARE(m_app.node().broadcastTransaction(MakeTransactionRef(transaction), source->getDefaultMaxTxFee(), reject), node::TransactionError::OK);
        const auto mempool = m_app.node().getMempoolSize();
        model.submit();
        QTRY_VERIFY(!model.busy());
        QVERIFY(!model.accepted());
        QVERIFY(!model.canSubmit());
        QVERIFY(!model.error().isEmpty());
        QCOMPARE(m_app.node().getMempoolSize(), mempool);
    }
};

BITCOINQML_REGISTER_WALLET_INTEGRATION_TEST(PsbtSubmitIntegrationTests)
#include <test_psbtsubmit_integration.moc>
