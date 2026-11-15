// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/models/desktopintentmodel.h>
#include <qml/guipresentation.h>
#ifdef ENABLE_WALLET
#include <qml/models/bitcoinurimodel.h>
#include <qml/paymentimportcreation.h>
#endif
#include <QTimer>
#include <limits>
#include <map>
#include <mutex>
#include <utility>

DesktopIntentModel::DesktopIntentModel(execution::SnapshotServices services, bool wallet_enabled, QObject* parent)
    : QObject(parent), m_services{services.scheduler, presentation::ReliablePost(std::move(services.post))}, m_wallet_enabled(wallet_enabled)
{
    const QPointer<DesktopIntentModel> observer{this};
    const auto post = m_services.post;
    const auto timer = presentation::MakeGuiObject();
    desktopintent::Services ports; ports.scheduler = m_services.scheduler;
    ports.wake = [post, timer](auto delay, auto callback) {
        post([timer, delay, callback = std::move(callback)] { QTimer::singleShot(delay, timer.get(), callback); });
    };
    m_owner = desktopintent::Owner::Create(std::move(ports), [observer, post](auto snapshot) {
        post([observer, snapshot = std::move(snapshot)]() mutable { if (observer) observer->project(std::move(snapshot)); });
    });
}
DesktopIntentModel::~DesktopIntentModel() { closeAdmission(); }
bool DesktopIntentModel::hasInspection() const
{
#ifdef ENABLE_WALLET
    return !m_inspection.isNull();
#else
    return false;
#endif
}
QString DesktopIntentModel::error() const
{
    if (!m_owner) return tr("Desktop request services are unavailable.");
    if (!m_wallet_enabled && m_snapshot.pending) return tr("Wallet support is unavailable. The request remains unapplied.");
    switch (m_snapshot.error) {
    case desktopintent::Error::None: return {};
    case desktopintent::Error::Invalid: return tr("The desktop request is invalid.");
    case desktopintent::Error::Capacity: return tr("The desktop request queue is full.");
    case desktopintent::Error::Closed: return tr("The application is stopping.");
    case desktopintent::Error::UnknownDelivery: return tr("The inspection handoff is unresolved. Check its original receipt before continuing.");
    case desktopintent::Error::TargetUnavailable: return tr("The inspection receiver is unavailable. The request remains queued.");
    }
    return {};
}
void DesktopIntentModel::project(desktopintent::Snapshot snapshot)
{
    if (snapshot.sequence <= m_snapshot.sequence) return;
    const bool attention = snapshot.pending > m_snapshot.pending && !m_closed;
    m_snapshot = std::move(snapshot);
    const QPointer<DesktopIntentModel> self{this};
    Q_EMIT changed();
    if (self && attention) Q_EMIT self->requestAttention();
}
void DesktopIntentModel::setReady(bool ready)
{
    m_ready = ready && !m_closed;
    if (m_owner && m_readiness < std::numeric_limits<uint64_t>::max())
        m_owner->readiness(++m_readiness, m_ready && static_cast<bool>(m_sink));
}
void DesktopIntentModel::closeAdmission()
{
    if (std::exchange(m_closed, true)) return;
    m_closed_gate->store(true);
    if (m_owner) m_owner->stop();
#ifdef ENABLE_WALLET
    if (m_inspection) m_inspection->cancel();
#endif
}
void DesktopIntentModel::inspectNext() { if (canInspect() && m_owner) m_owner->review(m_sink); }
void DesktopIntentModel::dismiss() { if (canDismiss() && m_owner) m_owner->dismiss(); }
void DesktopIntentModel::reconcile() { if (canReconcile() && m_owner) m_owner->reconcile(); }

