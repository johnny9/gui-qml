// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/wallet/walletcreationmodel.h>
#include <qml/wallet/walletmanager.h>
#include <qml/wallet/walletpassphrase.h>
#include <interfaces/wallet.h>
#include <support/allocators/secure.h>
#include <util/translation.h>
#include <wallet/walletutil.h>
#include <QFileInfo>

QString WalletCreationModel::nameSyntaxError(const QString& name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) return tr("Enter a wallet name.");
    if (trimmed == "." || trimmed == ".." || trimmed.contains('/') || trimmed.contains('\\') || trimmed.contains(QChar::Null))
        return tr("Use a wallet name without path separators.");
    return {};
}

QString WalletCreationModel::nameError(const QString& name) const
{
    const auto syntax = nameSyntaxError(name);
    if (!syntax.isEmpty()) return syntax;
    const QString trimmed = name.trimmed();
    if (!m_manager.initialized()) return tr("Wallets are not available.");
    if (QFileInfo::exists(m_manager.canonicalIdentity(trimmed))) return tr("A wallet with this name already exists.");
    for (int row = 0; row < m_manager.catalog()->rowCount(); ++row) {
        const auto index = m_manager.catalog()->index(row);
        for (int role : {WalletListModel::NameRole, WalletListModel::DisplayNameRole}) {
            if (m_manager.catalog()->data(index, role).toString().compare(trimmed, Qt::CaseInsensitive) == 0)
                return tr("A wallet with this name already exists.");
        }
    }
    return {};
}

bool WalletCreationModel::create(const QString& name, const QString& passphrase, const QString& confirmation)
{
    const QString error = nameError(name);
    if (!error.isEmpty()) return fail(WalletOperationResult::InvalidInput, error);
    if (passphrase != confirmation) return fail(WalletOperationResult::InvalidInput, tr("Passphrases do not match."));
    SecureString secret = WalletPassphrase(passphrase);
    const QString target = name.trimmed();
    auto* loader = &m_manager.loader();
    return launch(target, [loader, target, secret = std::move(secret)] {
        std::vector<bilingual_str> warnings;
        auto created = loader->createWallet(target.toStdString(), secret, wallet::WALLET_FLAG_DESCRIPTORS, warnings);
        if (!created) return WalletOperationResult::failure(WalletOperationResult::CoreError, QString::fromStdString(util::ErrorString(created).translated));
        WalletOperationResult result;
        result.wallet = std::shared_ptr<interfaces::Wallet>(std::move(*created));
        for (const auto& warning : warnings) result.warnings.append(QString::fromStdString(warning.translated));
        return result;
    });
}
