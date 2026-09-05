// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_WALLET_WALLETSESSION_H
#define BITCOIN_QML_WALLET_WALLETSESSION_H

#include <qml/wallet/walletoperationexecutor.h>

#include <QObject>
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
Q_SIGNALS:
    void changed();
    void activityChanged();
    void addressesChanged();
    void invalidated();
    void actionBusyChanged();
private:
    friend class WalletManager;
    std::shared_ptr<interfaces::Wallet> m_wallet;
    const quint64 m_id;
    quint64 m_generation{1};
    const QString m_name;
    QString m_identity;
    WalletOperationExecutor& m_executor;
    bool m_available{true};
    bool m_action_busy{false};
    std::vector<std::unique_ptr<interfaces::Handler>> m_handlers;
};

#endif // BITCOIN_QML_WALLET_WALLETSESSION_H
