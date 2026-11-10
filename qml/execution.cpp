// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/execution.h>

#include <cassert>
#include <algorithm>
#include <stdexcept>

namespace execution {
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

void TaskQueue::erase(std::map<TaskId, Record>::iterator record)
{
    auto& usage = m_usage[*budgetIndex(record->second.spec.lane)];
    --usage.tasks;
    usage.payload_bytes -= record->second.spec.payload_bytes;
    if (record->second.spec.work_class == WorkClass::Observation) {
        --m_observations.tasks;
        m_observations.payload_bytes -= record->second.spec.payload_bytes;
    }
    if (record->second.cleanup_reservation) m_cleanup_by_key.erase(record->second.spec.key);
    m_tasks.erase(record);
    maybeActivateCleanups();
}

Admission TaskQueue::submit(std::shared_ptr<Task> task, TaskSpec spec)
{
    return admit(std::move(task), spec, false);
}

Admission TaskQueue::reserveCleanup(std::shared_ptr<Task> task, TaskSpec spec)
{
    return admit(std::move(task), spec, true);
}

Admission TaskQueue::admit(std::shared_ptr<Task> task, TaskSpec spec, bool reserve_cleanup)
{
    std::lock_guard lock{m_mutex};
    if (m_closed) return {{}, Rejection::Closed};
    const auto index = budgetIndex(spec.lane);
    if (!task || !index) return {{}, Rejection::Invalid};
    if (reserve_cleanup && spec.lane != Lane::Cleanup) return {{}, Rejection::Invalid};
    const auto cleanup = m_cleanup_by_key.find(spec.key);
    if (reserve_cleanup && cleanup != m_cleanup_by_key.end()) return {{}, Rejection::Invalid};
    if (cleanup != m_cleanup_by_key.end() && m_tasks.at(cleanup->second).cleanup_requested) return {{}, Rejection::Closed};
    if (spec.work_class != WorkClass::Action && spec.work_class != WorkClass::Observation) return {{}, Rejection::Invalid};
    if (spec.work_class == WorkClass::Observation && *index != 0) return {{}, Rejection::Invalid};
    if (m_ordinary_closed && *index == 0) return {{}, Rejection::Closed};
    auto& usage = m_usage[*index];
    const auto budget = std::array{m_limits.ordinary, m_limits.control, m_limits.cleanup}[*index];
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
    ++usage.tasks;
    usage.payload_bytes += spec.payload_bytes;
    if (spec.work_class == WorkClass::Observation) {
        ++m_observations.tasks;
        m_observations.payload_bytes += spec.payload_bytes;
    }
    if (reserve_cleanup) {
        m_cleanup_by_key.emplace(spec.key, id);
        return {id, Rejection::None};
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
    m_tasks.at(id).phase = Phase::Queued;
    m_tasks.at(id).transport_pending = true;
    auto turn = [self = shared_from_this(), id] { self->execute(id); };
    const bool posted = m_routed_dispatch
        ? m_routed_dispatch(Lane::Cleanup, Phase::Queued, id, std::move(turn))
        : m_dispatch(Phase::Queued, id, std::move(turn));
    // This transport was reserved before resources could be acquired, and is
    // retained until its outcome is consumed. It cannot reject during drain.
    assert(posted);
}

bool TaskQueue::activateCleanup(TaskId id)
{
    std::lock_guard lock{m_mutex};
    const auto record = m_tasks.find(id);
    if (record == m_tasks.end() || record->second.phase != Phase::Reserved || record->second.cleanup_requested) return false;
    record->second.cleanup_requested = true;
    maybeActivateCleanups();
    return true;
}

void TaskQueue::maybeActivateCleanups()
{
    for (const auto& [key, id] : m_cleanup_by_key) {
        const auto& record = m_tasks.at(id);
        if (record.phase != Phase::Reserved || !record.cleanup_requested || m_borrowers.contains(key)) continue;
        const bool busy = std::any_of(m_tasks.begin(), m_tasks.end(), [&](const auto& task) {
            return task.first != id && task.second.spec.key == key && !task.second.outcome_consumed;
        });
        if (!busy) activateCleanupLocked(id);
    }
}

BorrowAdmission TaskQueue::borrow(ResourceKey key)
{
    std::lock_guard lock{m_mutex};
    if (m_closed || m_ordinary_closed) return {{}, Rejection::Closed};
    const auto cleanup = m_cleanup_by_key.find(key);
    if (cleanup == m_cleanup_by_key.end()) return {{}, Rejection::Invalid};
    if (m_tasks.at(cleanup->second).cleanup_requested) return {{}, Rejection::Closed};
    if (m_total_borrowers >= m_limits.resource_borrowers) return {{}, Rejection::Capacity};
    ++m_borrowers[key];
    ++m_total_borrowers;
    return {ResourceLease{shared_from_this(), key}, Rejection::None};
}

void TaskQueue::releaseBorrower(ResourceKey key)
{
    std::lock_guard lock{m_mutex};
    if (--m_borrowers.at(key) == 0) m_borrowers.erase(key);
    --m_total_borrowers;
    maybeActivateCleanups();
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
            maybeActivateCleanups();
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
    for (auto& [id, record] : m_tasks) {
        if (record.phase == Phase::Reserved) record.cleanup_requested = true;
    }
    maybeActivateCleanups();
}

void TaskQueue::closeOrdinaryAdmission()
{
    std::lock_guard lock{m_mutex};
    m_ordinary_closed = true;
}

bool TaskQueue::cancelQueued(TaskId id)
{
    std::lock_guard lock{m_mutex};
    const auto record = m_tasks.find(id);
    if (record == m_tasks.end() || record->second.phase != Phase::Queued || record->second.cancelled || record->second.cleanup_reservation) return false;
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
    return m_tasks.size() + m_total_borrowers;
}

std::optional<Phase> TaskQueue::phase(TaskId id) const
{
    std::lock_guard lock{m_mutex};
    const auto it = m_tasks.find(id);
    if (it == m_tasks.end()) return std::nullopt;
    return it->second.phase;
}
} // namespace execution
