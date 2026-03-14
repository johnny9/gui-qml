// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_WALLETQMLCONTROLLER_H
#define BITCOIN_QML_WALLETQMLCONTROLLER_H

#include <qml/models/walletqmlmodel.h>

#include <interfaces/handler.h>
#include <interfaces/node.h>
#include <interfaces/wallet.h>

#include <memory>

#include <QMutex>
#include <QObject>
#include <QThread>

class WalletQmlController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(WalletQmlModel* selectedWallet READ selectedWallet NOTIFY selectedWalletChanged)
    Q_PROPERTY(bool initialized READ initialized NOTIFY initializedChanged)
    Q_PROPERTY(bool isWalletLoaded READ isWalletLoaded NOTIFY isWalletLoadedChanged)
    Q_PROPERTY(bool noWalletsFound READ noWalletsFound NOTIFY noWalletsFoundChanged)
    Q_PROPERTY(bool walletLoadInProgress READ walletLoadInProgress NOTIFY walletLoadInProgressChanged)
    Q_PROPERTY(QString walletLoadError READ walletLoadError NOTIFY walletLoadErrorChanged)
    Q_PROPERTY(QString walletLoadWarnings READ walletLoadWarnings NOTIFY walletLoadWarningsChanged)
    Q_PROPERTY(bool walletMigrationInProgress READ walletMigrationInProgress NOTIFY walletMigrationInProgressChanged)
    Q_PROPERTY(QString walletMigrationError READ walletMigrationError NOTIFY walletMigrationErrorChanged)

public:
    explicit WalletQmlController(interfaces::Node& node, QObject *parent = nullptr);
    ~WalletQmlController();

    Q_INVOKABLE void setSelectedWallet(QString path);
    Q_INVOKABLE void createSingleSigWallet(const QString &name, const QString &passphrase);
    Q_INVOKABLE void importWallet(const QString& path);
    Q_INVOKABLE void clearWalletLoadStatus();
    Q_INVOKABLE void migrateWallet(const QString& path);
    Q_INVOKABLE void clearWalletMigrationStatus();
    Q_INVOKABLE QString normalizeWalletPath(const QString& path) const;
    Q_INVOKABLE bool walletPathExists(const QString& path) const;

    WalletQmlModel* selectedWallet() const;
    void unloadWallets();
    bool initialized() const { return m_initialized; }
    bool isWalletLoaded() const { return m_is_wallet_loaded; }
    void setWalletLoaded(bool loaded);
    bool noWalletsFound() const { return m_no_wallets_found; }
    void setNoWalletsFound(bool no_wallets_found);
    bool walletLoadInProgress() const { return m_wallet_load_in_progress; }
    QString walletLoadError() const { return m_wallet_load_error; }
    QString walletLoadWarnings() const { return m_wallet_load_warnings; }
    bool walletMigrationInProgress() const { return m_wallet_migration_in_progress; }
    QString walletMigrationError() const { return m_wallet_migration_error; }

Q_SIGNALS:
    void selectedWalletChanged();
    void initializedChanged();
    void isWalletLoadedChanged();
    void noWalletsFoundChanged();
    void walletLoadInProgressChanged();
    void walletLoadErrorChanged();
    void walletLoadWarningsChanged();
    void walletLoadSucceeded();
    void walletMigrationInProgressChanged();
    void walletMigrationErrorChanged();
    void walletMigrationRequired(const QString& path);
    void walletMigrationSucceeded();
    void walletMigrationFailed();

public Q_SLOTS:
    void initialize();

private:
    void handleLoadWallet(std::unique_ptr<interfaces::Wallet> wallet);
    void startWalletImport(const QString& path);
    void startWalletLoad(const QString& path);
    void startWalletMigration(const QString& path);
    QString resolveManagedWalletReference(const QString& path) const;
    QString inferWalletLoadTarget(const QString& normalized_path) const;
    QString inferRestoreWalletName(const QString& normalized_path) const;
    void setWalletLoadInProgress(bool in_progress);
    void setWalletLoadError(const QString& error);
    void setWalletLoadWarnings(const QString& warnings);
    void setWalletMigrationInProgress(bool in_progress);
    void setWalletMigrationError(const QString& error);

    bool m_initialized{false};
    interfaces::Node& m_node;
    WalletQmlModel* m_empty_wallet;
    WalletQmlModel* m_selected_wallet;
    QObject* m_worker;
    QThread* m_worker_thread;
    QMutex m_wallets_mutex;
    std::vector<WalletQmlModel*> m_wallets;
    std::unique_ptr<interfaces::Handler> m_handler_load_wallet;
    bool m_is_wallet_loaded{false};
    bool m_no_wallets_found{false};
    bool m_wallet_load_in_progress{false};
    bool m_wallet_load_requested{false};
    QString m_wallet_load_error;
    QString m_wallet_load_warnings;
    bool m_wallet_migration_in_progress{false};
    QString m_wallet_migration_error;

    bilingual_str m_error_message;
    std::vector<bilingual_str> m_warning_messages;
};

#endif // BITCOIN_QML_WALLETQMLCONTROLLER_H
