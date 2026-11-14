// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/models/walletoperationmodel.h>
#include <qml/models/walletqmlmodel.h>
#include <QScopeGuard>
#include <QQmlEngine>
#include <utility>

WalletOperationText::WalletOperationText(bool warning, QObject* parent) : QAbstractListModel(parent), m_warning(warning) {}
QString WalletOperationText::text() const { return m_owner ? m_warning ? m_owner->warningText() : m_owner->errorText() : QString{}; }
int WalletOperationText::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : int((text().size() + 1023) / 1024); }
QVariant WalletOperationText::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount() || (role != Qt::DisplayRole && role != Qt::UserRole + 1)) return {};
    return text().mid(index.row() * 1024, 1024);
}
QHash<int, QByteArray> WalletOperationText::roleNames() const { return {{Qt::UserRole + 1, "content"}}; }
void WalletOperationText::bind(std::shared_ptr<walletcreation::Owner> owner) { m_owner = std::move(owner); }
void WalletOperationText::refresh() { const QPointer<WalletOperationText> guard{this}; beginResetModel(); if (guard) endResetModel(); }
WalletOperationModel* WalletOperationModel::Make(Services services, walletcreation::Request request)
{
    if (!request.id) request.id = services.native.admission->allocate();
    if (!request.transport_error.empty()) { request.submitted_name = request.submitted_name.left(1024); request.extended_public_key.clear(); }
    return new WalletOperationModel(std::move(services), std::move(request));
}
WalletOperationModel::WalletOperationModel(Services services, walletcreation::Request request)
    : QObject(nullptr), m_services(std::move(services)), m_request(std::move(request))
{
    auto native_request = m_request;
    m_request.passphrase.reset(); m_request.confirmation.reset();
    const QPointer<QObject> projection_parent{m_services.projection_parent};
    const bool needs_parent = projection_parent != nullptr;
    QQmlEngine::setObjectOwnership(this, QQmlEngine::JavaScriptOwnership);
    const auto rejected_projection = [this, projection_parent] {
        if (m_ticket) {
            m_ticket->backend_release->release();
            if (m_services.publisher) m_services.publisher->finishCreation(m_ticket);
            m_ticket.reset();
        }
        // A refused request owns no Native resource or reconciliation right.
        // Keep its small diagnostic handle, not a lease on the live registry.
        m_services.native.open_names.reset(); m_services.publisher.reset();
        m_frame.outcome_known = true; m_frame.result.outcome_known = true;
        m_frame.result.result_id = operationId() + QStringLiteral(":rejected");
        m_frame.target_id.clear();
        if (projection_parent) { setParent(projection_parent); QQmlEngine::setObjectOwnership(this, QQmlEngine::CppOwnership); }
    };
    m_frame.target_id = m_request.submitted_name;
    m_accepted = m_request.transport_error.empty() && m_request.id && m_services.native.admission->request(m_request.id);
    if (!m_accepted) {
        m_frame.phase = walletcreation::Phase::Rejected;
        m_frame.error = !m_request.transport_error.empty() ? m_request.transport_error : !m_request.id
            ? WalletOperationError{QStringLiteral("IdentityExhausted"), tr("Wallet operation identity is exhausted. Restart before creating another wallet."), {}, {}}
            : WalletOperationError{QStringLiteral("Busy"), tr("Wait for the current wallet operation to finish."), {}, {}};
        rejected_projection(); return;
    }
    if (m_services.publisher) m_ticket = m_services.publisher->reserveCreation(operationId());
    if (!m_ticket || (needs_parent && !projection_parent)) {
        m_accepted = false; m_services.native.admission->presentationConsumed(m_request.id);
        m_frame.phase = walletcreation::Phase::Rejected;
        m_frame.error = {QStringLiteral("PublicationUnavailable"), tr("Wallet publication could not be reserved. No wallet was created."), {}, {}};
        rejected_projection(); return;
    }
    const QPointer<WalletOperationModel> guard{this};
    m_owner = walletcreation::Owner::Make(m_services.native, std::move(native_request),
        [guard, post = m_services.native.execution.post] { post([guard] { if (guard) guard->publish(); }); }, m_ticket->backend_release, m_ticket->observation);
    if (!m_owner) {
        m_accepted = false; m_services.native.admission->presentationConsumed(m_request.id);
        m_frame.phase = walletcreation::Phase::Rejected;
        m_frame.error = {QStringLiteral("TransportRejected"), tr("Wallet creation could not be retained. No wallet was created."), {}, {}};
    }
    if (!m_owner) { rejected_projection(); return; }
    if (projection_parent) { setParent(projection_parent); QQmlEngine::setObjectOwnership(this, QQmlEngine::CppOwnership); }
    else if (needs_parent) { if (m_owner) m_owner->detach(); m_accepted = false; m_services.native.admission->presentationConsumed(m_request.id); }
    m_errors.bind(m_owner); m_warnings.bind(m_owner);
}
WalletOperationModel::~WalletOperationModel()
{
    if (m_owner) m_owner->detach();
    if (m_ticket && m_services.publisher) m_services.publisher->finishCreation(m_ticket);
}
void WalletOperationModel::start()
{
    if (!m_accepted || !m_owner || m_closed) return;
    const QPointer<WalletOperationModel> guard{this}; const auto owner = m_owner;
    Q_EMIT activated(this);
    if (guard && !m_closed) owner->start(); else owner->detach();
}
void WalletOperationModel::closeAdmission()
{
    if (std::exchange(m_closed, true)) return;
    if (m_owner) {
        m_frame = m_owner->snapshot();
        m_owner->detach();
        if (walletcreation::Busy(m_frame.phase)) {
            m_frame.phase = walletcreation::Phase::Uncertain; m_frame.outcome_known = false;
            m_frame.error = {QStringLiteral("ProjectionClosed"), tr("The operation view closed. Its Native outcome remains owned; check the wallet directory before retrying."), {}, {}};
        }
    }
    m_errors.bind({}); m_warnings.bind({}); m_owner.reset();
    m_ticket.reset(); m_services.publisher.reset(); m_services.native.open_names.reset();
}
WalletQmlModel* WalletOperationModel::resultWallet() const { return m_result_wallet; }
QQmlListProperty<WalletQmlModel> WalletOperationModel::resultWallets()
{
    return {this, nullptr, nullptr, [](QQmlListProperty<WalletQmlModel>*) -> qsizetype { return 0; },
        [](QQmlListProperty<WalletQmlModel>*, qsizetype) -> WalletQmlModel* { return nullptr; }, nullptr};
}
bool WalletOperationModel::canOpenResult() const
{
    return !m_closed && phase() == Succeeded && m_result_wallet && m_result_wallet->available() && m_services.publisher && m_services.publisher->containsExact(m_result_wallet);
}
bool WalletOperationModel::canReconcile() const
{
    const auto observation = m_owner ? m_owner->observationForPublication() : walletopening::NativeEvents{};
    return !m_closed && phase() == Partial && !m_reconciled && m_ticket && m_owner && m_owner->walletForPublication()
        && !m_ticket->observation->failed() && !m_services.native.admission->creationPending() && !m_services.native.admission->legacyBusy()
        && !(observation.unloaded && observation.unloaded->load());
}
QString WalletOperationModel::errorText() const { return m_owner ? m_owner->errorText() : m_frame.error.message; }
QString WalletOperationModel::warningText() const { return m_owner ? m_owner->warningText() : QString{}; }
void WalletOperationModel::publish()
{
    if (m_closed || !m_owner || m_publishing) return;
    const auto frame = m_owner->snapshot();
    if (frame.revision <= m_frame.revision) return;
    const QPointer<WalletOperationModel> guard{this};
    m_publishing = true;
    const auto restore = qScopeGuard([guard] { if (guard) guard->m_publishing = false; });
    m_frame = frame;
    if (frame.publication_requested && frame.phase == walletcreation::Phase::Publishing && !m_publication_in_flight) {
        m_publication_in_flight = true;
        auto published = m_services.publisher ? m_services.publisher->publishCreation(m_ticket, m_owner->walletForPublication(), frame.target_id, m_owner->observationForPublication()) : walletpublication::Result{};
        if (!guard) return;
        if (m_result_wallet && published.wallet && m_result_wallet != published.wallet) {
            published = {{}, walletpublication::Availability::Unavailable, tr("The original wallet result cannot be replaced by another opening.")};
        } else if (!m_result_wallet && published.wallet) {
            m_result_wallet = published.wallet;
            m_available = connect(published.wallet, &WalletQmlModel::availableChanged, this, &WalletOperationModel::changed);
            m_destroyed = connect(published.wallet, &QObject::destroyed, this, &WalletOperationModel::changed);
        }
        m_owner->publicationConsumed(published.availability, published.error);
    }
    if (terminal()) {
        m_errors.refresh(); if (!guard) return;
        m_warnings.refresh(); if (!guard) return;
        if (!m_finished) { m_finished = true; Q_EMIT finished(operationId(), m_frame.result.result_id); if (!guard) return; }
        if (m_ticket && m_services.publisher) m_services.publisher->finishCreation(m_ticket);
        if (!guard) return;
        m_services.native.admission->presentationConsumed(m_request.id);
        m_publishing = false; // Idle may admit a successor after terminal delivery.
    }
    Q_EMIT changed();
}
WalletCreationReceipt WalletOperationModel::reject(const char* command, const QString& message)
{
    WalletCreationReceipt receipt{operationId() + QLatin1Char(':') + QString::fromLatin1(command), false,
        {QStringLiteral("UnsupportedTransition"), message, {}, {}}};
    Q_EMIT commandRejected(receipt.command_id, receipt.error); return receipt;
}
WalletCreationReceipt WalletOperationModel::cancel()
{
    if (m_closed || !canCancel()) return reject("cancel", tr("This operation cannot cancel an admitted wallet mutation."));
    m_owner->cancel(); return {operationId() + QStringLiteral(":cancel"), true, {}};
}
WalletCreationReceipt WalletOperationModel::retry()
{
    return reject("retry", tr("Edit the creation form and submit a new operation. A created wallet must not be created again."));
}
WalletCreationReceipt WalletOperationModel::reconcile()
{
    if (!canReconcile() || !m_services.native.admission->request(m_request.id)) return reject("reconcile", tr("This result cannot be reconciled now."));
    m_reconciled = true; m_finished = false; m_publication_in_flight = false;
    Q_EMIT activated(this);
    const auto owner = m_owner;
    const auto admitted = execution::Submit(*m_services.native.execution.scheduler, [owner] { owner->reconcilePublication(); }, [] {}, {},
        {execution::Lane::Domain, 256, m_services.native.execution.scheduler->allocateKey()});
    if (!admitted) { m_services.native.admission->presentationConsumed(m_request.id); return reject("reconcile", tr("The reconciliation command could not be queued.")); }
    return {operationId() + QStringLiteral(":reconcile"), true, {}};
}
WalletCreationReceipt WalletOperationModel::acknowledge(const QString& result_id)
{
    if (!canAcknowledge() || result_id != m_frame.result.result_id) return reject("acknowledge", tr("This answer does not belong to the current result."));
    m_acknowledged = true; Q_EMIT changed(); return {operationId() + QStringLiteral(":acknowledge"), true, {}};
}
