// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/wallet/walletsecuritymodel.h>
#include <qml/wallet/walletsession.h>
#include <qml/wallet/walletpassphrase.h>
#include <interfaces/wallet.h>
#include <QPointer>

WalletSecurityModel::WalletSecurityModel(WalletSession& session, QObject* parent)
    : QObject(parent), m_session(session)
{
    connect(&session, &WalletSession::changed, this, &WalletSecurityModel::changed);
    connect(&session, &WalletSession::actionBusyChanged, this, &WalletSecurityModel::changed);
    connect(&session, &WalletSession::invalidated, this, &WalletSecurityModel::changed);
}
bool WalletSecurityModel::encrypted() const { return m_session.available() && m_session.wallet().isCrypted(); }
bool WalletSecurityModel::locked() const { return m_session.available() && m_session.wallet().isLocked(); }
bool WalletSecurityModel::supported() const
{
    return m_session.available() && !m_session.wallet().privateKeysDisabled() && !m_session.wallet().hasExternalSigner();
}
bool WalletSecurityModel::busy() const { return m_session.actionBusy(); }
bool WalletSecurityModel::fail(const QString& message) { m_error = message; Q_EMIT changed(); return false; }

bool WalletSecurityModel::encrypt(const QString& passphrase, const QString& confirmation)
{
    if (!supported() || encrypted()) return fail(tr("Encryption is not available for this wallet."));
    if (passphrase.isEmpty() || passphrase != confirmation) return fail(tr("Enter matching non-empty passphrases."));
    m_error.clear();
    QPointer<WalletSecurityModel> self(this);
    if (!m_session.runAction([secret = WalletPassphrase(passphrase)](interfaces::Wallet& wallet) {
        return wallet.encryptWallet(secret) ? WalletOperationResult{} :
            WalletOperationResult::failure(WalletOperationResult::CoreError, tr("Could not encrypt the wallet."));
    }, [self](WalletOperationResult result) {
        if (!self) return;
        self->m_error = result.error;
        Q_EMIT self->changed();
        if (result.code == WalletOperationResult::Success) Q_EMIT self->succeeded();
    })) return fail(tr("Another wallet action is in progress."));
    return true;
}

bool WalletSecurityModel::changePassphrase(const QString& old_passphrase, const QString& new_passphrase, const QString& confirmation)
{
    if (!supported() || !encrypted()) return fail(tr("This wallet does not support passphrase changes."));
    if (new_passphrase.isEmpty() || new_passphrase != confirmation) return fail(tr("Enter matching non-empty passphrases."));
    m_error.clear();
    QPointer<WalletSecurityModel> self(this);
    if (!m_session.runAction([old_secret = WalletPassphrase(old_passphrase), new_secret = WalletPassphrase(new_passphrase)](interfaces::Wallet& wallet) {
        return wallet.changeWalletPassphrase(old_secret, new_secret) ? WalletOperationResult{} :
            WalletOperationResult::failure(WalletOperationResult::CoreError, tr("Could not change the passphrase. Check the current passphrase."));
    }, [self](WalletOperationResult result) {
        if (!self) return;
        self->m_error = result.error;
        Q_EMIT self->changed();
        if (result.code == WalletOperationResult::Success) Q_EMIT self->succeeded();
    })) return fail(tr("Another wallet action is in progress."));
    return true;
}
