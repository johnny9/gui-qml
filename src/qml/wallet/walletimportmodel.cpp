// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/wallet/walletimportmodel.h>
#include <qml/wallet/walletmanager.h>
#include <interfaces/wallet.h>
#include <util/translation.h>
#include <QFileInfo>
#include <QUrl>

QString WalletImportModel::localPath(const QString& path)
{
    const QUrl url(path);
    if (url.isLocalFile()) return QFileInfo(url.toLocalFile()).absoluteFilePath();
    if (!url.scheme().isEmpty() && !QFileInfo(path).isAbsolute()) return {};
    return path.trimmed().isEmpty() ? QString{} : QFileInfo(path).absoluteFilePath();
}

bool WalletImportModel::restore(const QString& backup, const QString& name)
{
    const QString path = localPath(backup);
    const QFileInfo file(path);
    if (path.isEmpty() || !file.isFile() || !file.isReadable())
        return fail(WalletOperationResult::InvalidInput, tr("Select a readable wallet backup file."));
    const QString error = m_manager.creation()->nameError(name);
    if (!error.isEmpty()) return fail(WalletOperationResult::InvalidInput, error);
    const QString target = name.trimmed();
    auto* loader = &m_manager.loader();
    return launch(target, [loader, target, path] {
        std::vector<bilingual_str> warnings;
        auto restored = loader->restoreWallet(fs::u8path(path.toStdString()), target.toStdString(), warnings, true);
        if (!restored) return WalletOperationResult::failure(WalletOperationResult::CoreError, QString::fromStdString(util::ErrorString(restored).translated));
        WalletOperationResult result;
        result.wallet = std::shared_ptr<interfaces::Wallet>(std::move(*restored));
        for (const auto& warning : warnings) result.warnings.append(QString::fromStdString(warning.translated));
        return result;
    });
}
