// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_PSBTSIGNATURES_H
#define BITCOIN_QML_PSBTSIGNATURES_H

struct PartiallySignedTransaction;
namespace psbtsignatures {
// Verify against supplied prevouts. Callers establish their authenticity before
// treating these signatures as authority for an immutable transaction review.
bool VerifyFinalizedInputs(const PartiallySignedTransaction& document, bool require_all);
} // namespace psbtsignatures

#endif // BITCOIN_QML_PSBTSIGNATURES_H
