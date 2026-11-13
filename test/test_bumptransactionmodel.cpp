// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <test/mocks/mockwallet.h>
#include <test/mocks/manualexecutionenvironment.h>
#include <test/mocks/signertransactions.h>
#include <qml/models/bumptransactionmodel.h>

#include <QSignalSpy>

#include <memory>

namespace {
const auto TEST_TXID = QStringLiteral("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");

std::shared_ptr<MockWallet> MakeBumpableWallet()
{
    auto wallet = std::make_shared<MockWallet>();
    wallet->transaction_can_be_bumped_fn = [](const Txid&) { return true; };
    return wallet;
}
struct Fixture {
    ManualRoutedExecution commands, retirement;
    std::shared_ptr<walletopening::Session> opening;
    std::deque<std::function<void()>> publications;
    std::unique_ptr<BumpTransactionModel> model;
    explicit Fixture(const std::shared_ptr<MockWallet>& wallet) {
        opening = std::make_shared<walletopening::Session>(commands.scheduler, retirement.scheduler,
            std::make_shared<std::atomic_bool>(false), [] {});
        opening->start(wallet); commands.drain(); opening->take();
        model = std::make_unique<BumpTransactionModel>(wallet.get());
        model->setOpening(opening, {commands.scheduler, [this](std::function<void()> turn) { publications.push_back(std::move(turn)); }});
    }
    ~Fixture() { if (model) model->closeAdmission(); commands.drain(); model.reset(); opening->retire(); commands.drain(); retirement.drain(); }
    void publish() { while (!publications.empty()) { auto turn = std::move(publications.front()); publications.pop_front(); turn(); } }
    void finish() { commands.drain(); publish(); }
    bool confirm() {
        const auto attempt = model->attemptId();
        const auto id = model->requestConfirmFeeBump();
        if (!id) return false;
        finish();
        return model->attemptId() == attempt && model->commitId() == id && model->state() == BumpTransactionModel::Succeeded;
    }
};
} // namespace

class BumpTransactionModelTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void quietCloseRetiresPreparedReviewBeforeFacadeDestruction_data() {
        QTest::addColumn<int>("stage");
        QTest::newRow("before-native")<<0;QTest::newRow("held-outcome")<<1;QTest::newRow("published-review")<<2;
    }
    void quietCloseRetiresPreparedReviewBeforeFacadeDestruction() {
        QFETCH(int,stage);auto wallet=MakeBumpableWallet();
        wallet->create_bump_transaction_fn=[](const Txid&,const wallet::CCoinControl&,std::vector<bilingual_str>&,CAmount& old_fee,CAmount& new_fee,CMutableTransaction& tx){
            old_fee=500;new_fee=1000;tx=*signerfixture::Review().GetUnsignedTx();return true;
        };
        Fixture fixture{wallet};QVERIFY(fixture.model->requestPrepareFeeBump(TEST_TXID,1));
        if(stage==1)fixture.commands.drain();
        if(stage==2){fixture.finish();QCOMPARE(fixture.model->state(),BumpTransactionModel::NeedsConfirmation);}
        QSignalSpy notices{fixture.model.get(),&BumpTransactionModel::stateChanged};
        fixture.model->closeAdmission();fixture.opening->retire();fixture.finish();fixture.commands.drain();fixture.retirement.drain();
        QCOMPARE(fixture.model->state(),BumpTransactionModel::Closed);QVERIFY(!fixture.model->busy());QCOMPARE(notices.count(),0);
        QCOMPARE(wallet->calls.createBumpTransaction.load(),1);QCOMPARE(wallet->calls.commitBumpTransaction.load(),0);
        QCOMPARE(fixture.commands.scheduler->pending(),size_t{0});QVERIFY(fixture.commands.quiescent());
    }
    void longCapturedPasswordKeepsOriginalCommitAndLockOwnership_data() {
        QTest::addColumn<bool>("close_first");
        QTest::newRow("held-gui") << false; QTest::newRow("close-before-effect") << true;
    }
    void longCapturedPasswordKeepsOriginalCommitAndLockOwnership() {
        QFETCH(bool, close_first);
        struct Wallet final : MockWallet {
            bool locked{true}; int unlocks{0}, locks{0};
            bool isCrypted() override { return true; } bool isLocked() override { return locked; }
            util::Expected<void, wallet::WalletError> unlock(const SecureString& value) override {
                ++unlocks;
                if (value != SecureString(40'000, 'p')) return util::Unexpected{wallet::WalletError{wallet::WalletErrorCode::PassphraseIncorrect, Untranslated("Wrong original")}};
                locked = false; return {};
            }
            bool lock() override { ++locks; locked = true; return true; }
        };
        const auto wallet = std::make_shared<Wallet>(); wallet->transaction_can_be_bumped_fn = [](const Txid&) { return true; };
        wallet->create_bump_transaction_fn = [](const Txid&, const wallet::CCoinControl&, std::vector<bilingual_str>&,
            CAmount& old_fee, CAmount& new_fee, CMutableTransaction& tx) {
            old_fee = 500; new_fee = 1000; tx = *signerfixture::Review().GetUnsignedTx(); return true;
        };
        wallet->sign_bump_transaction_fn = [](CMutableTransaction&) { return true; };
        bool committed_locked{false};
        wallet->commit_bump_transaction_fn = [&](const Txid&, CMutableTransaction&&, std::vector<bilingual_str>&, Txid& id) {
            committed_locked = wallet->locked; id = Txid::FromUint256(uint256::ONE); return true;
        };
        Fixture fixture{wallet}; QVERIFY(fixture.model->requestPrepareFeeBump(TEST_TXID, 1)); fixture.finish();
        QCOMPARE(fixture.model->state(), BumpTransactionModel::NeedsConfirmation);
        QString password(40'000, QChar{'p'});
        const auto ticket = fixture.model->requestConfirmFeeBumpWithPassphrase(password); QVERIFY(ticket);
        const auto owner = fixture.model->commitOperation(); password = "changed editor";
        QCOMPARE(wallet->unlocks, 0); QCOMPARE(wallet->calls.commitBumpTransaction.load(), 0);
        if (close_first) fixture.model->closeAdmission();
        QVERIFY(fixture.commands.step(execution::Lane::Core));
        QCOMPARE(wallet->unlocks, 1); QCOMPARE(wallet->locks, 1); QVERIFY(committed_locked);
        QCOMPARE(wallet->calls.commitBumpTransaction.load(), 1); QVERIFY(!owner->result());
        fixture.commands.drain(); QVERIFY(owner->result()); QVERIFY(owner->result()->value->committed);
        fixture.publish();
        QCOMPARE(fixture.model->state(), close_first ? BumpTransactionModel::Closed : BumpTransactionModel::Succeeded);
    }
    void manualPreparationDoesNotPublishBeforeItsOutcome()
    {
        auto wallet = MakeBumpableWallet();
        wallet->create_bump_transaction_fn = [](const Txid&, const wallet::CCoinControl&, std::vector<bilingual_str>&,
            CAmount& old_fee, CAmount& new_fee, CMutableTransaction& tx) {
            old_fee = 500; new_fee = 1000; tx = *signerfixture::Review().GetUnsignedTx(); return true;
        };
        Fixture fixture{wallet}; auto& model = *fixture.model;
        QVERIFY(model.requestPrepareFeeBump(TEST_TXID, 1));
        QCOMPARE(model.state(), BumpTransactionModel::Preparing);
        QCOMPARE(wallet->calls.createBumpTransaction.load(), 0);
        QVERIFY(fixture.commands.step(execution::Lane::Core));
        QCOMPARE(wallet->calls.createBumpTransaction.load(), 1);
        QCOMPARE(model.state(), BumpTransactionModel::Preparing);
        QVERIFY(fixture.publications.empty());
        QVERIFY(fixture.commands.step(execution::Lane::Domain));
        QCOMPARE(model.state(), BumpTransactionModel::Preparing);
        QCOMPARE(fixture.publications.size(), size_t{1});
        fixture.publish();
        QCOMPARE(model.state(), BumpTransactionModel::NeedsConfirmation);
    }
    void committedOutcomeSurvivesResetCloseAndObserverLoss()
    {
        auto wallet = MakeBumpableWallet();
        wallet->create_bump_transaction_fn = [](const Txid&, const wallet::CCoinControl&, std::vector<bilingual_str>&,
            CAmount& old_fee, CAmount& new_fee, CMutableTransaction& tx) {
            old_fee = 500; new_fee = 1000; tx = *signerfixture::Review().GetUnsignedTx(); return true;
        };
        wallet->sign_bump_transaction_fn = [](CMutableTransaction&) { return true; };
        wallet->commit_bump_transaction_fn = [](const Txid&, CMutableTransaction&&, std::vector<bilingual_str>&, Txid& id) {
            id = Txid::FromUint256(uint256::ONE); return true;
        };
        Fixture fixture{wallet};
        QVERIFY(fixture.model->requestPrepareFeeBump(TEST_TXID, 1)); fixture.finish();
        const auto commit = fixture.model->requestConfirmFeeBump(); QVERIFY(commit);
        QVERIFY(!fixture.model->requestConfirmFeeBump());
        const auto owner = fixture.model->commitOperation(); QVERIFY(owner);
        fixture.model->reset();
        QCOMPARE(fixture.model->state(), BumpTransactionModel::Committing);
        QVERIFY(!fixture.model->requestPrepareFeeBump(TEST_TXID, 1));
        fixture.model->closeAdmission();
        fixture.model.reset();
        fixture.opening->closeAdmission();
        QVERIFY(fixture.commands.step(execution::Lane::Core));
        QCOMPARE(wallet->calls.commitBumpTransaction.load(), 1);
        QVERIFY(!owner->result());
        QVERIFY(fixture.commands.step(execution::Lane::Domain));
        QVERIFY(owner->result()); QVERIFY(owner->result()->value);
        const auto result = *owner->result()->value;
        QVERIFY(result.committed);
        QCOMPARE(result.txid, Txid::FromUint256(uint256::ONE));
        QCOMPARE(result.original.GetHex(), TEST_TXID.toStdString());
        fixture.publish();
        QCOMPARE(wallet->calls.commitBumpTransaction.load(), 1);
    }
    void failedGuiTransportDoesNotRepeatCommittedMutation()
    {
        auto wallet = MakeBumpableWallet();
        wallet->create_bump_transaction_fn = [](const Txid&, const wallet::CCoinControl&, std::vector<bilingual_str>&,
            CAmount& old_fee, CAmount& new_fee, CMutableTransaction& tx) {
            old_fee = 500; new_fee = 1000; tx = *signerfixture::Review().GetUnsignedTx(); return true;
        };
        wallet->sign_bump_transaction_fn = [](CMutableTransaction&) { return true; };
        wallet->commit_bump_transaction_fn = [](const Txid&, CMutableTransaction&&, std::vector<bilingual_str>&, Txid& id) {
            id = Txid::FromUint256(uint256::ONE); return true;
        };
        Fixture fixture{wallet}; bool fail{false};
        fixture.model->setOpening(fixture.opening, {fixture.commands.scheduler, [&fixture, &fail](std::function<void()> turn) {
            if (std::exchange(fail, false)) throw std::runtime_error{"GUI transport unavailable"};
            fixture.publications.push_back(std::move(turn));
        }});
        QVERIFY(fixture.model->requestPrepareFeeBump(TEST_TXID, 1)); fixture.finish();
        fail = true; QVERIFY(fixture.model->requestConfirmFeeBump()); fixture.commands.drain();
        QVERIFY(fixture.model->busy());
        const auto owner = fixture.model->commitOperation();
        QVERIFY(owner->result()); QVERIFY(owner->result()->publication_failed); QVERIFY(owner->result()->value->committed);
        QCOMPARE(wallet->calls.commitBumpTransaction.load(), 1);
        fixture.model->retryPublications();
        QCOMPARE(fixture.model->state(), BumpTransactionModel::Succeeded);
        fixture.model->retryPublications(); fixture.publish();
        QCOMPARE(wallet->calls.commitBumpTransaction.load(), 1);
    }
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
        Fixture fixture{wallet};
        auto& model = *fixture.model;
        model.prepareFeeBump(TEST_TXID, 1);
        fixture.finish();
        QVERIFY(!fixture.confirm());
        QCOMPARE(model.state(), BumpTransactionModel::Failed);
        QCOMPARE(wallet->calls.signBumpTransaction.load(), 1);
        QCOMPARE(wallet->calls.commitBumpTransaction.load(), 0);
        QVERIFY(model.newTxid().isEmpty());
    }
    void temporaryUnlockRestoresBeforeCommit_data()
    {
        QTest::addColumn<int>("scenario");
        for (int scenario = 0; scenario < 6; ++scenario) QTest::newRow(qPrintable(QString::number(scenario))) << scenario;
    }
    void temporaryUnlockRestoresBeforeCommit()
    {
        QFETCH(int, scenario);
        struct Wallet : MockWallet {
            int unlocks{0}, locks{0};
            bool locked{true}, throw_unlock{false}, fail_lock{false};
            bool isCrypted() override { return true; }
            bool isLocked() override { return locked; }
            util::Expected<void, wallet::WalletError> unlock(const SecureString& passphrase) override {
                ++unlocks;
                if (throw_unlock) { locked = false; throw std::runtime_error{"Unlock outcome lost"}; }
                if (passphrase != "secret") return util::Unexpected{wallet::WalletError{wallet::WalletErrorCode::PassphraseIncorrect, Untranslated("Incorrect password")}};
                locked = false; return {};
            }
            bool lock() override { ++locks; if (fail_lock) return false; locked = true; return true; }
        };
        const auto wallet = std::make_shared<Wallet>();
        wallet->throw_unlock = scenario == 5; wallet->fail_lock = scenario == 4;
        wallet->transaction_can_be_bumped_fn = [](const Txid&) { return true; };
        wallet->create_bump_transaction_fn = [](const Txid&, const wallet::CCoinControl&, std::vector<bilingual_str>&,
            CAmount& old_fee, CAmount& new_fee, CMutableTransaction& tx) {
            old_fee = 500; new_fee = 1000; tx = *signerfixture::Review().GetUnsignedTx(); return true;
        };
        wallet->sign_bump_transaction_fn = [scenario](CMutableTransaction&) {
            if (scenario == 3) throw std::runtime_error{"Sign outcome lost"};
            return true;
        };
        bool committed_locked{false};
        wallet->commit_bump_transaction_fn = [&](const Txid&, CMutableTransaction&&, std::vector<bilingual_str>&, Txid& id) {
            committed_locked = wallet->locked; id = Txid::FromUint256(uint256::ONE); return true;
        };
        Fixture fixture{wallet}; QVERIFY(fixture.model->requestPrepareFeeBump(TEST_TXID, 1)); fixture.finish();
        const auto id = scenario == 0 ? fixture.model->requestConfirmFeeBump()
            : fixture.model->requestConfirmFeeBumpWithPassphrase(scenario == 1 ? "wrong" : "secret");
        QVERIFY(id); fixture.finish();
        const walletbump::Error errors[] = {walletbump::Error::NeedsUnlock, walletbump::Error::IncorrectPassphrase,
            walletbump::Error::None, walletbump::Error::Uncertain, walletbump::Error::RelockFailed, walletbump::Error::Uncertain};
        const auto result = fixture.model->commitResult(); QVERIFY(result);
        QCOMPARE(result->error, errors[scenario]); QCOMPARE(wallet->locks, scenario <= 1 ? 0 : 1);
        QCOMPARE(fixture.model->unlockErrorText().isEmpty(), scenario != 1);
        QCOMPARE(wallet->calls.commitBumpTransaction.load(), scenario == 2 ? 1 : 0);
        QCOMPARE(result->committed, scenario == 2);
        if (scenario == 2) QVERIFY(committed_locked);
    }
    void eligibilityChangingDuringSigningCannotCommit()
    {
        const auto wallet = MakeBumpableWallet();
        bool eligible{true};
        wallet->transaction_can_be_bumped_fn = [&](const Txid&) { return eligible; };
        wallet->create_bump_transaction_fn = [](const Txid&, const wallet::CCoinControl&, std::vector<bilingual_str>&,
            CAmount& old_fee, CAmount& new_fee, CMutableTransaction& tx) {
            old_fee = 500; new_fee = 1000; tx = *signerfixture::Review().GetUnsignedTx(); return true;
        };
        wallet->sign_bump_transaction_fn = [&](CMutableTransaction&) { eligible = false; return true; };
        Fixture fixture{wallet}; QVERIFY(fixture.model->requestPrepareFeeBump(TEST_TXID, 1)); fixture.finish();
        QVERIFY(!fixture.confirm());
        QCOMPARE(fixture.model->commitResult()->error, walletbump::Error::Ineligible);
        QCOMPARE(wallet->calls.signBumpTransaction.load(), 1);
        QCOMPARE(wallet->calls.commitBumpTransaction.load(), 0);
    }
    void stateIsIdleByDefault()
    {
        auto wallet = MakeBumpableWallet();
        Fixture fixture{wallet};
        auto& model = *fixture.model;
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

        Fixture fixture{wallet};
        auto& model = *fixture.model;
        QSignalSpy stateSpy(&model, &BumpTransactionModel::stateChanged);

        model.prepareFeeBump(TEST_TXID, 1);
        fixture.finish();

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

        Fixture fixture{wallet};
        auto& model = *fixture.model;
        model.prepareFeeBump(TEST_TXID, 1);
        fixture.finish();

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

        Fixture fixture{wallet};
        auto& model = *fixture.model;
        model.prepareFeeBump(TEST_TXID, 1);
        fixture.finish();

        QCOMPARE(model.state(), BumpTransactionModel::Failed);
        QVERIFY(!model.errorText().isEmpty());
    }

    void prepareFeeBump_invalidTxidFails()
    {
        auto wallet = MakeBumpableWallet();
        Fixture fixture{wallet};
        auto& model = *fixture.model;

        model.prepareFeeBump(QStringLiteral("not-a-txid"), 1);
        fixture.finish();

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

        Fixture fixture{wallet};
        auto& model = *fixture.model;
        model.prepareFeeBump(TEST_TXID, 1);
        fixture.finish();
        QCOMPARE(model.state(), BumpTransactionModel::NeedsConfirmation);

        fixture.confirm();

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
        Fixture fixture{wallet};
        auto& model = *fixture.model;
        model.prepareFeeBump(TEST_TXID, 1);
        fixture.finish();
        QVERIFY(fixture.confirm());
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
        Fixture fixture{wallet};
        auto& model = *fixture.model;
        model.prepareFeeBump(TEST_TXID, 1);
        fixture.finish();
        QVERIFY(!fixture.confirm());
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
        QVERIFY(!fixture.confirm()); // Failure is terminal until a new review.
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

        Fixture fixture{wallet};
        auto& model = *fixture.model;
        model.prepareFeeBump(TEST_TXID, 1);
        fixture.finish();

        fixture.confirm();

        QCOMPARE(model.state(), BumpTransactionModel::Failed);
        QCOMPARE(wallet->calls.commitBumpTransaction.load(), 0);
    }

    void confirmFeeBump_rejectsWhenNotReady()
    {
        auto wallet = MakeBumpableWallet();
        Fixture fixture{wallet};
        auto& model = *fixture.model;

        fixture.confirm();

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

        Fixture fixture{wallet};
        auto& model = *fixture.model;
        model.prepareFeeBump(TEST_TXID, 1);
        fixture.finish();

        wallet->transaction_can_be_bumped_fn = [](const Txid&) { return false; };
        fixture.confirm();

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

        Fixture fixture{wallet};
        auto& model = *fixture.model;
        model.prepareFeeBump(TEST_TXID, 1);
        fixture.finish();
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
