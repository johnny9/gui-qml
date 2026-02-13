// Copyright (c) 2025 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/peertableqmlmodel.h>

PeerTableQmlModel::PeerTableQmlModel(interfaces::Node& node, QObject* parent)
    : PeerTableModel(node, parent)
{
}

void PeerTableQmlModel::startAutoRefresh()
{
    PeerTableModel::startAutoRefresh();
}

void PeerTableQmlModel::stopAutoRefresh()
{
    PeerTableModel::stopAutoRefresh();
}
