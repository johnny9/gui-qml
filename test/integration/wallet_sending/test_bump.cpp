// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/integration/runner/test_cases.h>
#include <test/integration/fixture/test_wallet.h>
#include <test/mocks/messagebumptest.h>
#include <rpc/util.h>
#include <set>

namespace {
std::set<std::string> Mempool(BitcoinQmlApplication& app)
{
    std::set<std::string> ids;
    const auto mempool = qmltest::Rpc(app, "getrawmempool");
    for (const auto& id : mempool.getValues()) ids.insert(id.get_str());
    return ids;
}
QString SendReplaceable(BitcoinQmlApplication& app, WalletQmlModel& wallet, const QString& destination)
{
    UniValue params{UniValue::VOBJ};
    params.pushKV("address", destination.toStdString());
    params.pushKV("amount", UniValue(UniValue::VNUM, "0.01"));
    params.pushKV("replaceable", true);
    params.pushKV("fee_rate", 1);
    return QString::fromStdString(qmltest::Rpc(app, "sendtoaddress", params, wallet.name()).get_str());
}
QString SpendWholeCoin(BitcoinQmlApplication& app, const QString& wallet, const UniValue& coin, const QString& address)
{
    UniValue input{UniValue::VOBJ};
    input.pushKV("txid", coin.find_value("txid")); input.pushKV("vout", coin.find_value("vout"));
    input.pushKV("sequence", int64_t{0xfffffffd});
    UniValue inputs{UniValue::VARR}; inputs.push_back(input);
    UniValue outputs{UniValue::VOBJ}; outputs.pushKV(address.toStdString(), coin.find_value("amount"));
    UniValue create{UniValue::VARR}; create.push_back(inputs); create.push_back(outputs);
    const auto raw = qmltest::Rpc(app, "createrawtransaction", create);
    UniValue options{UniValue::VOBJ}; options.pushKV("add_inputs", false); options.pushKV("fee_rate", 1);
    UniValue subtract{UniValue::VARR}; subtract.push_back(0); options.pushKV("subtractFeeFromOutputs", subtract);
    UniValue fund{UniValue::VARR}; fund.push_back(raw); fund.push_back(options);
    const auto funded = qmltest::Rpc(app, "fundrawtransaction", fund, wallet);
    UniValue sign{UniValue::VARR}; sign.push_back(funded.find_value("hex"));
    const auto signed_transaction = qmltest::Rpc(app, "signrawtransactionwithwallet", sign, wallet);
    if (!signed_transaction.find_value("complete").get_bool()) throw std::runtime_error("fixture signature incomplete");
    UniValue send{UniValue::VARR}; send.push_back(signed_transaction.find_value("hex"));
    return QString::fromStdString(qmltest::Rpc(app, "sendrawtransaction", send).get_str());
}
} // namespace

