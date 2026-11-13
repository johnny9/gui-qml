// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_WALLETLISTMODEL_H
#define BITCOIN_QML_MODELS_WALLETLISTMODEL_H

#include <interfaces/wallet.h>
#include <qml/snapshotquery.h>
#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QPair>
#include <QSet>
#include <QString>

#include <optional>

class BackendExecutor;

namespace interfaces {
class Node;
}

class WalletListModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(bool walletDirLoaded READ walletDirLoaded NOTIFY walletDirLoadedChanged)
    Q_PROPERTY(bool projectionPending READ projectionPending NOTIFY projectionChanged)
    Q_PROPERTY(QString readError READ readError NOTIFY projectionChanged)

public:
    enum class LoadState {
        Closed    = 0,
        Open      = 1,
        Loading   = 2,
        LoadError = 3,
    };
    Q_ENUM(LoadState)

    WalletListModel(interfaces::Node& node, QObject *parent = nullptr);
    WalletListModel(interfaces::Node& node, execution::SnapshotServices services, QObject* parent = nullptr);
    ~WalletListModel() override;
    void closeAdmission();
    bool projectionPending() const { return m_pending; }
    QString readError() const { return m_error; }

    enum Roles {
        NameRole = Qt::UserRole + 1,
        FormatRole,
        DisplayNameRole,
        LoadStateRole,
        ErrorMessageRole,
        BalanceRole,
        KeySchemeKindRole,
        WalletSectionRole,
        BalanceSatoshiRole,
        BalanceKnownRole,
    };

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    bool walletDirLoaded() const { return m_wallet_dir_loaded; }

Q_SIGNALS:
    void walletListChanged(bool has_wallets);
    void walletDirLoadedChanged();
    void projectionChanged();

public Q_SLOTS:
    void listWalletDir();
    void setWalletLoadState(const QString& name, LoadState state, const QString& error = {});
    void setDisplayUnit(int unit);
    void setWalletInfo(const QString& name, qint64 balance, int keySchemeKind, bool balanceKnown = true);
    void refreshDisplayNames();

private:
    struct Item;
    struct Input;
    struct InputStore;
    struct State;
    struct Snapshot;
    void enqueue(Input input);
    void publishSnapshot();
    void publishNotices(quint64 revision);
    std::shared_ptr<InputStore> m_input;
    std::shared_ptr<State> m_state;
    std::shared_ptr<const Snapshot> m_visible;
    std::shared_ptr<execution::SnapshotQuery<std::shared_ptr<const Snapshot>>> m_query;
    execution::SnapshotServices m_services;
    std::shared_ptr<QObject> m_bridge;
    std::shared_ptr<BackendExecutor> m_owned_executor;
    quint64 m_revision{0}, m_directory_epoch{0};
    size_t m_notice{0};
    bool m_pending{false}, m_stopping{false}, m_wallet_dir_loaded{false};
    QString m_error;
    interfaces::Node& m_node;
};

#endif // BITCOIN_QML_MODELS_WALLETLISTMODEL_H
