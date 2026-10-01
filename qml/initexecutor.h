// Copyright (c) 2014-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_INITEXECUTOR_H
#define BITCOIN_QML_INITEXECUTOR_H

#include <interfaces/node.h>

#include <exception>
#include <functional>
#include <memory>

#include <QObject>
#include <QThread>

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

    /**
     * The optional factory runs once on the worker after successful node init,
     * before initializeResult. Its token is released on that worker before Core
     * shutdown, including when the executor exits without an explicit shutdown.
     */
    explicit QmlInitExecutor(interfaces::Node& node, SubscriptionFactory subscribe = {});
    ~QmlInitExecutor();

public Q_SLOTS:
    void initialize();
    void shutdown();

Q_SIGNALS:
    void initializeResult(bool success, interfaces::BlockAndHeaderTipInfo tip_info);
    void shutdownResult();
    void runawayException(const QString& message);

private:
    void handleRunawayException(const std::exception* e);
    void prepareShutdown();

    interfaces::Node& m_node;
    SubscriptionFactory m_subscription_factory;
    std::unique_ptr<interfaces::Handler> m_subscription; // Only accessed by the worker.
    QObject m_context;
    QThread m_thread;
    bool m_shutdown_started{false}; // Only accessed by the worker.
};

#endif // BITCOIN_QML_INITEXECUTOR_H
