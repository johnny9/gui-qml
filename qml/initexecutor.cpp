// Copyright (c) 2014-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/initexecutor.h>

#include <interfaces/handler.h>
#include <interfaces/node.h>
#include <util/exception.h>
#include <util/threadnames.h>

#include <utility>

#include <QDebug>
#include <QMetaObject>
#include <QObject>
#include <QString>
#include <QThread>

QmlInitExecutor::QmlInitExecutor(interfaces::Node& node, SubscriptionFactory subscribe)
    : QObject(), m_node(node), m_subscription_factory{std::move(subscribe)}
{
    m_context.moveToThread(&m_thread);
    m_thread.start();
}

QmlInitExecutor::~QmlInitExecutor()
{
    qDebug() << __func__ << ": Stopping thread";
    // Also disconnect backend subscriptions if the event loop exited without
    // an explicit shutdown request. Keep cleanup on the worker and finish it
    // before the caller releases the node/chain interfaces.
    QMetaObject::invokeMethod(&m_context, [this] {
        prepareShutdown();
        m_thread.quit();
    });
    m_thread.wait();
    qDebug() << __func__ << ": Stopped thread";
}

void QmlInitExecutor::prepareShutdown()
{
    if (m_shutdown_started) return;
    m_shutdown_started = true;
    m_subscription.reset();
}

void QmlInitExecutor::handleRunawayException(const std::exception* e)
{
    PrintExceptionContinue(e, "Runaway exception");
    Q_EMIT runawayException(e ? QString::fromUtf8(e->what()) : tr("Unknown exception"));
}

void QmlInitExecutor::initialize()
{
    QMetaObject::invokeMethod(&m_context, [this] {
        try {
            util::ThreadRename("qml-init");
            qDebug() << "Running initialization in thread";
            interfaces::BlockAndHeaderTipInfo tip_info;
            bool rv = m_node.appInitMain(&tip_info);
            if (rv && m_subscription_factory && !m_shutdown_started && !m_node.shutdownRequested()) {
                // Clear the factory before invoking it, even if registration throws.
                // The local subscription factory must use RAII while loading.
                auto subscribe{std::exchange(m_subscription_factory, {})};
                m_subscription = subscribe();
            }
            Q_EMIT initializeResult(rv, tip_info);
        } catch (const std::exception& e) {
            handleRunawayException(&e);
        } catch (...) {
            handleRunawayException(nullptr);
        }
    });
}

void QmlInitExecutor::shutdown()
{
    QMetaObject::invokeMethod(&m_context, [this] {
        try {
            qDebug() << "Running shutdown in thread";
            prepareShutdown();
            m_node.appShutdown();
            qDebug() << "Shutdown finished";
            Q_EMIT shutdownResult();
        } catch (const std::exception& e) {
            handleRunawayException(&e);
        } catch (...) {
            handleRunawayException(nullptr);
        }
    });
}
