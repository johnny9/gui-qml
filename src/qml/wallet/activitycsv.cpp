// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/wallet/activitycsv.h>
#include <qml/bitcoinunits.h>
#include <qml/wallet/transactionrecord.h>
#include <QCoreApplication>
#include <QDateTime>
#include <QSaveFile>
#include <QStringList>
#include <QTimeZone>

namespace {
QString Escape(QString value, bool free_text = false)
{
    // Quoting alone does not stop spreadsheets interpreting formulas.
    if (free_text && !value.isEmpty() && QStringLiteral("=+-@\t\r\n").contains(value.front())) value.prepend(QChar{0x27});
    value.replace('"', QStringLiteral("\"\""));
    return '"' + value + '"';
}
}

QByteArray SerializeActivityCsv(const QVector<QVariantMap>& rows, int display_unit)
{
    const auto unit{QmlBitcoinUnits::fromDisplayUnit(display_unit)};
    const QString unit_label{QmlBitcoinUnits::label(unit)};
    QByteArray csv{QStringLiteral("\"Row key\",\"Type\",\"Date (UTC)\",\"Transaction ID\",\"Address\",\"Label\",\"Amount (%1)\",\"Requested amount (%1)\",\"Status\",\"Request IDs\"\n").arg(unit_label).toUtf8()};
    const auto amount = [unit](const QVariant& value) {
        return QmlBitcoinUnits::format(unit, value.toLongLong(), false, QmlBitcoinUnits::SeparatorStyle::NEVER);
    };
    for (const auto& row : rows) {
        const bool pending{row.value("pendingRequest").toBool()};
        QString type{QStringLiteral("request")};
        if (!pending) {
            switch (row.value("kind").toInt()) {
            case TransactionRecord::Incoming: type = QStringLiteral("received"); break;
            case TransactionRecord::Outgoing: type = QStringLiteral("sent"); break;
            case TransactionRecord::SelfPayment: type = QStringLiteral("self"); break;
            case TransactionRecord::Generated: type = QStringLiteral("generated"); break;
            default: type = QStringLiteral("other"); break;
            }
        }
        const QStringList values{
            Escape(row.value("rowKey").toString()), Escape(type),
            Escape(QDateTime::fromSecsSinceEpoch(row.value("timestamp").toLongLong(), QTimeZone::UTC).toString(Qt::ISODate)),
            Escape(row.value("txid").toString()), Escape(row.value("address").toString()),
            Escape(row.value("label").toString(), true),
            Escape(pending ? QString{} : amount(row.value("amountSat"))),
            Escape(pending ? amount(row.value("requestedAmountSat")) : QString{}),
            Escape(row.value("status").toString()), Escape(row.value("requestIds").toStringList().join(';'))};
        csv.append(values.join(',').toUtf8());
        csv.append('\n');
    }
    return csv;
}

bool WriteActivityCsv(const QString& path, const QVector<QVariantMap>& rows, QString& error, int display_unit)
{
    error.clear();
    if (path.isEmpty()) { error = QCoreApplication::translate("ActivityCsv", "Choose an export file."); return false; }
    const QByteArray bytes{SerializeActivityCsv(rows, display_unit)};
    QSaveFile file{path};
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        error = file.errorString();
        return false;
    }
    return true;
}
