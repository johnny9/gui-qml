// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/models/networkmodel.h>
#include <qml/guipresentation.h>

#include <QPointer>

#include <limits>
#include <utility>

NetworkModel::NetworkModel(Ports ports, QObject* parent) : QObject(parent)
{
    const auto post = presentation::ReliablePost(std::move(ports.execution.post));
    ports.execution.post = post;
    const QPointer<NetworkModel> observer{this};
    m_service = std::make_unique<networkcommand::Service>(std::move(ports.execution),
        networkcommand::Ports{std::move(ports.observe), std::move(ports.set_active), std::move(ports.generation)},
        [post, observer] { post([observer] { if (observer) observer->publish(); }); },
        [post, observer] { post([observer] { if (observer) observer->finishDrain(); }); });
}
NetworkModel::~NetworkModel() { close(); }
QString NetworkModel::error() const
{
    using Error = networkcommand::Error;
    switch (m_error) {
    case Error::None: return {};
    case Error::Unavailable: return tr("Network controls are not ready. Wait for a current node observation.");
    case Error::Admission: return tr("The network change could not be started. Retry explicitly when the node is ready.");
    case Error::Unknown: return tr("The network action has no confirmed outcome. The action was not repeated.");
    case Error::Mismatch: return tr("The requested network state was not observed.");
    case Error::Closed: return tr("Network controls are closed.");
    }
    return {};
}
bool NetworkModel::requestActive(bool active)
{
    if (m_closed || m_request_id == std::numeric_limits<uint64_t>::max()) return false;
    ++m_request_id;
    m_ingress_pending = true; m_outcome = Pending; m_error = networkcommand::Error::None;
    const bool accepted = ready() && m_service->request({m_request_id, active, m_projection.generation, m_availability});
    if (!accepted) {
        m_ingress_pending = false; m_outcome = Rejected;
        m_error = ready() ? networkcommand::Error::Admission : networkcommand::Error::Unavailable;
    }
    Q_EMIT changed();
    return accepted;
}
void NetworkModel::observe(bool active, uint64_t generation)
{
    if (!m_closed) m_service->observe({active, generation});
}
void NetworkModel::setAvailable(bool available)
{
    if (m_closed || m_availability == std::numeric_limits<uint64_t>::max()) return;
    m_available = available;
    ++m_availability;
    if (!m_service->setAvailable(available, m_availability)) m_error = networkcommand::Error::Admission;
    Q_EMIT changed();
}
void NetworkModel::publish()
{
    if (m_drained) return;
    const auto value = m_service->projection();
    m_projection = value;
    if (value.acknowledged >= m_request_id) {
        m_ingress_pending = false; m_outcome = static_cast<Outcome>(value.outcome); m_error = value.error;
    }
    Q_EMIT changed();
}
void NetworkModel::close()
{
    if (std::exchange(m_closed, true)) return;
    m_available = false;
    m_service->close();
    Q_EMIT changed();
}
void NetworkModel::finishDrain()
{
    if (m_drained) return;
    m_projection = m_service->projection();
    m_drained = true; m_ingress_pending = false; m_projection.pending = false;
    m_outcome = Closed; m_error = networkcommand::Error::Closed;
    const QPointer<NetworkModel> observer{this};
    Q_EMIT changed();
    if (observer) Q_EMIT shutdownFinished();
}
