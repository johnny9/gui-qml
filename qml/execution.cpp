// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/execution.h>

#include <cassert>

namespace execution {
Admission TaskQueue::submit(std::shared_ptr<Task> task)
{
    std::lock_guard lock{m_mutex};
    if (m_closed) return {{}, Rejection::Closed};
    const TaskId id{++m_next_id};
    m_tasks.emplace(id, Record{Phase::Queued, task});
    if (!m_dispatch(Phase::Queued, id, [self = shared_from_this(), id] { self->execute(id); })) {
        m_tasks.erase(id);
        return {{}, Rejection::Transport};
    }
    return {id, Rejection::None};
}

void TaskQueue::execute(TaskId id)
{
    std::shared_ptr<Task> task;
    {
        std::lock_guard lock{m_mutex};
        auto& record = m_tasks.at(id);
        record.phase = Phase::Executing;
        task = record.task;
    }
    task->execute();
    {
        std::lock_guard lock{m_mutex};
        m_tasks.at(id).phase = Phase::OutcomeReady;
    }
    // Accepted outcomes use the still-live transport, including during drain.
    const bool queued = m_dispatch(Phase::OutcomeReady, id, [self = shared_from_this(), id] { self->deliver(id); });
    assert(queued);
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
        m_tasks.erase(id);
    }
    // The local task (including an unobserved owning result) dies on this lane.
}

void TaskQueue::closeAdmission()
{
    std::lock_guard lock{m_mutex};
    m_closed = true;
}

size_t TaskQueue::pending() const
{
    std::lock_guard lock{m_mutex};
    return m_tasks.size();
}

std::optional<Phase> TaskQueue::phase(TaskId id) const
{
    std::lock_guard lock{m_mutex};
    const auto it = m_tasks.find(id);
    if (it == m_tasks.end()) return std::nullopt;
    return it->second.phase;
}
} // namespace execution
