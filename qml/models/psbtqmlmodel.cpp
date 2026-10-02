// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/psbtqmlmodel.h>
#include <qml/backendexecutor.h>

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

    const QByteArray bytes{file.read(MAX_FILE_SIZE_PSBT + 1)};
    if (bytes.size() > MAX_FILE_SIZE_PSBT) return tr("PSBT file must be smaller than 100 MiB");
    if (file.error() != QFileDevice::NoError) return tr("Could not read PSBT file: %1").arg(file.errorString());
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

PsbtQmlModel::PsbtQmlModel(std::shared_ptr<interfaces::Wallet> wallet, interfaces::Node* node, QObject* parent, std::shared_ptr<BackendExecutor> executor)
    : QObject(parent)
    , m_wallet(std::move(wallet))
    , m_executor(executor ? std::move(executor) : std::make_shared<BackendExecutor>())
    , m_node(node)
{
}

void PsbtQmlModel::detachWallet()
{
    ++m_generation;
    m_wallet.reset();
    m_node = nullptr;
}

void PsbtQmlModel::clear()
{
    ++m_generation;
    m_pending = false;
    m_psbt.reset();
    m_status.clear();
    m_error.clear();
    m_summary.clear();
    m_can_sign = false;
    m_can_broadcast = false;
    m_complete = false;
    m_unsigned_inputs = 0;
    m_could_sign_inputs = 0;
    m_matched_txid.clear();
    Q_EMIT changed();
}

void PsbtQmlModel::setError(const QString& error)
{
    ++m_generation;
    m_pending = false;
    m_psbt.reset();
    m_status.clear();
    m_error = error;
    m_summary.clear();
    m_can_sign = false;
    m_can_broadcast = false;
    m_complete = false;
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
    return startOperation(Operation::Load, path) ? QString{} : tr("A PSBT operation is already in progress.");
}

void PsbtQmlModel::sign() { startOperation(Operation::Sign); }
void PsbtQmlModel::broadcast() { startOperation(Operation::Broadcast); }
void PsbtQmlModel::copyToClipboard() { startOperation(Operation::Copy); }

QString PsbtQmlModel::saveToFile(const QString& path)
{
    if (!m_psbt) return tr("No PSBT is loaded.");
    return startOperation(Operation::Save, path) ? QString{} : tr("A PSBT operation is already in progress.");
}

void PsbtQmlModel::refreshState(const QString& status_override)
{
    startOperation(Operation::Refresh, {}, status_override);
}

