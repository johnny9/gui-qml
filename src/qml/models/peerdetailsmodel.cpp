// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/peerdetailsmodel.h>

PeerDetailsModel::PeerDetailsModel(CNodeCombinedStats* nodeStats, const QModelIndex& index, PeerTableModel* parent)
: m_combinedStats{nodeStats}
, m_model{parent}
{
    m_row = index.row();
    connect(parent, &PeerTableModel::rowsRemoved, this, &PeerDetailsModel::onModelRowsRemoved);
    connect(parent, &PeerTableModel::dataChanged, this, &PeerDetailsModel::onModelDataChanged);
}

void PeerDetailsModel::onModelRowsRemoved(const QModelIndex&  parent, int first, int last)
{
    for (int row = first; row <= last; ++row) {
        QModelIndex index = m_model->index(row, 0, parent);
        int nodeIdInRow = m_model->data(index, PeerTableModel::NetNodeId).toInt();
        if (nodeIdInRow == this->nodeId()) {
            Q_EMIT disconnected();
            break;
        }
    }
}

void PeerDetailsModel::onModelDataChanged(const QModelIndex& /* top_left */, const QModelIndex& /* bottom_right */)
{
    if (m_model->data(m_model->index(m_row, 0), PeerTableModel::NetNodeId).isNull() ||
        m_model->data(m_model->index(m_row, 0), PeerTableModel::NetNodeId).toInt() != nodeId()) {
        // This peer has been removed from the model
        Q_EMIT disconnected();
        return;
    }

    m_combinedStats = m_model->data(m_model->index(m_row, 0), PeerTableModel::StatsRole).value<CNodeCombinedStats*>();

    // Only update when all information is available
    if (m_combinedStats && m_combinedStats->fNodeStateStatsAvailable) {
        Q_EMIT dataChanged();
    }
}
