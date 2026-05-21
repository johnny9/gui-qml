// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_WALLETUNLOCK_H
#define BITCOIN_QML_MODELS_WALLETUNLOCK_H

#include <support/allocators/secure.h>

#include <functional>

namespace interfaces {
class Wallet;
} // namespace interfaces

enum class WalletUnlockResult {
    AlreadyUnlocked,
    UnlockedNowRelockRequired,
    IncorrectPassphrase,
};

//! Try to unlock `wallet` with `passphrase`. `passphrase` is wiped on every return path.
WalletUnlockResult TryUnlockWithPassphrase(interfaces::Wallet& wallet,
                                           SecureString& passphrase);

class WalletRelockGuard
{
public:
    WalletRelockGuard(interfaces::Wallet& wallet,
                      std::function<void()> refresh_security_state,
                      bool active);
    ~WalletRelockGuard();

    WalletRelockGuard(const WalletRelockGuard&) = delete;
    WalletRelockGuard& operator=(const WalletRelockGuard&) = delete;

    void relock();

private:
    interfaces::Wallet& m_wallet;
    std::function<void()> m_refresh_security_state;
    bool m_active;
};

#endif // BITCOIN_QML_MODELS_WALLETUNLOCK_H
