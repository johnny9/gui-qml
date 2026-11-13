// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/walletsend.h>
#include <qml/retainedvalue.h>
#include <qml/backendexecutor.h>
#include <qml/snapshotquery.h>
#include <qml/psbtwork.h>
#include <test/mocks/manualexecutionenvironment.h>
#include <test/mocks/mockwallet.h>
#include <test/mocks/psbtfeefixture.h>
#include <test/qt_test_registry.h>

#include <chainparams.h>
#include <QSemaphore>
#include <QScopeGuard>
#include <QtTest/QtTest>
#include <string_view>

namespace {
using walletsend::Error;
using execution::Lane;
CTransactionRef Transaction()
{
    CMutableTransaction transaction; transaction.vin.resize(1); transaction.vout.emplace_back(1000, CScript{} << OP_TRUE);
    return MakeTransactionRef(std::move(transaction));
}
walletsend::Request SignedRequest()
{
    walletsend::Request request; request.reviewed = Transaction(); return request;
}
walletsend::Request PsbtRequest()
{
    CMutableTransaction funding; funding.vin.resize(1); funding.vout.emplace_back(2000, CScript{} << OP_DUP << OP_HASH160 << std::vector<unsigned char>(20) << OP_EQUALVERIFY << OP_CHECKSIG);
    const auto previous = MakeTransactionRef(std::move(funding));
    CMutableTransaction transaction{*Transaction()}; transaction.vin[0].prevout = COutPoint{previous->GetHash(), 0};
    PartiallySignedTransaction psbt{transaction, 2}; psbt.inputs[0].non_witness_utxo = previous;
    walletsend::Request request; request.reviewed = MakeTransactionRef(transaction);
    request.psbt = std::make_shared<const PartiallySignedTransaction>(psbt);
    request.proof = std::make_shared<const psbtfee::Analysis>(psbtfee::Analyze(psbt)); request.approved_fee = 1000;
    return request;
}
struct Wallet : StubWallet {
    bool locked{true}, encrypted{false}, no_keys{false}, signer{false};
    bool report_sign_error{false}, mutate_then_throw{false}, change_transaction{false}, throw_sign{false}, fail_lock{false}, throw_commit{false}, partial_sign{false};
    int commits{0}, signs{0}, unlocks{0}, locks{0}, labels{0}, status_reads{0};
    CTransactionRef last_transaction;
    std::function<void()> after_unlock,before_commit;
    std::function<void(PartiallySignedTransaction&, bool&)> metadata;
    int metadata_reads{0};
    std::vector<bool> metadata_derivations;
    std::string expected_password{"secret"};std::atomic<QThread*> unlock_thread{nullptr};
    int depth{0}; bool in_mempool{true}, abandoned{false}, missing{false}, mismatch{false}, throw_status{false};
    bool isCrypted() override { return encrypted; }
    bool isLocked() override { return locked; }
    bool privateKeysDisabled() override { return no_keys; }
    bool hasExternalSigner() override { return signer; }
    util::Expected<void, wallet::WalletError> unlock(const SecureString& secret) override {
        ++unlocks;unlock_thread=QThread::currentThread();
        if (std::string_view{secret.data(),secret.size()} != std::string_view{expected_password}) return util::Unexpected{wallet::WalletError{wallet::WalletErrorCode::PassphraseIncorrect, Untranslated("wrong")}};
        locked = false;if(after_unlock)after_unlock();return {};
    }
    bool lock() override { ++locks; if (fail_lock) return false; locked = true; return true; }
    std::optional<common::PSBTError> fillPSBT(const common::PSBTFillOptions& options, size_t*, PartiallySignedTransaction& psbt, bool& complete) override {
        if (!options.sign && metadata) {
            ++metadata_reads;
            metadata_derivations.push_back(options.bip32_derivs);
            metadata(psbt, complete);
            return {};
        }
        ++signs;
        if (report_sign_error || mutate_then_throw) {
            psbt.inputs[0].final_script_sig = CScript{} << OP_TRUE;
            if (mutate_then_throw) throw std::runtime_error{"unknown signer result after partial output"};
            return common::PSBTError::EXTERNAL_SIGNER_FAILED;
        }
        if (throw_sign) throw std::runtime_error{"signer failure"};
        if (partial_sign) { complete = false; return {}; }
        if (change_transaction) psbt.outputs[0].amount = 999;
        psbt.inputs[0].final_script_sig = CScript{} << OP_TRUE; complete = true; return {};
    }
    void commitTransaction(CTransactionRef transaction, const std::vector<std::string>&) override { if(before_commit)before_commit();++commits; last_transaction = std::move(transaction); if (throw_commit) throw std::runtime_error{"commit uncertainty"}; }
    interfaces::WalletTx getWalletTxDetails(const Txid&, interfaces::WalletTxStatus& status, std::vector<std::string>&, std::vector<std::string>&, bool& mempool, int&) override {
        ++status_reads; if (throw_status) throw std::runtime_error{"status unavailable"};
        status = {}; status.depth_in_main_chain = depth; status.is_abandoned = abandoned; mempool = in_mempool;
        interfaces::WalletTx record{}; if (!missing) record.tx = last_transaction;
        if (mismatch) { CMutableTransaction other{*last_transaction}; ++other.vout[0].nValue; record.tx = MakeTransactionRef(other); }
        return record;
    }
    bool getAddress(const CTxDestination&, std::string*, wallet::AddressPurpose*) override { return true; }
    bool setAddressBook(const CTxDestination&, const std::string&, const std::optional<wallet::AddressPurpose>&) override { ++labels; return true; }
};
struct Fixture {
    ManualRoutedExecution commands, retirement;
    std::shared_ptr<Wallet> wallet{std::make_shared<Wallet>()};
    std::shared_ptr<walletopening::Session> opening{std::make_shared<walletopening::Session>(commands.scheduler, retirement.scheduler, std::make_shared<std::atomic_bool>(false), [] {})};
    std::shared_ptr<walletrequests::Store> notes{std::make_shared<walletrequests::Store>(opening, [] {})};
    int notifications{0};
    Fixture() { opening->start(wallet); commands.drain(); opening->take(); }
    ~Fixture() { notes->close(); opening->retire(); commands.drain(); retirement.drain(); }
    std::shared_ptr<walletsend::Operation> operation() { return std::make_shared<walletsend::Operation>(opening, notes, [this](auto) { ++notifications; }); }
};
}

class WalletSendTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase() { SelectParams(ChainType::MAIN); }
    void assessedCompleteScriptsDoNotRequireSigning_data()
    {
        QTest::addColumn<bool>("nested");
        QTest::addColumn<int>("boundary");
        for (bool nested : {false, true}) for (int boundary = 0; boundary < 4; ++boundary)
            QTest::newRow(qPrintable(QStringLiteral("%1-%2").arg(nested ? "nested" : "witness").arg(boundary))) << nested << boundary;
    }
    void assessedCompleteScriptsDoNotRequireSigning()
    {
        QFETCH(bool, nested);
        QFETCH(int, boundary);
        ECC_Context ecc;
        const auto original = psbtfeefixture::SignedWithoutScripts(nested);
        const auto raw = psbtwork::SerializeRaw(original.document);
        auto expected_document = original.document;
        expected_document.inputs[0].witness_script = original.witness;
        expected_document.inputs[0].redeem_script = original.redeem;
        CMutableTransaction expected;
        QVERIFY(FinalizeAndExtractPSBT(expected_document, expected));
        auto incomplete = original.document;
        CMutableTransaction absent;
        QVERIFY(!FinalizeAndExtractPSBT(incomplete, absent));
        Fixture f;
        f.wallet->no_keys = true;
        f.wallet->encrypted = true;
        f.wallet->metadata = [original, boundary](PartiallySignedTransaction& document, bool& complete) {
            document.inputs[0].witness_script = original.witness;
            document.inputs[0].redeem_script = original.redeem;
            if (boundary == 2) document.outputs[0].amount = 1'499;
            complete = FinalizePSBT(document);
            // Finalization restores the authenticated witness UTXO. Inject the
            // hostile returned claim afterward so the action receives it.
            if (boundary == 3) document.inputs[0].witness_utxo = CTxOut{1'999, original.document.inputs[0].non_witness_utxo->vout[0].scriptPubKey};
        };
        walletsend::Request request;
        request.reviewed = MakeTransactionRef(*original.document.GetUnsignedTx());
        request.psbt = std::make_shared<const PartiallySignedTransaction>(original.document);
        request.proof = std::make_shared<const psbtfee::Analysis>(psbtfee::Analyze(original.document));
        request.approved_fee = 500;
        request.assessed_complete = true;
        auto operation = f.operation();
        QVERIFY(operation->start(std::move(request)));
        if (boundary == 1) f.opening->retire();
        QCOMPARE(f.wallet->metadata_reads, 0);
        QVERIFY(f.commands.step(Lane::Core));
        QCOMPARE(f.wallet->metadata_reads, 1);
        QCOMPARE(f.wallet->metadata_derivations, std::vector<bool>{false});
        QCOMPARE(f.wallet->signs, 0);
        QCOMPARE(f.wallet->unlocks, 0);
        QCOMPARE(f.wallet->locks, 0);
        QVERIFY(f.wallet->locked);
        QCOMPARE(f.wallet->commits, boundary < 2 ? 1 : 0);
        QVERIFY(!operation->result()); // Actual effect exists while its Domain receipt is held.
        QVERIFY(f.commands.step(Lane::Domain));
        auto result = operation->result();
        QVERIFY(result);
        QCOMPARE(result->error, boundary < 2 ? Error::None : boundary == 2 ? Error::ChangedTransaction : Error::InvalidFee);
        if (boundary < 2) {
            QVERIFY(result->committed);
            QCOMPARE(result->committed->GetWitnessHash(), MakeTransactionRef(expected)->GetWitnessHash());
        }
        QCOMPARE(psbtwork::SerializeRaw(original.document), raw);
        QCOMPARE(original.document.inputs[0].partial_sigs.size(), size_t{2});
        result.reset(); operation.reset();
        f.notes->close(); f.opening->retire();
        f.commands.drain(); f.retirement.drain(); f.commands.drain();
        QVERIFY(f.commands.quiescent()); QVERIFY(f.retirement.quiescent());
    }
    void retainedCoreTransactionDoesNotRetainAppCleanupLease()
    {
        Fixture f;
        const auto original = Transaction();
        auto retained = walletopening::RetainValues<CTransactionRef>(f.opening, [&] {
            return std::make_unique<CTransactionRef>(original);
        });
        QVERIFY(retained);
        const std::weak_ptr<CTransactionRef> original_owner{retained};
        walletsend::Request request;
        request.reviewed = CTransactionRef{retained, retained->get()};
        auto operation = f.operation();
        QVERIFY(operation->start(std::move(request)));
        retained.reset();
        QVERIFY(!original_owner.expired());
        QVERIFY(f.commands.step(Lane::Core));
        QCOMPARE(f.wallet->commits, 1); QVERIFY(!operation->result());
        QVERIFY(f.wallet->last_transaction);
        QCOMPARE(f.wallet->last_transaction->GetHash(), original->GetHash());
        QCOMPARE(f.wallet->last_transaction->GetWitnessHash(), original->GetWitnessHash());
        f.commands.drain();
        auto outcome = operation->result(); QVERIFY(outcome); QCOMPARE(outcome->error, Error::None);
        QVERIFY(outcome->committed); QCOMPARE(outcome->committed->GetHash(), original->GetHash());
        // Keep the mock's Core wallet history alive through the entire app drain.
        // A retained app alias in commitTransaction would prevent retirement.
        outcome.reset(); operation.reset(); f.notes->close(); f.opening->retire();
        f.commands.drain(); f.retirement.drain(); f.commands.drain();
        QVERIFY(original_owner.expired());
        const auto closed = f.opening->take(); QVERIFY(closed);
        QCOMPARE(closed->phase, walletopening::Phase::Closed);
        QVERIFY(f.commands.quiescent()); QVERIFY(f.retirement.quiescent());
        QVERIFY(f.wallet->last_transaction);
        QCOMPARE(f.wallet->last_transaction->GetHash(), original->GetHash());
    }

    void explicitReviewWithdrawalHasOneCommitBoundary_data()
    {
        QTest::addColumn<int>("phase");QTest::newRow("before-effect")<<0;QTest::newRow("after-unlock-before-commit")<<1;QTest::newRow("after-commit-claim")<<2;QTest::newRow("passive-close-preserves-consent")<<3;
    }
    void explicitReviewWithdrawalHasOneCommitBoundary()
    {
        QFETCH(int,phase);Fixture f;f.wallet->encrypted=true;
        auto consent=std::make_shared<walletsend::CommitConsent>();auto request=PsbtRequest();request.passphrase=SecureString{"secret"};request.consent=consent;
        if(phase==1)f.wallet->after_unlock=[consent]{consent->withdraw();};
        if(phase==2)f.wallet->before_commit=[consent]{consent->withdraw();};
        auto operation=f.operation();QVERIFY(operation->start(std::move(request)));QVERIFY(!operation->result());
        if(phase==0)consent->withdraw();
        if(phase==3)f.opening->closeAdmission();
        QVERIFY(f.commands.step(Lane::Core));QCOMPARE(f.wallet->commits,phase<2?0:1);QVERIFY(!operation->result());
        if(phase==0)QCOMPARE(f.wallet->unlocks,0);else{QCOMPARE(f.wallet->unlocks,1);QCOMPARE(f.wallet->locks,1);QVERIFY(f.wallet->locked);}
        QVERIFY(f.commands.step(Lane::Domain));QVERIFY(operation->result());
        QCOMPARE(operation->result()->error,phase<2?Error::Cancelled:Error::None);QCOMPARE(bool(operation->result()->committed),phase>=2);
        if(phase>=2){consent->withdraw();QVERIFY(operation->result()->committed);QCOMPARE(f.wallet->commits,1);QVERIFY(!consent->withdrawn());}
    }
    void capturedPasswordOwnsOriginalSend_data()
    {
        QTest::addColumn<int>("scenario");QTest::newRow("held-result")<<0;QTest::newRow("quiet-close-before-work")<<1;QTest::newRow("native-admission-refused")<<2;
    }
    void capturedPasswordOwnsOriginalSend()
    {
        QFETCH(int,scenario);Fixture f;f.wallet->encrypted=true;f.wallet->expected_password=std::string(40'000,'x');
        QString source=QString::fromStdString(f.wallet->expected_password);auto captured=secretinput::Capture(f.commands.scheduler,source);QVERIFY(captured);
        auto request=PsbtRequest();request.captured_passphrase=captured.input;auto operation=f.operation();
        if(scenario==2)f.commands.rejectLane(Lane::Core);
        const auto admitted=operation->start(std::move(request));source.fill(QChar{'y'});QCOMPARE(f.wallet->unlocks,0);QCOMPARE(f.wallet->commits,0);
        if(scenario==2){QVERIFY(!admitted);f.commands.drain();QCOMPARE(f.wallet->unlocks,0);QCOMPARE(f.wallet->commits,0);QCOMPARE(operation->result()->error,Error::Rejected);}
        else{QVERIFY(admitted);if(scenario==1)f.opening->closeAdmission();QVERIFY(f.commands.step(Lane::Core));QCOMPARE(f.wallet->unlocks,1);QCOMPARE(f.wallet->commits,1);QCOMPARE(f.wallet->locks,1);QVERIFY(f.wallet->locked);QVERIFY(!operation->result());
            QVERIFY(f.commands.step(Lane::Domain));QCOMPARE(operation->result()->error,Error::None);QVERIFY(operation->result()->committed);}
        captured.input.reset();operation.reset();f.commands.drain();f.retirement.drain();
    }
    void signerApprovalOwnsItsOriginalOutcome_data()
    {
        QTest::addColumn<int>("scenario");
        for (int scenario = 0; scenario < 6; ++scenario) QTest::newRow(qPrintable(QString::number(scenario))) << scenario;
    }
    void signerApprovalOwnsItsOriginalOutcome()
    {
        QFETCH(int, scenario);
        Fixture f;
        f.wallet->signer = scenario != 4;
        f.wallet->partial_sign = scenario == 1;
        f.wallet->change_transaction = scenario == 2;
        f.wallet->throw_sign = scenario == 5;
        const auto send = PsbtRequest();
        walletsigning::Request request{send.reviewed, send.psbt, send.proof, scenario == 3 ? std::optional<CAmount>{999} : send.approved_fee};
        const auto action = f.opening->startAction();
        QVERIFY(action);
        auto command = std::make_shared<execution::OwnedCommand<walletsigning::Result>>(f.commands.scheduler, [] {});
        QVERIFY(command->startWith([&](auto done, auto failed) {
            return f.opening->perform(action, [request, opening = f.opening](interfaces::Wallet& wallet) {
                return walletsigning::Approve(wallet, request, opening);
            }, std::move(done), std::move(failed));
        }));
        QCOMPARE(f.wallet->signs, 0);
        QVERIFY(!command->result());
        QVERIFY(f.commands.step(Lane::Core));
        QVERIFY(!command->result());
        f.opening->closeAdmission();
        QVERIFY(!f.opening->startAction());
        QVERIFY(f.commands.step(Lane::Domain));
        QVERIFY(command->result());
        QVERIFY(command->result()->value);
        const auto result = *command->result()->value;
        const walletsigning::Error expected[] = {walletsigning::Error::None, walletsigning::Error::None,
            walletsigning::Error::ChangedTransaction, walletsigning::Error::InvalidFee,
            walletsigning::Error::SignerNotFound, walletsigning::Error::Uncertain};
        QCOMPARE(result.error, expected[scenario]);
        QCOMPARE(result.partial, scenario == 1 || scenario == 5);
        QCOMPARE(bool(result.signed_transaction), scenario == 0);
        if (scenario == 1 || scenario == 2) QVERIFY(result.document);
        if (scenario == 2) QCOMPARE(result.document->GetUnsignedTx()->GetHash(), request.document->GetUnsignedTx()->GetHash());
        QCOMPARE(f.wallet->commits, 0);
        QCOMPARE(f.wallet->signs, scenario == 3 || scenario == 4 ? 0 : scenario == 1 ? 2 : 1);
    }
    void admittedSignerRunsAfterQueueClosureUsingReservedOutput()
    {
        Fixture f;
        f.wallet->signer = true;
        const auto send = PsbtRequest();
        walletsigning::Request request{send.reviewed, send.psbt, send.proof, send.approved_fee};
        auto output = walletopening::RetainValues<walletsigning::Result>(f.opening, [] { return std::make_unique<walletsigning::Result>(); });
        QVERIFY(output);
        auto action = f.opening->startAction(); QVERIFY(action);
        auto command = std::make_shared<execution::OwnedCommand<walletsigning::Result>>(f.commands.scheduler, [] {});
        QVERIFY(command->startWith([&](auto done, auto failed) {
            return f.opening->perform(action, [request, output](interfaces::Wallet& wallet) {
                return walletsigning::ApproveInto(wallet, request, output);
            }, std::move(done), std::move(failed));
        }));
        f.opening->closeAdmission();
        f.commands.scheduler->closeAdmission();
        QVERIFY(f.commands.step(execution::Lane::Core));
        QCOMPARE(f.wallet->signs, 1);
        QVERIFY(!command->result());
        QVERIFY(f.commands.step(execution::Lane::Domain));
        QVERIFY(command->result()->value);
        QCOMPARE(command->result()->value->error, walletsigning::Error::None);
        QVERIFY(command->result()->value->signed_transaction);
        command.reset(); output.reset(); action.reset();
        f.commands.drain();
    }
    void signerPreservesAuthenticatedOutputOnError_data()
    {
        QTest::addColumn<bool>("throws");
        QTest::newRow("reported-failure") << false;
        QTest::newRow("throw-after-output") << true;
    }
    void signerPreservesAuthenticatedOutputOnError()
    {
        QFETCH(bool, throws);
        Fixture f;
        f.wallet->signer = true;
        f.wallet->mutate_then_throw = throws;
        f.wallet->report_sign_error = !throws;
        const auto send = PsbtRequest();
        walletsigning::Request request{send.reviewed, send.psbt, send.proof, send.approved_fee};
        const auto result = walletsigning::Approve(*f.wallet, request, f.opening);
        QCOMPARE(result.error, throws ? walletsigning::Error::Uncertain : walletsigning::Error::SignerFailed);
        QVERIFY(result.partial);
        QVERIFY(result.document);
        QVERIFY(result.lifetime);
        QCOMPARE(result.document->inputs[0].final_script_sig, CScript{} << OP_TRUE);
        QVERIFY(psbtfee::MatchesReview(*result.document, *request.proof, request.approved_fee));
        QCOMPARE(f.wallet->commits, 0);
    }
    void realWorkerConsumesCommitAfterCloseWithoutGuiDelivery()
    {
        struct State { QSemaphore entered, release, consumed; std::atomic<QThread*> effect{nullptr}, outcome{nullptr}, observation{nullptr}; };
        struct RealWallet : Wallet {
            std::shared_ptr<State> state;
            explicit RealWallet(std::shared_ptr<State> value) : state(std::move(value)) {}
            void commitTransaction(CTransactionRef transaction, const std::vector<std::string>&) override {
                state->effect = QThread::currentThread(); state->entered.release(); state->release.acquire(); last_transaction = std::move(transaction);
            }
            interfaces::WalletTx getWalletTxDetails(const Txid& id, interfaces::WalletTxStatus& status, std::vector<std::string>& messages, std::vector<std::string>& requests, bool& mempool, int& blocks) override {
                state->observation = QThread::currentThread(); return Wallet::getWalletTxDetails(id, status, messages, requests, mempool, blocks);
            }
        };
        auto state = std::make_shared<State>(); BackendExecutor commands{execution::Limits::Bounded()}, retirement;
        auto opening = std::make_shared<walletopening::Session>(commands.scheduler(), retirement.scheduler(), std::make_shared<std::atomic_bool>(false), [] {});
        auto wallet = std::make_shared<RealWallet>(state);wallet->encrypted=true;wallet->expected_password=std::string(40'000,'r');auto client = opening->start(wallet).borrower; QVERIFY(client);
        auto captured=secretinput::Capture(commands.scheduler(),QString::fromStdString(wallet->expected_password));QVERIFY(captured);
        auto request=PsbtRequest();request.captured_passphrase=captured.input;
        auto operation = std::make_shared<walletsend::Operation>(opening, nullptr, [state](auto) { state->outcome = QThread::currentThread(); state->consumed.release(); });
        QVERIFY(operation->start(std::move(request)));captured.input.reset();
        const auto release = qScopeGuard([state] { state->release.release(); });
        QVERIFY(state->entered.tryAcquire(1, 5000)); QVERIFY(state->effect.load() != QThread::currentThread());
        QCOMPARE(wallet->unlocks,1);QVERIFY(wallet->unlock_thread.load()!=QThread::currentThread());
        opening->retire(); client.reset(); wallet.reset(); commands.shutdown();
        QVERIFY(!operation->result()); state->release.release();
        QVERIFY(state->consumed.tryAcquire(1, 5000)); QVERIFY(state->outcome.load() != QThread::currentThread());
        QVERIFY(state->observation.load()); QVERIFY(state->observation.load() != QThread::currentThread());
        QVERIFY(operation->result()->committed); QCOMPARE(operation->result()->error, Error::None);
        QVERIFY(!commands.isDrained()); operation.reset();
        QTRY_VERIFY(commands.isDrained()); opening.reset(); retirement.shutdown(); QTRY_VERIFY(retirement.isDrained());
    }
    void acceptedCommitAndConsumedOutcomeSurviveCloseWithoutGui()
    {
        Fixture f; auto operation = f.operation(); QVERIFY(operation->start(SignedRequest()));
        const auto effect = f.commands.dispatchNext(Lane::Core); QVERIFY(effect); QCOMPARE(f.wallet->commits, 0);
        f.notes->close(); f.opening->retire(); QVERIFY(!operation->result());
        QVERIFY(f.commands.finishWork(*effect)); QCOMPARE(f.wallet->commits, 1); QVERIFY(!operation->result());
        const auto outcome = f.commands.deliverOutcome(); QVERIFY(outcome); QVERIFY(f.commands.stepMailbox(*outcome));
        QCOMPARE(operation->result()->error, Error::None); QVERIFY(operation->result()->committed); QCOMPARE(f.notifications, 1);
        operation.reset();
        f.commands.drain(); f.retirement.drain(); QVERIFY(f.commands.quiescent() && f.retirement.quiescent());
    }
    void committedSubmissionObservationIsIndependent_data()
    {
        QTest::addColumn<int>("scenario");
        const char* names[] = {"mempool", "confirmed", "not-in-mempool", "conflicted", "abandoned", "missing", "foreign-transaction", "read-failed"};
        for (int n{0}; n < 8; ++n) QTest::newRow(names[n]) << n;
    }
    void committedSubmissionObservationIsIndependent()
    {
        QFETCH(int, scenario); Fixture f;
        f.wallet->in_mempool = scenario == 0; f.wallet->depth = scenario == 1 ? 2 : scenario == 3 ? -1 : 0;
        f.wallet->abandoned = scenario == 4; f.wallet->missing = scenario == 5; f.wallet->mismatch = scenario == 6; f.wallet->throw_status = scenario == 7;
        const auto operation = f.operation(); QVERIFY(operation->start(SignedRequest())); f.commands.drain();
        const auto result = *operation->result(); const walletsend::Submission expected[] = {
            walletsend::Submission::Mempool, walletsend::Submission::Confirmed, walletsend::Submission::NotInMempool, walletsend::Submission::Conflicted,
            walletsend::Submission::Abandoned, walletsend::Submission::Unknown, walletsend::Submission::Unknown, walletsend::Submission::Unknown};
        QCOMPARE(result.error, Error::None); QVERIFY(result.committed); QCOMPARE(result.submission, expected[scenario]);
        QCOMPARE(f.wallet->commits, 1); QCOMPARE(f.wallet->status_reads, 1);
    }
    void heldOutcomeRetainsTheNativeObservationRatherThanLaterState()
    {
        Fixture f; f.wallet->in_mempool = false; auto operation = f.operation(); QVERIFY(operation->start(SignedRequest()));
        QVERIFY(f.commands.step(Lane::Core)); QVERIFY(!operation->result()); f.wallet->in_mempool = true;
        f.commands.drain(); QCOMPARE(operation->result()->submission, walletsend::Submission::NotInMempool); QCOMPARE(f.wallet->status_reads, 1);
    }
    void publicationFailureCannotOverwriteAConfirmedCommit()
    {
        Fixture f; auto operation = std::make_shared<walletsend::Operation>(f.opening, f.notes, [](auto) { throw std::runtime_error{"publication transport failed"}; });
        QVERIFY(operation->start(SignedRequest())); f.commands.drain();
        QVERIFY(operation->result()->committed); QCOMPARE(operation->result()->error, Error::None); QCOMPARE(f.wallet->commits, 1);
        QVERIFY(operation->result()->publication_failed);
    }
    void sameOperationCannotCommitTwice()
    {
        Fixture f; auto operation = f.operation(); QVERIFY(operation->start(SignedRequest()));
        QCOMPARE(operation->start(SignedRequest()).rejection, execution::Rejection::Invalid);
        f.commands.drain(); QCOMPARE(f.wallet->commits, 1); QCOMPARE(f.notifications, 1);
    }
    void closedAndRejectedAdmissionCannotClaimCommit_data()
    {
        QTest::addColumn<bool>("close"); QTest::newRow("closed") << true; QTest::newRow("transport-rejected") << false;
    }
    void closedAndRejectedAdmissionCannotClaimCommit()
    {
        QFETCH(bool, close); Fixture f; if (close) f.opening->retire(); else f.commands.rejectLane(Lane::Core);
        auto operation = f.operation(); const auto admission = operation->start(SignedRequest());
        if (close) QVERIFY(!admission);
        f.commands.drain(); QVERIFY(operation->result()); QCOMPARE(operation->result()->error, Error::Rejected);
        QVERIFY(!operation->result()->committed); QCOMPARE(f.wallet->commits, 0);
    }
    void psbtSigningPreservesReviewAndRestoresLock_data()
    {
        QTest::addColumn<int>("scenario");
        for (int n{0}; n < 7; ++n) QTest::newRow(qPrintable(QString::number(n))) << n;
    }
    void psbtSigningPreservesReviewAndRestoresLock()
    {
        QFETCH(int, scenario); Fixture f; f.wallet->encrypted = true;
        auto request = PsbtRequest(); QCOMPARE(request.proof->state, psbtfee::State::Known);
        if (scenario != 1) request.passphrase = SecureString{scenario == 2 ? "wrong" : "secret"};
        f.wallet->change_transaction = scenario == 3; f.wallet->throw_sign = scenario == 4; f.wallet->fail_lock = scenario == 5;
        if (scenario == 6) request.approved_fee = 999;
        const auto operation = f.operation(); QVERIFY(operation->start(std::move(request))); f.commands.drain();
        const auto result = *operation->result();
        const Error expected[] = {Error::None, Error::NeedsUnlock, Error::IncorrectPassphrase, Error::ChangedTransaction, Error::Uncertain, Error::Uncertain, Error::InvalidFee};
        QCOMPARE(result.error, expected[scenario]); QCOMPARE(f.wallet->commits, scenario == 0 ? 1 : 0);
        QCOMPARE(bool(result.committed), scenario == 0);
        if (scenario == 0 || scenario == 3 || scenario == 4) { QVERIFY(f.wallet->locked); QCOMPARE(f.wallet->locks, 1); }
        QCOMPARE(result.relock_failed, scenario == 5);
        if (scenario == 2) QCOMPARE(f.wallet->locks, 0); // A known failed unlock owns no lock transition.
    }
    void notesAreConsequencesOfTheOriginalConfirmedCommit()
    {
        Fixture f; auto request = SignedRequest(); request.notes.insert("1BoatSLRHtKNngkdXEeobR76b53LETtpyT", "outgoing note");
        auto operation = f.operation(); QVERIFY(operation->start(request));
        f.notes->close(); f.opening->retire(); f.commands.drain(); f.retirement.drain();
        const auto result = *operation->result(); QVERIFY(result.committed); QVERIFY(result.notes);
        QCOMPARE(result.notes->result()->error, walletrequests::Error::None); QCOMPARE(f.wallet->labels, 1);
        Fixture failed; failed.wallet->throw_commit = true; auto uncertain = failed.operation(); QVERIFY(uncertain->start(std::move(request))); failed.commands.drain();
        QCOMPARE(uncertain->result()->error, Error::Uncertain); QVERIFY(!uncertain->result()->committed); QCOMPARE(failed.wallet->labels, 0);
    }
};
BITCOINQML_REGISTER_QT_TEST(WalletSendTests)
#include <test/test_walletsend.moc>
