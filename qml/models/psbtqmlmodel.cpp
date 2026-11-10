// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/psbtqmlmodel.h>
#include <qml/models/psbtfee.h>

#include <common/messages.h>
#include <interfaces/node.h>
#include <interfaces/wallet.h>
#include <key_io.h>
#include <node/psbt.h>
#include <node/transaction.h>
#include <node/types.h>
#include <policy/feerate.h>
#include <primitives/transaction.h>
#include <psbt.h>
#include <qml/bitcoinunits.h>
#include <script/script.h>
#include <script/solver.h>
#include <streams.h>
#include <util/result.h>
#include <util/strencodings.h>
#include <util/translation.h>

#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <QClipboard>
#include <QFile>
#include <QGuiApplication>
#include <QIODevice>
#include <QSaveFile>
#include <QUrl>
#include <QPointer>

namespace {

QString FormatBtc(CAmount amount)
{
    return QmlBitcoinUnits::format(QmlBitcoinUnits::Unit::BTC, amount) + QStringLiteral(" BTC");
}

QString SerializePsbtBase64(const PartiallySignedTransaction& psbt)
{
    DataStream stream{};
    stream << psbt;
    return QString::fromStdString(EncodeBase64(stream.str()));
}

QString TransactionErrorText(node::TransactionError error)
{
    return QString::fromStdString(common::TransactionErrorString(error).translated);
}

std::optional<std::pair<int, int>> ExtractMultisigSigInfo(const CScript& script)
{
    if (script.empty()) {
        return std::nullopt;
    }

    std::vector<std::vector<unsigned char>> solutions;
    if (Solver(script, solutions) == TxoutType::MULTISIG
        && solutions.size() >= 2
        && !solutions.front().empty()
        && !solutions.back().empty()) {
        const int required{solutions.front()[0]};
        const int total{solutions.back()[0]};
        if (required > 0 && total >= required) {
            return std::pair{required, total};
        }
    }
    if (const auto multi_a{MatchMultiA(script)}) {
        return std::pair{multi_a->first, static_cast<int>(multi_a->second.size())};
    }
    return std::nullopt;
}

util::Result<PartiallySignedTransaction> DecodePsbtFromBytes(const QByteArray& bytes)
{
    std::vector<std::byte> raw;
    raw.reserve(bytes.size());
    for (const char ch : bytes) {
        raw.push_back(static_cast<std::byte>(static_cast<unsigned char>(ch)));
    }
    return DecodeRawPSBT(std::span<const std::byte>{raw.data(), raw.size()});
}

} // namespace

QString PsbtQmlModel::LocalFilePath(const QString& path)
{
    const QUrl url(path);
    if (url.isLocalFile()) {
        return url.toLocalFile();
    }
    return path;
}

QString PsbtQmlModel::LoadPsbtFromFile(const QString& path, PartiallySignedTransaction& psbt)
{
    QFile file(LocalFilePath(path));
    if (!file.open(QIODevice::ReadOnly)) {
        return tr("Could not open PSBT file: %1").arg(file.errorString());
    }
    if (file.size() > MAX_FILE_SIZE_PSBT) {
        return tr("PSBT file must be smaller than 100 MiB");
    }

    const QByteArray bytes{file.readAll()};
    const auto raw_result{DecodePsbtFromBytes(bytes)};
    if (raw_result) {
        psbt = *raw_result;
        return {};
    }

    const auto base64_result{DecodeBase64PSBT(QString::fromUtf8(bytes).trimmed().toStdString())};
    if (base64_result) {
        psbt = *base64_result;
        return {};
    }

    const bilingual_str raw_error{util::ErrorString(raw_result)};
    const bilingual_str base64_error{util::ErrorString(base64_result)};
    const bilingual_str& decode_error{base64_error.original == "invalid base64" ? raw_error : base64_error};
    return tr("Could not decode PSBT: %1").arg(QString::fromStdString(decode_error.translated));
}

QByteArray PsbtQmlModel::SerializePsbtRaw(const PartiallySignedTransaction& psbt)
{
    DataStream stream{};
    stream << psbt;
    const std::string raw{stream.str()};
    return QByteArray(raw.data(), static_cast<qsizetype>(raw.size()));
}

QString PsbtQmlModel::PsbtErrorText(common::PSBTError error)
{
    return QString::fromStdString(common::PSBTErrorString(error).translated);
}

