// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_WALLET_FEEPOLICY_H
#define BITCOIN_QML_WALLET_FEEPOLICY_H

#include <consensus/amount.h>
#include <QString>
#include <optional>

struct FeePolicy {
    unsigned int target{2};
    std::optional<CAmount> custom_per_kvb;
};

//! Parse sat/vB to integer sat/kvB, without binary floating-point policy.
std::optional<CAmount> ParseCustomFeeRate(const QString& text);

#endif // BITCOIN_QML_WALLET_FEEPOLICY_H
