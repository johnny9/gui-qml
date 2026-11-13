// Copyright (c) 2024-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/models/peerdetailsmodel.h>
#include <qml/models/peerlistmodel.h>
PeerDetailsModel::PeerDetailsModel(PeerStatsHandle stats, PeerListModel* model) : QObject(model)
{
    if (!stats || !model) return;
    m_node_id = stats->nodeStats.nodeid;
    const auto row = model->rowForKey(QString::number(m_node_id));
    if (!row) { m_disconnected = true; return; }
    m_cache = static_cast<const PeerRecord*>(row->payload.get())->presentation;
    model->watchDetails(m_node_id, this);
}
void PeerDetailsModel::update(const PeerPresentation& value, bool available) {
    if (m_disconnected) return;
    const auto original_address = m_cache.rawAddress;
    m_cache = value; m_cache.rawAddress = original_address;
    if (available) Q_EMIT dataChanged();
}
void PeerDetailsModel::markDisconnected() { if (m_disconnected) return; m_disconnected = true; Q_EMIT disconnected(); }
