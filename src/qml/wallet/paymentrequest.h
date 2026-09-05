// Copyright (c) 2024-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_WALLET_PAYMENTREQUEST_H
#define BITCOIN_QML_WALLET_PAYMENTREQUEST_H
#include <qml/wallet/receiverequestentry.h>
#include <QObject>
#include <optional>

class PaymentRequest : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString id READ id NOTIFY changed)
    Q_PROPERTY(QString address READ address NOTIFY changed)
    Q_PROPERTY(QString label READ label WRITE setLabel NOTIFY changed)
    Q_PROPERTY(QString message READ message WRITE setMessage NOTIFY changed)
    Q_PROPERTY(QString noteSelf READ noteSelf WRITE setNoteSelf NOTIFY changed)
    Q_PROPERTY(QString amount READ amount WRITE setAmount NOTIFY changed)
    Q_PROPERTY(QString addressType READ addressType WRITE setAddressType NOTIFY changed)
    Q_PROPERTY(QString uri READ uri NOTIFY changed)
    Q_PROPERTY(QString qrImageSource READ qrImageSource NOTIFY changed)
public:
    explicit PaymentRequest(QObject* parent = nullptr) : QObject(parent) {}
    QString id() const { return m_entry.id ? QString::number(m_entry.id) : QString{}; }
    QString address() const { return QString::fromStdString(m_entry.recipient.address); }
    QString label() const { return QString::fromStdString(m_entry.recipient.label); }
    QString message() const { return QString::fromStdString(m_entry.recipient.message); }
    QString noteSelf() const { return QString::fromStdString(m_entry.recipient.noteSelf); }
    QString amount() const { return m_amount; }
    QString addressType() const { return m_address_type; }
    void setLabel(const QString& value);
    void setMessage(const QString& value);
    void setNoteSelf(const QString& value);
    void setAmount(const QString& value);
    void setAddressType(const QString& value);
    QString uri() const;
    QString qrImageSource() const;
    std::optional<QmlRecentRequestEntry> validatedEntry() const;
    void setEntry(const QmlRecentRequestEntry& entry);
    void useAsTemplate(const QmlRecentRequestEntry& entry, bool reuse_address);
    Q_INVOKABLE void clear();
Q_SIGNALS:
    void changed();
private:
    QmlRecentRequestEntry m_entry;
    QString m_amount, m_address_type;
};
#endif // BITCOIN_QML_WALLET_PAYMENTREQUEST_H
