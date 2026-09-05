// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_WALLET_WALLETVIEWMODEL_H
#define BITCOIN_QML_WALLET_WALLETVIEWMODEL_H

#include <qml/wallet/walletoverviewmodel.h>
#include <qml/wallet/transactionhistorymodel.h>
#include <qml/wallet/walletsession.h>
#include <qml/wallet/walletsecuritymodel.h>
#include <qml/wallet/walletstoragemodel.h>
#include <QObject>

class WalletSession;

/** Composition only: workflow behavior belongs to the corresponding child. */
class WalletViewModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString sessionId READ sessionId CONSTANT)
    Q_PROPERTY(TransactionHistoryModel* history READ history CONSTANT)
    Q_PROPERTY(WalletOverviewModel* overview READ overview CONSTANT)
    Q_PROPERTY(WalletSecurityModel* security READ security CONSTANT)
    Q_PROPERTY(WalletStorageModel* storage READ storage CONSTANT)
public:
    WalletViewModel(WalletSession& session, const QString& network, QObject* parent = nullptr)
        : QObject(parent), m_session(session), m_overview(session, network, this), m_security(session, this), m_storage(session, m_overview, this), m_history(session, this) {}
    QString sessionId() const { return QString::number(m_session.id()); }
    TransactionHistoryModel* history() { return &m_history; }
    WalletSession& session() const { return m_session; }
    WalletOverviewModel* overview() { return &m_overview; }
    WalletSecurityModel* security() { return &m_security; }
    WalletStorageModel* storage() { return &m_storage; }
private:
    WalletSession& m_session;
    WalletOverviewModel m_overview;
    WalletSecurityModel m_security;
    WalletStorageModel m_storage;
    TransactionHistoryModel m_history;
};

#endif // BITCOIN_QML_WALLET_WALLETVIEWMODEL_H
