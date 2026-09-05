// Copyright (c) 2024-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_WALLET_WALLETUNLOCKCONTEXT_H
#define BITCOIN_QML_WALLET_WALLETUNLOCKCONTEXT_H
#include <support/allocators/secure.h>
#include <QString>
namespace interfaces { class Wallet; }

/** Scoped to one WalletSession::runAction worker call, never a pending UI review. */
class WalletUnlockContext
{
public:
    WalletUnlockContext(interfaces::Wallet& wallet, const SecureString& passphrase);
    ~WalletUnlockContext();
    WalletUnlockContext(const WalletUnlockContext&) = delete;
    WalletUnlockContext& operator=(const WalletUnlockContext&) = delete;
    bool valid() const { return m_valid; }
    QString error() const { return m_error; }
private:
    interfaces::Wallet& m_wallet;
    bool m_relock{false}, m_valid{false};
    QString m_error;
};
#endif // BITCOIN_QML_WALLET_WALLETUNLOCKCONTEXT_H
