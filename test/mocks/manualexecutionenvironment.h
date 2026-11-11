// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_TEST_MOCKS_MANUALEXECUTIONENVIRONMENT_H
#define BITCOIN_QML_TEST_MOCKS_MANUALEXECUTIONENVIRONMENT_H

#include <qml/execution.h>
#include <qml/models/feereadservices.h>

#include <array>
#include <algorithm>
#ifndef Q_MOC_RUN
#include <charconv>
#endif
#include <chrono>
#include <deque>
#include <string>
#include <vector>

namespace manualexecution_detail {
    template <typename Integer>
    std::string Number(Integer value) {
        std::array<char, 32> buffer{};
        const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
        return {buffer.data(), result.ptr};
    }
}

/** Manual transport only: production TaskQueue owns admission/key decisions. */
class ManualRoutedExecution
{
public:
    struct Turn { execution::Lane lane; execution::Phase phase; execution::TaskId id; std::function<void()> run; };
    explicit ManualRoutedExecution(execution::Limits limits = execution::Limits::Bounded(), size_t core_slots = 1)
    {
        scheduler = std::make_shared<execution::TaskQueue>(
            [this](execution::Lane lane, execution::Phase phase, execution::TaskId id, std::function<void()> run) {
                if (phase == execution::Phase::Queued && reject_lane == lane) {
                    reject_lane.reset();
                    trace.push_back("reject:" + manualexecution_detail::Number(id.value));
                    return false;
                }
                queued.push_back({lane, phase, id, std::move(run)});
                trace.push_back("enqueue:" + manualexecution_detail::Number(static_cast<int>(lane)) + ":" + manualexecution_detail::Number(id.value));
                return true;
            }, limits, core_slots);
    }
    std::optional<execution::TaskId> dispatchNext(execution::Lane lane)
    {
        if (lane != execution::Lane::Core && std::any_of(active.begin(), active.end(), [lane](const auto& entry) { return entry.second.lane == lane; })) return {};
        const auto next = std::find_if(queued.begin(), queued.end(), [lane](const Turn& turn) { return turn.lane == lane; });
        if (next == queued.end()) return {};
        const auto id = next->id;
        active.emplace(id, std::move(*next));
        queued.erase(next);
        trace.push_back("dispatch:" + manualexecution_detail::Number(id.value));
        return id;
    }
    bool finishWork(execution::TaskId id)
    {
        const auto next = active.find(id);
        if (next == active.end()) return false;
        auto turn = std::move(next->second);
        active.erase(next);
        trace.push_back("finish:" + manualexecution_detail::Number(id.value));
        turn.run();
        return true;
    }
    std::optional<execution::TaskId> deliverOutcome()
    {
        const auto next = std::find_if(queued.begin(), queued.end(), [](const Turn& turn) { return turn.lane == execution::Lane::Domain; });
        if (next == queued.end() || next->phase != execution::Phase::OutcomeReady) return {};
        return dispatchNext(execution::Lane::Domain);
    }
    bool stepMailbox(execution::TaskId id)
    {
        const auto next = active.find(id);
        return next != active.end() && next->second.lane == execution::Lane::Domain && finishWork(id);
    }
    bool step(execution::Lane lane)
    {
        const auto id = dispatchNext(lane);
        return id && finishWork(*id);
    }
    void drain()
    {
        while (!active.empty() || !queued.empty()) {
            if (!active.empty()) finishWork(active.begin()->first);
            else step(queued.front().lane);
        }
    }
    bool quiescent() const { return queued.empty() && active.empty() && scheduler->pending() == 0; }
    std::shared_ptr<execution::TaskQueue> scheduler;
    std::optional<execution::Lane> reject_lane;
    std::deque<Turn> queued;
    std::map<execution::TaskId, Turn> active;
    std::vector<std::string> trace;
};

/** Timing/transport controller; executes the production ledger and decisions. */
class ManualExecutionEnvironment final : public FeeReadTimer
{
public:
    ManualExecutionEnvironment()
        : scheduler(std::make_shared<execution::TaskQueue>(
            [this](execution::Phase phase, execution::TaskId id, std::function<void()> turn) {
                if (phase == execution::Phase::Queued && std::exchange(reject_next, false)) {
                    m_trace.emplace_back("reject:" + manualexecution_detail::Number(id.value));
                    return false;
                }
                m_trace.emplace_back((phase == execution::Phase::Queued ? "enqueue-effect:" : "enqueue-outcome:") + manualexecution_detail::Number(id.value));
                turns.push_back({phase, id, std::move(turn)});
                return true;
            })) {}
    ~ManualExecutionEnvironment() override { scheduler->closeAdmission(); stop(); drain(); }
    void restart(std::function<void()> expired) override { m_expired = std::move(expired); m_due = m_now + std::chrono::milliseconds{250}; m_trace.emplace_back("deadline:" + manualexecution_detail::Number(m_due.count())); }
    void stop() override { m_expired = {}; m_trace.emplace_back("stop-timer"); }
    void advance(std::chrono::milliseconds duration)
    {
        m_now += duration;
        m_trace.emplace_back("clock:" + manualexecution_detail::Number(m_now.count()));
        if (m_expired && m_now >= m_due) {
            m_trace.emplace_back("expire");
            auto expired = std::exchange(m_expired, {});
            expired();
        }
    }
    bool step()
    {
        if (turns.empty()) return false;
        auto next = std::move(turns.front());
        turns.pop_front();
        m_trace.emplace_back((next.phase == execution::Phase::Queued ? "effect:" : "outcome:") + manualexecution_detail::Number(next.id.value));
        next.turn();
        return true;
    }
    void drain() { while (step()) {} }
    void post(std::function<void()> publication) { m_trace.emplace_back("post"); publications.push_back(std::move(publication)); }
    bool publishNext()
    {
        if (publications.empty()) return false;
        auto publication = std::move(publications.front());
        publications.pop_front();
        m_trace.emplace_back("publish");
        publication();
        return true;
    }
    void publishAll() { while (publishNext()) {} }
    bool quiescent() const { return turns.empty() && scheduler->pending() == 0 && !m_expired && publications.empty(); }
    std::string trace() const {
        std::string result;
        for (const auto& entry : m_trace) { if (!result.empty()) result += ' '; result += entry; }
        return result;
    }
    struct Turn { execution::Phase phase; execution::TaskId id; std::function<void()> turn; };
    std::shared_ptr<execution::TaskQueue> scheduler;
    std::deque<Turn> turns;
    bool reject_next{false};
    std::deque<std::function<void()>> publications;
private:
    std::vector<std::string> m_trace;
    std::chrono::milliseconds m_now{0};
    std::chrono::milliseconds m_due{0};
    std::function<void()> m_expired;
};

#endif // BITCOIN_QML_TEST_MOCKS_MANUALEXECUTIONENVIRONMENT_H
