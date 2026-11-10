// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_TEST_MOCKS_MANUALEXECUTIONENVIRONMENT_H
#define BITCOIN_QML_TEST_MOCKS_MANUALEXECUTIONENVIRONMENT_H

#include <qml/execution.h>
#include <qml/models/feereadservices.h>

#include <array>
#ifndef Q_MOC_RUN
#include <charconv>
#endif
#include <chrono>
#include <deque>
#include <string>
#include <vector>

/** Timing/transport controller; executes the production ledger and decisions. */
class ManualExecutionEnvironment final : public FeeReadTimer
{
public:
    ManualExecutionEnvironment()
        : scheduler(std::make_shared<execution::TaskQueue>(
            [this](execution::Phase phase, execution::TaskId id, std::function<void()> turn) {
                if (phase == execution::Phase::Queued && std::exchange(reject_next, false)) {
                    m_trace.emplace_back("reject:" + Number(id.value));
                    return false;
                }
                m_trace.emplace_back((phase == execution::Phase::Queued ? "enqueue-effect:" : "enqueue-outcome:") + Number(id.value));
                turns.push_back({phase, id, std::move(turn)});
                return true;
            })) {}
    ~ManualExecutionEnvironment() override { scheduler->closeAdmission(); stop(); drain(); }
    void restart(std::function<void()> expired) override { m_expired = std::move(expired); m_due = m_now + std::chrono::milliseconds{250}; m_trace.emplace_back("deadline:" + Number(m_due.count())); }
    void stop() override { m_expired = {}; m_trace.emplace_back("stop-timer"); }
    void advance(std::chrono::milliseconds duration)
    {
        m_now += duration;
        m_trace.emplace_back("clock:" + Number(m_now.count()));
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
        m_trace.emplace_back((next.phase == execution::Phase::Queued ? "effect:" : "outcome:") + Number(next.id.value));
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
        for (const auto& entry : m_trace) { if (!result.empty()) result += " "; result += entry; }
        return result;
    }
    struct Turn { execution::Phase phase; execution::TaskId id; std::function<void()> turn; };
    std::shared_ptr<execution::TaskQueue> scheduler;
    std::deque<Turn> turns;
    bool reject_next{false};
    std::deque<std::function<void()>> publications;
private:
    template <typename Integer>
    static std::string Number(Integer value) {
        std::array<char, 32> buffer{};
        const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
        return {buffer.data(), result.ptr};
    }
    std::vector<std::string> m_trace;
    std::chrono::milliseconds m_now{0};
    std::chrono::milliseconds m_due{0};
    std::function<void()> m_expired;
};

#endif // BITCOIN_QML_TEST_MOCKS_MANUALEXECUTIONENVIRONMENT_H
