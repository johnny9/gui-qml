// Copyright (c) 2023-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/peerlistsortproxy.h>
#include <qml/models/peerdetailsmodel.h>
#include <qml/models/peerlistmodel.h>
#include <util/check.h>

PeerListSortProxy::PeerListSortProxy(QObject* parent)
    : QSortFilterProxyModel(parent)
{
    m_sort_role = QmlPeerTableModel::NetNodeId;
    setSortRole(m_sort_role);
    setDynamicSortFilter(true);
}

QHash<int, QByteArray> PeerListSortProxy::roleNames() const
{
    if (sourceModel()) {
        return sourceModel()->roleNames();
    }
    return {};
}

int PeerListSortProxy::RoleNameToRole(const QString & name) const
{
    auto role_names = roleNames();
    auto keys = role_names.keys(name.toUtf8());
    if (!keys.empty()) {
        return keys.first();
    } else {
        return QmlPeerTableModel::NetNodeId;
    }
}

QVariant PeerListSortProxy::data(const QModelIndex& index, int role) const
{
    if (role == QmlPeerTableModel::StatsRole) {
        auto stats = QSortFilterProxyModel::data(index, role);
        auto details = new PeerDetailsModel(stats.value<CNodeCombinedStats*>(), qobject_cast<QmlPeerTableModel*>(sourceModel()));
        return QVariant::fromValue(details);
    }

    return QSortFilterProxyModel::data(index, role);
}

QString PeerListSortProxy::sortBy() const
{
    return m_sort_by;
}

void PeerListSortProxy::setSortBy(const QString & roleName)
{
    if (m_sort_by == roleName) return;

    m_sort_by = roleName;
    m_sort_role = RoleNameToRole(roleName);
    setSortRole(m_sort_role);
    sort(0);
    Q_EMIT sortByChanged(roleName);
}

bool PeerListSortProxy::lessThan(const QModelIndex& left_index, const QModelIndex& right_index) const
{
    const CNodeStats left_stats = Assert(sourceModel()->data(left_index, QmlPeerTableModel::StatsRole).value<CNodeCombinedStats*>())->nodeStats;
    const CNodeStats right_stats = Assert(sourceModel()->data(right_index, QmlPeerTableModel::StatsRole).value<CNodeCombinedStats*>())->nodeStats;

    switch (m_sort_role) {
    case QmlPeerTableModel::NetNodeId:
        return left_stats.nodeid < right_stats.nodeid;
    case QmlPeerTableModel::Age:
        return left_stats.m_connected > right_stats.m_connected;
    case QmlPeerTableModel::Address:
        return left_stats.m_addr_name.compare(right_stats.m_addr_name) < 0;
    case QmlPeerTableModel::Direction:
        return left_stats.fInbound > right_stats.fInbound;
    case QmlPeerTableModel::ConnectionType:
        return left_stats.m_conn_type < right_stats.m_conn_type;
    case QmlPeerTableModel::Network:
        return left_stats.m_network < right_stats.m_network;
    case QmlPeerTableModel::Ping:
        return left_stats.m_min_ping_time < right_stats.m_min_ping_time;
    case QmlPeerTableModel::Sent:
        return left_stats.nSendBytes < right_stats.nSendBytes;
    case QmlPeerTableModel::Received:
        return left_stats.nRecvBytes < right_stats.nRecvBytes;
    case QmlPeerTableModel::Subversion:
        return left_stats.cleanSubVer.compare(right_stats.cleanSubVer) < 0;
    }
    return false;
}
