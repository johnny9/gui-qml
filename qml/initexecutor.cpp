// Copyright (c) 2014-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/initexecutor.h>

#include <interfaces/node.h>
#include <util/threadnames.h>

#include <QString>

QmlInitExecutor::QmlInitExecutor(interfaces::Node& node)
    : QObject(), m_node(node)
{
    connect(&m_backend, &BackendExecutor::drained, this, &QmlInitExecutor::finishShutdown);
    connect(&m_control, &BackendExecutor::drained, this, &QmlInitExecutor::finishShutdown);
}

QmlInitExecutor::~QmlInitExecutor() = default;

void QmlInitExecutor::handleRunawayException(std::exception_ptr error)
{
    QString message{tr("Unknown exception")};
    try {
        std::rethrow_exception(error);
    } catch (const std::exception& e) {
        message = QString::fromUtf8(e.what());
    } catch (...) {
    }
    Q_EMIT runawayException(message);
}

void QmlInitExecutor::initialize()
{
    struct Result {
        bool success;
        interfaces::BlockAndHeaderTipInfo tip;
    };
    m_backend.submit(this, [node = &m_node] {
        util::ThreadRename("qml-init");
        Result result{};
        result.success = node->appInitMain(&result.tip);
        return result;
    }, [this](Result result) {
        Q_EMIT initializeResult(result.success, result.tip);
    }, [this](std::exception_ptr error) { handleRunawayException(error); });
}

void QmlInitExecutor::interrupt()
{
    if (m_interrupt_requested) return;
    m_interrupt_requested = true;
    m_control.submit(this, [node = &m_node] {
        util::ThreadRename("qml-control");
        node->startShutdown();
    }, [this] { Q_EMIT interruptResult(); }, [this](std::exception_ptr error) {
        handleRunawayException(error);
        Q_EMIT interruptResult();
    });
}

void QmlInitExecutor::shutdown()
{
    if (m_shutdown_requested) return;
    m_shutdown_requested = true;
    m_backend.submit(this, [node = &m_node] { node->appShutdown(); }, [this] {
        m_shutdown_complete = true;
        m_backend.shutdown();
        m_control.shutdown();
    }, [this](std::exception_ptr error) { handleRunawayException(error); });
}

void QmlInitExecutor::finishShutdown()
{
    if (!m_shutdown_complete || m_shutdown_emitted || !m_backend.isDrained() || !m_control.isDrained()) return;
    m_shutdown_emitted = true;
    Q_EMIT shutdownResult();
}
