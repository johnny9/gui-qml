// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/wallet/activitytimelinemodel.h>
#include <qml/wallet/transactionhistorymodel.h>
#include <qml/wallet/receiverequesthistorymodel.h>
#include <qml/bitcoinunits.h>
#include <QSet>
#include <algorithm>

ActivityTimelineModel::ActivityTimelineModel(quint64 session_id, TransactionHistoryModel& transactions,
                                           ReceiveRequestHistoryModel& requests, QObject* parent)
    : QAbstractListModel(parent), m_session_id{session_id}, m_transactions{transactions}, m_requests{requests}
{
    connect(&transactions, &TransactionHistoryModel::recordsChanged, this, &ActivityTimelineModel::rebuild);
    connect(&requests, &QAbstractItemModel::modelReset, this, &ActivityTimelineModel::rebuild);
    connect(&requests, &QAbstractItemModel::rowsInserted, this, &ActivityTimelineModel::rebuild);
    connect(&requests, &QAbstractItemModel::rowsRemoved, this, &ActivityTimelineModel::rebuild);
    connect(&requests, &QAbstractItemModel::dataChanged, this, &ActivityTimelineModel::rebuild);
    rebuild();
}

QHash<int, QByteArray> ActivityTimelineModel::roleNames() const
{
    return {{KeyRole, "rowKey"}, {TxidRole, "txid"}, {KindRole, "kind"}, {AddressRole, "address"},
            {LabelRole, "label"}, {AmountRole, "amountSat"}, {AmountDisplayRole, "amountDisplay"},
            {TimeRole, "timestamp"}, {StatusRole, "status"}, {PendingRole, "pendingRequest"},
            {RequestedAmountRole, "requestedAmountSat"}, {RequestIdsRole, "requestIds"}, {MessageRole, "message"}};
}

QVariant ActivityTimelineModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= count()) return {};
    const auto& row{m_rows[index.row()]};
    if (role == AmountDisplayRole) {
        if (row.value("pendingRequest").toBool()) {
            return tr("Requested %1").arg(QmlBitcoinUnits::format(QmlBitcoinUnits::fromDisplayUnit(m_display_unit), row.value("requestedAmountSat").toLongLong()));
        }
        return QmlBitcoinUnits::format(QmlBitcoinUnits::fromDisplayUnit(m_display_unit), row.value("amountSat").toLongLong(), true);
    }
    return row.value(QString::fromUtf8(roleNames().value(role)));
}

QVariantMap ActivityTimelineModel::details(const QString& key) const
{
    for (const auto& row : m_rows) if (row.value("rowKey").toString() == key) return row;
    return {};
}

void ActivityTimelineModel::setDisplayUnit(int unit)
{
    if (unit == m_display_unit) return;
    m_display_unit = unit;
    if (count()) Q_EMIT dataChanged(index(0), index(count() - 1), {AmountDisplayRole});
    Q_EMIT displayUnitChanged();
}

void ActivityTimelineModel::rebuild()
{
    QHash<QString, QStringList> request_ids;
    for (const auto& request : m_requests.entries()) {
        request_ids[QString::fromStdString(request.recipient.address)].append(QString::number(request.id));
    }
    for (auto& ids : request_ids) std::sort(ids.begin(), ids.end());
    QSet<QString> matched_addresses;
    QVector<QVariantMap> rows;
    for (const auto& transaction : m_transactions.records()) {
        QVariantMap row{m_transactions.details(transaction.key())};
        row.insert("kind", static_cast<int>(transaction.kind));
        row.insert("timestamp", transaction.time);
        row.insert("amountSat", QVariant::fromValue<qlonglong>(transaction.netAmount()));
        row.insert("pendingRequest", false);
        QStringList matches;
        if (transaction.qualifiesAsReceipt() && !transaction.address.isEmpty()) {
            matched_addresses.insert(transaction.address);
            matches = request_ids.value(transaction.address);
        }
        row.insert("requestIds", matches);
        rows.append(std::move(row));
    }
    for (const auto& request : m_requests.entries()) {
        const QString address{QString::fromStdString(request.recipient.address)};
        if (matched_addresses.contains(address)) continue;
        const QString id{QString::number(request.id)};
        rows.append({{"rowKey", QStringLiteral("%1:request:%2").arg(m_session_id).arg(id)},
                     {"txid", QString{}}, {"kind", -1}, {"timestamp", request.date.toSecsSinceEpoch()},
                     {"address", address}, {"label", QString::fromStdString(request.recipient.label)},
                     {"message", QString::fromStdString(request.recipient.message)},
                     {"requestedAmountSat", QVariant::fromValue<qlonglong>(request.recipient.amount)},
                     {"status", tr("Pending request")}, {"pendingRequest", true}, {"requestIds", QStringList{id}}});
    }
    std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) {
        const qint64 first{a.value("timestamp").toLongLong()}, second{b.value("timestamp").toLongLong()};
        return first != second ? first > second : a.value("rowKey").toString() < b.value("rowKey").toString();
    });
    beginResetModel();
    m_rows = std::move(rows);
    endResetModel();
    Q_EMIT recordsChanged();
}
