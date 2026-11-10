// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <qml/models/psbtqmlmodel.h>
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
    std::optional<common::PSBTError> fill_error;
    std::vector<bool> sign_args;
    std::function<void(PartiallySignedTransaction&)> on_sign;

    std::optional<common::PSBTError> fillPSBT(const common::PSBTFillOptions& options,
                                              size_t* n_signed,
                                              PartiallySignedTransaction& psbt,
                                              bool& complete) override
    {
        sign_args.push_back(options.sign);
        if (n_signed) {
            *n_signed = signable;
        }
        if (options.sign && on_sign) on_sign(psbt);
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
} // namespace

class PsbtQmlModelTests : public QObject
{
    Q_OBJECT
    std::unique_ptr<ECC_Context> m_ecc_context;

private Q_SLOTS:
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
        PsbtTestWallet wallet; MockNode node;
        wallet.private_keys_disabled = authority == 0;
        if (authority != 0) wallet.signable = 0;
        if (authority == 2) wallet.fill_error = common::PSBTError::MISSING_INPUTS;
        PsbtQmlModel model{&wallet, &node};
        model.refreshState(); // An absent document has no authority.
        QTemporaryDir directory; QVERIFY(directory.isValid()); const auto path = directory.filePath("partial.psbt");
        QCOMPARE(PsbtQmlModel::SavePsbtToFile(MakeUnsignedPsbt(), path), QString{});
        QCOMPARE(model.loadFromFile(path), QString{});
        QVERIFY(!model.canSign()); QVERIFY(!model.canBroadcast());
        model.broadcast();
        QCOMPARE(node.calls.broadcastTransaction.load(), 0);
        if (authority == 0) model.sign();
        model.detachWallet(); model.refreshState(); model.setNode(nullptr);
        QVERIFY(!model.canSign()); QVERIFY(!model.canBroadcast());
    }

    void feeProofCannotOutliveDocument_data()
    {
        QTest::addColumn<int>("scenario");
        const std::array names{"success", "unknown", "throw", "rejected", "cleared", "replaced", "destroyed", "detached", "node-changed"};
        for (int i = 0; i < int(names.size()); ++i) QTest::newRow(names[i]) << i;
    }
    void feeProofCannotOutliveDocument()
    {
        QFETCH(int, scenario);
        ManualExecutionEnvironment environment;
        PsbtTestWallet wallet;
        auto model = std::make_unique<PsbtQmlModel>(&wallet, nullptr);
        auto document = psbtfeefixture::Legacy(2, false);
        psbtfee::Outputs actual;
        for (auto& input : document.inputs) {
            actual.emplace(input.GetOutPoint(), input.non_witness_utxo->vout[input.prev_out]);
            input.witness_utxo = input.non_witness_utxo->vout[input.prev_out];
            input.non_witness_utxo = nullptr;
        }
        int reads{0};
        model->setFeeServices({environment.scheduler,
            [&reads, &actual, scenario](const COutPoint& outpoint) -> std::optional<CTxOut> {
                ++reads;
                if (scenario == 1) return std::nullopt;
                if (scenario == 2) throw std::runtime_error("lookup failed");
                return actual.at(outpoint);
            }, [&environment](auto publication) { environment.post(std::move(publication)); }});
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto path = directory.filePath("needs-proof.psbt");
        QCOMPARE(PsbtQmlModel::SavePsbtToFile(document, path), QString{});
        environment.reject_next = scenario == 3;
        QCOMPARE(model->loadFromFile(path), QString{});
        QVERIFY(!model->canBroadcast()); QVERIFY(!model->canSign()); QCOMPARE(reads, 0);
        model->sign();
        QCOMPARE(std::count(wallet.sign_args.begin(), wallet.sign_args.end(), true), 0);
        if (scenario == 3) {
            QVERIFY(!model->error().isEmpty());
            QVERIFY(environment.quiescent());
            return;
        }
        QVERIFY(environment.step());
        QVERIFY(!model->canBroadcast());
        QVERIFY(environment.step());
        QCOMPARE(environment.scheduler->pending(), size_t{0});
        QVERIFY(!model->canBroadcast());
        if (scenario == 4) model->clear();
        if (scenario == 5) {
            const auto other = directory.filePath("other.psbt");
            QCOMPARE(PsbtQmlModel::SavePsbtToFile(MakeUnsignedPsbt(), other), QString{});
            QCOMPARE(model->loadFromFile(other), QString{});
        }
        if (scenario == 6) model.reset();
        if (scenario == 7) model->detachWallet();
        if (scenario == 8) { model->setNode(nullptr); environment.drain(); }
        environment.publishAll();
        qInfo().noquote() << QString::fromStdString(environment.trace());
        QVERIFY2(environment.quiescent(), environment.trace().c_str());
        if (scenario == 6) return;
        if (scenario == 0 || scenario == 8) {
            QVERIFY(model->canBroadcast());
            QVERIFY(model->summary().join(' ').contains("0.00500000 BTC"));
        } else {
            QVERIFY(!model->canBroadcast());
            if (scenario == 4) QVERIFY(!model->loaded());
            if (scenario == 5) { QVERIFY(model->canSign()); QVERIFY(model->summary().join(' ').contains("0.00000500 BTC")); }
            if (scenario == 7) { model->broadcast(); QVERIFY(!model->canBroadcast()); }
        }
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
        PsbtTestWallet wallet; MockNode node; PsbtQmlModel model{&wallet, &node};
        QTemporaryDir directory; QVERIFY(directory.isValid()); const auto path = directory.filePath("invalid.psbt");
        QCOMPARE(PsbtQmlModel::SavePsbtToFile(psbt, path), QString{});
        QCOMPARE(model.loadFromFile(path), QString{});
        QVERIFY(model.loaded()); QVERIFY(!model.canSign()); QVERIFY(!model.canBroadcast());
        QVERIFY(!model.error().isEmpty()); QVERIFY(!model.summary().isEmpty());
        model.sign(); model.broadcast();
        QVERIFY(wallet.sign_args.empty()); QCOMPARE(node.calls.broadcastTransaction.load(), 0);
    }
    void unknownFeeBlocksDirectSigning()
    {
        PsbtTestWallet wallet; MockNode node; PsbtQmlModel model{&wallet, &node};
        QTemporaryDir directory; QVERIFY(directory.isValid()); const auto path = directory.filePath("unknown.psbt");
        QCOMPARE(PsbtQmlModel::SavePsbtToFile(psbtfeefixture::Legacy(2, true), path), QString{});
        QCOMPARE(model.loadFromFile(path), QString{});
        QVERIFY(!model.canSign()); QVERIFY(!model.canBroadcast());
        model.sign(); model.broadcast();
        QCOMPARE(std::count(wallet.sign_args.begin(), wallet.sign_args.end(), true), 0);
        QCOMPARE(node.calls.broadcastTransaction.load(), 0);
    }
    void signingCannotReplaceApprovedFeeMetadata()
    {
        PsbtTestWallet wallet; MockNode node; PsbtQmlModel model{&wallet, &node};
        QTemporaryDir directory; QVERIFY(directory.isValid()); const auto path = directory.filePath("approved.psbt");
        const auto approved = psbtfeefixture::Legacy(2, false);
        QCOMPARE(PsbtQmlModel::SavePsbtToFile(approved, path), QString{});
        QCOMPARE(model.loadFromFile(path), QString{});
        wallet.on_sign = [](PartiallySignedTransaction& psbt) {
            auto& input = psbt.inputs[1];
            input.witness_utxo = input.non_witness_utxo->vout[input.prev_out];
            input.witness_utxo.nValue -= psbtfeefixture::ACTUAL_FEE - 1;
            input.non_witness_utxo = nullptr;
        };
        model.sign();
        QCOMPARE(model.status(), QString("The transaction fee changed after review."));
        QVERIFY(model.psbt()->inputs[1].non_witness_utxo);
        QCOMPARE(model.psbt()->inputs[1].non_witness_utxo->GetHash(), approved.inputs[1].non_witness_utxo->GetHash());
        QCOMPARE(node.calls.broadcastTransaction.load(), 0);
        wallet.on_sign = {};
        model.sign(); QVERIFY(model.canBroadcast());
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
        node.broadcast_transaction_fn = [](CTransactionRef, CAmount, std::string&) { return node::TransactionError::OK; };
        PsbtQmlModel model{nullptr, &node};
        auto psbt = psbtfeefixture::Legacy(version, forged);
        CMutableTransaction extracted;
        QVERIFY(FinalizeAndExtractPSBT(psbt, extracted));
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto path = directory.filePath("legacy.psbt");
        QCOMPARE(PsbtQmlModel::SavePsbtToFile(psbt, path), QString{});
        QCOMPARE(model.loadFromFile(path), QString{});
        QVERIFY(model.complete());
        const bool can_broadcast = model.canBroadcast();
        model.broadcast();
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
    PsbtTestWallet wallet;
    PsbtQmlModel model{&wallet, nullptr};
    const PartiallySignedTransaction psbt{MakeUnsignedPsbt()};

    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    const QString raw_path{temp_dir.filePath(QStringLiteral("raw.psbt"))};
    QCOMPARE(PsbtQmlModel::SavePsbtToFile(psbt, raw_path), QString{});
    QCOMPARE(model.loadFromFile(raw_path), QString{});
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
    QCOMPARE(model.loadFromFile(base64_path), QString{});
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
    QVERIFY(!model.loadFromFile(path).isEmpty());
    QVERIFY(!model.loaded());
    QVERIFY(!model.error().isEmpty());

    model.clear();
    QVERIFY(model.error().isEmpty());
    QVERIFY(model.status().isEmpty());
}

void PsbtQmlModelTests::signUsesInjectedWallet()
{
    PsbtTestWallet wallet;
    PsbtQmlModel model{&wallet, nullptr};

    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    const QString path{temp_dir.filePath(QStringLiteral("unsigned.psbt"))};
    QCOMPARE(PsbtQmlModel::SavePsbtToFile(MakeUnsignedPsbt(), path), QString{});
    QCOMPARE(model.loadFromFile(path), QString{});

    model.sign();
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
    QCOMPARE(model.loadFromFile(source_path), QString{});
    QCOMPARE(model.saveToFile(saved_path), QString{});

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
    QCOMPARE(model.loadFromFile(path), QString{});
    QVERIFY(model.complete());
    QVERIFY(model.canBroadcast());

    model.broadcast();
    QVERIFY(model.status().startsWith(QStringLiteral("Transaction broadcast successfully.")));
    QCOMPARE(node.calls.broadcastTransaction.load(), 1);
}

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(PsbtQmlModelTests)
#else
QTEST_MAIN(PsbtQmlModelTests)
#endif
#include <test_psbtqmlmodel.moc>
