// Copyright (c) 2024-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/wallet/walletunlockcontext.h>
#include <interfaces/wallet.h>
#include <QObject>

WalletUnlockContext::WalletUnlockContext(interfaces::Wallet& wallet, const SecureString& passphrase) : m_wallet{wallet}
{
    if (!wallet.isCrypted() || !wallet.isLocked()) {
        m_valid = true;
    } else if (wallet.unlock(passphrase)) {
        m_valid = true;
        m_relock = true;
    } else {
        m_error = QObject::tr("The wallet password is incorrect or missing.");
    }
}

WalletUnlockContext::~WalletUnlockContext()
{
    if (m_relock) m_wallet.lock();
}
