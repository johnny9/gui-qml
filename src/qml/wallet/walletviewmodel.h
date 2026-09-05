// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_WALLET_WALLETVIEWMODEL_H
#define BITCOIN_QML_WALLET_WALLETVIEWMODEL_H

#include <qml/wallet/walletoverviewmodel.h>
#include <QObject>

class WalletSession;

/** Composition only: workflow behavior belongs to the corresponding child. */
class WalletViewModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(WalletOverviewModel* overview READ overview CONSTANT)
public:
    WalletViewModel(WalletSession& session, const QString& network, QObject* parent = nullptr)
        : QObject(parent), m_session(session), m_overview(session, network, this) {}
    WalletSession& session() const { return m_session; }
    WalletOverviewModel* overview() { return &m_overview; }
private:
    WalletSession& m_session;
    WalletOverviewModel m_overview;
};

#endif // BITCOIN_QML_WALLET_WALLETVIEWMODEL_H