#ifdef ENABLE_WALLET
namespace {
struct Creation {
    std::shared_ptr<paymentimport::CreationReceipt> receipt{std::make_shared<paymentimport::CreationReceipt>()};
    QPointer<PaymentImportOperation> operation; // Access only on GUI.
};
struct Creations {
    std::mutex mutex;
    std::map<desktopintent::Ticket, std::shared_ptr<Creation>> records;
};
desktopintent::InspectionReceipt ReadCreation(const Creation& creation)
{
    using State = paymentimport::CreationReceipt::State;
    switch (creation.receipt->state()) {
    case State::Accepted: return {desktopintent::Knowledge::Accepted, creation.receipt->source()};
    case State::NotAccepted: return {desktopintent::Knowledge::NotAccepted, 0};
    case State::Reserved:
    case State::Creating: return {};
    }
    return {};
}
}
void DesktopIntentModel::setFactory(BitcoinUriModel* factory)
{
    // A receiver is installed once; captured tickets cannot target a replacement.
    if (m_closed || m_sink || !factory || !m_wallet_enabled) return;
    const QPointer<DesktopIntentModel> self{this}; const QPointer<BitcoinUriModel> original{factory};
    const auto records = std::make_shared<Creations>();
    const auto post = m_services.post;
    const auto closed = m_closed_gate;
    auto sink = std::make_shared<desktopintent::Sink>(); sink->identity = 1;
    sink->inspect = [self, original, records, post, closed](auto delivery, auto done) {
        std::shared_ptr<Creation> creation;
        {
            std::lock_guard lock{records->mutex};
            if (const auto found = records->records.find(delivery.ticket); found != records->records.end()) {
                creation = found->second;
            } else {
                while (records->records.size() >= desktopintent::Queue::MAX_RECEIPTS) {
                    const auto oldest = records->records.begin();
                    if (ReadCreation(*oldest->second).knowledge == desktopintent::Knowledge::Unknown)
                        return execution::Admission{{}, execution::Rejection::Capacity};
                    records->records.erase(oldest);
                }
                creation = std::make_shared<Creation>();
                records->records.emplace(delivery.ticket, creation);
            }
        }
        post([self, original, creation, delivery = std::move(delivery), done = std::move(done), closed]() mutable {
            if (closed->load() || !self || !original) creation->receipt->withdraw();
            if (creation->receipt->state() == paymentimport::CreationReceipt::State::Reserved) {
                try {
                    const paymentimport::Input input{delivery.input.kind == desktopintent::Kind::File
                        ? paymentimport::Input::Kind::File : paymentimport::Input::Kind::Text, delivery.input.value};
                    creation->operation = original->requestImport(input, {}, self, creation->receipt);
                } catch (...) { creation->receipt->notAccepted(); }
                if (self && creation->operation && !closed->load()) {
                    self->m_inspection = creation->operation;
                    Q_EMIT self->inspectionChanged();
                    if (self) Q_EMIT self->changed();
                }
            }
            done(ReadCreation(*creation));
        });
        return execution::Admission{};
    };
    sink->query = [records](auto ticket, auto done) {
        std::shared_ptr<Creation> creation;
        { std::lock_guard lock{records->mutex}; const auto found = records->records.find(ticket);
          if (found != records->records.end()) creation = found->second; }
        done(creation ? ReadCreation(*creation) : desktopintent::InspectionReceipt{});
        return execution::Admission{};
    };
    sink->retire = [records, post](auto ticket) {
        std::shared_ptr<Creation> creation;
        { std::lock_guard lock{records->mutex}; const auto found = records->records.find(ticket);
          if (found != records->records.end()) creation = found->second; }
        if (!creation) return;
        creation->receipt->withdraw();
        post([creation] { if (creation->operation) creation->operation->cancel(); });
    };
    m_sink = std::move(sink);
    setReady(m_ready);
}
PaymentImportOperation* DesktopIntentModel::takeInspection(QObject* receiver)
{
    if (m_closed || !receiver || !m_inspection) return nullptr;
    const QPointer<DesktopIntentModel> self{this};
    const QPointer<PaymentImportOperation> operation{m_inspection};
    m_inspection = nullptr;
    operation->setParent(receiver);
    if (self) { Q_EMIT self->inspectionChanged(); if (self) Q_EMIT self->changed(); }
    return operation;
}
void DesktopIntentModel::closeInspection()
{
    const QPointer<DesktopIntentModel> self{this};
    const QPointer<PaymentImportOperation> operation{m_inspection};
    m_inspection = nullptr;
    if (operation) { operation->cancel(); if (operation) operation->deleteLater(); }
    if (self) { Q_EMIT self->inspectionChanged(); if (self) Q_EMIT self->changed(); }
}
#endif
