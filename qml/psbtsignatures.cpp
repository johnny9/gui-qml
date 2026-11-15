// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/psbtsignatures.h>

#include <psbt.h>

#include <optional>

bool psbtsignatures::VerifyFinalizedInputs(const PartiallySignedTransaction& document, bool require_all)
{
    if (document.inputs.empty()) return false;
    std::optional<PrecomputedTransactionData> data;
    for (size_t i = 0; i < document.inputs.size(); ++i) {
        if (!PSBTInputSigned(document.inputs[i])) {
            if (require_all) return false;
            continue;
        }
        if (!data) data = PrecomputePSBTData(document);
        if (!data || !PSBTInputSignedAndVerified(document, i, &*data)) return false;
    }
    return true;
}
