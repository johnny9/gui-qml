// Copyright (c) 2025-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/models/sendrecipientslistmodel.h>
#include <qml/guipresentation.h>
#include <qml/models/walletqmlmodel.h>
#include <qml/retainedvalue.h>
#include <QPointer>
#include <QScopeGuard>
#include <array>
#include <limits>
#include <mutex>
#include <set>
#include <stdexcept>
#include <utility>

struct SendRecipientsListModel::Backend {
    struct Delta { quint64 epoch; std::optional<SendRecipient::Snapshot> recipient; };
    std::mutex mutex;
    std::map<quint64, Delta> pending;
    quint64 requested_epoch{0}, requested_revision{0};
    quint64 epoch{0};
    std::map<quint64, SendRecipient::Snapshot> recipients;
};
struct SendRecipientsListModel::Retired {
    std::shared_ptr<QObject> owner;
    std::function<void(std::function<void()>)> post;
    std::deque<std::shared_ptr<Rows>> rows;
    bool pumping{false};
    std::shared_ptr<Backend> backend;
    std::shared_ptr<const DraftSnapshot> snapshot;
    std::shared_ptr<execution::SnapshotQuery<std::shared_ptr<const DraftSnapshot>>> query;
};
SendRecipientsListModel::SendRecipientsListModel(QObject* parent) : QAbstractListModel(parent), m_wallet(qobject_cast<WalletQmlModel*>(parent))
{
    m_recipient_owner = presentation::MakeGuiObject();
    m_retirement_post = presentation::QueuedPost();
    m_recipients->append(createRecipient());
}
SendRecipientsListModel::~SendRecipientsListModel() { closeAdmission(); retireValues(); }
SendRecipient* SendRecipientsListModel::createRecipient()
{
    auto* recipient = new SendRecipient(m_wallet, m_recipient_owner.get());
    recipient->setOwnerAdmissionGate(m_owner_closed);
    connectRecipientSignals(recipient);
    if (m_services.scheduler) recipient->setOpening(m_opening, m_services);
    return recipient;
}
void SendRecipientsListModel::setOpening(std::shared_ptr<walletopening::Session> opening, execution::SnapshotServices services)
{
    if (m_closed || m_query || (m_opening && m_opening != opening)) return;
    m_opening = std::move(opening); m_services = std::move(services);
    const auto setup = ++m_setup_revision;
    if (!m_services.scheduler && m_opening) m_services.scheduler = m_opening->scheduler();
    const QPointer<SendRecipientsListModel> observer{this};
    if (!m_services.post) m_services.post = presentation::QueuedPost();
    m_retirement_post = presentation::ReliablePost(m_services.post);
    const auto original_opening = m_opening;
    const auto ports = m_services;
    const auto source = m_recipients;
    const auto epoch = m_epoch;
    const auto current = [observer, setup, original_opening, ports, source, epoch] {
        return observer && !observer->m_closed && observer->m_setup_revision == setup && observer->m_epoch == epoch &&
            observer->m_opening == original_opening && observer->m_services.scheduler == ports.scheduler && observer->m_recipients == source;
    };
    if (!ports.scheduler) return;
    auto retired = execution::RetainValues<Retired>(ports.scheduler, [] { return std::make_unique<Retired>(); });
    if (!current()) return;
    if (!retired) { m_error = tr("Recipient cleanup is unavailable."); Q_EMIT validationChanged(); return; }
    auto backend = execution::RetainValues<Backend>(ports.scheduler, [] { return std::make_unique<Backend>(); });
    if (!current()) return;
    if (!backend) { m_error = tr("Recipient services are unavailable."); Q_EMIT validationChanged(); return; }
    auto rows = execution::RetainValues<Rows>(ports.scheduler, [source, current] {
        return current() ? std::make_unique<Rows>(std::move(*source)) : nullptr;
    });
    if (!current()) return;
    if (!rows) { m_error = tr("Recipient services are unavailable."); Q_EMIT validationChanged(); return; }
    m_retired = std::move(retired); m_backend = backend; m_recipients = std::move(rows);
    m_query = std::make_shared<execution::SnapshotQuery<std::shared_ptr<const DraftSnapshot>>>(ports.scheduler,
        [backend, scheduler = ports.scheduler] {
            std::map<quint64, Backend::Delta> deltas; quint64 epoch, revision;
            { std::lock_guard lock{backend->mutex}; deltas.swap(backend->pending); epoch = backend->requested_epoch; revision = backend->requested_revision; }
            if (backend->epoch != epoch) { backend->recipients.clear(); backend->epoch = epoch; }
            for (auto& [token, delta] : deltas) {
                if (delta.epoch != epoch) continue;
                if (delta.recipient) backend->recipients.insert_or_assign(token, std::move(*delta.recipient));
                else backend->recipients.erase(token);
            }
            auto snapshot = execution::RetainValues<DraftSnapshot>(scheduler, [&] {
                auto value = std::make_unique<DraftSnapshot>(); value->revision = revision; value->all_valid = !backend->recipients.empty();
                std::set<CTxDestination> destinations;
                for (const auto& [token, recipient] : backend->recipients) {
                    if (value->amount_total_valid) {
                        if ((recipient.amount > 0 && value->total > std::numeric_limits<CAmount>::max() - recipient.amount) ||
                            (recipient.amount < 0 && value->total < std::numeric_limits<CAmount>::min() - recipient.amount)) value->amount_total_valid = false;
                        else value->total += recipient.amount;
                    }
                    value->positions.emplace(token, value->recipients.size()); value->recipients.push_back(recipient);
                    if (recipient.validation_pending) ++value->pending_count;
                    if (recipient.address_valid && !destinations.insert(recipient.destination).second) value->duplicate_destinations = true;
                    if (!recipient.valid) {
                        ++value->invalid_count; value->all_valid = false;
                        if (value->error.isEmpty()) value->error = !recipient.input_acceptable ? recipient.input_error : backend->recipients.size() > 1 ? tr("Complete every recipient before continuing.") : QString{};
                    }
                }
                value->amount_total_valid = value->amount_total_valid && MoneyRange(value->total);
                if (value->duplicate_destinations) {
                    value->all_valid = false;
                    if (value->invalid_count == 0) value->error = tr("Recipient addresses must be unique.");
                }
                if (!value->amount_total_valid) { value->all_valid = false; value->error = tr("Total amount is outside the valid monetary range."); }
                if (value->recipients.empty()) value->error = tr("Enter at least one valid recipient to continue.");
                value->total_text = value->amount_total_valid ? BitcoinAmount::satsToBtcString(value->total) : QStringLiteral("—"); return value;
            });
            if (!snapshot) throw std::runtime_error{"Recipient snapshot cleanup unavailable"};
            return std::shared_ptr<const DraftSnapshot>{std::move(snapshot)};
        }, [post = ports.post, observer, setup] { post([observer, setup] { if (observer && observer->m_setup_revision == setup) observer->publishDraft(); }); }, execution::Lane::Domain);
    m_initializing = true; initializeRecipients(0);
}
void SendRecipientsListModel::initializeRecipients(int row)
{
    if (m_closed || !m_query) return;
    const QPointer<SendRecipientsListModel> guard{this};
    const auto rows = m_recipients;
    [[maybe_unused]] const auto owner = m_recipient_owner;
    const auto opening = m_opening;
    const auto services = m_services;
    const auto setup = m_setup_revision, epoch = m_epoch;
    const auto current = [guard, rows, setup, epoch] {
        return guard && !guard->m_closed && guard->m_setup_revision == setup &&
            guard->m_epoch == epoch && guard->m_recipients == rows;
    };
    const int end = std::min(row + 32, rows->size());
    for (; row < end; ++row) {
        auto* recipient = (*rows)[row]; recipient->setOpening(opening, services);
        if (!current()) return;
        submitRecipient(recipient); if (!current()) return;
    }
    m_initialize_row = row;
    if (row < count()) {
        try { services.post([current, guard, row] { if (current()) guard->initializeRecipients(row); }); }
        catch (...) { if (current()) m_error = tr("Recipient initialization is waiting for GUI delivery."); }
        return;
    }
    m_initializing = false; Q_EMIT validationChanged();
}
void SendRecipientsListModel::closeAdmission()
{
    if (m_closed) return;
    m_owner_closed->store(true); // Fences every child/editor before any GUI handoff.
    m_closed = true; ++m_unit_generation; ++m_clear_generation;
    if (m_query) m_query->close();
    if (!m_changing_rows) retireValues();
}
void SendRecipientsListModel::retireValues()
{
    if (!m_recipient_owner) return;
    auto retired = std::move(m_retired);
    if (!retired) retired = std::make_shared<Retired>(); // No Native services were installed.
    retired->owner = std::move(m_recipient_owner); retired->post = m_retirement_post;
    retired->rows.push_back(std::move(m_recipients));
    m_recipients = std::move(m_empty_rows);
    retired->backend = std::move(m_backend); retired->snapshot = std::move(m_snapshot); retired->query = std::move(m_query);
    if (!std::exchange(retired->pumping, true)) retired->post([retired] { retireClosedRows(retired); });
}
void SendRecipientsListModel::retireClosedRows(std::shared_ptr<Retired> retired)
{
    int budget{32};
    while (budget > 0 && !retired->rows.empty()) {
        const auto rows = retired->rows.front();
        if (rows->empty()) { retired->rows.pop_front(); --budget; continue; }
        auto* recipient = rows->takeAt(rows->size() - 1);
        recipient->closeAdmission(); delete recipient; --budget;
    }
    if (!retired->rows.empty()) retired->post([retired] { retireClosedRows(retired); });
    else retired->pumping = false;
}
void SendRecipientsListModel::retryPublications()
{
    if (m_closed) return;
    const QPointer<SendRecipientsListModel> guard{this};
    if (m_initializing) { initializeRecipients(m_initialize_row); return; }
    if (m_clearing) { trimToFront(m_clear_generation); return; }
    if (m_units_pending) { synchronizeUnits(m_pending_unit, m_unit_row, m_unit_generation); return; }
    if (!m_retrying) { m_retrying = true; retryRecipients(0); if (!guard || m_closed) return; }
    publishDraft(); if (!guard || m_closed) return;
    if (m_query && !m_pending && (m_failed || !m_snapshot || m_snapshot->revision != m_revision)) {
        m_pending = true; m_query->request(); Q_EMIT validationChanged();
    }
}
void SendRecipientsListModel::retryRecipients(int row)
{
    if (m_closed) return;
    const QPointer<SendRecipientsListModel> guard{this};
    const int end = std::min(row + 32, count());
    for (; row < end && row < count(); ++row) { (*m_recipients)[row]->retryPublications(); if (!guard || m_closed) return; }
    if (row < count()) {
        try { m_services.post([guard, row] { if (guard) guard->retryRecipients(row); }); }
        catch (...) { m_retrying = false; }
        return;
    }
    m_retrying = false;
}
std::shared_ptr<const SendRecipientsListModel::DraftSnapshot> SendRecipientsListModel::draftSnapshot() const
{
    return !m_closed && !m_failed && !validationPending() && m_snapshot && m_snapshot->revision == m_revision ? m_snapshot : nullptr;
}
void SendRecipientsListModel::submitRecipient(SendRecipient* recipient)
{
    if (m_closed || !m_query || recipientRow(recipient) < 0) return;
    auto value = recipient->draft(); const auto revision = ++m_revision;
    const auto token = value.token;
    const auto recipient_revision = value.revision;
    { std::lock_guard lock{m_backend->mutex}; m_backend->pending.insert_or_assign(token, Backend::Delta{m_epoch, std::move(value)});
      m_backend->requested_epoch = m_epoch; m_backend->requested_revision = revision; }
    const QPointer<SendRecipientsListModel> guard{this};
    const auto query = m_query;
    const auto setup = m_setup_revision, epoch = m_epoch;
    m_pending = true; query->request();
    if (!guard || m_closed || m_revision != revision || m_setup_revision != setup || m_epoch != epoch) return;
    Q_EMIT recipientDraftChanged(token, recipient_revision);
    if (guard && !m_closed) Q_EMIT validationChanged();
}
void SendRecipientsListModel::removeDraft(quint64 token)
{
    if (!m_query || m_closed) return;
    const auto revision = ++m_revision;
    { std::lock_guard lock{m_backend->mutex}; m_backend->pending.insert_or_assign(token, Backend::Delta{m_epoch, {}});
      m_backend->requested_epoch = m_epoch; m_backend->requested_revision = revision; }
    m_pending = true; m_query->request();
}
void SendRecipientsListModel::publishDraft()
{
    if (m_closed || !m_query) return;
    const auto result = m_query->take(); if (!result) return;
    const auto snapshot = result->snapshot ? *result->snapshot : std::shared_ptr<const DraftSnapshot>{};
    if (snapshot && snapshot->revision != m_revision) return;
    if (!m_pending && snapshot && m_snapshot && snapshot->revision == m_snapshot->revision) return;
    m_pending = false;
    if (!snapshot) { m_failed = true; m_error = tr("Recipient validation could not be completed."); Q_EMIT validationChanged(); return; }
    m_failed = false; m_snapshot = snapshot; m_error = snapshot->error; m_totalAmount = snapshot->total; m_total_text = snapshot->total_text;
    const QPointer<SendRecipientsListModel> guard{this};
    Q_EMIT totalAmountChanged(); if (guard) Q_EMIT validationChanged();
}
int SendRecipientsListModel::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : count(); }
QVariant SendRecipientsListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.model() != this || index.column() != 0 || index.row() < 0 || index.row() >= count()) return {};
    const auto* recipient = (*m_recipients)[index.row()];
    switch (role) {
    case RecipientRole: return QVariant::fromValue(const_cast<SendRecipient*>(recipient));
    case AddressRole: return recipient->address()->ellipsesAddress();
    case LabelRole: return recipient->label();
    case AmountRole: return recipient->amount()->toDisplay();
    case MessageRole: return recipient->message();
    case FormattedAddressRole: return recipient->address()->formattedAddress();
    case AmountUnitLabelRole: return recipient->amount()->unitLabel();
    default: return {};
    }
}
QHash<int, QByteArray> SendRecipientsListModel::roleNames() const
{
    return {{RecipientRole,"recipientObject"},{AddressRole,"address"},{LabelRole,"label"},{AmountRole,"amount"},
        {MessageRole,"message"},{FormattedAddressRole,"formattedAddress"},{AmountUnitLabelRole,"amountUnitLabel"}};
}
QList<SendRecipient*> SendRecipientsListModel::recipients() const
{
    QList<SendRecipient*> list; list.reserve(count()); for (int row = 0; row < count(); ++row) list.push_back((*m_recipients)[row]); return list;
}
int SendRecipientsListModel::recipientRow(const SendRecipient* recipient) const { return m_recipients->indexOf(const_cast<SendRecipient*>(recipient)); }
SendRecipient* SendRecipientsListModel::currentRecipient() const { return m_current < 0 || m_current >= count() ? nullptr : (*m_recipients)[m_current]; }
void SendRecipientsListModel::connectRecipientSignals(SendRecipient* recipient)
{
    const auto changed = [this, recipient](const QList<int>& roles) {
        const int row = recipientRow(recipient); if (m_closed || row < 0) return;
        const auto model_index = index(row); Q_EMIT dataChanged(model_index, model_index, roles);
    };
    connect(recipient, &SendRecipient::labelChanged, this, [changed] { changed({LabelRole}); });
    connect(recipient, &SendRecipient::messageChanged, this, [changed] { changed({MessageRole}); });
    connect(recipient->address(), &BitcoinAddress::formattedAddressChanged, this, [changed] { changed({AddressRole, FormattedAddressRole}); });
    connect(recipient->amount(), &BitcoinAmount::amountChanged, this, [changed] { changed({AmountRole}); });
    connect(recipient->amount(), &BitcoinAmount::unitChanged, this, [changed] { changed({AmountRole, AmountUnitLabelRole}); });
    connect(recipient->amount(), &BitcoinAmount::unitChanged, this, [this, recipient] {
        if (!m_syncing_units && !m_closed) synchronizeUnits(recipient->amount()->unit(), 0, ++m_unit_generation);
    });
    connect(recipient, &SendRecipient::draftChanged, this, [this, recipient] { submitRecipient(recipient); });
}
void SendRecipientsListModel::synchronizeUnits(BitcoinAmount::Unit unit, int row, quint64 generation)
{
    if (m_closed || generation != m_unit_generation) return;
    const QPointer<SendRecipientsListModel> guard{this}; m_syncing_units = true; m_units_pending = true; m_pending_unit = unit;
    const int end = std::min(row + 32, count());
    for (; row < end; ++row) { (*m_recipients)[row]->amount()->setUnit(unit); if (!guard) return; }
    m_syncing_units = false; m_unit_row = row;
    if (row < count() && m_services.post) {
        try { m_services.post([guard, unit, row, generation] { if (guard) guard->synchronizeUnits(unit, row, generation); }); }
        catch (...) { m_error = tr("Recipient unit presentation is waiting for GUI delivery."); }
    } else {
        m_units_pending = false;
    }
    Q_EMIT validationChanged();
}
void SendRecipientsListModel::add()
{
    if (m_closed || m_clearing || m_changing_rows) return;
    const QPointer<SendRecipientsListModel> guard{this};
    [[maybe_unused]] const auto owner = m_recipient_owner;
    const auto source = m_recipients;
    const auto setup = m_setup_revision, epoch = m_epoch, revision = m_revision;
    const auto* previous = currentRecipient();
    const auto unit = previous ? previous->amount()->unit() : BitcoinAmount::Unit::BTC;
    const auto current = [guard, source, setup, epoch, revision] {
        return guard && !guard->m_closed && !guard->m_clearing && !guard->m_changing_rows &&
            guard->m_recipients == source && guard->m_setup_revision == setup &&
            guard->m_epoch == epoch && guard->m_revision == revision;
    };
    const QPointer<SendRecipient> prepared{createRecipient()};
    bool adopted{false};
    const auto discard_unused = qScopeGuard([&] { if (!adopted && prepared) { prepared->closeAdmission(); prepared->deleteLater(); } });
    if (!current() || !prepared) return;
    prepared->amount()->setUnit(unit);
    if (!current() || !prepared) return;
    auto* recipient = prepared.data();
    const int row = count(); const auto rows = m_recipients;
    m_pending = true; ++m_revision; m_changing_rows = true;
    beginInsertRows({}, row, row); if (!guard) return;
    rows->append(recipient); adopted = true; endInsertRows(); if (!guard) return;
    m_changing_rows = false; if (m_closed) { retireValues(); return; }
    submitRecipient(recipient); if (!guard || m_closed) return;
    Q_EMIT countChanged(); if (!guard || m_closed) return;
    setCurrentIndex(row);
}
void SendRecipientsListModel::setCurrentIndex(int row)
{
    if (m_closed || row < 0 || row >= count() || row == m_current) return;
    m_current = row; const QPointer<SendRecipientsListModel> guard{this};
    Q_EMIT currentIndexChanged(); if (guard) Q_EMIT currentRecipientChanged();
}
void SendRecipientsListModel::next() { setCurrentIndex(m_current + 1); }
void SendRecipientsListModel::prev() { setCurrentIndex(m_current - 1); }
void SendRecipientsListModel::remove() { removeAt(m_current); }
void SendRecipientsListModel::removeAt(int row)
{
    if (m_closed || m_clearing || m_changing_rows || count() <= 1 || row < 0 || row >= count()) return;
    auto* previous = currentRecipient(); const QPointer<SendRecipientsListModel> guard{this};
    const auto rows = m_recipients; m_pending = true; ++m_revision; m_changing_rows = true;
    beginRemoveRows({}, row, row); if (!guard) return;
    auto* removed = rows->takeAt(row); const auto token = removed->identity();
    removed->closeAdmission(); removed->setParent(nullptr);
    const QPointer<SendRecipient> removed_guard{removed};
    const auto deferred_delete = qScopeGuard([removed_guard] { if (removed_guard) removed_guard->deleteLater(); });
    if (row < m_current || m_current >= count()) --m_current;
    endRemoveRows(); if (!guard) return;
    m_changing_rows = false; if (m_closed) { retireValues(); return; }
    removeDraft(token);
    Q_EMIT currentIndexChanged(); if (!guard || m_closed) return;
    if (previous != currentRecipient()) { Q_EMIT currentRecipientChanged(); if (!guard || m_closed) return; }
    Q_EMIT countChanged(); if (guard && !m_closed) Q_EMIT validationChanged();
}
void SendRecipientsListModel::retireRecipients(std::shared_ptr<Rows> rows)
{
    if (!m_retired) m_retired = std::make_shared<Retired>();
    m_retired->owner = m_recipient_owner; m_retired->post = m_retirement_post;
    m_retired->rows.push_back(std::move(rows));
    const auto retired = m_retired;
    if (!std::exchange(retired->pumping, true)) retired->post([retired] { retireClosedRows(retired); });
}
void SendRecipientsListModel::clear()
{
    if (m_closed || m_changing_rows) return;
    const QPointer<SendRecipientsListModel> guard{this};
    const auto scheduler = m_services.scheduler;
    const auto opening = m_opening;
    const auto source = m_recipients;
    const auto owner = m_recipient_owner;
    const auto setup = m_setup_revision, epoch = m_epoch, revision = m_revision, clear_generation = m_clear_generation;
    const auto current = [guard, scheduler, opening, source, owner, setup, epoch, revision, clear_generation] {
        return guard && !guard->m_closed && !guard->m_changing_rows && guard->m_setup_revision == setup &&
            guard->m_epoch == epoch && guard->m_revision == revision && guard->m_clear_generation == clear_generation &&
            guard->m_opening == opening && guard->m_services.scheduler == scheduler &&
            guard->m_recipients == source && guard->m_recipient_owner == owner;
    };
    auto replacement = scheduler ? execution::RetainValues<Rows>(scheduler, [] { return std::make_unique<Rows>(); }) : std::make_shared<Rows>();
    if (!current()) return;
    if (!replacement) { m_error = tr("Recipient reset could not be queued."); Q_EMIT validationChanged(); return; }
    const QPointer<SendRecipient> prepared{createRecipient()};
    bool adopted{false};
    const auto discard_unused = qScopeGuard([&] {
        if (!adopted && prepared) { prepared->closeAdmission(); prepared->deleteLater(); }
    });
    if (!current() || !prepared) return;
    ++m_epoch; ++m_clear_generation; m_clearing = false; m_initializing = false; m_initialize_row = 0;
    m_pending = true; ++m_revision;
    auto old = m_recipients; m_changing_rows = true;
    beginResetModel(); if (!guard) return;
    if (!m_closed) {
        m_recipients = std::move(replacement); m_current = 0;
        m_recipients->append(prepared.data()); adopted = true; retireRecipients(std::move(old));
    }
    endResetModel(); if (!guard) return;
    m_changing_rows = false; if (m_closed) { retireValues(); return; }
    submitRecipient(currentRecipient()); if (!guard || m_closed) return;
    Q_EMIT countChanged(); if (!guard || m_closed) return;
    Q_EMIT currentRecipientChanged(); if (!guard || m_closed) return;
    Q_EMIT currentIndexChanged(); if (!guard || m_closed) return;
    Q_EMIT validationChanged(); if (!guard || m_closed) return;
    if (m_wallet) m_wallet->clearSelectedCoins();
    if (guard && !m_closed) Q_EMIT listCleared();
}
void SendRecipientsListModel::clearToFront()
{
    if (m_closed || m_changing_rows) return;
    const QPointer<SendRecipientsListModel> guard{this};
    m_clearing = count() > 1;
    if (m_current != 0) { m_current = 0; Q_EMIT currentRecipientChanged(); if (!guard) return; Q_EMIT currentIndexChanged(); if (!guard) return; }
    m_clearing = count() > 1; trimToFront(++m_clear_generation);
}
void SendRecipientsListModel::trimToFront(quint64 generation)
{
    if (m_closed || generation != m_clear_generation) return;
    const QPointer<SendRecipientsListModel> guard{this};
    if (count() > 1) {
        const int first = std::max(1, count() - 32), last = count() - 1;
        const auto rows = m_recipients;
        std::array<QPointer<SendRecipient>, 32> detached;
        size_t detached_count{0};
        const auto deferred_delete = qScopeGuard([&] {
            for (size_t i{0}; i < detached_count; ++i) if (detached[i]) detached[i]->deleteLater();
        });
        m_changing_rows = true; beginRemoveRows({}, first, last); if (!guard) return;
        while (rows->size() > first) {
            auto* removed = rows->takeAt(rows->size() - 1); const auto token = removed->identity();
            removed->closeAdmission(); removed->setParent(nullptr);
            detached[detached_count++] = removed; removeDraft(token);
        }
        endRemoveRows(); if (!guard) return;
        m_changing_rows = false; if (m_closed) { retireValues(); return; }
        Q_EMIT countChanged(); if (!guard || m_closed) return;
    }
    if (count() > 1 && m_services.post) {
        try { m_services.post([guard, generation] { if (guard) guard->trimToFront(generation); }); }
        catch (...) { m_error = tr("Recipient removal is waiting for GUI delivery."); Q_EMIT validationChanged(); }
        return;
    }
    m_clearing = false; Q_EMIT validationChanged();
}
