// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <test/mocks/mockwallet.h>
#include <test/mocks/signertransactions.h>
#include <qml/models/bumptransactionmodel.h>

#include <QSignalSpy>

#include <memory>

namespace {
const auto TEST_TXID = QStringLiteral("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");

std::unique_ptr<MockWallet> MakeBumpableWallet()
{
    auto wallet = std::make_unique<MockWallet>();
    wallet->transaction_can_be_bumped_fn = [](const Txid&) { return true; };
    return wallet;
}
} // namespace

class BumpTransactionModelTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void signerSubstitutionCannotCommitReplacement_data()
    {
        QTest::addColumn<int>("mutation");
        for (size_t i = 0; i < signerfixture::CASES.size(); ++i)
            QTest::newRow(signerfixture::CASES[i].first) << int(i);
    }
    void signerSubstitutionCannotCommitReplacement()
    {
        QFETCH(int, mutation);
        auto wallet = MakeBumpableWallet();
        wallet->create_bump_transaction_fn = [](const Txid&, const wallet::CCoinControl&, std::vector<bilingual_str>&,
                                               CAmount& old_fee, CAmount& new_fee, CMutableTransaction& tx) {
            old_fee = 500; new_fee = 1000;
            tx = *signerfixture::Review().GetUnsignedTx();
            return true;
        };
        wallet->sign_bump_transaction_fn = [mutation](CMutableTransaction& tx) {
            signerfixture::Change(tx, signerfixture::CASES.at(mutation).second);
            return true;
        };
        wallet->commit_bump_transaction_fn = [](const Txid&, CMutableTransaction&&, std::vector<bilingual_str>&, Txid&) { return true; };
        BumpTransactionModel model(wallet.get());
        model.prepareFeeBump(TEST_TXID, 1);
        QVERIFY(!model.confirmFeeBump());
        QCOMPARE(model.state(), BumpTransactionModel::Failed);
        QCOMPARE(wallet->calls.signBumpTransaction.load(), 1);
        QCOMPARE(wallet->calls.commitBumpTransaction.load(), 0);
        QVERIFY(model.newTxid().isEmpty());
    }
    void stateIsIdleByDefault()
    {
        auto wallet = MakeBumpableWallet();
        BumpTransactionModel model(wallet.get());
        QCOMPARE(model.state(), BumpTransactionModel::Idle);
    }

    void prepareFeeBump_transitionsToNeedsConfirmation()
    {
        auto wallet = MakeBumpableWallet();
        wallet->create_bump_transaction_fn = [](const Txid&, const wallet::CCoinControl&, std::vector<bilingual_str>&,
                                                CAmount& old_fee, CAmount& new_fee, CMutableTransaction&) {
            old_fee = 500;
            new_fee = 1000;
            return true;
        };

        BumpTransactionModel model(wallet.get());
        QSignalSpy stateSpy(&model, &BumpTransactionModel::stateChanged);

        model.prepareFeeBump(TEST_TXID, 1);

        QCOMPARE(model.state(), BumpTransactionModel::NeedsConfirmation);
        QVERIFY(stateSpy.count() >= 2); // Idle -> Preparing -> NeedsConfirmation
    }

    void prepareFeeBump_populatesFees()
    {
        auto wallet = MakeBumpableWallet();
        wallet->create_bump_transaction_fn = [](const Txid&, const wallet::CCoinControl&, std::vector<bilingual_str>&,
                                                CAmount& old_fee, CAmount& new_fee, CMutableTransaction&) {
            old_fee = 500;
            new_fee = 1000;
            return true;
        };

        BumpTransactionModel model(wallet.get());
        model.prepareFeeBump(TEST_TXID, 1);

        QVERIFY(!model.oldFee().isEmpty());
        QVERIFY(!model.newFee().isEmpty());
        QVERIFY(!model.feeIncrease().isEmpty());
    }

    void prepareFeeBump_failureSetsErrorState()
    {
        auto wallet = MakeBumpableWallet();
        wallet->create_bump_transaction_fn = [](const Txid&, const wallet::CCoinControl&, std::vector<bilingual_str>& errors,
                                                CAmount&, CAmount&, CMutableTransaction&) {
            errors.emplace_back(Untranslated("insufficient fee"));
            return false;
        };

        BumpTransactionModel model(wallet.get());
        model.prepareFeeBump(TEST_TXID, 1);

        QCOMPARE(model.state(), BumpTransactionModel::Failed);
        QVERIFY(!model.errorText().isEmpty());
    }

    void prepareFeeBump_invalidTxidFails()
    {
        auto wallet = MakeBumpableWallet();
        BumpTransactionModel model(wallet.get());

        model.prepareFeeBump(QStringLiteral("not-a-txid"), 1);

        QCOMPARE(model.state(), BumpTransactionModel::Failed);
    }

    void prepareFeeBump_nullWalletFails()
    {
        BumpTransactionModel model(nullptr);

        model.prepareFeeBump(TEST_TXID, 1);

        QCOMPARE(model.state(), BumpTransactionModel::Failed);
    }

    void confirmFeeBump_signsAndCommits()
    {
        auto wallet = MakeBumpableWallet();
        wallet->create_bump_transaction_fn = [](const Txid&, const wallet::CCoinControl&, std::vector<bilingual_str>&,
                                                CAmount& old_fee, CAmount& new_fee, CMutableTransaction&) {
            old_fee = 500;
            new_fee = 1000;
            return true;
        };
        wallet->sign_bump_transaction_fn = [](CMutableTransaction&) { return true; };
        wallet->commit_bump_transaction_fn = [](const Txid&, CMutableTransaction&&, std::vector<bilingual_str>&, Txid& bumped_txid) {
            bumped_txid = Txid::FromUint256(*uint256::FromHex("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"));
            return true;
        };

        BumpTransactionModel model(wallet.get());
        model.prepareFeeBump(TEST_TXID, 1);
        QCOMPARE(model.state(), BumpTransactionModel::NeedsConfirmation);

        model.confirmFeeBump();

        QCOMPARE(model.state(), BumpTransactionModel::Succeeded);
        QVERIFY(!model.newTxid().isEmpty());
    }

    void confirmFeeBumpAllowsSignatureDataAndKeepsOriginalConflict()
    {
        auto wallet = MakeBumpableWallet();
        const auto approved = *signerfixture::Review().GetUnsignedTx();
        wallet->create_bump_transaction_fn = [approved](const Txid&, const wallet::CCoinControl&, std::vector<bilingual_str>&,
                                                       CAmount&, CAmount&, CMutableTransaction& tx) { tx = approved; return true; };
        wallet->sign_bump_transaction_fn = [](CMutableTransaction& tx) {
            for (auto& input : tx.vin) { input.scriptSig << OP_TRUE; input.scriptWitness.stack = {{1, 2}}; }
            return true;
        };
        std::optional<Txid> original;
        std::optional<CMutableTransaction> committed;
        wallet->commit_bump_transaction_fn = [&](const Txid& txid, CMutableTransaction&& tx, std::vector<bilingual_str>&, Txid&) {
            original = txid; committed = std::move(tx); return true;
        };
        BumpTransactionModel model(wallet.get());
        model.prepareFeeBump(TEST_TXID, 1);
        QVERIFY(model.confirmFeeBump());
        QCOMPARE(wallet->calls.commitBumpTransaction.load(), 1);
        QVERIFY(original); QVERIFY(committed);
        QCOMPARE(original->GetHex(), TEST_TXID.toStdString());
        QVERIFY(!committed->vin[0].scriptSig.empty());
        QVERIFY(!committed->vin[0].scriptWitness.IsNull());
        for (auto& input : committed->vin) { input.scriptSig.clear(); input.scriptWitness.SetNull(); }
        QCOMPARE(committed->GetHash(), approved.GetHash());
    }
    void confirmFeeBumpReportsCommitFailure_data()
    {
        QTest::addColumn<bool>("explicit_error");
        QTest::newRow("default-error") << false;
        QTest::newRow("core-error") << true;
    }
    void confirmFeeBumpReportsCommitFailure()
    {
        QFETCH(bool, explicit_error);
        auto wallet = MakeBumpableWallet();
        const auto approved = *signerfixture::Review().GetUnsignedTx();
        wallet->create_bump_transaction_fn = [approved](const Txid&, const wallet::CCoinControl&, std::vector<bilingual_str>&,
                                                       CAmount&, CAmount&, CMutableTransaction& tx) { tx = approved; return true; };
        wallet->sign_bump_transaction_fn = [](CMutableTransaction& tx) { tx.vin[0].scriptSig << OP_TRUE; return true; };
        std::optional<Txid> original;
        std::optional<CMutableTransaction> attempted;
        wallet->commit_bump_transaction_fn = [&](const Txid& txid, CMutableTransaction&& tx, std::vector<bilingual_str>& errors, Txid& bumped_txid) {
            original = txid;
            attempted = std::move(tx); // Core may consume its argument even when commit fails.
            bumped_txid = Txid::FromUint256(uint256::ONE);
            if (explicit_error) errors.emplace_back(Untranslated("commit refused"));
            return false;
        };
        BumpTransactionModel model(wallet.get());
        model.prepareFeeBump(TEST_TXID, 1);
        QVERIFY(!model.confirmFeeBump());
        QCOMPARE(model.state(), BumpTransactionModel::Failed);
        QCOMPARE(model.errorText(), explicit_error ? QString("commit refused") : QString("Failed to commit transaction."));
        QVERIFY(model.newTxid().isEmpty());
        QCOMPARE(wallet->calls.signBumpTransaction.load(), 1);
        QCOMPARE(wallet->calls.commitBumpTransaction.load(), 1);
        QVERIFY(original); QVERIFY(attempted);
        QCOMPARE(original->GetHex(), TEST_TXID.toStdString());
        QVERIFY(!attempted->vin[0].scriptSig.empty());
        attempted->vin[0].scriptSig.clear();
        QCOMPARE(attempted->GetHash(), approved.GetHash());
        QVERIFY(!model.confirmFeeBump()); // Failure is terminal until a new review.
        QCOMPARE(wallet->calls.commitBumpTransaction.load(), 1);
    }
    void confirmFeeBump_signFailureDoesNotCommit()
    {
        auto wallet = MakeBumpableWallet();
        wallet->create_bump_transaction_fn = [](const Txid&, const wallet::CCoinControl&, std::vector<bilingual_str>&,
                                                CAmount& old_fee, CAmount& new_fee, CMutableTransaction&) {
            old_fee = 500;
            new_fee = 1000;
            return true;
        };
        wallet->sign_bump_transaction_fn = [](CMutableTransaction&) { return false; };

        BumpTransactionModel model(wallet.get());
        model.prepareFeeBump(TEST_TXID, 1);

        model.confirmFeeBump();

        QCOMPARE(model.state(), BumpTransactionModel::Failed);
        QCOMPARE(wallet->calls.commitBumpTransaction.load(), 0);
    }

    void confirmFeeBump_rejectsWhenNotReady()
    {
        auto wallet = MakeBumpableWallet();
        BumpTransactionModel model(wallet.get());

        model.confirmFeeBump();

        QCOMPARE(model.state(), BumpTransactionModel::Idle);
    }

    void confirmFeeBump_rechecksEligibility()
    {
        auto wallet = MakeBumpableWallet();
        wallet->create_bump_transaction_fn = [](const Txid&, const wallet::CCoinControl&, std::vector<bilingual_str>&,
                                                CAmount& old_fee, CAmount& new_fee, CMutableTransaction&) {
            old_fee = 500;
            new_fee = 1000;
            return true;
        };

        BumpTransactionModel model(wallet.get());
        model.prepareFeeBump(TEST_TXID, 1);

        wallet->transaction_can_be_bumped_fn = [](const Txid&) { return false; };
        model.confirmFeeBump();

        QCOMPARE(model.state(), BumpTransactionModel::Failed);
    }

    void reset_clearsState()
    {
        auto wallet = MakeBumpableWallet();
        wallet->create_bump_transaction_fn = [](const Txid&, const wallet::CCoinControl&, std::vector<bilingual_str>&,
                                                CAmount& old_fee, CAmount& new_fee, CMutableTransaction&) {
            old_fee = 500;
            new_fee = 1000;
            return true;
        };

        BumpTransactionModel model(wallet.get());
        model.prepareFeeBump(TEST_TXID, 1);
        QCOMPARE(model.state(), BumpTransactionModel::NeedsConfirmation);

        model.reset();

        QCOMPARE(model.state(), BumpTransactionModel::Idle);
        QVERIFY(model.newTxid().isEmpty());
        QVERIFY(model.errorText().isEmpty());
    }
};

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(BumpTransactionModelTests)
#else
QTEST_MAIN(BumpTransactionModelTests)
#endif
#include <test_bumptransactionmodel.moc>
