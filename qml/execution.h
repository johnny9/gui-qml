// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_EXECUTION_H
#define BITCOIN_QML_EXECUTION_H

#ifndef Q_MOC_RUN
#include <compare>
#endif
#include <cstdint>
#include <exception>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

namespace execution {
struct TaskId {
    uint64_t value{0};
    auto operator<=>(const TaskId&) const = default;
};
enum class Rejection { None, Closed, Transport };
struct Admission {
    TaskId id;
    Rejection rejection{Rejection::None};
    explicit operator bool() const { return rejection == Rejection::None; }
};
enum class Phase { Queued, Executing, OutcomeReady, Delivering };

/** Owned data and callbacks. Execution and outcome delivery are separate turns. */
struct Task {
    virtual ~Task() = default;
    virtual void execute() = 0;
    virtual void deliver() = 0;
};

class Scheduler
{
public:
    virtual ~Scheduler() = default;
    virtual Admission submit(std::shared_ptr<Task> task) = 0;
    virtual size_t pending() const = 0;
};

/** Value-only command/effect adapter; neither callback may access GUI objects. */
template <typename Work, typename Done>
Admission Submit(Scheduler& scheduler, Work work, Done done,
                 std::function<void(std::exception_ptr)> failed = {})
{
    using Return = std::invoke_result_t<Work>;
    static_assert(!std::is_reference_v<Return>, "Return an owned result");
    using Result = std::conditional_t<std::is_void_v<Return>, std::monostate, Return>;
    struct Packet final : Task {
        std::optional<Work> work;
        Done done;
        std::function<void(std::exception_ptr)> failed;
        std::optional<Result> result;
        std::exception_ptr error;
        Packet(Work w, Done d, std::function<void(std::exception_ptr)> f)
            : work(std::move(w)), done(std::move(d)), failed(std::move(f)) {}
        void execute() override
        {
            try {
                if constexpr (std::is_void_v<Return>) {
                    std::invoke(*work);
                    result.emplace();
                } else {
                    result.emplace(std::invoke(*work));
                }
            } catch (...) {
                error = std::current_exception();
            }
            work.reset();
        }
        void deliver() override
        {
            if (error) {
                if (failed) failed(error);
            } else if constexpr (std::is_void_v<Return>) {
                std::invoke(done);
            } else {
                std::invoke(done, std::move(*result));
            }
        }
    };
    return scheduler.submit(std::make_shared<Packet>(std::move(work), std::move(done), std::move(failed)));
}

/**
 * Thread-safe admission and an owned task ledger. Dispatch must enqueue, never
 * invoke inline. Once admitted, the transport must remain available for its
 * outcome even after admission closes. Task bodies and delivery are serial.
 * Domain completion never depends on a GUI observer.
 */
class TaskQueue final : public Scheduler, public std::enable_shared_from_this<TaskQueue>
{
public:
    using Dispatch = std::function<bool(Phase, TaskId, std::function<void()>)>;
    explicit TaskQueue(Dispatch dispatch) : m_dispatch(std::move(dispatch)) {}
    Admission submit(std::shared_ptr<Task> task) override;
    void closeAdmission();
    size_t pending() const override;
    std::optional<Phase> phase(TaskId id) const;

private:
    void execute(TaskId id);
    void deliver(TaskId id);
    struct Record {
        Phase phase;
        std::shared_ptr<Task> task;
    };
    const Dispatch m_dispatch;
    mutable std::mutex m_mutex;
    std::map<TaskId, Record> m_tasks;
    uint64_t m_next_id{0};
    bool m_closed{false};
};
} // namespace execution

#endif // BITCOIN_QML_EXECUTION_H
