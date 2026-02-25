// Copyright (c) 2025 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/bantableqmlmodel.h>

#include <interfaces/node.h>
#include <net_types.h>

#include <QDateTime>
#include <QLocale>

BanTableQmlModel::BanTableQmlModel(interfaces::Node& node, QObject* parent)
    : QAbstractListModel(parent), m_node(node)
{
    // refresh() is not called here because banman is not yet initialized at
    // construction time. The first refresh is triggered by
    // NodeModel::setTimeRatioListInitial once AppInitMain() completes.
}

int BanTableQmlModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid()) return 0;
    return m_ban_list.size();
}

QVariant BanTableQmlModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= m_ban_list.size()) return {};
    const CCombinedBan& entry = m_ban_list.at(index.row());
    switch (static_cast<BanRoles>(role)) {
    case BanRoles::AddressRole:
        return QString::fromStdString(entry.subnet.ToString());
    case BanRoles::BanUntilRole: {
        QDateTime dt = QDateTime::fromSecsSinceEpoch(entry.banEntry.nBanUntil);
        return QLocale::system().toString(dt, QStringLiteral("MMMM d, yyyy h:mm AP"));
    }
    } // no default case, so the compiler can warn about missing cases
    return {};
}

QHash<int, QByteArray> BanTableQmlModel::roleNames() const
{
    return {
        {static_cast<int>(BanRoles::AddressRole), "address"},
        {static_cast<int>(BanRoles::BanUntilRole), "banUntil"},
    };
}

void BanTableQmlModel::unbanAt(int row)
{
    if (row < 0 || row >= m_ban_list.size()) return;
    m_node.unban(m_ban_list.at(row).subnet);
    // refresh() is triggered by NodeModel::bannedListChanged connected in bitcoin.cpp
}

void BanTableQmlModel::refresh()
{
    beginResetModel();
    banmap_t banMap;
    m_node.getBanned(banMap);
    m_ban_list.clear();
    m_ban_list.reserve(static_cast<int>(banMap.size()));
    for (const auto& [subnet, entry] : banMap) {
        m_ban_list.append({subnet, entry});
    }
    endResetModel();
    Q_EMIT countChanged();
}
