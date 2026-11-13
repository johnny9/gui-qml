// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <qml/models/psbtqmlmodel.h>
#include <qml/backendexecutor.h>
#include <test/mocks/mocknode.h>
#include <test/mocks/manualexecutionenvironment.h>
#include <test/mocks/mockwallet.h>
#include <test/mocks/psbtfeefixture.h>

#include <chainparams.h>
#include <key_io.h>
#include <node/types.h>
#include <primitives/transaction.h>
#include <psbt.h>
#include <script/script.h>

#include <QFile>
#include <QTemporaryDir>
#include <QThread>

#include <algorithm>
#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace {
const auto VALID_MAINNET_ADDRESS = QStringLiteral("1BoatSLRHtKNngkdXEeobR76b53LETtpyT");

class PsbtTestWallet : public StubWallet
{
public:
    bool private_keys_disabled{false};
    size_t signable{1};
    bool encrypted{false},locked{false};int unlock_calls{0},lock_calls{0};
    std::string expected_password,observed_password;QThread* unlock_thread{nullptr};
    bool isCrypted() override{return encrypted;}
    bool isLocked() override{return locked;}
    bool lock() override{++lock_calls;locked=true;return true;}
    util::Expected<void,wallet::WalletError> unlock(const SecureString& password) override{
        ++unlock_calls;unlock_thread=QThread::currentThread();observed_password.assign(password.data(),password.size());
        if(observed_password!=expected_password)return util::Unexpected{wallet::WalletError{wallet::WalletErrorCode::PassphraseIncorrect,Untranslated("incorrect test password")}};
        locked=false;return {};
    }
    std::optional<common::PSBTError> fill_error;
    std::vector<bool> sign_args;
    std::function<void(PartiallySignedTransaction&)> on_sign;
    std::function<void(PartiallySignedTransaction&)> on_inspect;
    std::vector<bool> derivation_args;
    QThread* effect_thread{nullptr};
    int private_accesses{0};
    wallet::CWallet* wallet() override { ++private_accesses; throw std::runtime_error("Private wallet access forbidden"); }

    std::optional<common::PSBTError> fillPSBT(const common::PSBTFillOptions& options,
                                              size_t* n_signed,
                                              PartiallySignedTransaction& psbt,
                                              bool& complete) override
    {
        effect_thread = QThread::currentThread();
        sign_args.push_back(options.sign);
        derivation_args.push_back(options.bip32_derivs);
        if (n_signed) {
            *n_signed = signable;
        }
        if (options.sign && on_sign) on_sign(psbt);
        if (!options.sign && on_inspect) on_inspect(psbt);
        complete = FinalizePSBT(psbt);
        return fill_error;
    }

    bool privateKeysDisabled() override { return private_keys_disabled; }
};

