// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/wallet/sendprepare.h>
#include <qml/wallet/sendpreview.h>
#include <qml/wallet/walletunlockcontext.h>
#include <interfaces/wallet.h>
#include <key_io.h>
#include <util/translation.h>

PreparedSendResult PrepareWalletSend(interfaces::Wallet& wallet, const SendDraftSnapshot& draft, const SecureString& passphrase)
{
    WalletUnlockContext unlock(wallet, passphrase);
    if (!unlock.valid()) return {{}, unlock.error()};
    const auto created = wallet.createTransaction(RecipientsForDraft(draft, wallet), CoinControlForDraft(draft, false), true, std::nullopt);
    if (!created) return {{}, QString::fromStdString(util::ErrorString(created).translated)};
    if (created->fee > wallet.getDefaultMaxTxFee()) return {{}, QObject::tr("The transaction fee exceeds the wallet's maximum fee.")};
    ReviewSnapshot review{draft.session_id, draft.session_generation, draft.revision, created->tx, created->fee, {}, {}};
    for (size_t i = 0; i < created->tx->vout.size(); ++i) {
        const auto& output = created->tx->vout[i];
        CTxDestination destination;
        ExtractDestination(output.scriptPubKey, destination);
        review.outputs.push_back({QString::fromStdString(EncodeDestination(destination)), output.nValue,
                                  created->change_pos && *created->change_pos == i});
    }
    return {std::move(review), {}};
}
