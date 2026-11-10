// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_WALLETLIFECYCLE_H
#define BITCOIN_QML_WALLETLIFECYCLE_H

#include <qml/execution.h>
#include <qml/walletlifecyclestate.h>

#include <interfaces/handler.h>
#include <interfaces/wallet.h>

#include <atomic>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace walletlife {
/** One existing catalog subscription, not the full wallet/session model. */
class LoadSubscription : public std::enable_shared_from_this<LoadSubscription>
{
    struct ReturnPacket;
public:
    struct Entry { std::string name; std::shared_ptr<interfaces::Wallet> wallet; bool select_wallet{true}; };
    using Callback = std::function<void(std::unique_ptr<interfaces::Wallet>)>;
    using Register = std::function<std::unique_ptr<interfaces::Handler>(Callback)>;
    using Capture = std::function<std::vector<std::unique_ptr<interfaces::Wallet>>()>;
    // Called on Domain. The implementation transports only this value to GUI.
    using Notify = std::function<void(Update)>;
    struct Handoff {
        ~Handoff();
        std::vector<Entry> entries;
        bool more{false};
    private:
        friend class LoadSubscription;
        Handoff() = default;
        Handoff(std::shared_ptr<LoadSubscription> owner, std::shared_ptr<ReturnPacket> packet,
                execution::TaskId disposal, std::optional<execution::ResourceLease> lease)
            : m_owner(std::move(owner)), m_packet(std::move(packet)), m_disposal(disposal), m_lease(std::move(lease)) {}
        std::shared_ptr<LoadSubscription> m_owner;
        std::shared_ptr<ReturnPacket> m_packet;
        execution::TaskId m_disposal;
        std::optional<execution::ResourceLease> m_lease;
    };

    LoadSubscription(std::shared_ptr<execution::Scheduler> scheduler, Register registration, Capture capture, Notify notify);
    execution::Admission start();
    void retire();
    void setNotificationContext(std::string deferred_name, bool action_pending);
    std::unique_ptr<Handoff> takeInitial();
    std::unique_ptr<Handoff> takeNotifications();

private:
    static constexpr size_t PENDING_HANDLES{64};
    static constexpr size_t GUI_HANDOFF{16};
    void ingress(std::unique_ptr<interfaces::Wallet> wallet);
    void requestScan();
    void scan();
    void registered();
    void failed();
    bool cleanup();
    std::unique_ptr<Handoff> take(bool initial);
    void returnHandoff(std::vector<Entry> entries, std::shared_ptr<ReturnPacket> packet, execution::TaskId disposal);
    static std::vector<Entry> captureEntries(std::vector<std::unique_ptr<interfaces::Wallet>> wallets);

    const std::shared_ptr<execution::Scheduler> m_scheduler;
    const Register m_registration;
    const Capture m_capture;
    const Notify m_notify;
    const execution::ResourceKey m_key;
    State m_state; // Accessed only by Domain outcome consumers.
    std::atomic_bool m_stopping{false}, m_scan_pending{false}, m_failed{false};
    std::mutex m_mutex;
    bool m_resync{false}, m_started{false}, m_initial_ready{false}, m_handoff_disposal_pending{false};
    std::optional<execution::TaskId> m_cleanup_id;
    std::optional<execution::TaskId> m_scan_id;
    struct NotificationContext { std::string deferred_name; bool select_wallet{true}; };
    struct Pending { std::unique_ptr<interfaces::Wallet> wallet; std::shared_ptr<const NotificationContext> context; };
    std::shared_ptr<const NotificationContext> m_notification_context{std::make_shared<NotificationContext>()};
    bool m_resync_select{true};
    std::unique_ptr<interfaces::Handler> m_handler;
    std::vector<Pending> m_pending;
    std::deque<Entry> m_initial;
    std::map<std::string, Entry> m_ready;
};
} // namespace walletlife

#endif // BITCOIN_QML_WALLETLIFECYCLE_H