PartiallySignedTransaction MakeUnsignedPsbt()
{
    auto psbt = psbtfeefixture::Legacy(2, false);
    for (auto& input : psbt.inputs) { input.final_script_sig.clear(); input.final_script_witness.SetNull(); }
    psbt.inputs.erase(psbt.inputs.begin() + 1, psbt.inputs.end());
    psbt.inputs[0].non_witness_utxo = nullptr;
    CMutableTransaction previous;
    previous.vin.emplace_back(COutPoint{Txid::FromUint256(uint256::ONE), 0});
    previous.vout.emplace_back(2'000, CScript{} << OP_DROP << OP_TRUE);
    const auto parent = MakeTransactionRef(previous);
    psbt.inputs[0].prev_txid = parent->GetHash();
    psbt.inputs[0].prev_out = 0;
    psbt.inputs[0].non_witness_utxo = parent;
    psbt.outputs[0].amount = 1'500;
    return psbt;
}

PartiallySignedTransaction MakeCompletePsbt()
{
    CMutableTransaction previous_tx;
    previous_tx.vin.emplace_back(COutPoint{Txid::FromUint256(uint256::ONE), 0});
    previous_tx.vout.emplace_back(2'000, CScript{} << OP_DROP << OP_TRUE);
    const CTransactionRef previous{MakeTransactionRef(previous_tx)};

    CMutableTransaction tx;
    tx.vin.emplace_back(COutPoint{previous->GetHash(), 0});
    tx.vout.emplace_back(1'500, GetScriptForDestination(DecodeDestination(VALID_MAINNET_ADDRESS.toStdString())));

    PartiallySignedTransaction psbt{tx, /*version=*/2};
    psbt.inputs[0].non_witness_utxo = previous;
    psbt.inputs[0].final_script_sig = CScript{} << std::vector<unsigned char>{};
    return psbt;
}

std::optional<PartiallySignedTransaction> DecodeRawPsbt(const QString& path)
{
    QFile file{path};
    if (!file.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }
    const QByteArray bytes{file.readAll()};
    std::vector<std::byte> raw;
    raw.reserve(bytes.size());
    for (const char byte : bytes) {
        raw.push_back(static_cast<std::byte>(static_cast<unsigned char>(byte)));
    }

    const auto psbt{DecodeRawPSBT(std::span<const std::byte>{raw.data(), raw.size()})};
    if (!psbt) {
        return std::nullopt;
    }
    return *psbt;
}
void Configure(PsbtQmlModel& model, const std::shared_ptr<PsbtTestWallet>& wallet)
{
    auto executor = std::make_shared<BackendExecutor>();
    auto opening = std::make_shared<walletopening::Session>(executor->scheduler(), executor->scheduler(),
        std::make_shared<std::atomic_bool>(false), [] {});
    opening->start(wallet);
    model.configureSession(opening, {executor->scheduler(), {}, {}});
    QObject::connect(&model, &QObject::destroyed, [opening, executor] {
        opening->retire(); executor->shutdown();
    });
}
bool AwaitIdle(PsbtQmlModel& model)
{
    return QTest::qWaitFor([&] { return !model.busy(); }, 5000);
}
QString AwaitLoad(PsbtQmlModel& model, const QString& path)
{
    const auto ticket = model.requestLoadFromFile(path);
    if (!ticket || !AwaitIdle(model) || model.operationId() != ticket) return QStringLiteral("Load did not complete");
    return model.phase() == PsbtQmlModel::Phase::Ready ? QString{} : model.error();
}
QString AwaitSave(PsbtQmlModel& model, const QString& path)
{
    const auto ticket = model.requestSaveToFile(path);
    if (!ticket || !AwaitIdle(model) || model.operationId() != ticket) return QStringLiteral("Save did not complete");
    return model.saved() ? QString{} : model.error();
}
} // namespace

class PsbtQmlModelTests : public QObject
{
    Q_OBJECT
    std::unique_ptr<ECC_Context> m_ecc_context;

private Q_SLOTS:
    void assessedScriptsKeepOriginalExportAndBroadcast_data()
    {
        QTest::addColumn<bool>("nested");
        QTest::addColumn<bool>("close_before_effect");
        for (bool nested : {false, true}) for (bool close : {false, true})
            QTest::newRow(qPrintable(QStringLiteral("%1-%2").arg(nested ? "nested" : "witness").arg(close ? "closed" : "live"))) << nested << close;
    }
    void assessedScriptsKeepOriginalExportAndBroadcast()
    {
        QFETCH(bool, nested);
        QFETCH(bool, close_before_effect);
        const auto original = psbtfeefixture::SignedWithoutScripts(nested);
        const auto raw = psbtwork::SerializeRaw(original.document);
        auto expected_document = original.document;
        expected_document.inputs[0].witness_script = original.witness;
        expected_document.inputs[0].redeem_script = original.redeem;
        CMutableTransaction expected;
        QVERIFY(FinalizeAndExtractPSBT(expected_document, expected));
        ManualRoutedExecution transport;
        std::deque<std::function<void()>> publications;
        auto wallet = std::make_shared<PsbtTestWallet>();
        wallet->private_keys_disabled = true;
        wallet->encrypted = true; wallet->locked = true;
        wallet->on_inspect = [original](PartiallySignedTransaction& document) {
            document.inputs[0].witness_script = original.witness;
            document.inputs[0].redeem_script = original.redeem;
        };
        MockNode node;
        CTransactionRef submitted;
        node.broadcast_transaction_fn = [&](CTransactionRef transaction, CAmount, CFeeRate, std::string&) {
            submitted = std::move(transaction);
            return node::TransactionError::OK;
        };
        auto opening = std::make_shared<walletopening::Session>(transport.scheduler, transport.scheduler,
            std::make_shared<std::atomic_bool>(false), [] {});
        QVERIFY(opening->start(wallet)); transport.drain(); opening->take();
        auto model = std::make_unique<PsbtQmlModel>(wallet.get(), &node);
        model->configureSession(opening, {transport.scheduler, {}, [&](auto turn) { publications.push_back(std::move(turn)); }});
        const auto publish_all = [&] {
            while (!publications.empty()) {
                auto turn = std::exchange(publications.front(), {});
                publications.pop_front(); turn();
            }
        };
        const auto cleanup = qScopeGuard([&] {
            model.reset(); opening->retire(); transport.drain(); publish_all(); transport.drain();
        });
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto source_path = directory.filePath("original.psbt");
        QCOMPARE(PsbtQmlModel::SavePsbtToFile(original.document, source_path), QString{});
        QVERIFY(model->requestLoadFromFile(source_path));
        QVERIFY(transport.step(execution::Lane::Core));
        QCOMPARE(wallet->sign_args, std::vector<bool>{false});
        QVERIFY(!model->loaded());
        QVERIFY(transport.step(execution::Lane::Domain));
        QVERIFY(!model->loaded());
        publish_all(); transport.drain();
        QVERIFY(model->complete()); QVERIFY(model->canBroadcast()); QVERIFY(!model->canSign());
        QVERIFY(model->psbt());
        QCOMPARE(psbtwork::SerializeRaw(*model->psbt()), raw);
        QCOMPARE(model->psbt()->inputs[0].partial_sigs.size(), size_t{2});
        const auto saved_path = directory.filePath("saved.psbt");
        QVERIFY(model->requestSaveToFile(saved_path)); transport.drain(); publish_all(); transport.drain();
        QVERIFY(model->saved());
        QFile saved{saved_path}; QVERIFY(saved.open(QIODevice::ReadOnly)); QCOMPARE(saved.readAll(), raw);
        QVERIFY(model->requestBroadcast());
        if (close_before_effect) { model->closeAdmission(); opening->retire(); }
        QVERIFY(transport.step(execution::Lane::Core));
        QCOMPARE(node.calls.broadcastTransaction.load(), 1);
        QVERIFY(submitted);
        QCOMPARE(submitted->GetWitnessHash(), MakeTransactionRef(expected)->GetWitnessHash());
        QCOMPARE(wallet->sign_args, (std::vector<bool>{false, false}));
        QCOMPARE(wallet->derivation_args, (std::vector<bool>{false, false}));
        QCOMPARE(wallet->unlock_calls, 0); QCOMPARE(wallet->lock_calls, 0); QVERIFY(wallet->locked);
        QVERIFY(!model->submitted());
        QVERIFY(transport.step(execution::Lane::Domain));
        QVERIFY(model->submitted()); // Receipt survives observer close and held GUI.
        QCOMPARE(psbtwork::SerializeRaw(original.document), raw);
        model.reset(); opening->retire(); transport.drain(); publish_all(); transport.drain();
        QVERIFY(transport.quiescent());
    }
    void authenticatedIncompleteDocumentStillNeedsSigningAuthority_data()
    {
        QTest::addColumn<int>("authority");
        QTest::newRow("private-keys-disabled") << 0;
        QTest::newRow("no-matching-key") << 1;
        QTest::newRow("wallet-fill-failed") << 2;
    }
    void authenticatedIncompleteDocumentStillNeedsSigningAuthority()
    {
        QFETCH(int, authority);
        auto wallet_owner = std::make_shared<PsbtTestWallet>(); auto& wallet = *wallet_owner; MockNode node;
        wallet.private_keys_disabled = authority == 0;
        if (authority != 0) wallet.signable = 0;
        if (authority == 2) wallet.fill_error = common::PSBTError::MISSING_INPUTS;
        PsbtQmlModel model{&wallet, &node}; Configure(model, wallet_owner);
        model.refreshState(); // An absent document has no authority.
        QTemporaryDir directory; QVERIFY(directory.isValid()); const auto path = directory.filePath("partial.psbt");
        QCOMPARE(PsbtQmlModel::SavePsbtToFile(MakeUnsignedPsbt(), path), QString{});
        QCOMPARE(AwaitLoad(model, path), QString{});
        QVERIFY(!model.canSign()); QVERIFY(!model.canBroadcast());
        model.broadcast(); QVERIFY(AwaitIdle(model));
        QCOMPARE(node.calls.broadcastTransaction.load(), 0);
        if (authority == 0) { model.sign(); QVERIFY(AwaitIdle(model)); }
        model.detachWallet(); model.refreshState(); model.setNode(nullptr);
        QVERIFY(!model.canSign()); QVERIFY(!model.canBroadcast());
    }

    void capturedLongPasswordUsesOriginalActionAndNativeMaterialization_data()
    {
        QTest::addColumn<bool>("close_before_effect");
        QTest::newRow("held-gui-result")<<false;
        QTest::newRow("original-action-after-close")<<true;
    }
    void capturedLongPasswordUsesOriginalActionAndNativeMaterialization()
    {
        QFETCH(bool,close_before_effect);ManualExecutionEnvironment environment;
        auto wallet=std::make_shared<PsbtTestWallet>();wallet->encrypted=true;wallet->locked=true;
        wallet->expected_password=std::string(40'000,'x');
        auto opening=std::make_shared<walletopening::Session>(environment.scheduler,environment.scheduler,std::make_shared<std::atomic_bool>(false),[]{});
        QVERIFY(opening->start(wallet));environment.drain();opening->take();
        PsbtQmlModel model{wallet.get(),nullptr};model.configureSession(opening,{environment.scheduler,{},[&](auto turn){environment.post(std::move(turn));}});
        QTemporaryDir directory;QVERIFY(directory.isValid());const auto path=directory.filePath("long-secret.psbt");
        QCOMPARE(PsbtQmlModel::SavePsbtToFile(MakeUnsignedPsbt(),path),QString{});
        QVERIFY(model.requestLoadFromFile(path));environment.drain();environment.publishAll();QVERIFY(model.canSign());
        QString original=QString::fromStdString(wallet->expected_password);
        const auto ticket=model.requestSignWithPassphrase(original);QVERIFY(ticket);QCOMPARE(wallet->unlock_calls,0);
        original.fill(QChar{'y'}); // Editing the source cannot retarget the captured action.
        if(close_before_effect){model.detachWallet();opening->retire();}
        QVERIFY(environment.step()); // Core owns materialization, unlock, signing and relock.
        QCOMPARE(wallet->unlock_calls,1);QCOMPARE(wallet->observed_password,wallet->expected_password);
        QCOMPARE(wallet->lock_calls,1);QVERIFY(wallet->locked);
        if(!close_before_effect)QVERIFY(model.busy());
        environment.drain(); // Actual result recorded while GUI publication remains held.
        QSignalSpy notices{&model,&PsbtQmlModel::changed};environment.publishAll();
        if(close_before_effect)QCOMPARE(notices.size(),0);
        else{QCOMPARE(model.operationId(),ticket);QCOMPARE(model.phase(),PsbtQmlModel::Phase::Ready);QCOMPARE(wallet->private_accesses,0);model.detachWallet();opening->retire();}
        environment.drain();environment.publishAll();QVERIFY(environment.quiescent());
    }

    void feeProofCannotOutliveDocument_data()
    {
        QTest::addColumn<int>("scenario");
        const std::array names{"success", "unknown", "throw", "rejected", "cleared", "replaced", "destroyed", "detached", "node-changed"};
        for (int i = 0; i < int(names.size()); ++i) QTest::newRow(names[i]) << i;
    }
    void acceptedBroadcastAndDrainDoNotRequireGuiPublication()
    {
        ManualRoutedExecution transport;
        std::deque<std::function<void()>> publications;
        MockNode node;
        QString actually_submitted;
        node.broadcast_transaction_fn = [&](CTransactionRef tx, CAmount, CFeeRate, std::string&) {
            actually_submitted = QString::fromStdString(tx->GetHash().ToString());
            return node::TransactionError::OK;
        };
        PsbtQmlModel model{nullptr, &node};
        model.setFeeServices({transport.scheduler, {}, [&](auto turn) { publications.push_back(std::move(turn)); }});
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto path = directory.filePath("complete.psbt");
        QCOMPARE(PsbtQmlModel::SavePsbtToFile(MakeCompletePsbt(), path), QString{});
        QVERIFY(model.requestLoadFromFile(path));
        QVERIFY(!model.loaded());
        QVERIFY(transport.step(execution::Lane::Core));
        QVERIFY(!model.loaded());
        QVERIFY(transport.step(execution::Lane::Domain));
        QVERIFY(!model.loaded()); QCOMPARE(publications.size(), size_t{1});
        auto publish = std::move(publications.front()); publications.pop_front(); publish();
        QVERIFY(model.loaded()); QVERIFY(model.canBroadcast());
        QVERIFY(model.requestBroadcast());
        QVERIFY(!model.requestBroadcast());
        QCOMPARE(node.calls.broadcastTransaction.load(), 0);
        QVERIFY(transport.step(execution::Lane::Core));
        QCOMPARE(node.calls.broadcastTransaction.load(), 1);
        QVERIFY(!model.submitted());
        model.detachWallet();
        QVERIFY(transport.step(execution::Lane::Domain));
        QVERIFY(model.submitted());
        QCOMPARE(model.submittedTxid(), actually_submitted);
        QVERIFY(!model.canBroadcast());
        QVERIFY(!publications.empty()); // Completion presentation remains held.
        transport.drain();
        QVERIFY(transport.quiescent());
        QCOMPARE(node.calls.broadcastTransaction.load(), 1);
    }
    void feeProofCannotOutliveDocument()
    {
        QFETCH(int, scenario);
        ManualExecutionEnvironment environment;
        auto wallet = std::make_shared<PsbtTestWallet>();
        auto opening = std::make_shared<walletopening::Session>(environment.scheduler, environment.scheduler,
            std::make_shared<std::atomic_bool>(false), [] {});
        QVERIFY(opening->start(wallet)); environment.drain(); opening->take();
        auto model = std::make_unique<PsbtQmlModel>(wallet.get(), nullptr);
        auto document = psbtfeefixture::Legacy(2, false);
        psbtfee::Outputs actual;
        for (auto& input : document.inputs) {
            actual.emplace(input.GetOutPoint(), input.non_witness_utxo->vout[input.prev_out]);
            input.witness_utxo = input.non_witness_utxo->vout[input.prev_out]; input.non_witness_utxo = nullptr;
        }
        int reads{0};
        model->configureSession(opening, {environment.scheduler,
            [&reads, &actual, scenario](const COutPoint& outpoint) -> std::optional<CTxOut> {
                ++reads;
                if (scenario == 1) return {};
                if (scenario == 2) throw std::runtime_error("lookup failed");
                return actual.at(outpoint);
            }, [&environment](auto publication) { environment.post(std::move(publication)); }});
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto path = directory.filePath("needs-proof.psbt");
        QCOMPARE(PsbtQmlModel::SavePsbtToFile(document, path), QString{});
        environment.rejectNext(scenario == 3);
        QVERIFY(model->requestLoadFromFile(path));
        QCOMPARE(reads, 0); QVERIFY(!model->loaded()); QVERIFY(!model->canBroadcast());
        if (scenario == 3) {
            environment.drain(); environment.publishAll();
            QCOMPARE(reads, 0); QVERIFY(!model->loaded()); QVERIFY(!model->error().isEmpty());
        } else {
            QVERIFY(environment.step()); // Native file/decode/lookup, no GUI publication.
            QVERIFY(reads > 0); QVERIFY(!model->loaded());
            QVERIFY(environment.step()); // Owned Domain result, GUI still held.
            QVERIFY(!model->loaded());
            if (scenario == 4) model->clear();
            if (scenario == 5) {
                const auto replacement = directory.filePath("replacement.psbt");
                QCOMPARE(PsbtQmlModel::SavePsbtToFile(MakeUnsignedPsbt(), replacement), QString{});
                QVERIFY(model->requestLoadFromFile(replacement));
            }
            if (scenario == 6) model.reset();
            if (scenario == 7) model->detachWallet();
            if (scenario == 8) { MockNode replacement; model->setNode(&replacement); model->setNode(nullptr); }
            environment.drain(); environment.publishAll();
            if (scenario == 0) { QVERIFY(model->loaded()); QVERIFY(model->canBroadcast()); }
            if (scenario == 1 || scenario == 2) { QVERIFY(model->loaded()); QVERIFY(!model->canBroadcast()); QVERIFY(!model->error().isEmpty()); }
            if (scenario == 4 || scenario == 7 || scenario == 8) { QVERIFY(!model->loaded()); QVERIFY(!model->canBroadcast()); }
            if (scenario == 5) { QVERIFY(model->loaded()); QVERIFY(!model->canBroadcast()); }
        }
        if (model) model->detachWallet();
        model.reset();
        opening->retire(); environment.drain(); environment.publishAll();
        QVERIFY(environment.quiescent());
    }

    void invalidDataCannotRenderOrSign_data()
    {
        QTest::addColumn<int>("mutation");
        QTest::newRow("invalid-output") << 0;
        QTest::newRow("incompatible-timelocks") << 1;
        QTest::newRow("duplicate-input") << 2;
        QTest::newRow("contradictory-amount") << 3;
    }
    void invalidDataCannotRenderOrSign()
    {
        QFETCH(int, mutation);
        auto psbt = psbtfeefixture::Legacy(2, false);
        switch (mutation) {
        case 0: psbt.outputs[0].amount = MAX_MONEY + 1; break;
        case 1: psbt.inputs[0].time_locktime = 500000001; psbt.inputs[1].height_locktime = 42; break;
        case 2: psbt.inputs[1].prev_out = psbt.inputs[0].prev_out; break;
        case 3: psbt.inputs[0].witness_utxo = psbt.inputs[0].non_witness_utxo->vout[0]; --psbt.inputs[0].witness_utxo.nValue; break;
        }
        auto wallet_owner = std::make_shared<PsbtTestWallet>(); auto& wallet = *wallet_owner; MockNode node; PsbtQmlModel model{&wallet, &node}; Configure(model, wallet_owner);
        QTemporaryDir directory; QVERIFY(directory.isValid()); const auto path = directory.filePath("invalid.psbt");
        QCOMPARE(PsbtQmlModel::SavePsbtToFile(psbt, path), QString{});
        QCOMPARE(AwaitLoad(model, path), QString{});
        QVERIFY(model.loaded()); QVERIFY(!model.canSign()); QVERIFY(!model.canBroadcast());
        QVERIFY(!model.error().isEmpty()); QVERIFY(!model.summary().isEmpty());
        model.sign(); QVERIFY(AwaitIdle(model)); model.broadcast(); QVERIFY(AwaitIdle(model));
        QVERIFY(wallet.sign_args.empty()); QCOMPARE(node.calls.broadcastTransaction.load(), 0);
    }
    void unknownFeeBlocksDirectSigning()
    {
        auto wallet_owner = std::make_shared<PsbtTestWallet>(); auto& wallet = *wallet_owner; MockNode node; PsbtQmlModel model{&wallet, &node}; Configure(model, wallet_owner);
        QTemporaryDir directory; QVERIFY(directory.isValid()); const auto path = directory.filePath("unknown.psbt");
        QCOMPARE(PsbtQmlModel::SavePsbtToFile(psbtfeefixture::Legacy(2, true), path), QString{});
        QCOMPARE(AwaitLoad(model, path), QString{});
        QVERIFY(!model.canSign()); QVERIFY(!model.canBroadcast());
        model.sign(); QVERIFY(AwaitIdle(model)); model.broadcast(); QVERIFY(AwaitIdle(model));
        QCOMPARE(std::count(wallet.sign_args.begin(), wallet.sign_args.end(), true), 0);
        QCOMPARE(node.calls.broadcastTransaction.load(), 0);
    }
    void signerFailurePreservesAuthenticatedPartialSignatures_data()
    {
        QTest::addColumn<bool>("throws");
        QTest::newRow("reported-error") << false;
        QTest::newRow("exception-after-signing") << true;
    }
    void signerFailurePreservesAuthenticatedPartialSignatures()
    {
        QFETCH(bool, throws);
        auto wallet_owner = std::make_shared<PsbtTestWallet>(); auto& wallet = *wallet_owner;
        PsbtQmlModel model{&wallet, nullptr}; Configure(model, wallet_owner);
        const auto signed_document = psbtfeefixture::Legacy(2, false);
        auto partial = signed_document;
        for (auto& input : partial.inputs) { input.final_script_sig.clear(); input.final_script_witness.SetNull(); }
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto path = directory.filePath("partial.psbt");
        QCOMPARE(PsbtQmlModel::SavePsbtToFile(partial, path), QString{});
        QCOMPARE(AwaitLoad(model, path), QString{});
        QVERIFY(model.canSign());
        wallet.on_sign = [&](PartiallySignedTransaction& document) {
            document.inputs[0] = signed_document.inputs[0];
            if (throws) throw std::runtime_error("signer failed after adding a valid signature");
        };
        if (!throws) wallet.fill_error = common::PSBTError::MISSING_INPUTS;
        QVERIFY(model.requestSign()); QVERIFY(AwaitIdle(model));
        QVERIFY(model.loaded()); QVERIFY(!model.complete()); QVERIFY(!model.canBroadcast());
        QVERIFY(!model.error().isEmpty());
        QVERIFY(model.psbt()->inputs[0].final_script_sig == signed_document.inputs[0].final_script_sig);
        QVERIFY(model.psbt()->inputs[1].final_script_sig.empty());
        QCOMPARE(model.psbt()->GetUnsignedTx()->GetHash(), signed_document.GetUnsignedTx()->GetHash());
        QCOMPARE(wallet.private_accesses, 0);
        const auto exported = directory.filePath("retained.psbt");
        QCOMPARE(AwaitSave(model, exported), QString{});
        const auto saved = DecodeRawPsbt(exported); QVERIFY(saved.has_value());
        QVERIFY(saved->inputs[0].final_script_sig == signed_document.inputs[0].final_script_sig);
    }
    void signingCannotReplaceApprovedFeeMetadata()
    {
        auto wallet_owner = std::make_shared<PsbtTestWallet>(); auto& wallet = *wallet_owner; MockNode node; PsbtQmlModel model{&wallet, &node}; Configure(model, wallet_owner);
        QTemporaryDir directory; QVERIFY(directory.isValid()); const auto path = directory.filePath("approved.psbt");
        const auto approved = psbtfeefixture::Legacy(2, false);
        auto incomplete = approved;
        incomplete.inputs[1].final_script_sig.clear(); incomplete.inputs[1].final_script_witness.SetNull();
        QCOMPARE(PsbtQmlModel::SavePsbtToFile(incomplete, path), QString{});
        QCOMPARE(AwaitLoad(model, path), QString{});
        wallet.on_sign = [](PartiallySignedTransaction& psbt) {
            auto& input = psbt.inputs[1];
            input.witness_utxo = input.non_witness_utxo->vout[input.prev_out];
            input.witness_utxo.nValue -= psbtfeefixture::ACTUAL_FEE - 1;
            input.non_witness_utxo = nullptr;
        };
        model.sign(); QVERIFY(AwaitIdle(model));
        QCOMPARE(model.error(), QString("The transaction fee changed after review."));
        QVERIFY(model.psbt()->inputs[1].non_witness_utxo);
        QCOMPARE(model.psbt()->inputs[1].non_witness_utxo->GetHash(), approved.inputs[1].non_witness_utxo->GetHash());
        QCOMPARE(node.calls.broadcastTransaction.load(), 0);
        wallet.on_sign = [approved](PartiallySignedTransaction& candidate) { candidate.inputs[1] = approved.inputs[1]; };
        model.sign(); QVERIFY(AwaitIdle(model)); QVERIFY(model.canBroadcast());
    }
    void legacyFeeMetadataCannotEnableBroadcast_data()
    {
        QTest::addColumn<uint>("version"); QTest::addColumn<bool>("forged");
        for (uint version : {0U, 2U}) for (bool forged : {false, true}) {
            const auto name = QString("v%1-%2").arg(version).arg(forged ? "forged" : "control").toUtf8();
            QTest::newRow(name.constData()) << version << forged;
        }
    }
    void legacyFeeMetadataCannotEnableBroadcast()
    {
        QFETCH(uint, version); QFETCH(bool, forged);
        MockNode node;
        node.broadcast_transaction_fn = [](CTransactionRef, CAmount, CFeeRate, std::string&) { return node::TransactionError::OK; };
        PsbtQmlModel model{nullptr, &node};
        auto psbt = psbtfeefixture::Legacy(version, forged);
        CMutableTransaction extracted;
        QVERIFY(FinalizeAndExtractPSBT(psbt, extracted));
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto path = directory.filePath("legacy.psbt");
        QCOMPARE(PsbtQmlModel::SavePsbtToFile(psbt, path), QString{});
        QCOMPARE(AwaitLoad(model, path), QString{});
        QVERIFY(model.complete());
        const bool can_broadcast = model.canBroadcast();
        model.broadcast(); QVERIFY(AwaitIdle(model));
        if (forged) {
            QVERIFY(!can_broadcast); QCOMPARE(node.calls.broadcastTransaction.load(), 0);
            QVERIFY(model.summary().contains(QString("Transaction fee is not available.")));
        } else {
            QVERIFY(can_broadcast); QCOMPARE(node.calls.broadcastTransaction.load(), 1);
            QVERIFY(model.summary().join(' ').contains("0.00500000 BTC"));
        }
    }
    void initTestCase();
    void cleanupTestCase() { m_ecc_context.reset(); }
    void loadsRawAndBase64Psbt();
    void invalidFileSetsErrorAndClearResetsState();
    void signUsesInjectedWallet();
    void savePreservesPsbtMetadata();
    void broadcastsCompletePsbt();
};

void PsbtQmlModelTests::initTestCase()
{
    SelectParams(ChainType::MAIN);
    m_ecc_context = std::make_unique<ECC_Context>();
}

void PsbtQmlModelTests::loadsRawAndBase64Psbt()
{
    auto wallet_owner = std::make_shared<PsbtTestWallet>(); auto& wallet = *wallet_owner;
    PsbtQmlModel model{&wallet, nullptr}; Configure(model, wallet_owner);
    const PartiallySignedTransaction psbt{MakeUnsignedPsbt()};

    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    const QString raw_path{temp_dir.filePath(QStringLiteral("raw.psbt"))};
    QCOMPARE(PsbtQmlModel::SavePsbtToFile(psbt, raw_path), QString{});
    QCOMPARE(AwaitLoad(model, raw_path), QString{});
    QVERIFY(model.loaded());
    QVERIFY(model.canSign());
    QCOMPARE(model.couldSignInputCount(), 1);
    QCOMPARE(model.unsignedInputCount(), 1);

    const QString base64_path{temp_dir.filePath(QStringLiteral("base64.psbt"))};
    QFile base64_file{base64_path};
    QVERIFY(base64_file.open(QIODevice::WriteOnly));
    const QByteArray base64{PsbtQmlModel::SerializePsbtRaw(psbt).toBase64()};
    QCOMPARE(base64_file.write(base64), base64.size());
    base64_file.close();

    model.clear();
    QCOMPARE(AwaitLoad(model, base64_path), QString{});
    QVERIFY(model.loaded());
    QCOMPARE(model.unsignedInputCount(), 1);
}

void PsbtQmlModelTests::invalidFileSetsErrorAndClearResetsState()
{
    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    const QString path{temp_dir.filePath(QStringLiteral("invalid.psbt"))};
    QFile file{path};
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("not a psbt"), 10LL);
    file.close();

    PsbtQmlModel model{nullptr, nullptr};
    QVERIFY(!AwaitLoad(model, path).isEmpty());
    QVERIFY(!model.loaded());
    QVERIFY(!model.error().isEmpty());

    model.clear();
    QVERIFY(model.error().isEmpty());
    QVERIFY(model.status().isEmpty());
}

void PsbtQmlModelTests::signUsesInjectedWallet()
{
    auto wallet_owner = std::make_shared<PsbtTestWallet>(); auto& wallet = *wallet_owner;
    PsbtQmlModel model{&wallet, nullptr}; Configure(model, wallet_owner);

    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    const QString path{temp_dir.filePath(QStringLiteral("unsigned.psbt"))};
    QCOMPARE(PsbtQmlModel::SavePsbtToFile(MakeUnsignedPsbt(), path), QString{});
    QCOMPARE(AwaitLoad(model, path), QString{});

    wallet.encrypted=true;wallet.locked=true;wallet.expected_password=std::string(40'000,'p');
    QVERIFY(model.requestSignWithPassphrase(QString::fromStdString(wallet.expected_password)));QVERIFY(AwaitIdle(model));
    QCOMPARE(wallet.unlock_calls,1);QCOMPARE(wallet.observed_password,wallet.expected_password);
    QVERIFY(wallet.unlock_thread!=QThread::currentThread());QVERIFY(wallet.locked);
    QVERIFY(wallet.effect_thread != QThread::currentThread());
    QCOMPARE(wallet.private_accesses, 0);
    QCOMPARE(wallet.sign_args, std::vector<bool>({false, true, false}));
    QVERIFY(model.status().contains(QStringLiteral("Signed 1 input")));
}

void PsbtQmlModelTests::savePreservesPsbtMetadata()
{
    PartiallySignedTransaction psbt{MakeUnsignedPsbt()};
    psbt.unknown[{0x50}] = {0x01};
    psbt.inputs[0].unknown[{0x51}] = {0x02};
    psbt.outputs[0].unknown[{0x52}] = {0x03};

    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    const QString source_path{temp_dir.filePath(QStringLiteral("source.psbt"))};
    const QString saved_path{temp_dir.filePath(QStringLiteral("saved.psbt"))};
    QCOMPARE(PsbtQmlModel::SavePsbtToFile(psbt, source_path), QString{});

    PsbtQmlModel model{nullptr, nullptr};
    QCOMPARE(AwaitLoad(model, source_path), QString{});
    QCOMPARE(AwaitSave(model, saved_path), QString{});

    const auto saved{DecodeRawPsbt(saved_path)};
    QVERIFY(saved.has_value());
    QCOMPARE(saved->unknown, psbt.unknown);
    QCOMPARE(saved->inputs[0].unknown, psbt.inputs[0].unknown);
    QCOMPARE(saved->outputs[0].unknown, psbt.outputs[0].unknown);
}

void PsbtQmlModelTests::broadcastsCompletePsbt()
{
    MockNode node;
    node.broadcast_transaction_fn = [](CTransactionRef, CAmount, CFeeRate, std::string&) {
        return node::TransactionError::OK;
    };

    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    const QString path{temp_dir.filePath(QStringLiteral("complete.psbt"))};
    QCOMPARE(PsbtQmlModel::SavePsbtToFile(MakeCompletePsbt(), path), QString{});

    PsbtQmlModel model{nullptr, &node};
    QCOMPARE(AwaitLoad(model, path), QString{});
    QVERIFY(model.complete());
    QVERIFY(model.canBroadcast());

    model.broadcast(); QVERIFY(AwaitIdle(model));
    QVERIFY(model.submitted());
    QVERIFY(!model.submittedTxid().isEmpty());
    QVERIFY(model.status().startsWith(QStringLiteral("Transaction accepted by the local node.")));
    QCOMPARE(node.calls.broadcastTransaction.load(), 1);
}

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(PsbtQmlModelTests)
#else
QTEST_MAIN(PsbtQmlModelTests)
#endif
#include <test_psbtqmlmodel.moc>
