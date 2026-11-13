// Copyright (c) 2014-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_INITEXECUTOR_H
#define BITCOIN_QML_INITEXECUTOR_H

#include <interfaces/node.h>
#include <qml/backendexecutor.h>

#include <atomic>
#include <exception>
#include <functional>
#include <memory>

#include <QObject>

QT_BEGIN_NAMESPACE
class QString;
QT_END_NAMESPACE

namespace interfaces {
class Handler;
}

/** Runs app initialization and shutdown work off the GUI thread. */
class QmlInitExecutor : public QObject
{
    Q_OBJECT
public:
    using SubscriptionFactory = std::function<std::unique_ptr<interfaces::Handler>()>;

    explicit QmlInitExecutor(interfaces::Node& node, SubscriptionFactory subscribe = {},
                             std::shared_ptr<std::atomic_bool> diagnostic_loss = {});
    ~QmlInitExecutor();
    // Available during direct runawayException receipt delivery only.
    std::shared_ptr<execution::Scheduler> fatalDiagnosticScheduler() const { return m_fatal_diagnostic_scheduler; }

public Q_SLOTS:
    void initialize();
    void interrupt();
    void shutdown();

Q_SIGNALS:
    void initializeResult(bool success, interfaces::BlockAndHeaderTipInfo tip_info,
                          bool initial_block_download, bool shutdown_requested);
    void interruptResult();
    void shutdownResult();
    void runawayException(const QString& message);

private:
    void handleRunawayException(const QString& message);
    void diagnosticTransportFailed(std::shared_ptr<execution::Scheduler> original_diagnostic_scope = {});
    void finishShutdown();

    struct WorkerState;
    interfaces::Node& m_node;
    std::shared_ptr<WorkerState> m_worker_state;
    std::shared_ptr<std::atomic_bool> m_diagnostic_loss;
    BackendExecutor m_init_and_shutdown_executor;
    BackendExecutor m_interrupt_executor;
    std::shared_ptr<execution::Scheduler> m_fatal_diagnostic_scheduler;
    bool m_initialize_requested{false};
    bool m_interrupt_requested{false};
    bool m_shutdown_requested{false};
    bool m_app_shutdown_complete{false};
    bool m_shutdown_result_emitted{false};
    bool m_runaway_exception{false};
};

#endif // BITCOIN_QML_INITEXECUTOR_H
