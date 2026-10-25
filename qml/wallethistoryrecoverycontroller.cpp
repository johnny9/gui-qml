// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/wallethistoryrecoverycontroller.h>

#include <interfaces/wallet.h>
#include <interfaces/node.h>
#include <qml/backendexecutor.h>
#include <qml/models/walletunlock.h>
#include <qml/util.h>

#include <QThread>
#include <QScopeGuard>
#include <QPointer>

#include <algorithm>
#include <exception>
#include <utility>

using wallet_history::Outcome;
using wallet_history::Phase;

WalletHistoryRecoveryController::WalletHistoryRecoveryController(
    interfaces::Node* node, std::weak_ptr<interfaces::Wallet> wallet,
    const QString& wallet_name, QObject* parent)
    : WalletHistoryRecoveryController(node && node->context() ? Work{[node, wallet, name = wallet_name.toStdString()](
          const std::shared_ptr<wallet_history::Control>& control, std::optional<SecureString> passphrase) {
          const auto handle = wallet.lock();
          if (!handle) return wallet_history::Result{Outcome::Cancelled, {}};
          bool relock{false};
          if (passphrase) {
              const auto unlocked = TryUnlockWithPassphrase(*handle, *passphrase);
              passphrase.reset();
              if (unlocked == WalletUnlockResult::IncorrectPassphrase) {
                  control->update({Phase::WaitingForWallet, 0, 0, {}, true});
                  return wallet_history::Result{Outcome::Failed, "The wallet password is incorrect."};
              }
              relock = unlocked == WalletUnlockResult::UnlockedNowRelockRequired;
          }
          WalletRelockGuard guard{*handle, [] {}, relock};
          control->wallet_unlocked = !handle->isLocked();
          return wallet_history::Recover(*node, *handle, name, control);
      }} : Work{}, parent)
{
}

WalletHistoryRecoveryController::WalletHistoryRecoveryController(Work work, QObject* parent)
    : QObject(parent), m_work(std::move(work))
{
    setObjectName(QStringLiteral("walletHistoryRecoveryController"));
    m_poll_timer.setInterval(100);
    connect(&m_poll_timer, &QTimer::timeout, this, &WalletHistoryRecoveryController::poll);
    m_retry_timer.setSingleShot(true);
    m_retry_timer.setInterval(2000);
    connect(&m_retry_timer, &QTimer::timeout, this, [this] { run(); });
    m_completion_timer.setSingleShot(true);
    m_completion_timer.setInterval(5000);
    connect(&m_completion_timer, &QTimer::timeout, this, [this] {
        m_hide_complete = true;
        Q_EMIT changed();
    });
}

WalletHistoryRecoveryController::~WalletHistoryRecoveryController()
{
    shutdown();
}

void WalletHistoryRecoveryController::start()
{
    if (m_started || m_stopping || !m_work) return;
    m_started = true;
    run();
}

void WalletHistoryRecoveryController::run(std::optional<SecureString> passphrase)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_running || m_stopping || m_cancelled || !m_work) return;
    if (!m_executor) {
        m_executor = std::make_unique<BackendExecutor>();
        m_executor->setObjectName(QStringLiteral("wallet-history-recovery"));
        m_drained = false;
        connect(m_executor.get(), &BackendExecutor::drained, this, [this] {
            m_running = false;
            m_drained = true;
            const QPointer<WalletHistoryRecoveryController> guard{this};
            Q_EMIT changed();
            if (guard) Q_EMIT drained();
        });
    }
    m_control = std::make_shared<wallet_history::Control>();
    m_running = true;
    m_error.clear();
    m_hide_complete = false;
    m_completion_timer.stop();
    if (m_progress.phase == Phase::Failed || m_progress.phase == Phase::Cancelled) m_progress = {};
    m_poll_timer.start();
    const auto completion = std::make_shared<std::promise<void>>();
    m_completion = completion->get_future().share();
    const QPointer<WalletHistoryRecoveryController> guard{this};
    Q_EMIT changed();
    if (!guard) {
        completion->set_value();
        return;
    }
    const bool submitted = m_executor->submit(this, [work = m_work, control = m_control, completion, passphrase = std::move(passphrase)]() mutable {
        const auto finished = qScopeGuard([&] { completion->set_value(); });
        return work(control, std::move(passphrase));
    }, [this](wallet_history::Result result) {
        complete(std::move(result));
    }, [this](std::exception_ptr exception) {
        QString error{tr("Wallet history recovery failed.")};
        try {
            std::rethrow_exception(exception);
        } catch (const std::exception& e) {
            error = QString::fromUtf8(e.what());
        } catch (...) {
        }
        complete({Outcome::Failed, error.toStdString()});
    });
    if (!submitted) {
        completion->set_value();
        m_running = false;
        m_poll_timer.stop();
        if (!m_stopping) complete({Outcome::Failed, tr("Wallet history recovery is stopping.").toStdString()});
    }
}

void WalletHistoryRecoveryController::poll()
{
    if (!m_control || m_stopping || m_cancelled) return;
    const auto latest = m_control->snapshot();
    if (latest.phase == m_progress.phase && latest.current == m_progress.current &&
        latest.total == m_progress.total && latest.detail == m_progress.detail &&
        latest.needs_unlock == m_progress.needs_unlock) return;
    m_progress = latest;
    Q_EMIT changed();
}

