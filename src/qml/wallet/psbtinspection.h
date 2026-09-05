// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_WALLET_PSBTINSPECTION_H
#define BITCOIN_QML_WALLET_PSBTINSPECTION_H

#include <psbt.h>
#include <QString>
#include <optional>
#include <vector>

namespace interfaces { class Node; class Wallet; }

struct PsbtOutput {
    QString address;
    CAmount amount;
    bool owned;
};
struct PsbtInspection {
    CTransactionRef transaction;
    std::optional<CAmount> fee;
    std::vector<PsbtOutput> outputs;
    bool complete{false};
    bool can_sign{false};
    bool needs_unlock{false};
    bool known{false};
    bool inputs_verified{false};
    int unsigned_inputs{0};
    QString error;
};

/** Analysis uses copies: neither import nor a capability probe changes a PSBT. */
PsbtInspection InspectPsbt(const PartiallySignedTransaction& psbt, interfaces::Wallet& wallet, interfaces::Node& node);
bool PsbtInputsMatchChain(const PartiallySignedTransaction& psbt, interfaces::Node& node);
bool PsbtTransactionKnown(const CTransactionRef& transaction, interfaces::Wallet& wallet, interfaces::Node& node);

#endif // BITCOIN_QML_WALLET_PSBTINSPECTION_H
