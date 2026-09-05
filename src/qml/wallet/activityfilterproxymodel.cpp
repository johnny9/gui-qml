// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/wallet/activityfilterproxymodel.h>
#include <qml/wallet/activitytimelinemodel.h>
#include <qml/wallet/activitycsv.h>
#include <qml/wallet/transactionrecord.h>
#include <QDate>
#include <QDateTime>
#include <QFileDialog>
#include <QPointer>
#include <QTimeZone>

ActivityFilterProxyModel::ActivityFilterProxyModel(ActivityTimelineModel& timeline, QObject* parent)
    : QSortFilterProxyModel(parent), m_timeline{timeline}
{
    setSourceModel(&timeline);
    setDynamicSortFilter(true);
    sort(0, Qt::DescendingOrder);
    connect(this, &QAbstractItemModel::rowsInserted, this, &ActivityFilterProxyModel::countChanged);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &ActivityFilterProxyModel::countChanged);
    connect(this, &QAbstractItemModel::modelReset, this, &ActivityFilterProxyModel::countChanged);
    connect(this, &QAbstractItemModel::layoutChanged, this, &ActivityFilterProxyModel::countChanged);
}

void ActivityFilterProxyModel::setSearchText(const QString& text)
{
    if (m_search == text) return;
    m_search = text;
    invalidateFilter();
    Q_EMIT filtersChanged();
}
void ActivityFilterProxyModel::setTypeFilter(int type)
{
    if (type < All || type > Requests || type == m_type) return;
    m_type = type;
    invalidateFilter();
    Q_EMIT filtersChanged();
}
void ActivityFilterProxyModel::setFromDate(const QString& date)
{
    if (m_from == date) return;
    m_from = date;
    invalidateFilter();
    Q_EMIT filtersChanged();
}
void ActivityFilterProxyModel::setThroughDate(const QString& date)
{
    if (m_through == date) return;
    m_through = date;
    invalidateFilter();
    Q_EMIT filtersChanged();
}

bool ActivityFilterProxyModel::filterAcceptsRow(int source_row, const QModelIndex& parent) const
{
    const auto index{m_timeline.index(source_row, 0, parent)};
    const int kind{index.data(ActivityTimelineModel::KindRole).toInt()};
    const bool pending{index.data(ActivityTimelineModel::PendingRole).toBool()};
    if (m_type == Requests && !pending) return false;
    if (m_type == Received && (pending || kind != TransactionRecord::Incoming)) return false;
    if (m_type == Sent && (pending || kind != TransactionRecord::Outgoing)) return false;
    if (m_type == Self && (pending || kind != TransactionRecord::SelfPayment)) return false;
    if (m_type == Generated && (pending || kind != TransactionRecord::Generated)) return false;
    const QDate date{QDateTime::fromSecsSinceEpoch(index.data(ActivityTimelineModel::TimeRole).toLongLong(), QTimeZone::UTC).date()};
    const QDate from{QDate::fromString(m_from, Qt::ISODate)}, through{QDate::fromString(m_through, Qt::ISODate)};
    // Invalid entered dates match nothing, never silently broaden the export.
    if ((!m_from.isEmpty() && !from.isValid()) || (!m_through.isEmpty() && !through.isValid())) return false;
    if ((from.isValid() && date < from) || (through.isValid() && date > through)) return false;
    if (m_search.isEmpty()) return true;
    for (const auto role : {ActivityTimelineModel::AddressRole, ActivityTimelineModel::LabelRole,
                            ActivityTimelineModel::TxidRole, ActivityTimelineModel::MessageRole}) {
        if (index.data(role).toString().contains(m_search, Qt::CaseInsensitive)) return true;
    }
    return false;
}

bool ActivityFilterProxyModel::lessThan(const QModelIndex& left, const QModelIndex& right) const
{
    const qint64 first{left.data(ActivityTimelineModel::TimeRole).toLongLong()};
    const qint64 second{right.data(ActivityTimelineModel::TimeRole).toLongLong()};
    // Descending date, ascending stable key on ties.
    return first != second ? first < second :
        left.data(ActivityTimelineModel::KeyRole).toString() > right.data(ActivityTimelineModel::KeyRole).toString();
}

QVector<QVariantMap> ActivityFilterProxyModel::snapshot() const
{
    QVector<QVariantMap> rows;
    rows.reserve(rowCount());
    for (int row = 0; row < rowCount(); ++row) {
        rows.append(m_timeline.rows().at(mapToSource(index(row, 0)).row()));
    }
    return rows;
}

bool ActivityFilterProxyModel::exportToPath(const QString& path)
{
    const auto rows{snapshot()}; // Never read live models during file I/O.
    const bool ok{WriteActivityCsv(path, rows, m_error, m_timeline.displayUnit())};
    Q_EMIT errorChanged();
    return ok;
}

bool ActivityFilterProxyModel::chooseExportFile()
{
    const QPointer<ActivityFilterProxyModel> self{this};
    const QString path{QFileDialog::getSaveFileName(nullptr, tr("Export activity"), QStringLiteral("activity.csv"), tr("CSV files (*.csv)"))};
    return self && !path.isEmpty() && self->exportToPath(path);
}