bool PsbtQmlModel::startOperation(Operation operation, const QString& path, const QString& status)
{
    if (m_pending || (operation != Operation::Load && !m_psbt)) return false;
    const auto generation = ++m_generation;
    const auto current = m_psbt ? std::optional{*m_psbt} : std::nullopt;
    m_pending = true;
    Q_EMIT changed();
    struct Result {
        std::optional<PartiallySignedTransaction> psbt;
        QString status;
        QString error;
        QString clipboard;
        QStringList summary;
        bool complete{false};
        bool can_sign{false};
        int unsigned_inputs{0};
        int could_sign{0};
    };
    const bool accepted = m_executor->submit(this, [wallet = m_wallet, node = m_node, current, operation, path, status] {
        Result out;
        out.psbt = current.value_or(PartiallySignedTransaction{CMutableTransaction{}});
        out.status = status;
        if (operation == Operation::Load) out.error = LoadPsbtFromFile(path, *out.psbt);
        if (!out.error.isEmpty()) { out.psbt.reset(); return out; }
        if (operation == Operation::Sign) {
            if (!wallet || wallet->privateKeysDisabled()) {
                out.status = tr("This wallet cannot sign transactions because private keys are disabled.");
            } else {
                size_t signed_inputs{0};
                bool complete{false};
                const auto error = wallet->fillPSBT({.sign = true, .bip32_derivs = true}, &signed_inputs, *out.psbt, complete);
                if (error) out.error = tr("Could not sign PSBT: %1").arg(PsbtErrorText(*error));
                else if (complete) out.status = tr("PSBT signed. Transaction is ready for broadcast.");
                else if (signed_inputs) out.status = tr("Signed %n input(s). More signatures are still required.", "", static_cast<int>(signed_inputs));
                else out.status = tr("This wallet could not add any signatures to the PSBT.");
            }
        } else if (operation == Operation::Broadcast) {
            CMutableTransaction tx;
            if (!node) out.error = tr("Cannot broadcast from this context because the node interface is unavailable.");
            else if (!FinalizeAndExtractPSBT(*out.psbt, tx)) out.error = tr("PSBT is not complete and cannot be broadcast.");
            else {
                const auto transaction = MakeTransactionRef(std::move(tx));
                std::string detail;
                const auto error = node->broadcastTransaction(transaction, node::DEFAULT_MAX_RAW_TX_FEE_RATE.GetFeePerK(), detail);
                if (error == node::TransactionError::OK) out.status = tr("Transaction broadcast successfully. Transaction ID: %1").arg(QString::fromStdString(transaction->GetHash().ToString()));
                else out.error = tr("Transaction broadcast failed: %1").arg(TransactionErrorText(error) + QString::fromStdString(detail.empty() ? "" : ": " + detail));
            }
        } else if (operation == Operation::Save) {
            out.error = SavePsbtToFile(*out.psbt, path);
            out.status = tr("PSBT saved.");
        } else if (operation == Operation::Copy) {
            out.clipboard = SerializePsbtBase64(*out.psbt);
            out.status = tr("PSBT copied to clipboard.");
        }
        out.complete = FinalizePSBT(*out.psbt);
        size_t could_sign{0};
        if (wallet) {
            if (const auto error = wallet->fillPSBT({.sign = false, .bip32_derivs = true}, &could_sign, *out.psbt, out.complete); error && out.error.isEmpty()) out.error = PsbtErrorText(*error);
        }
        out.could_sign = static_cast<int>(could_sign);
        out.unsigned_inputs = static_cast<int>(CountPSBTUnsignedInputs(*out.psbt));
        out.can_sign = !out.complete && wallet && !wallet->privateKeysDisabled() && could_sign > 0;
        out.summary = buildSummary(*out.psbt, wallet.get());
        if (!out.error.isEmpty()) out.status = out.error;
        else if (out.status.isEmpty()) {
            if (out.complete) out.status = tr("Transaction is fully signed and ready for broadcast.");
            else if (!wallet) out.status = tr("No wallet is loaded. You can inspect, copy, or save this PSBT.");
            else if (wallet->privateKeysDisabled()) out.status = tr("This wallet cannot sign transactions because private keys are disabled.");
            else if (could_sign) out.status = tr("This wallet can sign %n input(s).", "", static_cast<int>(could_sign));
            else out.status = tr("This wallet does not have the right keys to sign this PSBT.");
        }
        return out;
    }, [this, generation](Result out) {
        if (generation != m_generation) return;
        m_pending = false;
        m_psbt = out.psbt ? std::make_unique<PartiallySignedTransaction>(std::move(*out.psbt)) : nullptr;
        m_status = out.status;
        m_error = out.error;
        m_summary = std::move(out.summary);
        m_complete = out.complete;
        m_can_broadcast = out.complete;
        m_can_sign = out.can_sign;
        m_unsigned_inputs = out.unsigned_inputs;
        m_could_sign_inputs = out.could_sign;
        if (!out.clipboard.isEmpty()) QGuiApplication::clipboard()->setText(out.clipboard);
        Q_EMIT changed();
        Q_EMIT operationFinished(out.error.isEmpty());
    }, [this, generation](std::exception_ptr) {
        if (generation != m_generation) return;
        m_pending = false;
        m_error = tr("The PSBT operation failed.");
        Q_EMIT changed();
        Q_EMIT operationFinished(false);
    });
    if (!accepted) { m_pending = false; Q_EMIT changed(); }
    return accepted;
}

QStringList PsbtQmlModel::buildSummary(const PartiallySignedTransaction& psbt, interfaces::Wallet* wallet)
{
    QStringList lines;
    const auto unsigned_tx{psbt.GetUnsignedTx()};
    if (!unsigned_tx) {
        lines << tr("PSBT does not contain an unsigned transaction.");
        return lines;
    }

    CAmount total{0};
    for (const CTxOut& output : unsigned_tx->vout) {
        total += output.nValue;
        CTxDestination destination;
        const QString address{ExtractDestination(output.scriptPubKey, destination) ? QString::fromStdString(EncodeDestination(destination)) : tr("unknown destination")};
        const bool own_address{wallet && wallet->txoutIsMine(output)};
        lines << tr("Sends %1 to %2%3").arg(FormatBtc(output.nValue), address, own_address ? tr(" (own address)") : QString());
    }

    const node::PSBTAnalysis analysis{node::AnalyzePSBT(psbt)};
    if (!analysis.error.empty()) {
        lines << tr("Analysis: %1").arg(QString::fromStdString(analysis.error));
    }
    if (analysis.fee) {
        lines << tr("Pays transaction fee: %1").arg(FormatBtc(*analysis.fee));
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
