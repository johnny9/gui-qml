// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_WALLET_WALLETMANAGER_H
#define BITCOIN_QML_WALLET_WALLETMANAGER_H

#include <qml/wallet/walletlistmodel.h>
#include <qml/wallet/walletoperationexecutor.h>
#include <qml/wallet/walletviewmodel.h>

#include <map>
#include <memory>

namespace interfaces { class Handler; class Node; class WalletLoader; }
class WalletSession;

class WalletManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(WalletViewModel* selectedWallet READ selectedWallet NOTIFY selectedWalletChanged)
    Q_PROPERTY(WalletListModel* catalog READ catalog CONSTANT)
    Q_PROPERTY(bool initialized READ initialized NOTIFY initializedChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString loadError READ loadError NOTIFY loadStatusChanged)
public:
    explicit WalletManager(interfaces::Node& node, const QString& network, QObject* parent = nullptr);
    ~WalletManager() override;
    WalletViewModel* selectedWallet() const;
    WalletListModel* catalog() { return &m_catalog; }
    bool initialized() const { return m_initialized; }
    bool busy() const { return m_operation_id != 0; }
    QString loadError() const { return m_load_error; }
    interfaces::WalletLoader& loader() const;
    QString canonicalIdentity(const QString& name) const;
    quint64 beginOperation(const QString& name, WalletOperationExecutor::Work work, WalletOperationExecutor::Completion completion);
    Q_INVOKABLE void initialize();
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void selectWallet(const QString& name);
    Q_INVOKABLE void closeWallet(const QString& name);
    void shutdown();
Q_SIGNALS:
    void initializedChanged();
    void selectedWalletChanged();
    void busyChanged();
    void loadStatusChanged();
    void migrationRequired(const QString& name);
    void drained();
private:
    struct Instance;
    WalletViewModel* publish(std::shared_ptr<interfaces::Wallet> wallet);
    void retire(const QString& identity);
    void collectRetired();
    interfaces::Node& m_node;
    QString m_network;
    WalletOperationExecutor m_executor;
    WalletListModel m_catalog;
    std::map<QString, std::unique_ptr<Instance>> m_instances;
    std::vector<std::unique_ptr<Instance>> m_retired;
    std::unique_ptr<interfaces::Handler> m_load_handler;
    QString m_selected;
    QString m_deferred_identity;
    QString m_load_error;
    quint64 m_next_session_id{1};
    quint64 m_next_operation_id{1};
    quint64 m_operation_id{0};
    bool m_initialized{false};
    bool m_stopping{false};
};

#endif // BITCOIN_QML_WALLET_WALLETMANAGER_H
