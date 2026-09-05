// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_WALLET_WALLETCREATIONMODEL_H
#define BITCOIN_QML_WALLET_WALLETCREATIONMODEL_H

#include <qml/wallet/walletworkflowmodel.h>

class WalletCreationModel : public WalletWorkflowModel
{
    Q_OBJECT
public:
    using WalletWorkflowModel::WalletWorkflowModel;
    static QString nameSyntaxError(const QString& name);
    Q_INVOKABLE QString nameError(const QString& name) const;
    Q_INVOKABLE bool create(const QString& name, const QString& passphrase, const QString& confirmation);
};

#endif // BITCOIN_QML_WALLET_WALLETCREATIONMODEL_H
