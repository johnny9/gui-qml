// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/wallet/psbtinspection.h>
#include <interfaces/node.h>
#include <interfaces/wallet.h>
#include <key_io.h>
#include <node/psbt.h>
#include <rpc/util.h>
#include <script/miniscript.h>
#include <univalue.h>
#include <util/strencodings.h>
#include <algorithm>
#include <QObject>

namespace {
// Parse the same Miniscript language Core signs, retaining only keys actually
// used by the script. Extra derivation records are not signing requirements.
struct TaprootScriptKeys {
    using Key = XOnlyPubKey;
    const PSBTInput& input;
    mutable std::set<Key> keys;
    auto MsContext() const { return miniscript::MiniscriptContext::TAPSCRIPT; }
    static bool KeyCompare(const Key& a, const Key& b) { return a < b; }
    template<typename I> std::optional<Key> FromPKBytes(I first, I last) const
    {
        if (last - first != 32) return {};
        const Key key(std::vector<unsigned char>(first, last));
        keys.insert(key);
        return key;
    }
    template<typename I> std::optional<Key> FromPKHBytes(I first, I last) const
    {
        const uint160 hash(std::vector<unsigned char>(first, last));
        for (const auto& [key, path] : input.m_tap_bip32_paths) {
            if (Hash160(key) == hash) { keys.insert(key); return key; }
        }
        return {};
    }
};

bool TaprootKeyRequired(const PSBTInput& input, const XOnlyPubKey& key)
{
    CTxOut utxo;
    if (!input.GetUTXO(utxo) || !utxo.scriptPubKey.IsPayToTaproot()) return false;
    const XOnlyPubKey output(std::span{utxo.scriptPubKey}.subspan(2));
    if (input.m_tap_key_sig.empty()) {
        if (key == output) return true;
        const auto tweaked = key.CreateTapTweak(input.m_tap_merkle_root.IsNull() ? nullptr : &input.m_tap_merkle_root);
        if (key == input.m_tap_internal_key && tweaked && tweaked->first == output) return true;
    }
    for (const auto& [leaf, controls] : input.m_tap_scripts) {
        const auto& [script, version] = leaf;
        if (version != TAPROOT_LEAF_TAPSCRIPT) continue;
        const auto hash = ComputeTapleafHash(version, script);
        if (input.m_tap_script_sigs.contains({key, hash})) continue;
        const bool committed = std::ranges::any_of(controls, [&](const auto& control) {
            if (control.size() < TAPROOT_CONTROL_BASE_SIZE || control.size() > TAPROOT_CONTROL_MAX_SIZE ||
                (control.size() - TAPROOT_CONTROL_BASE_SIZE) % TAPROOT_CONTROL_NODE_SIZE != 0 || (control[0] & 0xfe) != version) return false;
            return output.CheckTapTweak(XOnlyPubKey(std::span{control}.subspan(1, 32)), ComputeTaprootMerkleRoot(control, hash), control[0] & 1);
        });
        if (!committed) continue;
        const TaprootScriptKeys parsed{input, {}};
        if (miniscript::FromScript(CScript(script.begin(), script.end()), parsed) && parsed.keys.contains(key)) return true;
    }
    return false;
}
}

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
        result.needs_unlock = wallet.isLocked();
        for (size_t index = 0; index < analyzed.inputs.size(); ++index) {
            const auto& input = analyzed.inputs[index];
            if (PSBTInputSigned(input)) continue;
            // A multisig output need not belong to this wallet as a whole.
            // Core can contribute a local key named by its public derivation
            // data without claiming ownership or signing during import.
            for (const auto& [pubkey, origin] : input.hd_keypaths) {
                const auto& required = analysis.inputs[index].missing_sigs;
                if (std::ranges::find(required, pubkey.GetID()) != required.end() && wallet.hasSigningKey(pubkey)) result.can_sign = true;
            }
            for (const auto& [pubkey, paths] : input.m_tap_bip32_paths) {
                if (!TaprootKeyRequired(input, pubkey)) continue;
                for (const unsigned char prefix : {0x02, 0x03}) {
                    std::vector<unsigned char> full{prefix};
                    full.insert(full.end(), pubkey.begin(), pubkey.end());
                    if (wallet.hasSigningKey(CPubKey(full))) result.can_sign = true;
                }
            }
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
