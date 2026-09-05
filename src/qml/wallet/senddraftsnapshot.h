// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_WALLET_SENDDRAFTSNAPSHOT_H
#define BITCOIN_QML_WALLET_SENDDRAFTSNAPSHOT_H

#include <consensus/amount.h>
#include <QString>
#include <vector>

struct SendRecipientValue {
    QString address;
    QString label;
    CAmount amount{0};
    bool subtract_fee{false};
    bool maximum{false};
};

//! Captured inputs, never a second mutable draft. Workers receive a value copy.
struct SendDraftSnapshot {
    quint64 session_id{0};
    quint64 session_generation{0};
    quint64 revision{0};
    std::vector<SendRecipientValue> recipients;
};

#endif // BITCOIN_QML_WALLET_SENDDRAFTSNAPSHOT_H
