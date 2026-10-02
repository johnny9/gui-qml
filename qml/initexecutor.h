// Copyright (c) 2014-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_INITEXECUTOR_H
#define BITCOIN_QML_INITEXECUTOR_H

#include <interfaces/node.h>
#include <qml/backendexecutor.h>

#include <exception>

#include <QObject>

QT_BEGIN_NAMESPACE
class QString;
QT_END_NAMESPACE

/** Runs app initialization and shutdown work off the GUI thread. */
class QmlInitExecutor : public QObject
{
    Q_OBJECT
public:
    explicit QmlInitExecutor(interfaces::Node& node);
    ~QmlInitExecutor();

public Q_SLOTS:
    void initialize();
    /** Runs interruption independently of a potentially blocked initialize job. */
    void interrupt();
    void shutdown();

Q_SIGNALS:
    void initializeResult(bool success, interfaces::BlockAndHeaderTipInfo tip_info);
    void interruptResult();
    void shutdownResult();
    void runawayException(const QString& message);

private:
    void handleRunawayException(std::exception_ptr error);
    void finishShutdown();

    interfaces::Node& m_node;
    BackendExecutor m_backend;
    BackendExecutor m_control;
    bool m_interrupt_requested{false};
    bool m_shutdown_requested{false};
    bool m_shutdown_complete{false};
    bool m_shutdown_emitted{false};
};

#endif // BITCOIN_QML_INITEXECUTOR_H