QString PsbtQmlModel::SavePsbtToFile(const PartiallySignedTransaction& psbt, const QString& path)
{
    QSaveFile file(LocalFilePath(path));
    if (!file.open(QIODevice::WriteOnly)) {
        return tr("Could not save PSBT: %1").arg(file.errorString());
    }

    const QByteArray raw{SerializePsbtRaw(psbt)};
    if (file.write(raw) != raw.size()) {
        file.cancelWriting();
        return tr("Could not write the complete PSBT file.");
    }
    if (!file.commit()) {
        return tr("Could not save PSBT: %1").arg(file.errorString());
    }
    return {};
}

bool PsbtQmlModel::IsMultisigPsbtInput(const PartiallySignedTransaction& psbt, std::size_t index)
{
    return MultisigPsbtInputSigInfo(psbt, index).has_value();
}

std::optional<std::pair<int, int>> PsbtQmlModel::MultisigPsbtInputSigInfo(const PartiallySignedTransaction& psbt, std::size_t index)
{
    const PSBTInput& input{psbt.inputs[index]};
    if (auto info{ExtractMultisigSigInfo(input.redeem_script)}) return info;
    if (auto info{ExtractMultisigSigInfo(input.witness_script)}) return info;

    CTxOut utxo;
    if (input.GetUTXO(utxo)) {
        if (auto info{ExtractMultisigSigInfo(utxo.scriptPubKey)}) return info;
    }
    return std::nullopt;
}

PsbtQmlModel::PsbtQmlModel(interfaces::Wallet* wallet, interfaces::Node* node, QObject* parent)
    : QObject(parent)
    , m_wallet(wallet)
    , m_node(node)
{
}

void PsbtQmlModel::setNode(interfaces::Node* node)
{
    const bool recheck = m_fee_pending;
    m_fee_request.reset();
    m_fee_pending = false;
    m_node = node;
    if (recheck && !m_detached) {
        m_fee_attempted = false;
        refreshState();
    }
}

void PsbtQmlModel::clear()
{
    m_fee_request.reset();
    m_fee_proof.reset();
    m_fee_pending = m_fee_attempted = false;
    m_psbt.reset();
    m_status.clear();
    m_error.clear();
    m_summary.clear();
    m_can_sign = false;
    m_can_broadcast = false;
    m_complete = false;
    m_reviewed_fee.reset();
    m_unsigned_inputs = 0;
    m_could_sign_inputs = 0;
    m_matched_txid.clear();
    Q_EMIT changed();
}

void PsbtQmlModel::setError(const QString& error)
{
    m_fee_request.reset();
    m_fee_proof.reset();
    m_fee_pending = m_fee_attempted = false;
    m_psbt.reset();
    m_status.clear();
    m_error = error;
    m_summary.clear();
    m_can_sign = false;
    m_can_broadcast = false;
    m_complete = false;
    m_reviewed_fee.reset();
    m_unsigned_inputs = 0;
    m_could_sign_inputs = 0;
    m_matched_txid.clear();
    Q_EMIT changed();
}

void PsbtQmlModel::setMatchedTxid(const QString& txid)
{
    m_matched_txid = txid;
    Q_EMIT changed();
}

QString PsbtQmlModel::loadFromFile(const QString& path)
{
    CMutableTransaction empty_tx;
    PartiallySignedTransaction psbt{empty_tx, /*version=*/2};
    QString error{LoadPsbtFromFile(path, psbt)};
    if (!error.isEmpty()) {
        setError(error);
        return error;
    }

    m_fee_request.reset();
    m_fee_proof.reset();
    m_fee_pending = m_fee_attempted = false;
    m_psbt = std::make_shared<const PartiallySignedTransaction>(std::move(psbt));
    refreshState();
    return QString{};
}

