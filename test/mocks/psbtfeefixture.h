// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_TEST_MOCKS_PSBTFEEFIXTURE_H
#define BITCOIN_QML_TEST_MOCKS_PSBTFEEFIXTURE_H

#include <key.h>
#include <psbt.h>
#include <script/signingprovider.h>
#include <script/solver.h>

#include <stdexcept>
#include <vector>

namespace psbtfeefixture {
inline constexpr CAmount ACTUAL_FEE{500'000};
inline PartiallySignedTransaction Legacy(uint32_t version, bool forged, bool with_change = false)
{
    CKey first, second;
    first.MakeNewKey(true); second.MakeNewKey(true);
    FillableSigningProvider provider;
    if (!provider.AddKey(first) || !provider.AddKey(second)) throw std::runtime_error("fixture keys");
    CMutableTransaction previous;
    previous.vin.emplace_back(COutPoint{Txid::FromUint256(uint256::ONE), 0});
    previous.vout.emplace_back(1'000'000, GetScriptForDestination(PKHash(first.GetPubKey())));
    previous.vout.emplace_back(1'000'000, GetScriptForDestination(PKHash(second.GetPubKey())));
    const auto parent = MakeTransactionRef(previous);
    CMutableTransaction tx;
    tx.vin.emplace_back(COutPoint{parent->GetHash(), 0});
    tx.vin.emplace_back(COutPoint{parent->GetHash(), 1});
    tx.vout.emplace_back(1'500'000, GetScriptForDestination(PKHash(first.GetPubKey())));
    if (with_change) {
        tx.vout[0] = CTxOut{1'300'000, CScript{} << OP_DUP << OP_HASH160 << std::vector<unsigned char>(20, 0x42) << OP_EQUALVERIFY << OP_CHECKSIG};
        tx.vout.emplace_back(200'000, GetScriptForDestination(PKHash(first.GetPubKey())));
    }
    PartiallySignedTransaction psbt{tx, version};
    for (auto& input : psbt.inputs) input.non_witness_utxo = parent;
    const auto data = PrecomputePSBTData(psbt);
    if (!data) throw std::runtime_error("fixture precompute");
    for (size_t i = 0; i < psbt.inputs.size(); ++i) {
        if (SignPSBTInput(provider, psbt, i, &*data, {}) != PSBTError::OK) throw std::runtime_error("fixture signing");
    }
    if (!FinalizePSBT(psbt)) throw std::runtime_error("fixture finalization");
    if (forged) {
        psbt.inputs[1].non_witness_utxo = nullptr;
        psbt.inputs[1].witness_utxo = parent->vout[1];
        psbt.inputs[1].witness_utxo.nValue -= ACTUAL_FEE - 1;
    }
    return psbt;
}
} // namespace psbtfeefixture

#endif // BITCOIN_QML_TEST_MOCKS_PSBTFEEFIXTURE_H
