// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/execution.h>

#include <cassert>
#include <algorithm>
#include <atomic>
#include <stdexcept>

namespace execution {
class TaskQueue::ScopedScheduler final : public Scheduler {
    const std::shared_ptr<TaskQueue> m_queue;
    const uint64_t m_scope;
    bool m_registered{false};
    std::atomic_bool m_closed{false};
public:
    ScopedScheduler(std::shared_ptr<TaskQueue> queue, uint64_t scope) : m_queue(std::move(queue)), m_scope(scope) {}
    ~ScopedScheduler() override { if (m_registered) m_queue->releaseScope(m_scope); }
    void registered() { m_registered = true; }
    Admission submit(std::shared_ptr<Task> task, TaskSpec spec) override {
        if (m_closed) return {{}, Rejection::Closed};
        if (spec.lane != Lane::Core && spec.lane != Lane::Domain) return {{}, Rejection::Invalid};
        return m_queue->admit(std::move(task), spec, false, m_scope);
    }
    Admission reserveCleanup(std::shared_ptr<Task> task, TaskSpec spec) override { return m_queue->admit(std::move(task), spec, true, m_scope); }
    bool activateCleanup(TaskId id) override { return m_queue->activateCleanupForScope(id, m_scope); }
    BorrowAdmission borrow(ResourceKey key) override { return m_closed ? BorrowAdmission{{}, Rejection::Closed} : m_queue->borrowForScope(key, m_scope); }
    ResourceKey allocateKey() override { return m_queue->allocateKey(); }
    size_t pending() const override { return m_queue->pending(); }
    void closeOrdinaryAdmission() override { m_closed = true; }
    bool cancelQueued(TaskId id) override { return m_queue->cancelQueuedForScope(id, m_scope); }
};

std::shared_ptr<Scheduler> TaskQueue::openActionScope(size_t bytes)
{
    std::lock_guard lock{m_mutex};
    if (m_closed || m_ordinary_closed || m_next_scope == std::numeric_limits<uint64_t>::max()) return {};
    if (m_scope_usage.tasks >= m_limits.action_scopes.tasks || bytes > m_limits.action_scopes.payload_bytes - m_scope_usage.payload_bytes) return {};
    const auto id = ++m_next_scope;
    // Construct before recording admission so allocation failure leaves no
    // pending scope that could strand the Native drain.
    auto result = std::make_shared<ScopedScheduler>(shared_from_this(), id);
    m_scopes.emplace(id, bytes); ++m_scope_usage.tasks; m_scope_usage.payload_bytes += bytes;
    result->registered();
    return result;
}

void TaskQueue::releaseScope(uint64_t scope)
{
    std::lock_guard lock{m_mutex};
    const auto found = m_scopes.find(scope);
    assert(found != m_scopes.end());
    --m_scope_usage.tasks; m_scope_usage.payload_bytes -= found->second; m_scopes.erase(found);
    if (m_scope_idle_cleanups.contains(scope)) m_released_cleanup_scopes.insert(scope);
    pumpCleanups();
    // An idle original capability may be the final ledger entry. Without a
    // transport turn, workers that already observed it during shutdown would
    // never recheck quiescence. This wake owns no task, payload or effect.
    if (m_closed && m_tasks.empty() && m_total_borrowers == 0 && m_scopes.empty()) {
        const bool posted = m_routed_dispatch
            ? m_routed_dispatch(Lane::Control, Phase::OutcomeReady, {}, [] {})
            : m_dispatch(Phase::OutcomeReady, {}, [] {});
        assert(posted); // The original capability retained its drain transport.
    }
    // Every remaining task, cleanup reservation and borrower has its own
    // ledger entry; cached values do not consume an action-capability slot.
}

ResourceLease::ResourceLease(ResourceLease&& other) noexcept : m_queue(std::move(other.m_queue)), m_key(other.m_key) {}
ResourceLease::~ResourceLease() { if (m_queue) m_queue->releaseBorrower(m_key); }

TaskQueue::TaskQueue(RoutedDispatch dispatch, Limits limits, size_t core_slots)
    : m_routed_dispatch(std::move(dispatch)), m_limits(limits), m_core_slots(core_slots)
{
    if (!m_routed_dispatch || core_slots == 0 || limits.actions_before_observation == 0) throw std::invalid_argument("Invalid execution transport or capacity");
}

std::optional<size_t> TaskQueue::budgetIndex(Lane lane)
{
    switch (lane) {
    case Lane::Domain:
    case Lane::Core: return 0;
    case Lane::Control: return 1;
    case Lane::Cleanup: return 2;
    }
    return std::nullopt;
}

void TaskQueue::releaseWorkKey(Record& record)
{
    if (!std::exchange(record.resource_counted, false)) return;
    const auto key = record.spec.key;
    const auto count = m_work_by_key.find(key);
    assert(count != m_work_by_key.end() && count->second);
    if (--count->second == 0) m_work_by_key.erase(count);
}

void TaskQueue::erase(std::map<TaskId, Record>::iterator record)
{
    const auto key = record->second.spec.key;
    const auto bytes = record->second.spec.payload_bytes;
    if (record->second.cleanup_reservation) {
        --m_cleanup_reservations.tasks;
        m_cleanup_reservations.payload_bytes -= bytes;
        if (record->second.cleanup_queued) {
            --m_usage[2].tasks;
            m_usage[2].payload_bytes -= bytes;
        }
        m_cleanup_by_key.erase(key);
        unindexCleanup(record->first, record->second.scope);
    } else {
        auto& usage = m_usage[*budgetIndex(record->second.spec.lane)];
        --usage.tasks;
        usage.payload_bytes -= bytes;
        releaseWorkKey(record->second);
    }
    if (record->second.spec.work_class == WorkClass::Observation) {
        --m_observations.tasks;
        m_observations.payload_bytes -= bytes;
    }
    m_tasks.erase(record);
    maybeActivateCleanup(key);
}

Admission TaskQueue::submit(std::shared_ptr<Task> task, TaskSpec spec)
{
    return admit(std::move(task), spec, false);
}

Admission TaskQueue::reserveCleanup(std::shared_ptr<Task> task, TaskSpec spec)
{
    return admit(std::move(task), spec, true);
}

Admission TaskQueue::admit(std::shared_ptr<Task> task, TaskSpec spec, bool reserve_cleanup, uint64_t scope)
{
    std::lock_guard lock{m_mutex};
    const bool admitted_action = scope && m_scopes.contains(scope);
    if ((scope && !admitted_action) || (m_closed && !admitted_action)) return {{}, Rejection::Closed};
    const auto index = budgetIndex(spec.lane);
    if (!task || !index) return {{}, Rejection::Invalid};
    if (reserve_cleanup && spec.lane != Lane::Cleanup) return {{}, Rejection::Invalid};
    const auto cleanup = m_cleanup_by_key.find(spec.key);
    if (reserve_cleanup && cleanup != m_cleanup_by_key.end()) return {{}, Rejection::Invalid};
    if (cleanup != m_cleanup_by_key.end() && m_tasks.at(cleanup->second).cleanup_requested) return {{}, Rejection::Closed};
    if (spec.work_class != WorkClass::Action && spec.work_class != WorkClass::Observation) return {{}, Rejection::Invalid};
    if (spec.work_class == WorkClass::Observation && *index != 0) return {{}, Rejection::Invalid};
    if (m_ordinary_closed && *index == 0 && !admitted_action) return {{}, Rejection::Closed};
    auto& usage = reserve_cleanup ? m_cleanup_reservations : m_usage[*index];
    const auto budget = reserve_cleanup ? m_limits.cleanup_reservations
        : std::array{m_limits.ordinary, m_limits.control, m_limits.cleanup}[*index];
    // Reject reservations that could never fit a runnable cleanup turn.
    if (reserve_cleanup && (m_limits.cleanup.tasks == 0 || spec.payload_bytes > m_limits.cleanup.payload_bytes)) return {{}, Rejection::Capacity};
    // Subtraction avoids overflow even for a producer's oversized request.
    if (usage.tasks >= budget.tasks || spec.payload_bytes > budget.payload_bytes - usage.payload_bytes) {
        return {{}, Rejection::Capacity};
    }
    if (spec.work_class == WorkClass::Observation &&
        (m_observations.tasks >= m_limits.observations.tasks || spec.payload_bytes > m_limits.observations.payload_bytes - m_observations.payload_bytes)) {
        return {{}, Rejection::Capacity};
    }
    const TaskId id{++m_next_id};
    m_tasks.emplace(id, Record{reserve_cleanup ? Phase::Reserved : Phase::Queued, task, spec, false, reserve_cleanup});
    m_tasks.at(id).scope = scope;
    ++usage.tasks;
    usage.payload_bytes += spec.payload_bytes;
    if (spec.work_class == WorkClass::Observation) {
        ++m_observations.tasks;
        m_observations.payload_bytes += spec.payload_bytes;
    }
    if (reserve_cleanup) {
        m_cleanup_by_key.emplace(spec.key, id);
        maybeActivateCleanup(spec.key);
        return {id, Rejection::None};
    }
    m_tasks.at(id).resource_counted = true;
    ++m_work_by_key[spec.key];
    if (cleanup != m_cleanup_by_key.end()) {
        unindexCleanup(cleanup->second, m_tasks.at(cleanup->second).scope);
    }
    if (m_routed_dispatch && spec.lane == Lane::Core) {
        auto& waiting = m_waiting[spec.key];
        if (waiting.empty() && !m_running.contains(spec.key)) m_ready.push_back(spec.key);
        waiting.push_back(id);
        if (!pumpCore(id)) return {{}, Rejection::Transport};
        return {id, Rejection::None};
    }
    auto turn = [self = shared_from_this(), id] { self->execute(id); };
    m_tasks.at(id).transport_pending = true;
    const bool posted = m_routed_dispatch
        ? m_routed_dispatch(spec.lane, Phase::Queued, id, std::move(turn))
        : m_dispatch(Phase::Queued, id, std::move(turn));
    if (!posted) {
        erase(m_tasks.find(id));
        return {{}, Rejection::Transport};
    }
    return {id, Rejection::None};
}

void TaskQueue::activateCleanupLocked(TaskId id)
{
    auto& record = m_tasks.at(id);
    record.phase = Phase::Queued;
    record.cleanup_requested = true; // Physical retirement fences every later use of this resource key.
    record.transport_pending = true;
    record.cleanup_queued = true;
    ++m_usage[2].tasks;
    m_usage[2].payload_bytes += record.spec.payload_bytes;
    auto turn = [self = shared_from_this(), id] { self->execute(id); };
    const bool posted = m_routed_dispatch
        ? m_routed_dispatch(Lane::Cleanup, Phase::Queued, id, std::move(turn))
        : m_dispatch(Phase::Queued, id, std::move(turn));
    // This transport was reserved before resources could be acquired, and is
    // retained until its outcome is consumed. It cannot reject during drain.
    assert(posted);
}

bool TaskQueue::activateCleanup(TaskId id) { return activateCleanupForScope(id, 0); }

bool TaskQueue::activateCleanupForScope(TaskId id, uint64_t scope)
{
    std::lock_guard lock{m_mutex};
    const auto record = m_tasks.find(id);
    if ((scope && (!m_scopes.contains(scope) || record == m_tasks.end() || record->second.scope != scope)) ||
        record == m_tasks.end() || !record->second.cleanup_reservation || record->second.phase != Phase::Reserved || record->second.cleanup_requested ||
        (m_cleanup_all_requested && (!record->second.scope || !m_scopes.contains(record->second.scope)))) return false;
    record->second.cleanup_requested = true;
    maybeActivateCleanup(record->second.spec.key);
    return true;
}

void TaskQueue::maybeActivateCleanup(ResourceKey key)
{
    const auto cleanup = m_cleanup_by_key.find(key);
    if (cleanup != m_cleanup_by_key.end()) {
        const auto id = cleanup->second;
        const auto& record = m_tasks.at(id);
        if (record.phase == Phase::Reserved && !m_borrowers.contains(key) && !m_work_by_key.contains(key)) {
            if (record.scope && !record.cleanup_requested) {
                m_scope_idle_cleanups[record.scope].insert(id);
                if (!m_scopes.contains(record.scope)) m_released_cleanup_scopes.insert(record.scope);
            } else {
                m_idle_cleanups.insert(id);
                if (record.cleanup_requested) m_ready_cleanups.insert(id);
            }
        } else {
            unindexCleanup(id, record.scope);
        }
    }
    pumpCleanups();
}

void TaskQueue::unindexCleanup(TaskId id, uint64_t scope)
{
    m_idle_cleanups.erase(id); m_ready_cleanups.erase(id);
    const auto group = m_scope_idle_cleanups.find(scope);
    if (group != m_scope_idle_cleanups.end()) {
        group->second.erase(id);
        if (group->second.empty()) { m_scope_idle_cleanups.erase(group); m_released_cleanup_scopes.erase(scope); }
    }
}

void TaskQueue::pumpCleanups()
{
    // Even legacy unlimited configurations only dispatch a bounded batch per
    // ingress turn. Outcomes free a slot and continue the indexed ready set.
    for (size_t dispatched = 0; dispatched < 16; ++dispatched) {
        const auto& ready = m_cleanup_all_requested ? m_idle_cleanups : m_ready_cleanups;
        if ((ready.empty() && m_released_cleanup_scopes.empty()) || m_usage[2].tasks >= m_limits.cleanup.tasks) return;
        const auto id = !ready.empty() ? *ready.begin() : *m_scope_idle_cleanups.at(*m_released_cleanup_scopes.begin()).begin();
        const auto& record = m_tasks.at(id);
        if (record.spec.payload_bytes > m_limits.cleanup.payload_bytes - m_usage[2].payload_bytes) return;
        unindexCleanup(id, record.scope);
        activateCleanupLocked(id);
    }
}

BorrowAdmission TaskQueue::borrow(ResourceKey key)
{
    return borrowForScope(key, 0);
}

BorrowAdmission TaskQueue::borrowForScope(ResourceKey key, uint64_t scope)
{
    std::lock_guard lock{m_mutex};
    const bool admitted_action = scope && m_scopes.contains(scope);
    if ((scope && !admitted_action) || ((m_closed || m_ordinary_closed) && !admitted_action)) return {{}, Rejection::Closed};
    const auto cleanup = m_cleanup_by_key.find(key);
    if (cleanup == m_cleanup_by_key.end()) return {{}, Rejection::Invalid};
    if (scope && m_tasks.at(cleanup->second).scope != scope) return {{}, Rejection::Invalid};
    if (m_tasks.at(cleanup->second).cleanup_requested || m_tasks.at(cleanup->second).phase != Phase::Reserved) return {{}, Rejection::Closed};
    if (m_total_borrowers >= m_limits.resource_borrowers) return {{}, Rejection::Capacity};
    ++m_borrowers[key];
    ++m_total_borrowers;
    unindexCleanup(cleanup->second, m_tasks.at(cleanup->second).scope);
    return {ResourceLease{shared_from_this(), key}, Rejection::None};
}

void TaskQueue::releaseBorrower(ResourceKey key)
{
    std::lock_guard lock{m_mutex};
    if (--m_borrowers.at(key) == 0) m_borrowers.erase(key);
    --m_total_borrowers;
    maybeActivateCleanup(key);
}

ResourceKey TaskQueue::allocateKey()
{
    std::lock_guard lock{m_mutex};
    if (m_next_owner == 0) throw std::overflow_error("Execution resource identity exhausted");
    const auto owner = m_next_owner;
    m_next_owner = owner == std::numeric_limits<uint64_t>::max() ? 0 : owner + 1;
    return {owner, 1};
}

void TaskQueue::releaseCore(ResourceKey key)
{
    m_running.erase(key);
    const auto waiting = m_waiting.find(key);
    if (!waiting->second.empty()) m_ready.push_back(key);
    else m_waiting.erase(waiting);
}

bool TaskQueue::pumpCore(std::optional<TaskId> proposed)
{
    bool accepted{true};
    while (m_running.size() < m_core_slots && !m_ready.empty()) {
        const auto wanted = m_action_burst < m_limits.actions_before_observation ? WorkClass::Action : WorkClass::Observation;
        const auto preferred = std::find_if(m_ready.begin(), m_ready.end(), [&](ResourceKey key) {
            return m_tasks.at(m_waiting.at(key).front()).spec.work_class == wanted;
        });
        const auto selected = preferred == m_ready.end() ? m_ready.begin() : preferred;
        const auto key = *selected;
        m_ready.erase(selected);
        auto& waiting = m_waiting.at(key);
        const auto id = waiting.front();
        waiting.pop_front();
        if (m_tasks.at(id).spec.work_class == WorkClass::Observation) m_action_burst = 0;
        else if (m_action_burst < m_limits.actions_before_observation) ++m_action_burst;
        m_running.insert(key);
        m_tasks.at(id).transport_pending = true;
        if (!m_routed_dispatch(Lane::Core, Phase::Queued, id, [self = shared_from_this(), id] { self->execute(id); })) {
            m_tasks.at(id).transport_pending = false;
            releaseCore(key);
            if (proposed == id) {
                erase(m_tasks.find(id));
                accepted = false;
            } else {
                queueOutcome(id, NotStarted::Cause::Transport);
            }
        }
    }
    return accepted;
}

void TaskQueue::queueOutcome(TaskId id, std::optional<NotStarted::Cause> not_started)
{
    auto& record = m_tasks.at(id);
    record.phase = Phase::OutcomeReady;
    if (not_started) {
        // An admitted task whose transport later rejects still owns its failure
        // and captures. Reserved cleanup disposes them before domain delivery.
        const bool posted = m_routed_dispatch(Lane::Cleanup, Phase::OutcomeReady, id,
            [self = shared_from_this(), id, task = record.task, cause = *not_started] {
                task->notStarted(cause);
                std::lock_guard lock{self->m_mutex};
                self->queueOutcome(id);
            });
        assert(posted);
        return;
    }
    auto turn = [self = shared_from_this(), id] { self->deliver(id); };
    const bool posted = m_routed_dispatch
        ? m_routed_dispatch(Lane::Domain, Phase::OutcomeReady, id, std::move(turn))
        : m_dispatch(Phase::OutcomeReady, id, std::move(turn));
    // Accepted outcomes retain their transport until consumption, also during
    // drain. A broken transport is a lifetime bug, never a silent lost result.
    assert(posted);
}

void TaskQueue::execute(TaskId id)
{
    std::shared_ptr<Task> task;
    TaskSpec spec;
    bool cancelled;
    {
        std::lock_guard lock{m_mutex};
        const auto found = m_tasks.find(id);
        if (found == m_tasks.end()) return;
        found->second.transport_pending = false;
        // A cancelled turn retains transport capacity until actual dequeue,
        // even when its resource-owning outcome was consumed independently.
        if (found->second.phase != Phase::Queued) {
            if (found->second.outcome_consumed) erase(found);
            return;
        }
        auto& record = found->second;
        record.phase = Phase::Executing;
        task = record.task;
        spec = record.spec;
        cancelled = record.cancelled;
    }
    if (cancelled) task->notStarted(NotStarted::Cause::Cancelled);
    else task->execute();
    {
        std::lock_guard lock{m_mutex};
        if (m_routed_dispatch && spec.lane == Lane::Core) releaseCore(spec.key);
        queueOutcome(id);
        if (m_routed_dispatch) pumpCore();
    }
}

void TaskQueue::deliver(TaskId id)
{
    std::shared_ptr<Task> task;
    {
        std::lock_guard lock{m_mutex};
        auto& record = m_tasks.at(id);
        record.phase = Phase::Delivering;
        task = record.task;
    }
    task->deliver();
    {
        std::lock_guard lock{m_mutex};
        const auto found = m_tasks.find(id);
        if (found->second.transport_pending) {
            found->second.outcome_consumed = true;
            found->second.phase = Phase::AwaitingTransport;
            found->second.task.reset();
            releaseWorkKey(found->second);
            maybeActivateCleanup(found->second.spec.key);
        } else {
            erase(found);
        }
    }
    // The local task (including an unobserved owning result) dies on this lane.
}

void TaskQueue::closeAdmission()
{
    std::lock_guard lock{m_mutex};
    m_closed = true;
    m_cleanup_all_requested = true;
    pumpCleanups();
}

void TaskQueue::closeOrdinaryAdmission()
{
    std::lock_guard lock{m_mutex};
    m_ordinary_closed = true;
}

bool TaskQueue::cancelQueued(TaskId id) { return cancelQueuedForScope(id, 0); }

bool TaskQueue::cancelQueuedForScope(TaskId id, uint64_t scope)
{
    std::lock_guard lock{m_mutex};
    const auto record = m_tasks.find(id);
    if ((scope && (!m_scopes.contains(scope) || record == m_tasks.end() || record->second.scope != scope)) ||
        record == m_tasks.end() || record->second.phase != Phase::Queued || record->second.cancelled || record->second.cleanup_reservation) return false;
    record->second.cancelled = true;
    if (m_routed_dispatch) {
        if (record->second.spec.lane == Lane::Core) {
            const auto key = record->second.spec.key;
            auto& waiting = m_waiting.at(key);
            const auto queued = std::find(waiting.begin(), waiting.end(), id);
            if (queued != waiting.end()) {
                waiting.erase(queued);
                if (waiting.empty() && !m_running.contains(key)) {
                    m_waiting.erase(key);
                    std::erase(m_ready, key);
                }
            } else {
                releaseCore(key);
            }
        }
        queueOutcome(id, NotStarted::Cause::Cancelled);
        pumpCore();
    }
    return true;
}

size_t TaskQueue::pending() const
{
    std::lock_guard lock{m_mutex};
    return m_tasks.size() + m_total_borrowers + m_scopes.size();
}

std::optional<Phase> TaskQueue::phase(TaskId id) const
{
    std::lock_guard lock{m_mutex};
    const auto it = m_tasks.find(id);
    if (it == m_tasks.end()) return std::nullopt;
    return it->second.phase;
}
} // namespace execution
