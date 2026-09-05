// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_WALLET_WALLETOPERATIONEXECUTOR_H
#define BITCOIN_QML_WALLET_WALLETOPERATIONEXECUTOR_H

#include <QObject>
#include <QStringList>
#include <QThread>

#include <functional>
#include <memory>

namespace interfaces { class Wallet; }

struct WalletOperationResult {
    enum Code { Success, Busy, Unavailable, InvalidInput, CoreError };
    Code code{Success};
    quint64 operation_id{0};
    QString error;
    QStringList warnings;
    QStringList companions;
    QString backup_path;
    std::shared_ptr<interfaces::Wallet> wallet;

    static WalletOperationResult failure(Code code, QString error);
};

/** Serialized, GUI-delivered wallet operations. Stop accepting work before Core shutdown. */
class WalletOperationExecutor : public QObject
{
    Q_OBJECT
public:
    using Work = std::function<WalletOperationResult()>;
    using Completion = std::function<void(WalletOperationResult)>;
    explicit WalletOperationExecutor(QObject* parent = nullptr);
    ~WalletOperationExecutor() override;
    bool submit(Work work, Completion completion);
    void drain();
Q_SIGNALS:
    void drained();
private:
    QThread m_thread;
    QObject* m_worker;
    bool m_accepting{true};
};

#endif // BITCOIN_QML_WALLET_WALLETOPERATIONEXECUTOR_H
