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
} // namespace

class FeeBumpIntegrationTests : public QObject
{
    Q_OBJECT
    BitcoinQmlApplication& m_app;
public:
    explicit FeeBumpIntegrationTests(BitcoinQmlApplication& app) : m_app(app) {}
private Q_SLOTS:
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
        QCOMPARE(bump->state(), BumpTransactionModel::Failed);
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
