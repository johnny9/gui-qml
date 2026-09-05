// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_WALLET_WALLETPASSPHRASE_H
#define BITCOIN_QML_WALLET_WALLETPASSPHRASE_H

#include <support/allocators/secure.h>
#include <support/cleanse.h>
#include <QString>

inline SecureString WalletPassphrase(const QString& passphrase)
{
    QByteArray bytes = passphrase.toUtf8();
    SecureString secret(bytes.constData(), bytes.size());
    memory_cleanse(bytes.data(), bytes.size());
    return secret;
}

#endif // BITCOIN_QML_WALLET_WALLETPASSPHRASE_H
