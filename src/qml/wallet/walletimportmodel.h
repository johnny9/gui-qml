// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_WALLET_WALLETIMPORTMODEL_H
#define BITCOIN_QML_WALLET_WALLETIMPORTMODEL_H

#include <qml/wallet/walletworkflowmodel.h>

class WalletImportModel : public WalletWorkflowModel
{
    Q_OBJECT
public:
    using WalletWorkflowModel::WalletWorkflowModel;
    static QString localPath(const QString& path);
    Q_INVOKABLE bool restore(const QString& backup, const QString& name);
};

#endif // BITCOIN_QML_WALLET_WALLETIMPORTMODEL_H