void PsbtQmlModel::sign()
{
    if (!m_psbt || !m_wallet) {
        setError(tr("No signable PSBT is loaded."));
        return;
    }
    if (m_wallet->privateKeysDisabled()) {
        refreshState(tr("This wallet cannot sign transactions because private keys are disabled."));
        return;
    }

    const auto approved_fee = m_reviewed_fee;
    if (!m_fee_proof || !psbtfee::MatchesReview(*m_psbt, *m_fee_proof, approved_fee)) {
        m_error = tr("The transaction fee is missing or invalid.");
        m_can_sign = m_can_broadcast = false;
        Q_EMIT changed();
        return;
    }
    PartiallySignedTransaction candidate{*m_psbt};
    if (m_fee_proof) psbtfee::FillMissing(candidate, *m_fee_proof);
    bool complete{false};
    size_t signed_inputs{0};
    const std::optional<common::PSBTError> error{
        m_wallet->fillPSBT({.sign = true, .bip32_derivs = true}, &signed_inputs, candidate, complete)};
    if (error) {
        refreshState(tr("Could not sign PSBT: %1").arg(PsbtErrorText(*error)));
        return;
    }

    complete = FinalizePSBT(candidate);
    if (!psbtfee::MatchesReview(candidate, *m_fee_proof, approved_fee)) {
        refreshState(tr("The transaction fee changed after review."));
        return;
    }
    m_psbt = std::make_shared<const PartiallySignedTransaction>(std::move(candidate));
    if (complete) {
        refreshState(tr("PSBT signed. Transaction is ready for broadcast."));
    } else if (signed_inputs > 0) {
        refreshState(tr("Signed %n input(s). More signatures are still required.", "", static_cast<int>(signed_inputs)));
    } else {
        refreshState(tr("This wallet could not add any signatures to the PSBT."));
    }
}

void PsbtQmlModel::broadcast()
{
    if (m_detached) {
        setError(tr("This wallet is closing."));
        return;
    }
    if (!m_psbt) {
        setError(tr("No PSBT is loaded."));
        return;
    }
    if (!m_node) {
        refreshState(tr("Cannot broadcast from this context because the node interface is unavailable."));
        return;
    }

    PartiallySignedTransaction candidate{*m_psbt};
    if (m_fee_proof) psbtfee::FillMissing(candidate, *m_fee_proof);
    CMutableTransaction mutable_tx;
    const bool complete = FinalizeAndExtractPSBT(candidate, mutable_tx);
    if (!m_fee_proof || !psbtfee::MatchesReview(candidate, *m_fee_proof, m_reviewed_fee)) {
        m_error = tr("The transaction fee is missing or invalid.");
        m_can_sign = m_can_broadcast = false;
        Q_EMIT changed();
        return;
    }
    if (!complete) {
        refreshState(tr("PSBT is not complete and cannot be broadcast."));
        return;
    }

    const CTransactionRef tx{MakeTransactionRef(std::move(mutable_tx))};
    std::string error_string;
    const node::TransactionError error{m_node->broadcastTransaction(tx, node::DEFAULT_MAX_RAW_TX_FEE_RATE.GetFeePerK(), node::DEFAULT_MAX_RAW_TX_FEE_RATE, error_string)};
    if (error == node::TransactionError::OK) {
        refreshState(tr("Transaction broadcast successfully. Transaction ID: %1").arg(QString::fromStdString(tx->GetHash().ToString())));
    } else {
        QString message{TransactionErrorText(error)};
        if (!error_string.empty()) {
            message += QStringLiteral(": ") + QString::fromStdString(error_string);
        }
        refreshState(tr("Transaction broadcast failed: %1").arg(message));
    }
}

void PsbtQmlModel::copyToClipboard()
{
    if (!m_psbt) {
        return;
    }
    QGuiApplication::clipboard()->setText(SerializePsbtBase64(*m_psbt));
    refreshState(tr("PSBT copied to clipboard."));
}

QString PsbtQmlModel::saveToFile(const QString& path)
{
    if (!m_psbt) {
        QString err{tr("No PSBT is loaded.")};
        refreshState(err);
        return err;
    }
    QString err{SavePsbtToFile(*m_psbt, path)};
    if (!err.isEmpty()) {
        refreshState(err);
        return err;
    }
    refreshState(tr("PSBT saved."));
    return QString{};
}

