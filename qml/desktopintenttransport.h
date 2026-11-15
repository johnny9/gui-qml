// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_DESKTOPINTENTTRANSPORT_H
#define BITCOIN_QML_DESKTOPINTENTTRANSPORT_H

#include <qml/desktopintentowner.h>

#include <functional>
#include <memory>

namespace desktopintent::transport {
enum class StartStatus { Primary, Forwarded, Rejected, Unknown };
struct StartResult {
    StartStatus status{StartStatus::Rejected};
    QString error;
    std::vector<Receipt> receipts;
};

/** One process-lifetime native endpoint. Filesystem, local sockets, locks and
 * waits run on Native. Its QObject remains on the original Native event loop;
 * a scheduler borrow survives until that object's physical destruction.
 * completed/retired callbacks run on Domain and must not access GUI objects.
 * start() attempts once; a lost forwarding ACK authorizes only original-key
 * queries. Missing/evicted receipts remain Unknown, without replay.
 */
class Service final {
public:
    static std::shared_ptr<Service> Create(std::shared_ptr<execution::Scheduler> scheduler, std::shared_ptr<Owner> owner);
    ~Service();
    execution::Admission start(QString profile_directory, QString network, std::vector<Input> inputs,
                               std::function<void(StartResult)> completed);
    // Admission means stop was requested. retired is delivered only after
    // Native sockets/server/lock are physically gone. Destruction also stops.
    execution::Admission stop(std::function<void()> retired = {});
    bool retired() const;
private:
    struct State;
    explicit Service(std::shared_ptr<State> state);
    const std::shared_ptr<State> m_state;
};
} // namespace desktopintent::transport
#endif // BITCOIN_QML_DESKTOPINTENTTRANSPORT_H
