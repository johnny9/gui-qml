// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_WALLET_WALLETSESSION_H
#define BITCOIN_QML_WALLET_WALLETSESSION_H

#include <qml/wallet/walletoperationexecutor.h>
#include <uint256.h>

#include <QObject>
#include <QTimer>
#include <memory>
#include <vector>

namespace interfaces { class Handler; class Wallet; }

/** Owns exactly one Core wallet. Feature models borrow this session, never a selector. */
class WalletSession : public QObject
{
    Q_OBJECT
public:
    WalletSession(std::shared_ptr<interfaces::Wallet> wallet, quint64 id, WalletOperationExecutor& executor, QObject* parent = nullptr);
    ~WalletSession() override;
    interfaces::Wallet& wallet() const;
    quint64 id() const { return m_id; }
    quint64 generation() const { return m_generation; }
    bool available() const { return m_available; }
    bool actionBusy() const { return m_action_busy; }
    QString name() const { return m_name; }
    QString identity() const { return m_identity.isEmpty() ? m_name : m_identity; }
    void invalidate();
    bool runAction(std::function<WalletOperationResult(interfaces::Wallet&)> work, WalletOperationExecutor::Completion completion);
    // Serialized backend reads do not acquire a write action or emit changed().
    bool runRead(std::function<WalletOperationResult(interfaces::Wallet&)> work, WalletOperationExecutor::Completion completion);
Q_SIGNALS:
    void changed();
    void activityChanged();
    void addressesChanged();
    void invalidated();
    void actionBusyChanged();
private Q_SLOTS:
    void pollProcessedTip(bool initial = false);
private:
    friend class WalletManager;
    friend class WalletHistoryIntegrationTests; // Control poll timing without a public testing API.
    std::shared_ptr<interfaces::Wallet> m_wallet;
    const quint64 m_id;
    quint64 m_generation{1};
    const QString m_name;
    QString m_identity;
    WalletOperationExecutor& m_executor;
    bool m_available{true};
    bool m_action_busy{false};
    bool m_tip_read_pending{false};
    uint256 m_processed_tip;
    QTimer m_tip_timer;
    std::vector<std::unique_ptr<interfaces::Handler>> m_handlers;
};

#endif // BITCOIN_QML_WALLET_WALLETSESSION_H
