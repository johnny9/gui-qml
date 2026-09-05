// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/wallet/signverifymessagemodel.h>
#include <qml/wallet/walletsession.h>
#include <qml/wallet/walletunlockcontext.h>
#include <qml/wallet/walletpassphrase.h>
#include <common/signmessage.h>
#include <interfaces/wallet.h>
#include <key_io.h>
#include <QPointer>

SignVerifyMessageModel::SignVerifyMessageModel(WalletSession& session, QObject* parent)
    : QObject(parent), m_session{session}
{
    connect(&session, &WalletSession::actionBusyChanged, this, &SignVerifyMessageModel::changed);
    connect(&session, &WalletSession::invalidated, this, &SignVerifyMessageModel::clear);
}
bool SignVerifyMessageModel::busy() const { return m_session.actionBusy(); }
bool SignVerifyMessageModel::available() const { return m_session.available(); }
bool SignVerifyMessageModel::IsLegacyAddress(const QString& address)
{
    return std::holds_alternative<PKHash>(DecodeDestination(address.trimmed().toStdString()));
}
bool SignVerifyMessageModel::Verify(const QString& address, const QString& message, const QString& signature)
{
    return IsLegacyAddress(address) && !signature.trimmed().isEmpty() &&
        MessageVerify(address.trimmed().toStdString(), signature.trimmed().toStdString(), message.toStdString()) == MessageVerificationResult::OK;
}
bool SignVerifyMessageModel::verify(const QString& address, const QString& message, const QString& signature)
{
    const bool valid{Verify(address, message, signature)};
    m_verification = valid ? tr("Signature verified.") : tr("Signature verification failed. Check the legacy address, exact message and signature.");
    Q_EMIT changed();
    return valid;
}
void SignVerifyMessageModel::clear()
{
    ++m_revision;
    m_signature.clear();
    m_error.clear();
    m_verification.clear();
    m_needs_unlock = false;
    Q_EMIT changed();
}
bool SignVerifyMessageModel::sign(const QString& address, const QString& message, const QString& passphrase)
{
    clear();
    if (!available() || !IsLegacyAddress(address) || message.toUtf8().size() > 1024 * 1024) {
        m_error = tr("Choose an available wallet and a legacy P2PKH address.");
        Q_EMIT changed();
        return false;
    }
    const auto destination{DecodeDestination(address.trimmed().toStdString())};
    const PKHash key{std::get<PKHash>(destination)};
    struct Result { QString signature; bool needs_unlock{false}; };
    auto result{std::make_shared<Result>()};
    const quint64 revision{m_revision};
    const QPointer<SignVerifyMessageModel> self{this};
    const bool accepted{m_session.runAction([result, key, destination, message, secret = WalletPassphrase(passphrase)](interfaces::Wallet& wallet) {
        if (wallet.privateKeysDisabled() || wallet.hasExternalSigner() || !wallet.isSpendable(destination)) {
            return WalletOperationResult::failure(WalletOperationResult::Unavailable, QObject::tr("A supported local private key is not available for this address."));
        }
        if (wallet.isCrypted() && wallet.isLocked() && secret.empty()) {
            result->needs_unlock = true;
            return WalletOperationResult::failure(WalletOperationResult::InvalidInput, QObject::tr("Enter your wallet password to sign this message."));
        }
        WalletUnlockContext unlock{wallet, secret};
        if (!unlock.valid()) {
            result->needs_unlock = wallet.isLocked();
            return WalletOperationResult::failure(WalletOperationResult::CoreError, unlock.error());
        }
        std::string signature;
        const auto signed_result{wallet.signMessage(message.toStdString(), key, signature)};
        if (signed_result != SigningResult::OK) {
            return WalletOperationResult::failure(WalletOperationResult::CoreError, QString::fromStdString(SigningResultString(signed_result)));
        }
        result->signature = QString::fromStdString(signature);
        return WalletOperationResult{};
    }, [self, result, revision](WalletOperationResult status) {
        if (!self || revision != self->m_revision) return;
        self->m_error = status.error;
        self->m_signature = result->signature;
        self->m_needs_unlock = result->needs_unlock;
        Q_EMIT self->changed();
    })};
    if (!accepted) { m_error = tr("The wallet is busy or unavailable."); Q_EMIT changed(); }
    return accepted;
}
