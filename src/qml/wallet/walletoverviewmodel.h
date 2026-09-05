// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_WALLET_WALLETOVERVIEWMODEL_H
#define BITCOIN_QML_WALLET_WALLETOVERVIEWMODEL_H

#include <QObject>

class WalletSession;

class WalletOverviewModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString name READ name CONSTANT)
    Q_PROPERTY(QString displayName READ displayName NOTIFY changed)
    Q_PROPERTY(QString balance READ balance NOTIFY changed)
    Q_PROPERTY(bool encrypted READ encrypted NOTIFY changed)
    Q_PROPERTY(bool locked READ locked NOTIFY changed)
    Q_PROPERTY(bool localSigning READ localSigning NOTIFY changed)
    Q_PROPERTY(bool canReceive READ canReceive NOTIFY changed)
    Q_PROPERTY(QString keyScheme READ keyScheme NOTIFY changed)
    Q_PROPERTY(bool available READ available NOTIFY changed)
public:
    explicit WalletOverviewModel(WalletSession& session, const QString& network, QObject* parent = nullptr);
    QString name() const;
    QString displayName() const;
    QString balance() const;
    bool encrypted() const;
    bool locked() const;
    bool localSigning() const;
    bool canReceive() const;
    QString keyScheme() const;
    bool available() const;
    QString aliasKey() const;
Q_SIGNALS:
    void changed();
private:
    WalletSession& m_session;
    QString m_network;
};

#endif // BITCOIN_QML_WALLET_WALLETOVERVIEWMODEL_H
