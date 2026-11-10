// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/psbtfeeservices.h>
#include <test/mocks/mocknode.h>
#include <test/mocks/manualexecutionenvironment.h>
#include <test/qt_test_registry.h>

#include <key.h>
#include <script/script.h>
#include <script/solver.h>

#include <QtTest/QtTest>

#include <limits>
#include <vector>

namespace {
PartiallySignedTransaction Amounts(const std::vector<CAmount>& input_values, const std::vector<CAmount>& output_values)
{
    CMutableTransaction previous;
    previous.vin.emplace_back(COutPoint{Txid::FromUint256(uint256::ONE), 0});
    for (auto value : input_values) previous.vout.emplace_back(value, CScript{} << OP_TRUE);
    const auto parent = MakeTransactionRef(previous);
    CMutableTransaction tx;
    for (size_t i = 0; i < input_values.size(); ++i) tx.vin.emplace_back(COutPoint{parent->GetHash(), uint32_t(i)});
    for (auto value : output_values) tx.vout.emplace_back(value, CScript{} << OP_TRUE);
    PartiallySignedTransaction psbt{tx};
    for (auto& input : psbt.inputs) input.non_witness_utxo = parent;
    return psbt;
}
PartiallySignedTransaction Witness(const CScript& script)
{
    auto psbt = Amounts({2000}, {1500});
    psbt.inputs[0].non_witness_utxo = nullptr;
    psbt.inputs[0].witness_utxo = CTxOut{2000, script};
    return psbt;
}
} // namespace

class PsbtFeeTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void malformedRpcReplySettlesAsFailure()
    {
        MockNode node;
        node.get_unspent_output_fn = [](const COutPoint&) { return std::optional<Coin>{}; };
        node.execute_rpc_fn = [](const std::string&, const UniValue&, const std::string&) { return UniValue{true}; };
        const auto document = std::make_shared<const PartiallySignedTransaction>(Witness(CScript{} << OP_TRUE));
        const auto seed = std::make_shared<const psbtfee::Analysis>(psbtfee::Analyze(*document));
        ManualExecutionEnvironment environment;
        bool published{false}, failed{false};
        psbtfee::Services services{environment.scheduler, psbtfee::NodeLookup(&node),
            [&environment](auto publication) { environment.post(std::move(publication)); }};
        QVERIFY(psbtfee::Request(services, document, seed, [&](psbtfee::Resolution result) {
            published = true; failed = result.failed; QVERIFY(!result.proof);
        }));
        environment.drain(); QVERIFY(!published);
        environment.publishAll(); QVERIFY(published); QVERIFY(failed); QVERIFY(environment.quiescent());
    }

    void mempoolLookupUsesPublicNodeInterface_data()
    {
        QTest::addColumn<QString>("amount"); QTest::addColumn<QString>("script"); QTest::addColumn<bool>("known");
        QTest::newRow("valid-mempool-output") << QString("0.00002000") << QString("51") << true;
        QTest::newRow("zero-value") << QString("0.00000000") << QString("51") << true;
        QTest::newRow("empty-script") << QString("0.00002000") << QString("") << true;
        QTest::newRow("invalid-money") << QString("bad") << QString("51") << false;
        QTest::newRow("invalid-script") << QString("0.00002000") << QString("zz") << false;
    }
    void mempoolLookupUsesPublicNodeInterface()
    {
        QFETCH(QString, amount); QFETCH(QString, script); QFETCH(bool, known);
        MockNode node;
        const COutPoint outpoint{Txid::FromUint256(uint256::ONE), 3};
        node.get_unspent_output_fn = [](const COutPoint&) { return std::optional<Coin>{}; };
        std::string called_method, called_uri;
        UniValue called_parameters;
        node.execute_rpc_fn = [&](const std::string& method, const UniValue& parameters, const std::string& uri) {
            called_method = method; called_parameters = parameters; called_uri = uri;
            UniValue output{UniValue::VOBJ}, pubkey{UniValue::VOBJ};
            output.pushKV("value", amount.toStdString());
            pubkey.pushKV("hex", script.toStdString()); output.pushKV("scriptPubKey", pubkey);
            return output;
        };
        const auto result = psbtfee::NodeLookup(&node)(outpoint);
        QCOMPARE(called_method, std::string("gettxout")); QCOMPARE(called_uri, std::string{});
        QCOMPARE(called_parameters.size(), size_t{3});
        QCOMPARE(called_parameters[0].get_str(), outpoint.hash.ToString());
        QCOMPARE(called_parameters[1].getInt<uint32_t>(), outpoint.n);
        QVERIFY(called_parameters[2].get_bool());
        QCOMPARE(result.has_value(), known);
        if (known) {
            QCOMPARE(result->nValue, amount == "0.00000000" ? CAmount{0} : CAmount{2000});
            QCOMPARE(result->scriptPubKey, script.isEmpty() ? CScript{} : CScript{} << OP_TRUE);
        }
        QCOMPARE(node.calls.getUnspentOutput.load(), 1); QCOMPARE(node.calls.executeRpc.load(), 1);
    }

    void malformedMetadataCannotMatchAReview()
    {
        auto document = Amounts({2000, 2000}, {3000});
        const auto proof = psbtfee::Analyze(document);
        document.inputs[0].witness_utxo = CTxOut{-2, CScript{} << OP_TRUE};
        QCOMPARE(psbtfee::Analyze(document).state, psbtfee::State::Invalid);
        QVERIFY(!psbtfee::MatchesReview(document, proof, CAmount{1000}));
        const auto prepared = psbtfee::Prepared(CTransaction{*document.GetUnsignedTx()}, 1000);
        QVERIFY(!psbtfee::MatchesReview(document, prepared, CAmount{1000}));
        document.inputs[0].witness_utxo.SetNull();
        document.inputs[0].height_locktime = 42;
        document.inputs[1].time_locktime = 500000001;
        QVERIFY(!psbtfee::MatchesReview(document, proof, CAmount{1000}));
    }
    void readerReusesIndependentProofAndSkipsUnnecessaryLookups()
    {
        const auto document = Amounts({2000, 2000}, {3000});
        const auto proof = psbtfee::Analyze(document);
        int reads{0};
        auto lookup = [&reads](const COutPoint&) -> std::optional<CTxOut> { ++reads; return std::nullopt; };
        const auto full = psbtfee::Resolve(document, {}, lookup);
        QCOMPARE(full.proof->amount, std::optional<CAmount>{1000});
        QCOMPARE(reads, 0);
        auto incomplete = document;
        incomplete.inputs[1].non_witness_utxo = nullptr;
        const psbtfee::Analysis partial = psbtfee::Analyze(incomplete);
        const auto unavailable = psbtfee::Resolve(incomplete, partial, lookup);
        QVERIFY(!unavailable.proof->amount); QCOMPARE(reads, 1);
        incomplete.inputs[0].non_witness_utxo = nullptr;
        const auto cached = psbtfee::Resolve(incomplete, proof, lookup);
        QCOMPARE(cached.proof->amount, std::optional<CAmount>{1000});
        QCOMPARE(reads, 1);
        incomplete.inputs[0].non_witness_utxo = document.inputs[0].non_witness_utxo;
        const auto partially_cached = psbtfee::Resolve(incomplete, proof, lookup);
        QCOMPARE(partially_cached.proof->amount, std::optional<CAmount>{1000});
        QCOMPARE(reads, 1);
        auto three = Amounts({2000, 2000, 2000}, {3000});
        psbtfee::Analysis one_cached;
        one_cached.verified.emplace(three.inputs[1].GetOutPoint(), three.inputs[1].non_witness_utxo->vout[1]);
        three.inputs[1].non_witness_utxo = three.inputs[2].non_witness_utxo = nullptr;
        const auto still_missing = psbtfee::Resolve(three, one_cached, lookup);
        QVERIFY(!still_missing.proof->amount); QCOMPARE(reads, 2);
    }

    void nodeLookupPreservesMissingAndSpent()
    {
        QVERIFY(!psbtfee::NodeLookup(nullptr));
        MockNode node;
        int state{0};
        node.get_unspent_output_fn = [&state](const COutPoint&) -> std::optional<Coin> {
            if (state == 0) return std::nullopt;
            if (state == 1) return Coin{};
            return Coin{CTxOut{2000, CScript{} << OP_TRUE}, 1, false};
        };
        node.execute_rpc_fn = [](const std::string&, const UniValue&, const std::string&) { return UniValue{}; };
        const auto lookup = psbtfee::NodeLookup(&node);
        const COutPoint outpoint{Txid::FromUint256(uint256::ONE), 0};
        QVERIFY(!lookup(outpoint)); state = 1;
        QVERIFY(!lookup(outpoint)); state = 2;
        QCOMPARE(lookup(outpoint)->nValue, CAmount{2000});
        QCOMPARE(node.calls.getUnspentOutput.load(), 3);
    }
    void missingServicePortsRejectAdmission()
    {
        ManualExecutionEnvironment environment;
        const auto document = std::make_shared<const PartiallySignedTransaction>(Amounts({2000}, {1500}));
        const auto seed = std::make_shared<const psbtfee::Analysis>(psbtfee::Analyze(*document));
        bool published{false};
        psbtfee::Services services;
        auto publish = [&](psbtfee::Resolution) { published = true; };
        QVERIFY(!psbtfee::Request(services, document, seed, publish));
        services.scheduler = environment.scheduler;
        QVERIFY(!psbtfee::Request(services, document, seed, publish));
        services.lookup = [](const COutPoint&) { return std::optional<CTxOut>{}; };
        QVERIFY(!psbtfee::Request(services, document, seed, publish));
        QVERIFY(!published);
        QVERIFY(environment.quiescent());
    }

    void reviewedFeeMustBePresentAndExact()
    {
        auto psbt = Amounts({2000}, {1500});
        const auto proof = psbtfee::Analyze(psbt);
        QVERIFY(!psbtfee::MatchesReview(psbt, proof, std::nullopt));
        QVERIFY(!psbtfee::MatchesReview(psbt, proof, CAmount{499}));
        QVERIFY(psbtfee::MatchesReview(psbt, proof, CAmount{500}));
        psbt.inputs[0].non_witness_utxo = nullptr;
        QVERIFY(psbtfee::MatchesReview(psbt, proof, CAmount{500})); // Retained independent proof.
        psbt.outputs[0].amount = 1400;
        QVERIFY(!psbtfee::MatchesReview(psbt, proof, CAmount{500}));
    }
    void checkedAmounts_data()
    {
        QTest::addColumn<qint64>("input"); QTest::addColumn<qint64>("output"); QTest::addColumn<bool>("known");
        QTest::newRow("normal") << qint64(2000) << qint64(1500) << true;
        QTest::newRow("zero-fee") << qint64(2000) << qint64(2000) << true;
        QTest::newRow("zero-values") << qint64(0) << qint64(0) << true;
        QTest::newRow("max-money") << qint64(MAX_MONEY) << qint64(MAX_MONEY) << true;
        QTest::newRow("negative-fee") << qint64(1500) << qint64(2000) << false;
        for (auto invalid : {qint64(-1), qint64(MAX_MONEY + 1), std::numeric_limits<qint64>::max(), std::numeric_limits<qint64>::min()}) {
            QTest::newRow(qPrintable(QString("input-%1").arg(invalid))) << invalid << qint64(1) << false;
            QTest::newRow(qPrintable(QString("output-%1").arg(invalid))) << qint64(2000) << invalid << false;
        }
    }
    void checkedAmounts()
    {
        QFETCH(qint64, input); QFETCH(qint64, output); QFETCH(bool, known);
        const auto result = psbtfee::Analyze(Amounts({input}, {output}));
        QCOMPARE(result.state, known ? psbtfee::State::Known : psbtfee::State::Invalid);
        QCOMPARE(result.amount.has_value(), known);
        if (known) QCOMPARE(*result.amount, input - output); // Independent exact arithmetic.
    }
    void invalidStructureAndAuthentication_data()
    {
        QTest::addColumn<int>("mutation");
        for (int i = 0; i < 9; ++i) QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void invalidStructureAndAuthentication()
    {
        QFETCH(int, mutation);
        auto psbt = Amounts({2000, 2000}, {3000});
        switch (mutation) {
        case 0: psbt.inputs.clear(); break;
        case 1: psbt.outputs.clear(); break;
        case 2: psbt.inputs[0].height_locktime = 42; psbt.inputs[1].time_locktime = 500000001; break;
        case 3: psbt.inputs[1].prev_out = 0; break;
        case 4: psbt.inputs[0].prev_txid = Txid::FromUint256(uint256::ONE); break;
        case 5: psbt.inputs[0].prev_out = 2; break;
        case 6: psbt.inputs[0].witness_utxo = CTxOut{2001, CScript{} << OP_TRUE}; break;
        case 7: psbt = Amounts({MAX_MONEY, MAX_MONEY}, {1}); break;
        case 8: psbt = Amounts({MAX_MONEY}, {MAX_MONEY, MAX_MONEY}); break;
        }
        QCOMPARE(psbtfee::Analyze(psbt).state, psbtfee::State::Invalid);
    }
    void missingAndMatchingMetadata()
    {
        auto psbt = Amounts({2000}, {1500});
        psbt.inputs[0].witness_utxo = psbt.inputs[0].non_witness_utxo->vout[0];
        QCOMPARE(psbtfee::Analyze(psbt).amount, std::optional<CAmount>{500});
        psbt.inputs[0].non_witness_utxo = nullptr;
        auto result = psbtfee::Analyze(psbt);
        QCOMPARE(result.state, psbtfee::State::Missing);
        QVERIFY(!result.amount); QVERIFY(!result.prevouts[0]);
        psbt.inputs[0].witness_utxo.SetNull();
        QCOMPARE(psbtfee::Analyze(psbt).state, psbtfee::State::Missing);
        psbt.outputs[0].amount = -1;
        QCOMPARE(psbtfee::Analyze(psbt).state, psbtfee::State::Invalid);
    }
    void witnessMetadataRequiresIndependentProof_data()
    {
        QTest::addColumn<int>("kind");
        for (int i = 0; i < 5; ++i) QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void witnessMetadataRequiresIndependentProof()
    {
        QFETCH(int, kind);
        const std::vector<CScript> scripts{
            CScript{} << OP_TRUE,
            CScript{} << OP_0 << std::vector<unsigned char>(20, 1),
            CScript{} << OP_0 << std::vector<unsigned char>(32, 1),
            CScript{} << OP_1 << std::vector<unsigned char>(32, 1),
            CScript{} << OP_2 << std::vector<unsigned char>(32, 1)};
        auto psbt = Witness(scripts[kind]);
        QCOMPARE(psbtfee::Analyze(psbt).state, psbtfee::State::Missing);
        const psbtfee::Outputs trusted{{psbt.inputs[0].GetOutPoint(), CTxOut{2000, scripts[kind]}}};
        const auto proof = psbtfee::Analyze(psbt, trusted);
        QCOMPARE(proof.amount, std::optional<CAmount>{500});
        psbt.inputs[0].witness_utxo.nValue = 1501;
        QCOMPARE(psbtfee::Analyze(psbt, trusted).state, psbtfee::State::Invalid);
        QVERIFY(!psbtfee::MatchesReview(psbt, proof, CAmount{500}));
        psbt.inputs[0].witness_utxo.SetNull();
        psbtfee::FillMissing(psbt, proof);
        QCOMPARE(psbt.inputs[0].witness_utxo, trusted.begin()->second);
        QVERIFY(psbtfee::MatchesReview(psbt, proof, CAmount{500}));
    }
    void trustedProofCannotContradictFullParent()
    {
        const auto psbt = Amounts({2000}, {1500});
        const psbtfee::Outputs mismatch{{psbt.inputs[0].GetOutPoint(), CTxOut{2001, CScript{} << OP_TRUE}}};
        QCOMPARE(psbtfee::Analyze(psbt, mismatch).state, psbtfee::State::Invalid);
    }
    void preparedFeeIsBoundToExactTransaction()
    {
        auto psbt = Amounts({2000}, {1500});
        const auto tx = CTransaction{*psbt.GetUnsignedTx()};
        const auto proof = psbtfee::Prepared(tx, 500);
        QVERIFY(psbtfee::MatchesReview(psbt, proof, CAmount{500}));
        psbt.inputs[0].non_witness_utxo = nullptr;
        QVERIFY(psbtfee::MatchesReview(psbt, proof, CAmount{500}));
        psbt.inputs[0].witness_utxo = CTxOut{1501, CScript{} << OP_TRUE};
        QVERIFY(!psbtfee::MatchesReview(psbt, proof, CAmount{500}));
        QVERIFY(!psbtfee::MatchesReview(psbt, proof, CAmount{1}));
        QVERIFY(!psbtfee::Prepared(tx, -1).amount);
        QVERIFY(!psbtfee::Prepared(tx, MAX_MONEY + 1).amount);
        psbt.outputs[0].amount = 1499;
        QVERIFY(!psbtfee::MatchesReview(psbt, proof, CAmount{500}));
    }
};
BITCOINQML_REGISTER_QT_TEST(PsbtFeeTests)
#include <test_psbtfee.moc>
