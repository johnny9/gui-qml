// Copyright (c) 2014-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/initexecutor.h>
#include <qml/retainedvalue.h>

#ifdef ENABLE_TEST_AUTOMATION
#include <common/args.h>
#include <stdexcept>
#endif

#include <interfaces/handler.h>
#include <interfaces/node.h>
#include <util/exception.h>
#include <util/threadnames.h>

#include <QString>
#include <QPointer>
#include <QScopeGuard>

#include <atomic>
#include <cstring>
#include <utility>

struct QmlInitExecutor::WorkerState {
    SubscriptionFactory pending_subscription_factory;
    std::unique_ptr<interfaces::Handler> subscription;
    std::atomic_bool stopping{false};
    bool initialization_threw{false};

    explicit WorkerState(SubscriptionFactory subscribe) : pending_subscription_factory(std::move(subscribe)) {}
    void retireSubscription()
    {
        subscription.reset();
        pending_subscription_factory = {};
    }
};

QmlInitExecutor::QmlInitExecutor(interfaces::Node& node, SubscriptionFactory subscribe, std::shared_ptr<std::atomic_bool> diagnostic_loss)
    : QObject(), m_node(node), m_worker_state(std::make_shared<WorkerState>(std::move(subscribe))), m_diagnostic_loss(std::move(diagnostic_loss))
{
    m_init_and_shutdown_executor.setObjectName(QStringLiteral("node-init-shutdown"));
    m_interrupt_executor.setObjectName(QStringLiteral("node-interrupt"));
    connect(&m_init_and_shutdown_executor, &BackendExecutor::drained, this, &QmlInitExecutor::finishShutdown);
    connect(&m_interrupt_executor, &BackendExecutor::drained, this, &QmlInitExecutor::finishShutdown);
}

QmlInitExecutor::~QmlInitExecutor()
{
    m_worker_state->stopping = true;
    m_init_and_shutdown_executor.submit(this, [state = std::move(m_worker_state)] { state->retireSubscription(); }, [] {});
}

namespace {
void PrepareRunawayException(QString& message, const std::shared_ptr<std::atomic_bool>& loss) noexcept
{
    // Called only inside the original Native lifecycle effect. The exception
    // itself, full conversion and Core's diagnostic I/O never cross to GUI.
    const auto error = std::current_exception();
    try {
        try { std::rethrow_exception(error); }
        catch (const std::exception& e) {
            PrintExceptionContinue(&e, "Runaway exception");
            const auto* text = e.what();
            const auto size = std::strlen(text);
            constexpr size_t MAX_BYTES{4 * 1024 * 1024};
            if (size > MAX_BYTES) {
                if (loss) *loss = true;
                message = QString::fromUtf8(text, 16384) + QStringLiteral(" ") + QObject::tr("[complete fatal diagnostic exceeds available capacity; check the node log]");
            } else message = QString::fromUtf8(text, static_cast<qsizetype>(size));
        }
        catch (...) { PrintExceptionContinue(nullptr, "Runaway exception"); message = QObject::tr("Unknown exception"); }
    } catch (...) {
        if (loss) *loss = true;
        message = QObject::tr("The fatal error diagnostic could not be prepared.");
    }
}
}

void QmlInitExecutor::handleRunawayException(const QString& message)
{
    // The direct receipt observer may retain the original capability. The
    // executor must release its publication alias even when nobody consumes
    // the fatal receipt, or an otherwise idle worker could never drain.
    const QPointer<QmlInitExecutor> observer{this};
    const auto release = qScopeGuard([observer] {
        if (observer) observer->m_fatal_diagnostic_scheduler.reset();
    });
    if (m_runaway_exception) return;
    m_runaway_exception = true;
    Q_EMIT runawayException(message);
}

void QmlInitExecutor::diagnosticTransportFailed(std::shared_ptr<execution::Scheduler> original_diagnostic_scope)
{
    m_fatal_diagnostic_scheduler = std::move(original_diagnostic_scope);
    if (m_diagnostic_loss) *m_diagnostic_loss = true;
    handleRunawayException(tr("The node lifecycle operation or its diagnostic could not be queued."));
}