void PsbtQmlModel::refreshState(const QString& status_override)
{
    if (!m_psbt || m_detached) return;
    auto fee = psbtfee::Analyze(*m_psbt, m_fee_proof ? m_fee_proof->verified : psbtfee::Outputs{});
    auto candidate = *m_psbt;
    bool complete{false};
    size_t could_sign{0};
    std::optional<common::PSBTError> fill_error;
    if (fee.state != psbtfee::State::Invalid) {
        if (m_fee_proof) psbtfee::FillMissing(candidate, *m_fee_proof);
        complete = FinalizePSBT(candidate);
        if (m_wallet) {
            fill_error = m_wallet->fillPSBT({.sign = false, .bip32_derivs = true}, &could_sign, candidate, complete);
        }
        fee = psbtfee::Analyze(candidate, m_fee_proof ? m_fee_proof->verified : psbtfee::Outputs{});
        m_psbt = std::make_shared<const PartiallySignedTransaction>(std::move(candidate));
    }
    m_fee_proof = std::make_shared<const psbtfee::Analysis>(fee);
    m_reviewed_fee = fee.amount;
    m_error = fill_error ? PsbtErrorText(*fill_error) : !fee.amount ? tr("The transaction fee is missing or invalid.") : QString{};
    m_complete = complete;
    m_can_broadcast = complete && fee.amount.has_value();
    m_could_sign_inputs = static_cast<int>(could_sign);
    m_unsigned_inputs = static_cast<int>(CountPSBTUnsignedInputs(*m_psbt));
    m_can_sign = fee.amount && !complete && m_wallet && !m_wallet->privateKeysDisabled() && could_sign > 0;
    m_summary = buildSummary(*m_psbt);

    if (!status_override.isEmpty()) {
        m_status = status_override;
    } else if (!m_error.isEmpty()) {
        m_status = m_error;
    } else if (complete) {
        m_status = tr("Transaction is fully signed and ready for broadcast.");
    } else if (!m_wallet) {
        m_status = tr("No wallet is loaded. You can inspect, copy, or save this PSBT.");
    } else if (m_wallet->privateKeysDisabled()) {
        m_status = tr("This wallet cannot sign transactions because private keys are disabled.");
    } else if (could_sign > 0) {
        m_status = tr("This wallet can sign %n input(s).", "", static_cast<int>(could_sign));
    } else {
        m_status = tr("This wallet does not have the right keys to sign this PSBT.");
    }

    if (fee.state == psbtfee::State::Missing && !m_fee_attempted && m_fee_services.lookup) requestFeeProof();
    if (m_fee_pending) {
        m_status = tr("Checking transaction fee.");
        m_error.clear();
    }
    Q_EMIT changed();
}

void PsbtQmlModel::requestFeeProof()
{
    m_fee_attempted = m_fee_pending = true;
    const auto token = std::make_shared<const psbtfee::RequestIdentity>();
    m_fee_request = token;
    const QPointer<PsbtQmlModel> observer{this};
    const auto admission = psbtfee::Request(m_fee_services, m_psbt, m_fee_proof,
        [observer, token](psbtfee::Resolution result) {
            if (!observer || observer->m_fee_request != token) return;
            observer->m_fee_request.reset();
            observer->m_fee_pending = false;
            if (!result.failed && result.proof) observer->m_fee_proof = std::move(result.proof);
            observer->refreshState();
        });
    if (!admission) {
        m_fee_request.reset();
        m_fee_pending = false;
    }
}

QStringList PsbtQmlModel::buildSummary(const PartiallySignedTransaction& psbt) const
{
    QStringList lines;
    const auto unsigned_tx{psbt.GetUnsignedTx()};
    if (!unsigned_tx) {
        lines << tr("PSBT does not contain an unsigned transaction.");
        return lines;
    }

    const auto fee = psbtfee::Analyze(psbt, m_fee_proof ? m_fee_proof->verified : psbtfee::Outputs{});
    if (fee.state == psbtfee::State::Invalid) {
        lines << tr("The transaction fee is missing or invalid.") << tr("Transaction fee is not available.");
        return lines;
    }
    const CAmount total{fee.output_total};
    for (const CTxOut& output : unsigned_tx->vout) {
        CTxDestination destination;
        const QString address{ExtractDestination(output.scriptPubKey, destination) ? QString::fromStdString(EncodeDestination(destination)) : tr("unknown destination")};
        const bool own_address{m_wallet && m_wallet->txoutIsMine(output)};
        lines << tr("Sends %1 to %2%3").arg(FormatBtc(output.nValue), address, own_address ? QString{QStringLiteral(" ") + tr("(own address)")} : QString());
    }

    const node::PSBTAnalysis analysis{node::AnalyzePSBT(psbt)};
    if (!analysis.error.empty()) {
        lines << tr("Analysis: %1").arg(QString::fromStdString(analysis.error));
    }
    if (fee.amount) {
        lines << tr("Pays transaction fee: %1").arg(FormatBtc(*fee.amount));
    } else {
        lines << tr("Transaction fee is not available.");
    }
    lines << tr("Total output amount: %1").arg(FormatBtc(total));

    const int unsigned_inputs{static_cast<int>(CountPSBTUnsignedInputs(psbt))};
    if (unsigned_inputs > 0) {
        lines << tr("Unsigned inputs: %1").arg(unsigned_inputs);
    }
    return lines;
}
