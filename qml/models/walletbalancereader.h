// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_WALLETBALANCEREADER_H
#define BITCOIN_QML_MODELS_WALLETBALANCEREADER_H

#include <qml/execution.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

namespace walletbalance {
enum class Status { Ready, Busy, Unavailable, Failed, Rejected, Closed };
struct Snapshot {
    int64_t trusted{0};
    std::string observed_block;
};
struct Result {
    Status status{Status::Unavailable};
    std::optional<Snapshot> snapshot;
};

/** One opening's replaceable observation, not a wallet/session authority.
 * The injected read runs on Core; its result is consumed on Domain. The GUI
 * takes an owned value from a bounded single-slot publication mailbox.
 */
class Reader : public std::enable_shared_from_this<Reader>
{
public:
    using Read = std::function<Result()>;
    // Only enqueue a GUI wakeup. May be called from any submitting thread.
    using Notify = std::function<void()>;
    Reader(std::shared_ptr<execution::Scheduler> scheduler, Read read, Notify notify,
           std::shared_ptr<std::atomic_bool> closed);
    execution::Rejection refresh();
    void close();
    std::optional<Result> take();

private:
    struct Attempt { bool started{false}; };
    // Call only after reserving m_active under m_mutex; never submit while locked.
    execution::Rejection submitReserved();
    void complete(const std::shared_ptr<Attempt>& attempt, Result result);
    void publish(Result result);
    const std::shared_ptr<execution::Scheduler> m_scheduler;
    const Read m_read;
    const Notify m_notify;
    const std::shared_ptr<std::atomic_bool> m_closed;
    const execution::ResourceKey m_key;
    // Never held across Core, scheduler, transport or observer calls.
    std::mutex m_mutex;
    std::optional<Snapshot> m_last_snapshot;
    std::optional<Result> m_latest;
    bool m_dirty{false}, m_active{false}, m_notification_pending{false};
};
} // namespace walletbalance

#endif // BITCOIN_QML_MODELS_WALLETBALANCEREADER_H
