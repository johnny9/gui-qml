// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/paymentrequest.h>

#include <key_io.h>

#include <QString>

PaymentRequest::PaymentRequest(QObject* parent)
    : QObject(parent)
{
    m_amount = new BitcoinAmount(this);
}

QString PaymentRequest::address() const
{
    return QString::fromStdString(EncodeDestination(m_destination));
}

QString PaymentRequest::addressFormatted() const
{
    return FormatAddress(address());
}

QString PaymentRequest::label() const
{
    return m_label;
}

void PaymentRequest::setLabel(const QString& label)
{
    if (m_label == label) {
        return;
    }
    m_label = label;
    Q_EMIT labelChanged();
}

QString PaymentRequest::message() const
{
    return m_message;
}

void PaymentRequest::setMessage(const QString& message)
{
    if (m_message == message) {
        return;
    }
    m_message = message;
    Q_EMIT messageChanged();
}

QString PaymentRequest::addressType() const
{
    return m_address_type;
}

void PaymentRequest::setAddressType(const QString& address_type)
{
    if (m_address_type == address_type) {
        return;
    }
    m_address_type = address_type;
    Q_EMIT addressTypeChanged();
}

BitcoinAmount* PaymentRequest::amount() const
{
    return m_amount;
}

QString PaymentRequest::amountError() const
{
    return m_amountError;
}

void PaymentRequest::setAmountError(const QString& error)
{
    if (m_amountError == error) {
        return;
    }
    m_amountError = error;
    Q_EMIT amountErrorChanged();
}

QString PaymentRequest::id() const
{
    return m_id;
}

void PaymentRequest::setId(unsigned int id)
{
    const QString new_id = QString::number(id);
    if (m_id == new_id) {
        return;
    }
    m_id = new_id;
    Q_EMIT idChanged();
}

bool PaymentRequest::needsUnlock() const
{
    return m_needs_unlock;
}

void PaymentRequest::setNeedsUnlock(bool needs_unlock)
{
    if (m_needs_unlock == needs_unlock) {
        return;
    }
    m_needs_unlock = needs_unlock;
    Q_EMIT needsUnlockChanged();
}

QString PaymentRequest::unlockError() const
{
    return m_unlock_error;
}

void PaymentRequest::setUnlockError(const QString& error)
{
    if (m_unlock_error == error) {
        return;
    }
    m_unlock_error = error;
    Q_EMIT unlockErrorChanged();
}

void PaymentRequest::setDestination(const CTxDestination& destination)
{
    m_destination = destination;
    Q_EMIT addressChanged();
}

CTxDestination PaymentRequest::destination() const
{
    return m_destination;
}

void PaymentRequest::clear()
{
    m_destination = CNoDestination();
    m_label.clear();
    m_message.clear();
    m_address_type.clear();
    m_amount->clear();
    m_amountError.clear();
    m_id.clear();
    m_needs_unlock = false;
    m_unlock_error.clear();
    Q_EMIT addressChanged();
    Q_EMIT labelChanged();
    Q_EMIT messageChanged();
    Q_EMIT addressTypeChanged();
    Q_EMIT amountErrorChanged();
    Q_EMIT idChanged();
    Q_EMIT needsUnlockChanged();
    Q_EMIT unlockErrorChanged();
}

QString PaymentRequest::FormatAddress(const QString& address)
{
    QString formatted;
    formatted.reserve(address.length() + address.length() / 4);
    for (int i = 0; i < address.length(); ++i) {
        if (i > 0 && (i % 4) == 0) {
            formatted += QChar(' ');
        }
        formatted += address[i];
    }
    return formatted;
}
