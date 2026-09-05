// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_WALLET_WALLETMIGRATIONMODEL_H
#define BITCOIN_QML_WALLET_WALLETMIGRATIONMODEL_H

#include <qml/wallet/walletworkflowmodel.h>

class WalletMigrationModel : public WalletWorkflowModel
{
    Q_OBJECT
    Q_PROPERTY(QString walletName READ walletName NOTIFY targetChanged)
    Q_PROPERTY(bool needsPassphrase READ needsPassphrase NOTIFY targetChanged)
public:
    using WalletWorkflowModel::WalletWorkflowModel;
    QString walletName() const { return m_name; }
    bool needsPassphrase() const { return m_encrypted; }
    Q_INVOKABLE bool inspect(const QString& name);
    Q_INVOKABLE bool migrate(const QString& passphrase);
Q_SIGNALS:
    void targetChanged();
private:
    QString m_name;
    bool m_encrypted{false};
};

#endif // BITCOIN_QML_WALLET_WALLETMIGRATIONMODEL_H
