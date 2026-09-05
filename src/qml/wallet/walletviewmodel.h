// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_WALLET_WALLETVIEWMODEL_H
#define BITCOIN_QML_WALLET_WALLETVIEWMODEL_H

#include <qml/wallet/walletoverviewmodel.h>
#include <qml/wallet/transactionhistorymodel.h>
#include <qml/wallet/walletreceivemodel.h>
#include <qml/wallet/activitytimelinemodel.h>
#include <qml/wallet/activityfilterproxymodel.h>
#include <qml/wallet/addresslistmodel.h>
#include <qml/wallet/signverifymessagemodel.h>
#include <qml/wallet/walletsession.h>
#include <qml/wallet/walletsecuritymodel.h>
#include <qml/wallet/walletstoragemodel.h>
#include <qml/wallet/walletsendmodel.h>
#include <qml/wallet/psbtmodel.h>
#include <qml/wallet/bumpfeemodel.h>
#include <QObject>

class WalletSession;

/** Composition only: workflow behavior belongs to the corresponding child. */
class WalletViewModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString sessionId READ sessionId CONSTANT)
    Q_PROPERTY(WalletReceiveModel* receive READ receive CONSTANT)
    Q_PROPERTY(TransactionHistoryModel* history READ history CONSTANT)
    Q_PROPERTY(ActivityTimelineModel* activity READ activity CONSTANT)
    Q_PROPERTY(ActivityFilterProxyModel* activityFilter READ activityFilter CONSTANT)
    Q_PROPERTY(AddressListModel* addresses READ addresses CONSTANT)
    Q_PROPERTY(SignVerifyMessageModel* messages READ messages CONSTANT)
    Q_PROPERTY(WalletOverviewModel* overview READ overview CONSTANT)
    Q_PROPERTY(WalletSendModel* send READ send CONSTANT)
    Q_PROPERTY(PsbtModel* psbt READ psbt CONSTANT)
    Q_PROPERTY(BumpFeeModel* bump READ bump CONSTANT)
    Q_PROPERTY(WalletSecurityModel* security READ security CONSTANT)
    Q_PROPERTY(WalletStorageModel* storage READ storage CONSTANT)
public:
    WalletViewModel(WalletSession& session, const QString& network, CFeeRate dust_relay_fee, interfaces::Node& node, QObject* parent = nullptr)
        : QObject(parent), m_session(session), m_overview(session, network, this), m_security(session, this), m_storage(session, m_overview, this), m_history(session, this), m_receive(session, network, this), m_activity(session.id(), m_history, *m_receive.history(), this), m_activity_filter(m_activity, this), m_addresses(session, this), m_messages(session, this), m_send(session, dust_relay_fee, this), m_psbt(session, node, this), m_bump(session, node, this) {}
    QString sessionId() const { return QString::number(m_session.id()); }
    WalletReceiveModel* receive() { return &m_receive; }
    TransactionHistoryModel* history() { return &m_history; }
    ActivityTimelineModel* activity() { return &m_activity; }
    ActivityFilterProxyModel* activityFilter() { return &m_activity_filter; }
    AddressListModel* addresses() { return &m_addresses; }
    SignVerifyMessageModel* messages() { return &m_messages; }
    WalletSession& session() const { return m_session; }
    WalletSendModel* send() { return &m_send; }
    PsbtModel* psbt() { return &m_psbt; }
    BumpFeeModel* bump() { return &m_bump; }
    WalletOverviewModel* overview() { return &m_overview; }
    WalletSecurityModel* security() { return &m_security; }
    WalletStorageModel* storage() { return &m_storage; }
private:
    WalletSession& m_session;
    WalletOverviewModel m_overview;
    WalletSecurityModel m_security;
    WalletStorageModel m_storage;
    TransactionHistoryModel m_history;
    WalletReceiveModel m_receive;
    ActivityTimelineModel m_activity;
    ActivityFilterProxyModel m_activity_filter;
    AddressListModel m_addresses;
    SignVerifyMessageModel m_messages;
    WalletSendModel m_send;
    PsbtModel m_psbt;
    BumpFeeModel m_bump;
};

#endif // BITCOIN_QML_WALLET_WALLETVIEWMODEL_H
