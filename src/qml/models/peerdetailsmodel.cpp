// Copyright (c) 2024-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/peerdetailsmodel.h>

PeerDetailsModel::PeerDetailsModel(CNodeCombinedStats* nodeStats, QmlPeerTableModel* parent)
: m_combinedStats{nodeStats}
, m_model{parent}
, m_disconnected{false}
{
    for (int row = 0; row < m_model->rowCount(); ++row) {
        QModelIndex index = m_model->index(row, 0);
        int nodeIdInRow = m_model->data(index, QmlPeerTableModel::NetNodeId).toInt();
        if (nodeIdInRow == m_combinedStats->nodeStats.nodeid) {
            m_row = row;
            break;
        }
    }
    connect(parent, &QmlPeerTableModel::rowsRemoved, this, &PeerDetailsModel::onModelRowsRemoved);
    connect(parent, &QmlPeerTableModel::dataChanged, this, &PeerDetailsModel::onModelDataChanged);
}

void PeerDetailsModel::onModelRowsRemoved(const QModelIndex&  parent, int first, int last)
{
    for (int row = first; row <= last; ++row) {
        QModelIndex index = m_model->index(row, 0, parent);
        int nodeIdInRow = m_model->data(index, QmlPeerTableModel::NetNodeId).toInt();
        if (nodeIdInRow == this->nodeId()) {
            if (!m_disconnected) {
                m_disconnected = true;
                Q_EMIT disconnected();
            }
            break;
        }
    }
}

void PeerDetailsModel::onModelDataChanged(const QModelIndex& /* top_left */, const QModelIndex& /* bottom_right */)
{
    if (m_model->data(m_model->index(m_row, 0), QmlPeerTableModel::NetNodeId).isNull() ||
        m_model->data(m_model->index(m_row, 0), QmlPeerTableModel::NetNodeId).toInt() != nodeId()) {
        if (!m_disconnected) {
            m_disconnected = true;
            Q_EMIT disconnected();
        }
        return;
    }

    m_combinedStats = m_model->data(m_model->index(m_row, 0), QmlPeerTableModel::StatsRole).value<CNodeCombinedStats*>();

    // Only update when all information is available
    if (m_combinedStats && m_combinedStats->fNodeStateStatsAvailable) {
        Q_EMIT dataChanged();
    }
}
