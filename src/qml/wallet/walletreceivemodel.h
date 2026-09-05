// Copyright (c) 2024-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_WALLET_WALLETRECEIVEMODEL_H
#define BITCOIN_QML_WALLET_WALLETRECEIVEMODEL_H
#include <qml/wallet/paymentrequest.h>
#include <qml/wallet/receiverequesthistorymodel.h>
#include <QObject>
class WalletSession;

class WalletReceiveModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(PaymentRequest* draft READ draft CONSTANT)
    Q_PROPERTY(ReceiveRequestHistoryModel* history READ history CONSTANT)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(bool needsUnlock READ needsUnlock NOTIFY changed)
    Q_PROPERTY(QStringList addressTypes READ addressTypes NOTIFY changed)
    Q_PROPERTY(QString defaultAddressType READ defaultAddressType WRITE setDefaultAddressType NOTIFY changed)
public:
    WalletReceiveModel(WalletSession& session, const QString& network, QObject* parent = nullptr);
    PaymentRequest* draft() { return &m_draft; }
    ReceiveRequestHistoryModel* history() { return &m_history; }
    QString error() const { return m_error.isEmpty() ? m_read_error : m_error; }
    bool busy() const;
    bool available() const;
    bool needsUnlock() const { return m_needs_unlock; }
    QStringList addressTypes() const { return m_address_types; }
    QString defaultAddressType() const { return m_default_type; }
    void setDefaultAddressType(const QString& type);
    Q_INVOKABLE void reload();
    Q_INVOKABLE bool save(const QString& passphrase = {});
    Q_INVOKABLE bool remove(const QString& id);
    Q_INVOKABLE bool edit(const QString& id);
    Q_INVOKABLE bool useAsTemplate(const QString& id, bool reuse_address = false);
    Q_INVOKABLE bool useAddress(const QString& address);
    Q_INVOKABLE void clear();
Q_SIGNALS:
    void changed();
    void requestSaved(const QString& id);
private:
    WalletSession& m_session;
    PaymentRequest m_draft;
    ReceiveRequestHistoryModel m_history;
    QString m_error, m_read_error, m_settings_key, m_default_type;
    QStringList m_address_types;
    bool m_loading{false}, m_reload_pending{false}, m_needs_unlock{false};
    quint64 m_draft_revision{0};
    int64_t m_last_id{0}; // Never retarget an old request route within this session.
};
#endif // BITCOIN_QML_WALLET_WALLETRECEIVEMODEL_H
