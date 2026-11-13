// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_SENDDRAFT_H
#define BITCOIN_QML_SENDDRAFT_H

#include <addresstype.h>
#include <consensus/amount.h>
#include <QString>
#include <QtGlobal>
#include <algorithm>
#include <map>
#include <optional>
#include <vector>

namespace senddraft {
struct Recipient {
    quint64 token{0}, revision{0};
    QString address, label, message, request_label, address_error, amount_error, input_error;
    CAmount amount{0};
    CTxDestination destination;
    bool valid{false}, address_valid{false}, input_acceptable{true}, payment_request{false}, validation_pending{true};
};
struct Snapshot {
    std::vector<Recipient> recipients;
    std::map<quint64, size_t> positions;
    quint64 revision{0};
    CAmount total{0};
    QString total_text, error;
    int invalid_count{0}, pending_count{0};
    bool all_valid{false}, duplicate_destinations{false}, amount_total_valid{true};
};

// Run with the captured immutable draft on the background executor. The
// maximum recipient's previous amount does not participate in its replacement.
inline std::optional<CAmount> GrossRemainder(const Snapshot& draft, quint64 maximum, CAmount available)
{
    if (!maximum || !MoneyRange(available) || !draft.positions.contains(maximum)) return std::nullopt;
    CAmount other{0};
    for (const auto& recipient : draft.recipients) {
        if (recipient.token == maximum) continue;
        if (!recipient.valid || recipient.validation_pending || !MoneyRange(recipient.amount) || recipient.amount > MAX_MONEY - other) return std::nullopt;
        other += recipient.amount;
    }
    return std::max<CAmount>(0, available - other);
}
} // namespace senddraft
#endif // BITCOIN_QML_SENDDRAFT_H