void WalletHistoryRecoveryController::complete(wallet_history::Result result)
{
    poll();
    m_poll_timer.stop();
    m_running = false;
    if (m_cancelled || result.outcome == Outcome::Cancelled) {
        m_progress.phase = Phase::Cancelled;
    } else {
        switch (result.outcome) {
        case Outcome::Complete:
            m_progress.phase = Phase::Complete;
            m_completion_timer.start();
            break;
        case Outcome::Disabled: m_progress.phase = Phase::Disabled; break;
        case Outcome::Retry: m_retry_timer.start(); break;
        case Outcome::Failed:
            m_progress.phase = Phase::Failed;
            m_error = QString::fromStdString(result.error);
            break;
        case Outcome::Cancelled: break;
        }
    }
    const QPointer<WalletHistoryRecoveryController> guard{this};
    Q_EMIT changed();
    if (guard) Q_EMIT finished();
}

void WalletHistoryRecoveryController::cancel()
{
    if (m_stopping || !incomplete()) return;
    m_cancelled = true;
    m_retry_timer.stop();
    if (m_control) m_control->cancelled = true;
    m_progress.phase = Phase::Cancelled;
    m_error.clear();
    Q_EMIT changed();
}

void WalletHistoryRecoveryController::retry()
{
    if (m_stopping || m_running) return;
    m_started = true;
    m_cancelled = false;
    m_retry_timer.stop();
    run();
}

void WalletHistoryRecoveryController::unlockAndRetry(const QString& passphrase)
{
    if (m_stopping || m_running || !needsUnlock()) return;
    m_cancelled = false;
    m_retry_timer.stop();
    run(QmlUtil::SecureStringFromQString(passphrase));
}

void WalletHistoryRecoveryController::shutdown()
{
    if (m_stopping) return;
    m_stopping = true;
    m_poll_timer.stop();
    m_retry_timer.stop();
    m_completion_timer.stop();
    if (m_control) m_control->cancelled = true;
    if (m_executor) m_executor->shutdown();
}

bool WalletHistoryRecoveryController::enabled() const
{
    return m_started && m_progress.phase != Phase::Disabled && m_progress.phase != Phase::Checking;
}

bool WalletHistoryRecoveryController::incomplete() const
{
    return m_started && m_progress.phase != Phase::Disabled && m_progress.phase != Phase::Complete;
}

bool WalletHistoryRecoveryController::visible() const
{
    return m_started && m_progress.phase != Phase::Disabled && !m_hide_complete;
}

bool WalletHistoryRecoveryController::indeterminate() const
{
    return m_progress.total <= 0 && m_progress.phase != Phase::Complete;
}

double WalletHistoryRecoveryController::progress() const
{
    if (m_progress.phase == Phase::Complete) return 1;
    return m_progress.total > 0 ? std::clamp(double(m_progress.current) / m_progress.total, 0.0, 1.0) : 0;
}

QString WalletHistoryRecoveryController::state() const
{
    if (!m_started) return QStringLiteral("disabled");
    switch (m_progress.phase) {
    case Phase::Checking: return QStringLiteral("checking");
    case Phase::WaitingForNode: return QStringLiteral("waiting-node");
    case Phase::WaitingForFilters: return QStringLiteral("waiting-filters");
    case Phase::WaitingForWallet: return QStringLiteral("waiting-wallet");
    case Phase::Scanning: return QStringLiteral("scanning");
    case Phase::Downloading: return QStringLiteral("downloading");
    case Phase::Rescanning: return QStringLiteral("rescanning");
    case Phase::Complete: return QStringLiteral("complete");
    case Phase::Disabled: return QStringLiteral("disabled");
    case Phase::Failed: return QStringLiteral("error");
    case Phase::Cancelled: return QStringLiteral("cancelled");
    }
    return {};
}

QString WalletHistoryRecoveryController::statusText() const
{
    switch (m_progress.phase) {
    case Phase::Checking: return tr("Checking wallet history…");
    case Phase::WaitingForNode: return tr("Waiting for the node to finish syncing…");
    case Phase::WaitingForFilters: return tr("Waiting for the block filter index…");
    case Phase::WaitingForWallet:
        return needsUnlock() ? tr("Unlock the wallet to recover its history.") : tr("Waiting for the wallet…");
    case Phase::Scanning: return tr("Searching wallet history: %1 of %2 blocks").arg(m_progress.current).arg(m_progress.total);
    case Phase::Downloading: return tr("Downloading wallet history: %1 of %2 blocks").arg(m_progress.current).arg(m_progress.total);
    case Phase::Rescanning: return tr("Recovering wallet transactions: %1 of %2 blocks").arg(m_progress.current).arg(m_progress.total);
    case Phase::Complete: return tr("Wallet history recovered");
    case Phase::Failed: return tr("Wallet history is incomplete");
    case Phase::Cancelled: return m_running ? tr("Stopping wallet history recovery…") : tr("Wallet history recovery paused");
    case Phase::Disabled: return {};
    }
    return {};
}
