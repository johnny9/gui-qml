// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/models/walletstatesnapshot.h>
#include <wallet/coincontrol.h>
#include <wallet/scriptpubkeyman.h>
#include <wallet/types.h>
#include <wallet/wallet.h>
namespace {
bool WalletUsesMultiKeyDescriptor(const wallet::CWallet& wallet)
{
    for (const auto* spk_man : wallet.GetActiveScriptPubKeyMans()) {
        const auto* descriptor_spk_man = dynamic_cast<const wallet::DescriptorScriptPubKeyMan*>(spk_man);
        if (!descriptor_spk_man) {
            continue;
        }

        std::string descriptor;
        if (descriptor_spk_man->GetDescriptorString(descriptor, /*priv=*/false) &&
            descriptor.find("multi(") != std::string::npos) {
            return true;
        }
    }

    return false;
}
}

WalletKeyScheme ReadWalletKeyScheme(interfaces::Wallet& wallet)
{
    const wallet::CWallet* raw_wallet = wallet.wallet();
    if (raw_wallet) {
        LOCK(raw_wallet->cs_wallet);
        if (raw_wallet->IsWalletFlagSet(wallet::WALLET_FLAG_EXTERNAL_SIGNER)) {
            return WalletKeyScheme::ExternalSigner;
        }
        if (raw_wallet->IsWalletFlagSet(wallet::WALLET_FLAG_DISABLE_PRIVATE_KEYS)) {
            return WalletKeyScheme::WatchOnly;
        }
        if (WalletUsesMultiKeyDescriptor(*raw_wallet)) {
            return WalletKeyScheme::MultiKey;
        }
        return WalletKeyScheme::SingleKey;
    }
    if (wallet.privateKeysDisabled()) {
        return WalletKeyScheme::WatchOnly;
    }
    return WalletKeyScheme::SingleKey;
}

WalletStateBackend::WalletStateBackend(std::shared_ptr<interfaces::Wallet> wallet) : m_wallet(std::move(wallet)) {}

WalletStateSnapshot WalletStateBackend::read(interfaces::Node*)
{
    WalletStateSnapshot state;
    state.name = QString::fromStdString(m_wallet->getWalletName());
    state.balance = m_wallet->getBalance();
    state.available_balance = m_wallet->getAvailableBalance(wallet::CCoinControl{});
    state.key_scheme = ReadWalletKeyScheme(*m_wallet);
    state.encrypted = m_wallet->isCrypted();
    state.locked = m_wallet->isLocked();
    state.private_keys_disabled = m_wallet->privateKeysDisabled();
    state.external_signer = m_wallet->hasExternalSigner();
    return state;
}
