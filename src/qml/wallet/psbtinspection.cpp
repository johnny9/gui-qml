// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/wallet/psbtinspection.h>
#include <interfaces/node.h>
#include <interfaces/wallet.h>
#include <key_io.h>
#include <node/psbt.h>
#include <rpc/util.h>
#include <univalue.h>
#include <util/strencodings.h>
#include <QObject>

bool PsbtInputsMatchChain(const PartiallySignedTransaction& psbt, interfaces::Node& node)
{
    for (const auto& input : psbt.inputs) {
        CTxOut provided;
        if (!input.GetUTXO(provided)) return false;
        // Node::getUnspentOutput only reads the confirmed UTXO set. gettxout
        // also includes mempool outputs and excludes inputs spent in mempool.
        UniValue args(UniValue::VARR);
        args.push_back(input.GetOutPoint().hash.GetHex());
        args.push_back(input.GetOutPoint().n);
        args.push_back(true);
        const auto coin = node.executeRpc("gettxout", args, "");
        if (coin.isNull()) return false;
        const auto script = ParseHex(coin.find_value("scriptPubKey").find_value("hex").get_str());
        if (provided.nValue != AmountFromValue(coin.find_value("value")) || provided.scriptPubKey != CScript(script.begin(), script.end())) return false;
    }
    return !psbt.inputs.empty();
}

bool PsbtTransactionKnown(const CTransactionRef& transaction, interfaces::Wallet& wallet, interfaces::Node& node)
{
    if (wallet.getTx(transaction->GetHash())) return true;
    UniValue args(UniValue::VARR);
    args.push_back(transaction->GetHash().GetHex());
    try {
        node.executeRpc("getmempoolentry", args, "");
        return true;
    } catch (const UniValue&) {
        return false;
    }
}

PsbtInspection InspectPsbt(const PartiallySignedTransaction& psbt, interfaces::Wallet& wallet, interfaces::Node& node)
{
    PsbtInspection result;
    // Wallet metadata can help inspect a local unsigned transaction. It is not
    // silently merged into the imported document, whose exact bytes are kept.
    auto analyzed = psbt;
    bool complete{false};
    if (wallet.fillPSBT({.sign = false, .finalize = false}, nullptr, analyzed, complete)) {
        result.error = QObject::tr("Core could not analyze the PSBT for this wallet.");
        return result;
    }
    const auto analysis = node::AnalyzePSBT(analyzed);
    if (!analysis.error.empty()) {
        result.error = QString::fromStdString(analysis.error);
        return result;
    }
    auto transaction = analyzed.GetUnsignedTx();
    if (!transaction || transaction->vin.empty() || transaction->vout.empty()) {
        result.error = QObject::tr("The PSBT has no valid transaction inputs or outputs.");
        return result;
    }
    auto finalized = analyzed;
    result.complete = FinalizeAndExtractPSBT(finalized, *transaction);
    result.transaction = MakeTransactionRef(*transaction);
    result.fee = analysis.fee;
    result.unsigned_inputs = CountPSBTUnsignedInputs(analyzed);
    result.known = PsbtTransactionKnown(result.transaction, wallet, node);
    result.inputs_verified = PsbtInputsMatchChain(analyzed, node);
    if (!result.complete && !wallet.privateKeysDisabled() && !wallet.hasExternalSigner()) {
        for (const auto& input : analyzed.inputs) {
            CTxOut utxo;
            if (!PSBTInputSigned(input) && input.GetUTXO(utxo) && wallet.txoutIsMine(utxo)) result.can_sign = true;
        }
    }
    for (const auto& output : transaction->vout) {
        CTxDestination destination;
        const QString address = ExtractDestination(output.scriptPubKey, destination)
            ? QString::fromStdString(EncodeDestination(destination)) : QObject::tr("Non-address output");
        result.outputs.push_back({address, output.nValue, wallet.txoutIsMine(output)});
    }
    return result;
}
