// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/wallet/walletmigrationmodel.h>
#include <qml/wallet/walletmanager.h>
#include <qml/wallet/walletpassphrase.h>
#include <interfaces/wallet.h>
#include <util/translation.h>

bool WalletMigrationModel::inspect(const QString& name)
{
    if (busy() || !m_manager.initialized()) return false;
    clear();
    bool legacy{false};
    for (const auto& [entry, format] : m_manager.loader().listWalletDir()) {
        if (m_manager.canonicalIdentity(QString::fromStdString(entry)) == m_manager.canonicalIdentity(name) && format == "bdb") legacy = true;
    }
    if (!legacy) return fail(WalletOperationResult::InvalidInput, tr("Select an existing legacy wallet to migrate."));
    m_name = name;
    m_encrypted = m_manager.loader().isEncrypted(name.toStdString());
    Q_EMIT targetChanged();
    return true;
}

bool WalletMigrationModel::migrate(const QString& passphrase)
{
    if (m_name.isNull()) return fail(WalletOperationResult::InvalidInput, tr("Select a legacy wallet first."));
    if (m_encrypted && passphrase.isEmpty()) return fail(WalletOperationResult::InvalidInput, tr("Enter the wallet passphrase."));
    auto* loader = &m_manager.loader();
    const QString name = m_name;
    return launch(name, [loader, name, secret = WalletPassphrase(passphrase)] {
        auto migrated = loader->migrateWallet(name.toStdString(), secret, true);
        if (!migrated) return WalletOperationResult::failure(WalletOperationResult::CoreError, QString::fromStdString(util::ErrorString(migrated).translated));
        WalletOperationResult result;
        result.wallet = std::shared_ptr<interfaces::Wallet>(std::move(migrated->wallet));
        result.backup_path = QString::fromStdString(fs::PathToString(migrated->backup_path));
        if (migrated->watchonly_wallet_name) result.companions.append(QString::fromStdString(*migrated->watchonly_wallet_name));
        if (migrated->solvables_wallet_name) result.companions.append(QString::fromStdString(*migrated->solvables_wallet_name));
        return result;
    });
}
