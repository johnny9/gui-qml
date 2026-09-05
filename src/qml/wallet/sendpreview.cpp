// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/wallet/sendpreview.h>
#include <chainparams.h>
#include <interfaces/wallet.h>
#include <key_io.h>
#include <pubkey.h>
#include <wallet/types.h>

#include <algorithm>
#include <stdexcept>

namespace {
CTxDestination PreviewChangeDestination(OutputType type)
{
    const uint160 dummy_key_hash{};
    switch (type) {
    case OutputType::BECH32M: return WitnessV1Taproot{XOnlyPubKey::NUMS_H};
    case OutputType::BECH32: return WitnessV0KeyHash{dummy_key_hash};
    case OutputType::P2SH_SEGWIT: return ScriptHash{GetScriptForDestination(WitnessV0KeyHash{dummy_key_hash})};
    case OutputType::LEGACY: return PKHash{dummy_key_hash};
    case OutputType::UNKNOWN: break;
    }
    throw std::runtime_error("The wallet change type is unavailable.");
}

int FallbackFeeMultiplier(unsigned int target)
{
    if (target == 1) return 3;
    if (target == 2) return 2;
    return 1;
}

std::optional<CAmount> TryPreview(const SendPreviewBackend& backend, const std::vector<wallet::CRecipient>& recipients, const wallet::CCoinControl& control, bool& fallback)
{
    if (const auto fee = backend.create_unsigned(recipients, control)) return fee;
    if (recipients.empty() || std::ranges::any_of(recipients, [](const auto& recipient) { return recipient.fSubtractFeeFromAmount; })) return {};
    // Full-balance drafts may lack the fee buffer. Estimate a value-only
    // subtract-fee variant without changing the actual recipient inputs.
    auto adjusted = recipients;
    std::ranges::max_element(adjusted, {}, &wallet::CRecipient::nAmount)->fSubtractFeeFromAmount = true;
    if (const auto fee = backend.create_unsigned(adjusted, control)) {
        fallback = true;
        return fee;
    }
    return {};
}
} // namespace

wallet::CCoinControl CoinControlForDraft(const SendDraftSnapshot& draft, bool preview)
{
    wallet::CCoinControl control;
    if (draft.selected_only && draft.selected_inputs.empty()) throw std::runtime_error("Select at least one available coin or enable automatic selection.");
    control.m_allow_other_inputs = !draft.selected_only;
    for (const auto& outpoint : draft.selected_inputs) control.Select(outpoint);
    control.m_change_type = draft.change_type;
    control.m_confirm_target = draft.fees.target;
    if (draft.fees.custom_per_kvb) {
        control.m_confirm_target.reset();
        control.m_feerate = CFeeRate(*draft.fees.custom_per_kvb);
    } else if (Params().GetChainType() == ChainType::REGTEST) {
        // Preserve the source's explicit regtest policy, including ordinary
        // preparation. A user-selected custom rate always takes precedence.
        control.m_confirm_target.reset();
        control.m_feerate = CFeeRate(wallet::DEFAULT_TRANSACTION_MINFEE);
    }
    if (preview) control.destChange = PreviewChangeDestination(draft.change_type);
    return control;
}

std::vector<wallet::CRecipient> RecipientsForDraft(const SendDraftSnapshot& draft, interfaces::Wallet& backend)
{
    if (!draft.selected_inputs.empty()) {
        std::set<COutPoint> available;
        for (const auto& [destination, group] : backend.listCoins()) {
            for (const auto& [outpoint, output] : group) {
                if (!backend.isLockedCoin(outpoint)) available.insert(outpoint);
            }
        }
        for (const auto& outpoint : draft.selected_inputs) {
            if (!available.contains(outpoint)) throw std::runtime_error("A selected coin is no longer available. Review the coin selection.");
        }
    }
    CAmount fixed_total{0};
    int maximum_count{0};
    for (const auto& recipient : draft.recipients) {
        if (recipient.maximum) { ++maximum_count; continue; }
        if (!MoneyRange(recipient.amount) || recipient.amount <= 0 || recipient.amount > MAX_MONEY - fixed_total) throw std::runtime_error("Invalid recipient amount.");
        fixed_total += recipient.amount;
    }
    if (maximum_count > 1 || draft.recipients.empty()) throw std::runtime_error("Invalid recipient draft.");
    const CAmount available = maximum_count ? backend.getAvailableBalance(CoinControlForDraft(draft, false)) : 0;
    if (maximum_count && available <= fixed_total) throw std::runtime_error("Insufficient funds for the remaining-balance recipient.");
    std::vector<wallet::CRecipient> result;
    for (const auto& recipient : draft.recipients) {
        const auto destination = DecodeDestination(recipient.address.toStdString());
        if (!IsValidDestination(destination)) throw std::runtime_error("Invalid recipient address.");
        result.push_back({destination, recipient.maximum ? available - fixed_total : recipient.amount, recipient.subtract_fee || recipient.maximum});
    }
    return result;
}

FeePreview EstimateSendFee(const SendDraftSnapshot& draft, const std::vector<wallet::CRecipient>& recipients, quint64 request_id, const SendPreviewBackend& backend)
{
    FeePreview preview{draft.session_id, draft.session_generation, draft.revision, request_id, {}, false, false, {}};
    auto control = CoinControlForDraft(draft, true);
    preview.fee = TryPreview(backend, recipients, control, preview.fallback);
    if (!preview.fee && !draft.fees.custom_per_kvb && Params().GetChainType() != ChainType::REGTEST) {
        const CAmount required = backend.required_fee(1000);
        if (required > 0 && required <= MAX_MONEY / 3) {
            control.m_confirm_target.reset();
            control.m_feerate = CFeeRate(required * FallbackFeeMultiplier(draft.fees.target));
            preview.fee = TryPreview(backend, recipients, control, preview.fallback);
            preview.fallback = true;
        }
    }
    if (!preview.fee) {
        preview.error = QObject::tr("The wallet could not estimate this transaction. Check its funds and recipients.");
        return preview;
    }
    CAmount total{0};
    bool subtract{false};
    for (const auto& recipient : recipients) {
        total += recipient.nAmount;
        subtract |= recipient.fSubtractFeeFromAmount;
    }
    const CAmount balance = backend.available_balance(control);
    preview.affordable = total <= balance && (subtract || *preview.fee <= balance - total);
    if (!preview.affordable) preview.error = QObject::tr("The draft leaves insufficient funds for its fee.");
    return preview;
}

FeePreview EstimateSendFee(interfaces::Wallet& backend, const SendDraftSnapshot& draft, quint64 request_id)
{
    const SendPreviewBackend preview_backend{
        [&backend](const auto& recipients, const auto& control) -> std::optional<CAmount> {
            if (const auto created = backend.createTransaction(recipients, control, false, std::nullopt)) return created->fee;
            return {};
        },
        [&backend](const auto& control) { return backend.getAvailableBalance(control); },
        [&backend](unsigned int size) { return backend.getRequiredFee(size); },
    };
    return EstimateSendFee(draft, RecipientsForDraft(draft, backend), request_id, preview_backend);
}
