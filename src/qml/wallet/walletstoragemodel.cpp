// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/wallet/walletstoragemodel.h>
#include <qml/wallet/walletsession.h>
#include <qml/wallet/walletoverviewmodel.h>
#include <qml/wallet/walletimportmodel.h>
#include <interfaces/wallet.h>

#include <QDesktopServices>
#include <QFileInfo>
#include <QPointer>
#include <QSettings>
#include <QUrl>

WalletStorageModel::WalletStorageModel(WalletSession& session, WalletOverviewModel& overview, QObject* parent)
    : QObject(parent), m_session(session), m_overview(overview)
{
    connect(&session, &WalletSession::actionBusyChanged, this, &WalletStorageModel::changed);
    connect(&session, &WalletSession::invalidated, this, &WalletStorageModel::changed);
}
QString WalletStorageModel::location() const { return m_session.identity(); }
bool WalletStorageModel::busy() const { return m_session.actionBusy(); }
bool WalletStorageModel::fail(const QString& message) { m_error = message; Q_EMIT changed(); return false; }

bool WalletStorageModel::backup(const QString& destination)
{
    if (!m_session.available()) return fail(tr("This wallet is no longer available."));
    const QString path = WalletImportModel::localPath(destination);
    if (path.isEmpty() || QFileInfo::exists(path)) return fail(tr("Choose a new backup file path."));
    m_error.clear();
    QPointer<WalletStorageModel> self(this);
    if (!m_session.runAction([path](interfaces::Wallet& wallet) {
        return wallet.backupWallet(path.toStdString()) ? WalletOperationResult{} :
            WalletOperationResult::failure(WalletOperationResult::CoreError, tr("Could not write the wallet backup."));
    }, [self](WalletOperationResult result) {
        if (!self) return;
        self->m_error = result.error;
        Q_EMIT self->changed();
        if (result.code == WalletOperationResult::Success) Q_EMIT self->succeeded();
    })) return fail(tr("Another wallet action is in progress."));
    return true;
}

bool WalletStorageModel::setAlias(const QString& alias)
{
    if (!m_session.available()) return fail(tr("This wallet is no longer available."));
    QSettings settings;
    // A write adopts the network/identity-scoped key and clears the old alias.
    settings.remove(QStringLiteral("walletDisplayNames/%1").arg(m_session.name()));
    if (alias.trimmed().isEmpty()) settings.remove(m_overview.aliasKey());
    else settings.setValue(m_overview.aliasKey(), alias.trimmed());
    settings.sync();
    if (settings.status() != QSettings::NoError) return fail(tr("Could not save the wallet display name."));
    m_error.clear();
    Q_EMIT m_overview.changed();
    Q_EMIT changed();
    return true;
}

bool WalletStorageModel::openLocation()
{
    if (!m_session.available()) return fail(tr("This wallet is no longer available."));
    const QFileInfo file(location());
    if (!file.exists()) return fail(tr("The wallet location does not exist."));
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(file.isDir() ? file.absoluteFilePath() : file.absolutePath())))
        return fail(tr("Could not open the wallet location."));
    m_error.clear();
    Q_EMIT changed();
    return true;
}
