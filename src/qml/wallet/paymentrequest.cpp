// Copyright (c) 2024-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/wallet/paymentrequest.h>
#include <qml/wallet/receiverequesthistorymodel.h>
#include <key_io.h>
#include <outputtype.h>
#include <util/moneystr.h>

void PaymentRequest::setLabel(const QString& value) { if (label() != value) { m_entry.recipient.label = value.toStdString(); Q_EMIT changed(); } }
void PaymentRequest::setMessage(const QString& value) { if (message() != value) { m_entry.recipient.message = value.toStdString(); Q_EMIT changed(); } }
void PaymentRequest::setNoteSelf(const QString& value) { if (noteSelf() != value) { m_entry.recipient.noteSelf = value.toStdString(); Q_EMIT changed(); } }
void PaymentRequest::setAmount(const QString& value) { if (m_amount != value) { m_amount = value; Q_EMIT changed(); } }
void PaymentRequest::setAddressType(const QString& value) { if (m_address_type != value) { m_address_type = value; Q_EMIT changed(); } }

std::optional<QmlRecentRequestEntry> PaymentRequest::validatedEntry() const
{
    auto entry{m_entry};
    const auto parsed{m_amount.trimmed().isEmpty() ? std::optional<CAmount>{0} : ParseMoney(m_amount.trimmed().toStdString())};
    if (!parsed || !MoneyRange(*parsed) || entry.recipient.label.size() > 1024 ||
        entry.recipient.message.size() > 4096 || entry.recipient.noteSelf.size() > 4096) return {};
    entry.recipient.amount = *parsed;
    return entry;
}

QString PaymentRequest::uri() const
{
    const auto entry{validatedEntry()};
    return entry ? ReceiveRequestHistoryModel::BuildBitcoinUri(address(), entry->recipient.amount, label(), message()) : QString{};
}

void PaymentRequest::setEntry(const QmlRecentRequestEntry& entry)
{
    m_entry = entry;
    m_amount = entry.recipient.amount > 0 ? QString::fromStdString(FormatMoney(entry.recipient.amount)) : QString{};
    if (!entry.recipient.address.empty()) {
        const auto type{OutputTypeFromDestination(DecodeDestination(entry.recipient.address))};
        if (type) m_address_type = QString::fromStdString(FormatOutputType(*type));
    }
    Q_EMIT changed();
}

QString PaymentRequest::qrImageSource() const
{
    const QString payload{uri()};
    return payload.isEmpty() || payload.toUtf8().size() > 2953 ? QString{} : QStringLiteral("image://walletqr/") +
        QString::fromLatin1(payload.toUtf8().toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

void PaymentRequest::useAsTemplate(const QmlRecentRequestEntry& entry, bool reuse_address)
{
    setEntry(entry);
    auto draft{entry};
    draft.id = 0;
    draft.date = {};
    if (!reuse_address) draft.recipient.address.clear();
    m_entry = draft;
    Q_EMIT changed();
}

void PaymentRequest::clear()
{
    m_address_type.clear();
    setEntry({});
}
