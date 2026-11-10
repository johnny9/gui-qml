// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/walletlifecycle.h>

#include <algorithm>
#include <iterator>
#include <set>
#include <stdexcept>

namespace walletlife {
namespace {
constexpr size_t TRANSPORT_BYTES{sizeof(void*)};
}

LoadSubscription::LoadSubscription(std::shared_ptr<execution::Scheduler> scheduler, Register registration, Capture capture, Notify notify, CoherentResync resync)
    : m_scheduler(std::move(scheduler)), m_registration(std::move(registration)), m_capture(std::move(capture)), m_notify(std::move(notify)), m_coherent_resync(std::move(resync)), m_key(m_scheduler ? m_scheduler->allocateKey() : execution::ResourceKey{})
{
    if (!m_scheduler || !m_registration || !m_capture || !m_notify) throw std::invalid_argument("Missing wallet lifecycle port");
    m_pending.reserve(PENDING_HANDLES);
}

std::vector<LoadSubscription::Entry> LoadSubscription::captureEntries(std::vector<std::unique_ptr<interfaces::Wallet>> wallets)
{
    std::vector<Entry> entries;
    entries.reserve(wallets.size());
    for (auto& wallet : wallets) {
        if (!wallet) throw std::runtime_error("Invalid wallet snapshot");
        entries.push_back({wallet->getWalletName(), std::move(wallet)});
    }
    return entries;
}

execution::Admission LoadSubscription::start()
{
    {
        std::lock_guard lock{m_mutex};
        if (m_stopping) return {{}, execution::Rejection::Closed};
        if (m_started) return {{}, execution::Rejection::Invalid};
        m_started = true;
    }
    const auto self = shared_from_this();
    const auto cleanup = execution::Submit(*m_scheduler, [self] { return self->cleanup(); }, [self](bool cleanup_failed) {
        auto reason = self->m_failure.load();
        if (self->m_failed) {
            if (reason == FailureReason::None) reason = FailureReason::Backend;
            if (const auto publication = self->m_state.apply(Event::RegistrationFailed)) self->m_notify({*publication, false, reason});
        }
        self->m_state.apply(Event::Close);
        if (const auto publication = self->m_state.apply(Event::CleanupFinished)) self->m_notify({*publication, cleanup_failed, reason});
    }, {}, {execution::Lane::Cleanup, TRANSPORT_BYTES, m_key}, true);
    if (!cleanup) {
        m_stopping = true;
        return cleanup;
    }
    {
        std::lock_guard lock{m_mutex};
        m_cleanup_id = cleanup.id;
    }
    if (m_stopping) {
        m_scheduler->activateCleanup(cleanup.id);
        return {{}, execution::Rejection::Closed};
    }
    const auto admitted = execution::Submit(*m_scheduler, [self] {
        if (self->m_stopping) return;
        auto handler = self->m_registration([weak = self->weak_from_this()](std::unique_ptr<interfaces::Wallet> wallet) {
            if (const auto owner = weak.lock()) owner->ingress(std::move(wallet));
        });
        if (!handler) throw std::runtime_error("Wallet notification producer unavailable");
        {
            std::lock_guard lock{self->m_mutex};
            self->m_handler = std::move(handler);
        }
        if (self->m_stopping) return;
        auto entries = captureEntries(self->m_capture());
        std::deque<Entry> initial{std::make_move_iterator(entries.begin()), std::make_move_iterator(entries.end())};
        std::lock_guard lock{self->m_mutex};
        self->m_initial.swap(initial);
    }, [self] { self->registered(); }, [self](std::exception_ptr) { self->failed(); }, {execution::Lane::Core, TRANSPORT_BYTES, m_key});
    if (!admitted) {
        m_failed = true;
        retire();
    }
    return admitted;
}

void LoadSubscription::registered()
{
    if (m_stopping) {
        m_state.apply(Event::Close);
        return;
    }
    if (const auto publication = m_state.apply(Event::Registered)) {
        {
            std::lock_guard lock{m_mutex};
            m_initial_ready = true;
        }
        m_notify({*publication, false});
    }
}

void LoadSubscription::failed()
{
    if (m_stopping) return;
    m_failed = true;
    auto reason = FailureReason::None;
    m_failure.compare_exchange_strong(reason, FailureReason::Backend);
    if (const auto publication = m_state.apply(Event::RegistrationFailed)) m_notify({*publication, false, m_failure.load()});
    retire();
}

void LoadSubscription::ingress(std::unique_ptr<interfaces::Wallet> wallet)
{
    if (m_stopping) return;
    auto borrowed = m_scheduler->borrow(m_key);
    if (!borrowed) {
        if (borrowed.rejection == execution::Rejection::Capacity) {
            { std::lock_guard lock{m_mutex}; m_resync = true; m_resync_select &= m_notification_context->select_wallet; }
            requestScan();
        }
        return;
    }
    {
        std::lock_guard lock{m_mutex};
        if (m_stopping) return;
        if (wallet && m_pending.size() < PENDING_HANDLES) m_pending.push_back({std::move(wallet), m_notification_context});
        else { m_resync = true; m_resync_select &= m_notification_context->select_wallet; }
    }
    requestScan();
    // The counted lease covers transfer/admission; an overflow handle remains
    // on the backend producer, while the full-resync marker retains its fact.
}

void LoadSubscription::requestScan()
{
    if (m_stopping || m_scan_pending.exchange(true)) return;
    const auto self = shared_from_this();
    execution::Admission admitted;
    bool stopping;
    {
        // Publication is queued, so recording the accepted id under this mutex
        // lets retirement cancel it even when its Core transport is blocked.
        std::lock_guard lock{m_mutex};
        admitted = execution::Submit(*m_scheduler, [self] { self->scan(); }, [self] {
            { std::lock_guard finished_lock{self->m_mutex}; self->m_scan_id.reset(); }
            self->m_scan_pending = false;
            if (self->m_stopping) return;
            if (const auto publication = self->m_state.apply(Event::Notification)) self->m_notify({*publication, false});
            bool more;
            {
                std::lock_guard pending_lock{self->m_mutex};
                more = !self->m_pending.empty() || self->m_resync;
            }
            if (more) self->requestScan();
        }, [self](std::exception_ptr) { self->failed(); }, {execution::Lane::Core, TRANSPORT_BYTES, m_key, execution::WorkClass::Observation});
        if (admitted) m_scan_id = admitted.id;
        stopping = m_stopping;
    }
    if (stopping && admitted) m_scheduler->cancelQueued(admitted.id);
    if (!admitted && !m_stopping) {
        // Keep the coalescing fence set: a rejected mandatory refresh becomes
        // one explicit failure, rather than an unbounded sequence of retries.
        m_failed = true;
        const auto reported = execution::Submit(*m_scheduler, [] {}, [self] { self->failed(); }, {}, {execution::Lane::Control, TRANSPORT_BYTES, m_key});
        if (!reported) retire();
    }
}

void LoadSubscription::scan()
{
    std::vector<Pending> pending;
    bool resync, resync_select;
    {
        std::lock_guard lock{m_mutex};
        pending.swap(m_pending);
        resync = std::exchange(m_resync, false);
        resync_select = std::exchange(m_resync_select, true);
        m_full_capture_pending = resync;
    }
    if (m_stopping) return;
    std::vector<Entry> entries;
    if (!resync) {
        entries.reserve(pending.size());
        for (auto& notification : pending) {
            auto name = notification.wallet->getWalletName();
            if (!notification.context->deferred_name.empty() && name == notification.context->deferred_name) continue;
            entries.push_back({std::move(name), std::move(notification.wallet), notification.context->select_wallet});
        }
        std::lock_guard lock{m_mutex};
        const auto space = DIRTY_KEYS > m_ready.size() ? DIRTY_KEYS - m_ready.size() : 0;
        std::set<std::string> new_keys;
        for (const auto& entry : entries) {
            if (!m_ready.contains(entry.name)) new_keys.insert(entry.name);
            if (new_keys.size() > space) { resync = true; break; }
        }
        if (resync) {
            m_full_capture_pending = true;
            resync_select &= m_ready_select;
        }
    }
    if (resync) {
        // Compact notification churn into one authoritative backend snapshot.
        // No new GUI claim can observe the invalidated accumulator while the
        // potentially blocking capture runs outside the mailbox mutex.
        for (const auto& notification : pending) resync_select &= notification.context->select_wallet;
        if (!m_coherent_resync.capture) {
            auto reason = FailureReason::None;
            m_failure.compare_exchange_strong(reason, FailureReason::CoherentResyncUnavailable);
            m_failed = true;
            throw std::runtime_error("Coherent wallet resynchronization unavailable");
        }
        entries = captureEntries(m_coherent_resync.capture());
        for (auto& entry : entries) entry.select_wallet = resync_select;
        std::map<std::string, Entry> snapshot;
        for (auto& entry : entries) {
            const auto name = entry.name;
            snapshot.insert_or_assign(name, std::move(entry));
        }
        {
            std::lock_guard lock{m_mutex};
            m_ready.swap(snapshot);
            m_ready_select = m_ready.empty() || resync_select;
            m_full_capture_pending = false;
        }
        return;
    }
    std::vector<Entry> retired;
    retired.reserve(entries.size());
    {
        std::lock_guard lock{m_mutex};
        for (auto& entry : entries) {
            const auto slot = m_ready.try_emplace(entry.name).first;
            retired.push_back(std::move(slot->second));
            m_ready_select &= entry.select_wallet;
            slot->second = std::move(entry);
        }
    }
    // Replaced backend handles die outside the mailbox mutex, on Core.
}

void LoadSubscription::setNotificationContext(std::string deferred_name, bool action_pending)
{
    auto context = std::make_shared<NotificationContext>(NotificationContext{std::move(deferred_name), !action_pending});
    std::lock_guard lock{m_mutex};
    m_notification_context = std::move(context);
}

void LoadSubscription::retire()
{
    m_stopping = true;
    std::optional<execution::TaskId> cleanup, scan;
    { std::lock_guard lock{m_mutex}; cleanup = m_cleanup_id; scan = m_scan_id; }
    if (scan) m_scheduler->cancelQueued(*scan);
    if (cleanup) m_scheduler->activateCleanup(*cleanup);
}

bool LoadSubscription::cleanup()
{
    m_stopping = true;
    // The pre-admitted reservation waits for every same-owner outcome and
    // counted callback/handoff lease. No worker waits for another queued turn.
    bool cleanup_failed{false};
    std::unique_ptr<interfaces::Handler> handler;
    { std::lock_guard lock{m_mutex}; handler = std::move(m_handler); }
    if (handler) {
        try { handler->disconnect(); }
        catch (...) { cleanup_failed = true; }
        handler.reset();
    }
    std::vector<Pending> pending;
    std::deque<Entry> initial;
    std::map<std::string, Entry> ready;
    {
        std::lock_guard lock{m_mutex};
        pending.swap(m_pending);
        initial.swap(m_initial);
        ready.swap(m_ready);
    }
    return cleanup_failed;
}

struct LoadSubscription::ReturnPacket {
    std::vector<Entry> entries;
    std::optional<execution::ResourceLease> lifetime;
    explicit ReturnPacket(std::optional<execution::ResourceLease> lease) : lifetime(std::move(lease)) {}
};

std::unique_ptr<LoadSubscription::Handoff> LoadSubscription::take(bool initial)
{
    auto borrowed = m_scheduler->borrow(m_key);
    if (!borrowed) {
        if (borrowed.rejection == execution::Rejection::Capacity) { m_failed = true; retire(); }
        return {};
    }
    {
        std::lock_guard lock{m_mutex};
        if (m_stopping || m_failed || !m_initial_ready || m_handoff_disposal_pending ||
            (!initial && (m_resync || m_full_capture_pending))) return {};
        if (initial ? m_initial.empty() : m_ready.empty()) return std::unique_ptr<Handoff>{new Handoff};
        m_handoff_disposal_pending = true;
    }
    const auto self = shared_from_this();
    const auto packet = std::make_shared<ReturnPacket>(std::move(borrowed.lease));
    const auto key = m_scheduler->allocateKey();
    const auto disposal = execution::Submit(*m_scheduler, [packet] { packet->entries.clear(); }, [self, initial, packet] {
        (void)packet; // Retain the parent lease through outcome consumption.
        { std::lock_guard lock{self->m_mutex}; self->m_handoff_disposal_pending = false; }
        if (self->m_stopping) return;
        if (initial) self->m_notify({Publication::InitialHandoff, false});
        else if (const auto publication = self->m_state.apply(Event::Notification)) self->m_notify({*publication, false});
    }, {}, {execution::Lane::Cleanup, TRANSPORT_BYTES, key}, true);
    if (!disposal) {
        { std::lock_guard lock{m_mutex}; m_handoff_disposal_pending = false; }
        m_failed = true; retire(); return {};
    }
    auto guarded = m_scheduler->borrow(key);
    if (!guarded) {
        m_scheduler->activateCleanup(disposal.id);
        if (!m_stopping) { m_failed = true; retire(); }
        return {};
    }
    auto result = std::unique_ptr<Handoff>{new Handoff(self, packet, disposal.id, std::move(guarded.lease))};
    std::lock_guard lock{m_mutex};
    if (m_stopping) {
        result->m_owner.reset();
        m_scheduler->activateCleanup(disposal.id);
        return {};
    }
    while (result->entries.size() < GUI_HANDOFF) {
        if (initial) {
            if (m_initial.empty()) break;
            result->entries.push_back(std::move(m_initial.front()));
            m_initial.pop_front();
        } else {
            if (m_ready.empty()) break;
            auto node = m_ready.extract(m_ready.begin());
            result->entries.push_back(std::move(node.mapped()));
        }
    }
    result->more = initial ? !m_initial.empty() : !m_ready.empty();
    if (!initial && m_ready.empty()) m_ready_select = true;
    return result;
}

std::unique_ptr<LoadSubscription::Handoff> LoadSubscription::takeInitial() { return take(true); }
std::unique_ptr<LoadSubscription::Handoff> LoadSubscription::takeNotifications() { return take(false); }
LoadSubscription::Handoff::~Handoff() { if (m_owner) m_owner->returnHandoff(std::move(entries), std::move(m_packet), m_disposal); }
void LoadSubscription::returnHandoff(std::vector<Entry> entries, std::shared_ptr<ReturnPacket> packet, execution::TaskId disposal)
{
    packet->entries = std::move(entries);
    m_scheduler->activateCleanup(disposal);
    // The scoped disposal lease drops after this method returns. Its reserved
    // cleanup then releases every shadow backend reference off GUI, including
    // duplicates and unclaimed entries. The parent lease lasts through it.
}
} // namespace walletlife
