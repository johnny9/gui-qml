// Copyright (c) 2023 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/peerlistsortproxy.h>
#include <qml/models/peerdetailsmodel.h>
#include <qt/peertablemodel.h>
#include <util/check.h>

PeerListSortProxy::PeerListSortProxy(QObject* parent)
    : QSortFilterProxyModel(parent)
{
}

QHash<int, QByteArray> PeerListSortProxy::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[PeerTableModel::NetNodeId] = "nodeId";
    roles[PeerTableModel::Age] = "age";
    roles[PeerTableModel::Address] = "address";
    roles[PeerTableModel::Direction] = "direction";
    roles[PeerTableModel::ConnectionType] = "connectionType";
    roles[PeerTableModel::Network] = "network";
    roles[PeerTableModel::Ping] = "ping";
    roles[PeerTableModel::Sent] = "sent";
    roles[PeerTableModel::Received] = "received";
    roles[PeerTableModel::Subversion] = "subversion";
    roles[PeerTableModel::StatsRole] = "stats";
    return roles;
}

int PeerListSortProxy::RoleNameToIndex(const QString & name) const
{
    auto role_names = roleNames();
    auto keys = role_names.keys(name.toUtf8());
    if (!keys.empty()) {
        return keys.first();
    } else {
        return PeerTableModel::NetNodeId;
    }
}

QVariant PeerListSortProxy::data(const QModelIndex& index, int role) const
{
    if (role == PeerTableModel::StatsRole) {
        auto stats = QSortFilterProxyModel::data(index, role);
        auto details = new PeerDetailsModel(stats.value<CNodeCombinedStats*>(), qobject_cast<PeerTableModel*>(sourceModel()));
        return QVariant::fromValue(details);
    } else if (role == PeerTableModel::NetNodeId) {
        return QSortFilterProxyModel::data(index, role);
    }

    QModelIndex converted_index = QSortFilterProxyModel::index(index.row(), role);
    return QSortFilterProxyModel::data(converted_index, Qt::DisplayRole);
}

QString PeerListSortProxy::sortBy() const
{
    return m_sort_by;
}

void PeerListSortProxy::setSortBy(const QString & roleName)
{
    if (m_sort_by != roleName) {
        m_sort_by = roleName;
        sort(RoleNameToIndex(roleName));
        Q_EMIT sortByChanged(roleName);
    }
}

bool PeerListSortProxy::lessThan(const QModelIndex& left_index, const QModelIndex& right_index) const
{
    const CNodeStats left_stats = Assert(sourceModel()->data(left_index, PeerTableModel::StatsRole).value<CNodeCombinedStats*>())->nodeStats;
    const CNodeStats right_stats = Assert(sourceModel()->data(right_index, PeerTableModel::StatsRole).value<CNodeCombinedStats*>())->nodeStats;

    switch (static_cast<PeerTableModel::ColumnIndex>(left_index.column())) {
    case PeerTableModel::NetNodeId:
        return left_stats.nodeid < right_stats.nodeid;
    case PeerTableModel::Age:
        return left_stats.m_connected > right_stats.m_connected;
    case PeerTableModel::Address:
        return left_stats.m_addr_name.compare(right_stats.m_addr_name) < 0;
    case PeerTableModel::Direction:
        return left_stats.fInbound > right_stats.fInbound;
    case PeerTableModel::ConnectionType:
        return left_stats.m_conn_type < right_stats.m_conn_type;
    case PeerTableModel::Network:
        return left_stats.m_network < right_stats.m_network;
    case PeerTableModel::Ping:
        return left_stats.m_min_ping_time < right_stats.m_min_ping_time;
    case PeerTableModel::Sent:
        return left_stats.nSendBytes < right_stats.nSendBytes;
    case PeerTableModel::Received:
        return left_stats.nRecvBytes < right_stats.nRecvBytes;
    case PeerTableModel::Subversion:
        return left_stats.cleanSubVer.compare(right_stats.cleanSubVer) < 0;
    }
    assert(false);
}
