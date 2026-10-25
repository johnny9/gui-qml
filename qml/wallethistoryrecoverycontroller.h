// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_WALLETHISTORYRECOVERYCONTROLLER_H
#define BITCOIN_QML_WALLETHISTORYRECOVERYCONTROLLER_H

#include <qml/wallethistoryrecoverybackend.h>
#include <support/allocators/secure.h>

#include <QObject>
#include <QString>
#include <QTimer>

#include <functional>
#include <future>
#include <memory>
#include <optional>

class BackendExecutor;
namespace interfaces {
class Node;
class Wallet;
}

class WalletHistoryRecoveryController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled NOTIFY changed)
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(bool incomplete READ incomplete NOTIFY changed)
    Q_PROPERTY(bool visible READ visible NOTIFY changed)
    Q_PROPERTY(bool indeterminate READ indeterminate NOTIFY changed)
    Q_PROPERTY(bool needsUnlock READ needsUnlock NOTIFY changed)
    Q_PROPERTY(double progress READ progress NOTIFY changed)
    Q_PROPERTY(QString state READ state NOTIFY changed)
    Q_PROPERTY(QString statusText READ statusText NOTIFY changed)
    Q_PROPERTY(QString errorText READ errorText NOTIFY changed)

public:
    using Work = std::function<wallet_history::Result(const std::shared_ptr<wallet_history::Control>&,
                                                     std::optional<SecureString>)>;
    WalletHistoryRecoveryController(interfaces::Node* node, std::weak_ptr<interfaces::Wallet> wallet,
                                    const QString& wallet_name, QObject* parent = nullptr);
    explicit WalletHistoryRecoveryController(Work work, QObject* parent = nullptr);
    ~WalletHistoryRecoveryController() override;

    void start();
    void shutdown();
    bool isDrained() const { return m_drained; }
    std::shared_future<void> completion() const { return m_completion; }
    bool enabled() const;
    bool active() const { return m_running; }
    bool incomplete() const;
    bool visible() const;
    bool indeterminate() const;
    bool needsUnlock() const { return m_progress.needs_unlock; }
    double progress() const;
    QString state() const;
    QString statusText() const;
    QString errorText() const { return m_error; }

    Q_INVOKABLE void cancel();
    Q_INVOKABLE void retry();
    Q_INVOKABLE void unlockAndRetry(const QString& passphrase);

Q_SIGNALS:
    void changed();
    void finished();
    void drained();

private:
    void run(std::optional<SecureString> passphrase = std::nullopt);
    void poll();
    void complete(wallet_history::Result result);

    Work m_work;
    std::shared_future<void> m_completion;
    std::unique_ptr<BackendExecutor> m_executor;
    std::shared_ptr<wallet_history::Control> m_control;
    wallet_history::Progress m_progress;
    QTimer m_poll_timer;
    QTimer m_retry_timer;
    QTimer m_completion_timer;
    QString m_error;
    bool m_started{false};
    bool m_running{false};
    bool m_cancelled{false};
    bool m_stopping{false};
    bool m_drained{true};
    bool m_hide_complete{false};
};

#endif // BITCOIN_QML_WALLETHISTORYRECOVERYCONTROLLER_H
