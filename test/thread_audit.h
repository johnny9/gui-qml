// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_TEST_THREAD_AUDIT_H
#define BITCOIN_QML_TEST_THREAD_AUDIT_H

#include <interfaces/chain.h>
#include <interfaces/node.h>
#include <interfaces/wallet.h>

#include <QThread>

#include <atomic>
#include <memory>
#include <functional>
#include <mutex>

namespace qmlintegration {

// Bootstrap temporarily allows local setup until the runtime handoff; onboarding
// may still be visible. LocalShutdown is temporary until #981 moves shutdown
// polling to a worker. An IPC proxy never receives GUI-thread exceptions.
enum class ThreadPolicy { Worker, Bootstrap, LocalShutdown, Forbidden };
enum class TestPhase { Bootstrap, Running, Shutdown };

class ThreadAudit
{
public:
    explicit ThreadAudit(bool local = true);
    void check(const char* method, ThreadPolicy policy) const;
    void setPhase(TestPhase phase) { m_phase.store(phase); }
    void setObserver(std::function<void(const char*)> observer);

private:
    QThread* const m_gui_thread;
    const bool m_local;
    std::atomic<TestPhase> m_phase{TestPhase::Bootstrap};
    mutable std::mutex m_observer_mutex;
    std::function<void(const char*)> m_observer;
};

std::unique_ptr<interfaces::Node> CheckNode(std::unique_ptr<interfaces::Node> backend, std::shared_ptr<ThreadAudit> audit);
std::unique_ptr<interfaces::Chain> CheckChain(std::unique_ptr<interfaces::Chain> backend, std::shared_ptr<ThreadAudit> audit);
std::unique_ptr<interfaces::Wallet> CheckWallet(std::unique_ptr<interfaces::Wallet> backend, std::shared_ptr<ThreadAudit> audit);
std::unique_ptr<interfaces::WalletLoader> CheckWalletLoader(std::unique_ptr<interfaces::WalletLoader> backend, std::shared_ptr<ThreadAudit> audit);

} // namespace qmlintegration
#endif // BITCOIN_QML_TEST_THREAD_AUDIT_H
