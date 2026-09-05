// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/wallet/walletoperationexecutor.h>

#include <utility>

WalletOperationResult WalletOperationResult::failure(Code code, QString error)
{
    WalletOperationResult result;
    result.code = code;
    result.error = std::move(error);
    return result;
}

WalletOperationExecutor::WalletOperationExecutor(QObject* parent)
    : QObject(parent), m_worker(new QObject)
{
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_thread.start();
}

WalletOperationExecutor::~WalletOperationExecutor()
{
    // Normal shutdown calls drain() and keeps the GUI event loop running until
    // drained(). Waiting here is only the exception/startup-failure fallback.
    m_thread.quit();
    m_thread.wait();
}

bool WalletOperationExecutor::submit(Work work, Completion completion)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!m_accepting) return false;
    return QMetaObject::invokeMethod(m_worker, [this, work = std::move(work), completion = std::move(completion)]() mutable {
        WalletOperationResult result;
        try {
            result = work();
        } catch (...) {
            // Exceptions may include operation inputs (including credentials).
            // Expected, user-facing failures must use explicit typed results.
            result = WalletOperationResult::failure(WalletOperationResult::CoreError, tr("The wallet operation failed."));
        }
        // Release captured backend handles and operation-only secrets before
        // reporting completion or processing the drain barrier.
        work = {};
        QMetaObject::invokeMethod(this, [completion = std::move(completion), result = std::move(result)]() mutable {
            completion(std::move(result));
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}

void WalletOperationExecutor::drain()
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!m_accepting) return;
    m_accepting = false;
    QMetaObject::invokeMethod(m_worker, [this] {
        QMetaObject::invokeMethod(this, [this] { Q_EMIT drained(); }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}
