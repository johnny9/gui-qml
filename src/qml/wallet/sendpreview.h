// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_WALLET_SENDPREVIEW_H
#define BITCOIN_QML_WALLET_SENDPREVIEW_H

#include <qml/wallet/feeselectionmodel.h>
#include <wallet/coincontrol.h>
#include <wallet/wallet.h>
#include <functional>

namespace interfaces { class Wallet; }

/** Narrow unsigned-preview boundary. Core still creates transactions and owns
 * balance/fee policy; controlled replies let the fallback rules be tested. */
struct SendPreviewBackend {
    std::function<std::optional<CAmount>(const std::vector<wallet::CRecipient>&, const wallet::CCoinControl&)> create_unsigned;
    std::function<CAmount(const wallet::CCoinControl&)> available_balance;
    std::function<CAmount(unsigned int)> required_fee;
};

wallet::CCoinControl CoinControlForDraft(const SendDraftSnapshot& draft, bool preview);
std::vector<wallet::CRecipient> RecipientsForDraft(const SendDraftSnapshot& draft, interfaces::Wallet& backend);
FeePreview EstimateSendFee(const SendDraftSnapshot& draft, const std::vector<wallet::CRecipient>& recipients, quint64 request_id, const SendPreviewBackend& backend);
FeePreview EstimateSendFee(interfaces::Wallet& backend, const SendDraftSnapshot& draft, quint64 request_id);

#endif // BITCOIN_QML_WALLET_SENDPREVIEW_H
