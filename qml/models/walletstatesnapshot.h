// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_MODELS_WALLETSTATESNAPSHOT_H
#define BITCOIN_QML_MODELS_WALLETSTATESNAPSHOT_H
#include <interfaces/wallet.h>
#include <QString>
#include <memory>
namespace interfaces { class Node; }
enum class WalletKeyScheme { SingleKey, WatchOnly, MultiKey, ExternalSigner };
WalletKeyScheme ReadWalletKeyScheme(interfaces::Wallet& wallet);
struct WalletStateSnapshot {
    QString name;
    CAmount balance{0};
    CAmount available_balance{0};
    WalletKeyScheme key_scheme{WalletKeyScheme::SingleKey};
    bool encrypted{false};
    bool locked{false};
    bool private_keys_disabled{false};
    bool external_signer{false};
};
class WalletStateBackend {
public:
    explicit WalletStateBackend(std::shared_ptr<interfaces::Wallet> wallet);
    WalletStateSnapshot read(interfaces::Node* node);
private:
    std::shared_ptr<interfaces::Wallet> m_wallet;
};
#endif // BITCOIN_QML_MODELS_WALLETSTATESNAPSHOT_H