class FeeBumpIntegrationTests : public QObject
{
    Q_OBJECT
    BitcoinQmlApplication& m_app;
public:
    explicit FeeBumpIntegrationTests(BitcoinQmlApplication& app) : m_app(app) {}
private Q_SLOTS:
    void independentSpendOfAddedInputCannotReplaceTheUnrelatedPayment()
    {
        auto* destination = qmltest::CreateWallet(m_app, "added_input_destination"); QVERIFY(destination);
        auto* wallet = qmltest::CreateWallet(m_app, "added_input_sender"); QVERIFY(wallet);
        QVERIFY(qmltest::FundWallet(m_app, *wallet));
        const auto mining = qmltest::NewAddress(m_app, wallet->name());
        qmltest::Rpc(m_app, "generatetoaddress", qmltest::Json("[1,\"" + mining.toStdString() + "\"]"));
        const auto coins = qmltest::Rpc(m_app, "listunspent", qmltest::Json("[101]"), wallet->name());
        QCOMPARE(coins.size(), size_t{2});
        const auto address = qmltest::NewAddress(m_app, destination->name());
        const auto original = SpendWholeCoin(m_app, wallet->name(), coins[0], address);
        const auto remaining = qmltest::Rpc(m_app, "listunspent", qmltest::Json("[101]"), wallet->name());
        QCOMPARE(remaining.size(), size_t{1});
        auto* bump = wallet->bumpModel(); QVERIFY(bump);
        QVERIFY2(AwaitBumpPreparation(*bump, original, 1), qPrintable(bump->errorText()));
        const auto preparation = bump->preparationOperation()->result(); QVERIFY(preparation && preparation->value);
        const auto review = preparation->value->review; QVERIFY(review);
        QCOMPARE(review->added_inputs.size(), size_t{1});
        QCOMPARE(review->added_inputs.front().hash.GetHex(), remaining[0].find_value("txid").get_str());
        QCOMPARE(review->added_inputs.front().n, remaining[0].find_value("vout").getInt<uint32_t>());
        const auto unrelated = SpendWholeCoin(m_app, wallet->name(), remaining[0], address);
        const auto before = Mempool(m_app); QVERIFY(before.contains(original.toStdString())); QVERIFY(before.contains(unrelated.toStdString()));
        QVERIFY(!AwaitBumpCommit(*bump));
        QCOMPARE(bump->state(), BumpTransactionModel::NeedsRevalidation);
        const auto outcome = bump->commitResult(); QVERIFY(outcome); QVERIFY(!outcome->committed);
        QCOMPARE(outcome->error, walletbump::Error::Ineligible);
        QCOMPARE(Mempool(m_app), before);
    }
    void futureLockRemainsInReviewAndIndependentAbandonmentInvalidatesIt()
    {
        auto* destination = qmltest::CreateWallet(m_app, "abandon_destination"); QVERIFY(destination);
        auto* wallet = qmltest::CreateWallet(m_app, "abandon_sender"); QVERIFY(wallet);
        QVERIFY(qmltest::FundWallet(m_app, *wallet));
        const int height = qmltest::Rpc(m_app, "getblockcount").getInt<int>();
        UniValue outputs{UniValue::VOBJ};
        outputs.pushKV(qmltest::NewAddress(m_app, destination->name()).toStdString(), UniValue{UniValue::VNUM, "0.01"});
        UniValue options{UniValue::VOBJ}; options.pushKV("locktime", height + 100);
        UniValue params{UniValue::VOBJ}; params.pushKV("outputs", outputs); params.pushKV("fee_rate", 1); params.pushKV("options", options);
        const auto sent = qmltest::Rpc(m_app, "send", params, wallet->name());
        QVERIFY(sent.find_value("complete").get_bool());
        const auto original = QString::fromStdString(sent.find_value("txid").get_str());
        QVERIFY(!Mempool(m_app).contains(original.toStdString()));
        auto* bump = wallet->bumpModel(); QVERIFY(bump);
        QVERIFY2(AwaitBumpPreparation(*bump, original, 1), qPrintable(bump->errorText()));
        const auto preparation = bump->preparationOperation()->result(); QVERIFY(preparation && preparation->value);
        const auto review = preparation->value->review; QVERIFY(review);
        QCOMPARE(review->transaction.nLockTime, uint32_t(height + 100));
        qmltest::Rpc(m_app, "abandontransaction", qmltest::Json("[\"" + original.toStdString() + "\"]"), wallet->name());
        QVERIFY(!AwaitBumpCommit(*bump));
        QCOMPARE(bump->state(), BumpTransactionModel::NeedsRevalidation);
        const auto outcome = bump->commitResult(); QVERIFY(outcome); QVERIFY(!outcome->committed);
        QCOMPARE(outcome->error, walletbump::Error::Ineligible);
        QVERIFY(Mempool(m_app).empty());
    }
    void committedReplacementPreservesPaymentAndIsObservedByRpc()
    {
        auto* destination = qmltest::CreateWallet(m_app, "bump_destination"); QVERIFY(destination);
        const auto address = qmltest::NewAddress(m_app, destination->name());
        auto* wallet = qmltest::CreateWallet(m_app, "bump_sender"); QVERIFY(wallet);
        QVERIFY(qmltest::FundWallet(m_app, *wallet));
        const auto original = SendReplaceable(m_app, *wallet, address);
        QVERIFY(Mempool(m_app).contains(original.toStdString()));
        auto* bump = wallet->bumpModel(); QVERIFY(bump);
        QVERIFY2(AwaitBumpPreparation(*bump, original, 1), qPrintable(bump->errorText()));
        QCOMPARE(bump->oldTxid(), original);
        const auto attempt = bump->attemptId();
        const auto commit = bump->requestConfirmFeeBump(); QVERIFY(commit);
        QVERIFY(!bump->requestConfirmFeeBump());
        QTRY_VERIFY(!bump->busy());
        QCOMPARE(bump->attemptId(), attempt); QCOMPARE(bump->commitId(), commit);
        QCOMPARE(bump->state(), BumpTransactionModel::Succeeded);
        const auto replacement = bump->newTxid(); QVERIFY(!replacement.isEmpty()); QVERIFY(replacement != original);
        const auto outcome = bump->commitResult(); QVERIFY(outcome); QVERIFY(outcome->committed);
        QCOMPARE(QString::fromStdString(outcome->original.GetHex()), original);
        QCOMPARE(QString::fromStdString(outcome->txid.GetHex()), replacement);
        QCOMPARE(outcome->record, walletbump::Record::Recorded);
        QCOMPARE(outcome->submission, walletbump::Submission::Mempool);
        QCOMPARE(outcome->relay, walletbump::Relay::Unknown);
        QCOMPARE(bump->walletRecordState(), BumpTransactionModel::WalletRecordRecorded);
        QCOMPARE(bump->walletSubmissionState(), BumpTransactionModel::WalletSubmissionMempool);
        QCOMPARE(bump->relayState(), BumpTransactionModel::RelayUnknown);
        const auto mempool = Mempool(m_app);
        QVERIFY(mempool.contains(replacement.toStdString())); QVERIFY(!mempool.contains(original.toStdString()));
        const auto transaction = qmltest::Rpc(m_app, "gettransaction", qmltest::Json("[\"" + replacement.toStdString() + "\"]"), wallet->name());
        QCOMPARE(transaction.find_value("replaces_txid").get_str(), original.toStdString());
        const auto previous = qmltest::Rpc(m_app, "gettransaction", qmltest::Json("[\"" + original.toStdString() + "\"]"), wallet->name());
        QCOMPARE(previous.find_value("replaced_by_txid").get_str(), replacement.toStdString());
        const auto decoded = qmltest::Rpc(m_app, "decoderawtransaction", qmltest::Json("[\"" + transaction.find_value("hex").get_str() + "\"]"));
        int matched{0};
        for (const auto& output : decoded.find_value("vout").getValues()) {
            const auto& script = output.find_value("scriptPubKey");
            if (script.find_value("address").isStr() && script.find_value("address").get_str() == address.toStdString()) {
                QCOMPARE(AmountFromValue(output.find_value("value")), CAmount{1'000'000}); ++matched;
            }
        }
        QCOMPARE(matched, 1);
    }
    void independentConfirmationInvalidatesReviewedReplacement()
    {
        auto* destination = qmltest::CreateWallet(m_app, "confirmed_destination"); QVERIFY(destination);
        const auto address = qmltest::NewAddress(m_app, destination->name());
        auto* wallet = qmltest::CreateWallet(m_app, "confirmed_sender"); QVERIFY(wallet);
        QVERIFY(qmltest::FundWallet(m_app, *wallet));
        const auto original = SendReplaceable(m_app, *wallet, address);
        auto* bump = wallet->bumpModel(); QVERIFY(bump);
        QVERIFY2(AwaitBumpPreparation(*bump, original, 1), qPrintable(bump->errorText()));
        // The real RPC state changes independently after review, before the
        // model dispatches its accepted sign/commit effect.
        const auto mining = qmltest::NewAddress(m_app, wallet->name());
        qmltest::Rpc(m_app, "generatetoaddress", qmltest::Json("[1,\"" + mining.toStdString() + "\"]"));
        const auto transaction = qmltest::Rpc(m_app, "gettransaction", qmltest::Json("[\"" + original.toStdString() + "\"]"), wallet->name());
        QVERIFY(transaction.find_value("confirmations").getInt<int>() >= 1);
        QVERIFY(!AwaitBumpCommit(*bump));
        QCOMPARE(bump->state(), BumpTransactionModel::NeedsRevalidation);
        QVERIFY(bump->newTxid().isEmpty());
        const auto result = bump->commitResult(); QVERIFY(result); QVERIFY(!result->committed);
        QCOMPARE(result->error, walletbump::Error::Ineligible);
        QVERIFY(!Mempool(m_app).contains(original.toStdString()));
    }
};

std::unique_ptr<QObject> CreateFeeBumpTests(BitcoinQmlApplication& app, WalletMode)
{
    return std::make_unique<FeeBumpIntegrationTests>(app);
}
#include <test_bump.moc>
