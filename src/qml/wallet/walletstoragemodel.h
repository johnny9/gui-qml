// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_WALLET_WALLETSTORAGEMODEL_H
#define BITCOIN_QML_WALLET_WALLETSTORAGEMODEL_H

#include <QObject>
#include <QString>
class WalletSession;
class WalletOverviewModel;

class WalletStorageModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString location READ location CONSTANT)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
public:
    WalletStorageModel(WalletSession& session, WalletOverviewModel& overview, QObject* parent = nullptr);
    QString location() const;
    bool busy() const;
    QString error() const { return m_error; }
    Q_INVOKABLE bool backup(const QString& destination);
    Q_INVOKABLE bool setAlias(const QString& alias);
    Q_INVOKABLE bool openLocation();
Q_SIGNALS:
    void changed();
    void succeeded();
private:
    bool fail(const QString& message);
    WalletSession& m_session;
    WalletOverviewModel& m_overview;
    QString m_error;
};

#endif // BITCOIN_QML_WALLET_WALLETSTORAGEMODEL_H
