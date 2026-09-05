// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_WALLET_WALLETSECURITYMODEL_H
#define BITCOIN_QML_WALLET_WALLETSECURITYMODEL_H

#include <QObject>
#include <QString>
class WalletSession;

class WalletSecurityModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool encrypted READ encrypted NOTIFY changed)
    Q_PROPERTY(bool locked READ locked NOTIFY changed)
    Q_PROPERTY(bool supported READ supported NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
public:
    explicit WalletSecurityModel(WalletSession& session, QObject* parent = nullptr);
    bool encrypted() const;
    bool locked() const;
    bool supported() const;
    bool busy() const;
    QString error() const { return m_error; }
    Q_INVOKABLE bool encrypt(const QString& passphrase, const QString& confirmation);
    Q_INVOKABLE bool changePassphrase(const QString& old_passphrase, const QString& new_passphrase, const QString& confirmation);
Q_SIGNALS:
    void changed();
    void succeeded();
private:
    bool fail(const QString& message);
    WalletSession& m_session;
    QString m_error;
};

#endif // BITCOIN_QML_WALLET_WALLETSECURITYMODEL_H