void QmlInitExecutor::initialize()
{
    if (m_initialize_requested || m_shutdown_requested || m_runaway_exception) return;
    m_initialize_requested = true;
    struct Result {
        bool success;
        interfaces::BlockAndHeaderTipInfo tip;
        bool initial_block_download;
        bool shutdown_requested;
        bool fatal{false};
    };
    auto diagnostic_scope = m_init_and_shutdown_executor.scheduler()->openActionScope();
    auto diagnostic = execution::RetainValues<QString>(diagnostic_scope, [] { return std::make_unique<QString>(); });
    if (!diagnostic) { diagnosticTransportFailed(diagnostic_scope); return; }
    const auto accepted = m_init_and_shutdown_executor.submit(this, [node = &m_node, state = m_worker_state, diagnostic, loss = m_diagnostic_loss] {
        try {
            util::ThreadRename("qml-init");
            Result result{};
            result.success = node->appInitMain(&result.tip);
#ifdef ENABLE_TEST_AUTOMATION
            if (result.success && gArgs.IsArgSet("-test-automation") && gArgs.GetArg("-test-fatal-exception", "") == "initialize") {
                throw std::runtime_error("Test fatal exception after initialization");
            }
#endif
            if (result.success && state->pending_subscription_factory && !state->stopping && !node->shutdownRequested()) {
                auto subscribe_once{std::exchange(state->pending_subscription_factory, {})};
                state->subscription = subscribe_once();
            }
            result.initial_block_download = result.success && node->isInitialBlockDownload();
            result.shutdown_requested = node->shutdownRequested();
            return result;
        } catch (...) {
            // Shutdown may already be queued behind this task before the GUI
            // receives runawayException. Never clean up a fatally failed node.
            state->initialization_threw = true;
            PrepareRunawayException(*diagnostic, loss);
            Result result{}; result.fatal = true; return result;
        }
    }, [this, diagnostic, diagnostic_scope](Result result) {
        if (result.fatal) {
            m_fatal_diagnostic_scheduler = diagnostic_scope;
            handleRunawayException(*diagnostic);
        }
        else if (!m_runaway_exception) {
            Q_EMIT initializeResult(result.success, result.tip, result.initial_block_download, result.shutdown_requested);
        }
    }, [this, diagnostic_scope](std::exception_ptr) { diagnosticTransportFailed(diagnostic_scope); });
    if (!accepted) diagnosticTransportFailed(diagnostic_scope);
}

void QmlInitExecutor::interrupt()
{
    if (m_interrupt_requested || m_runaway_exception) return;
    m_interrupt_requested = true;
    m_worker_state->stopping = true;
    auto diagnostic_scope = m_interrupt_executor.scheduler()->openActionScope();
    auto diagnostic = execution::RetainValues<QString>(diagnostic_scope, [] { return std::make_unique<QString>(); });
    if (!diagnostic) { diagnosticTransportFailed(diagnostic_scope); return; }
    const auto accepted = m_interrupt_executor.submit(this, [node = &m_node, diagnostic, loss = m_diagnostic_loss] {
        try {
            util::ThreadRename("qml-control");
            node->startShutdown();
#ifdef ENABLE_TEST_AUTOMATION
            if (gArgs.IsArgSet("-test-automation") && gArgs.GetArg("-test-fatal-exception", "") == "interrupt") {
                throw std::runtime_error("Test fatal exception during interruption");
            }
#endif
            return true;
        } catch (...) { PrepareRunawayException(*diagnostic, loss); return false; }
    }, [this, diagnostic, diagnostic_scope](bool success) {
        if (!success) {
            m_fatal_diagnostic_scheduler = diagnostic_scope;
            handleRunawayException(*diagnostic);
        }
        else if (!m_runaway_exception) Q_EMIT interruptResult();
    }, [this, diagnostic_scope](std::exception_ptr) { diagnosticTransportFailed(diagnostic_scope); });
    if (!accepted) diagnosticTransportFailed(diagnostic_scope);
}

void QmlInitExecutor::shutdown()
{
    if (m_shutdown_requested || m_runaway_exception) return;
    m_shutdown_requested = true;
    m_worker_state->stopping = true;
    auto diagnostic_scope = m_init_and_shutdown_executor.scheduler()->openActionScope();
    auto diagnostic = execution::RetainValues<QString>(diagnostic_scope, [] { return std::make_unique<QString>(); });
    if (!diagnostic) { diagnosticTransportFailed(diagnostic_scope); return; }
    const auto accepted = m_init_and_shutdown_executor.submit(this, [node = &m_node, state = m_worker_state, diagnostic, loss = m_diagnostic_loss] {
        if (state->initialization_threw) return 0;
        try {
#ifdef ENABLE_TEST_AUTOMATION
            if (gArgs.IsArgSet("-test-automation") && gArgs.GetArg("-test-fatal-exception", "") == "shutdown") {
                throw std::runtime_error("Test fatal exception before shutdown");
            }
#endif
            state->retireSubscription();
            node->appShutdown();
            return 1;
        } catch (...) { PrepareRunawayException(*diagnostic, loss); return -1; }
    }, [this, diagnostic, diagnostic_scope](int completed) {
        if (completed < 0) {
            m_fatal_diagnostic_scheduler = diagnostic_scope;
            handleRunawayException(*diagnostic);
            return;
        }
        if (!completed) return;
        m_app_shutdown_complete = true;
        m_init_and_shutdown_executor.shutdown();
        m_interrupt_executor.shutdown();
    }, [this, diagnostic_scope](std::exception_ptr) { diagnosticTransportFailed(diagnostic_scope); });
    if (!accepted) diagnosticTransportFailed(diagnostic_scope);
}

void QmlInitExecutor::finishShutdown()
{
    if (m_runaway_exception || !m_app_shutdown_complete || m_shutdown_result_emitted) return;
    if (!m_init_and_shutdown_executor.isDrained() || !m_interrupt_executor.isDrained()) return;
    m_shutdown_result_emitted = true;
    Q_EMIT shutdownResult();
}
