// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/psbtpolicy.h>

#include <coins.h>
#include <consensus/consensus.h>
#include <consensus/tx_verify.h>
#include <interfaces/chain.h>
#include <interfaces/node.h>
#include <policy/policy.h>
#include <util/result.h>

#include <QCoreApplication>

#include <algorithm>
#include <limits>
#include <map>

namespace {
QString Tr(const char* text) { return QCoreApplication::translate("PsbtQmlModel", text); }
} // namespace

psbtpolicy::Read psbtpolicy::ReadNode(interfaces::Node* node, interfaces::Chain* chain)
{
    return [node, chain](CTransactionRef transaction, CAmount fee) {
        Facts facts;
        if (!node || !chain || !transaction || !MoneyRange(fee)) return facts;
        try {
            const auto tip = node->getBestBlockHash();
            int height{-1}; int64_t mtp{0}; bool active{false};
            if (!chain->findBlock(tip, interfaces::FoundBlock{}.height(height).mtpTime(mtp).inActiveChain(active))
                || !active || height < 0 || height == std::numeric_limits<int>::max()) return facts;
            std::map<COutPoint, Coin> coins;
            for (const auto& input : transaction->vin) coins.try_emplace(input.prevout);
            chain->findCoins(coins); // Includes available mempool parents.
            for (const auto& [outpoint, coin] : coins) {
                facts.missing_inputs |= coin.IsSpent();
                facts.immature_inputs |= !coin.IsSpent() && coin.IsCoinBase()
                    && int64_t(height) + 1 - coin.nHeight < COINBASE_MATURITY;
            }
            facts.final = IsFinalTx(*transaction, height + 1, mtp);
            const auto minimum = std::max(chain->mempoolMinFee(), chain->relayMinFee());
            facts.sufficient_fee = fee >= minimum.GetFee(GetVirtualTransactionSize(*transaction));
            const auto limits = chain->checkChainLimits(transaction);
            facts.chain_limits = bool(limits);
            if (!limits) facts.chain_error = QString::fromStdString(util::ErrorString(limits).translated).left(4096);
            // A reorg during this collection invalidates the whole observation.
            if (node->getBestBlockHash() != tip) return Facts{};
            facts.tip = QString::fromStdString(tip.ToString());
            facts.observed = true;
        } catch (...) { return Facts{}; }
        return facts;
    };
}

psbtpolicy::Decision psbtpolicy::Evaluate(const Facts& facts)
{
    if (!facts.observed) return {State::Unknown, Tr("Fully signed. Current node policy will be checked when you submit.")};
    if (facts.missing_inputs) return {State::Blocked, Tr("An input is missing or already spent at the observed chain tip. Refresh the review before submitting.")};
    if (facts.immature_inputs) return {State::Blocked, Tr("An input spends an immature coinbase output. Refresh the review after more blocks.")};
    if (!facts.final) return {State::Blocked, Tr("The transaction lock time is not final at the observed chain tip.")};
    if (!facts.sufficient_fee) return {State::Blocked, Tr("The fee is below the node's current advertised minimum.")};
    if (!facts.chain_limits) return {State::Blocked, facts.chain_error.isEmpty() ? Tr("The transaction exceeds the node's current mempool chain limits.") : facts.chain_error};
    return {State::NoKnownObstacle, Tr("Fully signed. Read-only node checks found no obstacle; submission will check current policy, including conflicts and replacements.")};
}
