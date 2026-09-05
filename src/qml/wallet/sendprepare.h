// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_WALLET_SENDPREPARE_H
#define BITCOIN_QML_WALLET_SENDPREPARE_H

#include <qml/wallet/senddraftsnapshot.h>
#include <qml/wallet/transactionreviewmodel.h>
#include <support/allocators/secure.h>
namespace interfaces { class Wallet; }

struct PreparedSendResult {
    std::optional<ReviewSnapshot> review;
    QString error;
};

//! Call only inside WalletSession::runAction; unlock lifetime ends before return.
PreparedSendResult PrepareWalletSend(interfaces::Wallet& wallet, const SendDraftSnapshot& draft, const SecureString& passphrase);

#endif // BITCOIN_QML_WALLET_SENDPREPARE_H
