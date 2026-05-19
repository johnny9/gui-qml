// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/signverifymessagemodel.h>

#include <common/signmessage.h>
#include <key_io.h>
#include <qml/models/walletunlock.h>
#include <qml/util.h>
#include <support/allocators/secure.h>

namespace {
std::optional<PKHash> LegacyP2PKHFromAddress(const QString& address)
{
    const CTxDestination destination{DecodeDestination(address.trimmed().toStdString())};
    if (const auto* pkhash{std::get_if<PKHash>(&destination)}) {
        return *pkhash;
    }
    return std::nullopt;
}
} // namespace

SignVerifyMessageModel::SignVerifyMessageModel(interfaces::Wallet* wallet, QObject* parent)
    : QObject(parent), m_wallet(wallet)
{
}

void SignVerifyMessageModel::setWallet(interfaces::Wallet* wallet)
{
    if (m_wallet == wallet) {
        return;
    }
    m_wallet = wallet;
    clear();
}

void SignVerifyMessageModel::setSecurityStateChangedFn(SecurityStateChangedFn fn)
{
    m_security_state_changed = std::move(fn);
}

bool SignVerifyMessageModel::isLegacyP2PKHAddress(const QString& address) const
{
    return LegacyP2PKHFromAddress(address).has_value();
}

bool SignVerifyMessageModel::signMessage(const QString& address, const QString& message)
{
    return signMessageInternal(address, message, std::nullopt);
}

bool SignVerifyMessageModel::signMessageWithPassphrase(const QString& address, const QString& message, const QString& passphrase)
{
    return signMessageInternal(address, message, passphrase);
}

bool SignVerifyMessageModel::signMessageInternal(const QString& address, const QString& message, const std::optional<QString>& passphrase)
{
    clearSigningStatus();
    setSignature(QString());

    if (!m_wallet) {
        setSigningStatus(tr("No wallet is selected."));
        return false;
    }
    const auto pkhash{LegacyP2PKHFromAddress(address)};
    if (!pkhash) {
        setSigningStatus(tr("Enter a legacy P2PKH bitcoin address."));
        return false;
    }

    bool relock{false};
    if (!unlockForSigning(passphrase, relock)) {
        return false;
    }

    WalletRelockGuard relock_guard{*m_wallet, [this] { notifySecurityStateChanged(); }, relock};

    std::string signature;
    const SigningResult result{m_wallet->signMessage(message.toStdString(), *pkhash, signature)};

    if (result != SigningResult::OK) {
        setSigningStatus(QString::fromStdString(SigningResultString(result)));
        return false;
    }

    setSignature(QString::fromStdString(signature));
    return true;
}

bool SignVerifyMessageModel::verifyMessage(const QString& address, const QString& message, const QString& signature) const
{
    if (!LegacyP2PKHFromAddress(address)) {
        return false;
    }
    if (signature.trimmed().isEmpty()) {
        return false;
    }
    return MessageVerify(
        address.trimmed().toStdString(),
        signature.trimmed().toStdString(),
        message.toStdString()) == MessageVerificationResult::OK;
}

void SignVerifyMessageModel::clear()
{
    clearSigningStatus();
    setSignature(QString());
}

void SignVerifyMessageModel::clearSigningStatus()
{
    setSigningStatus(QString());
}

bool SignVerifyMessageModel::unlockForSigning(const std::optional<QString>& passphrase, bool& relock)
{
    relock = false;
    if (!m_wallet) {
        return true;
    }
    if (!m_wallet->isCrypted() || !m_wallet->isLocked()) {
        return true;
    }
    if (!passphrase.has_value()) {
        setSigningStatus(tr("Enter your wallet password to sign this message."), true);
        return false;
    }

    SecureString secure_passphrase{QmlUtil::SecureStringFromQString(*passphrase)};
    switch (TryUnlockWithPassphrase(*m_wallet, secure_passphrase)) {
    case WalletUnlockResult::IncorrectPassphrase:
        setSigningStatus(tr("The wallet password you entered was incorrect."));
        return false;
    case WalletUnlockResult::AlreadyUnlocked:
        return true;
    case WalletUnlockResult::UnlockedNowRelockRequired:
        relock = true;
        notifySecurityStateChanged();
        return true;
    }
    return false;
}

void SignVerifyMessageModel::setSigningStatus(const QString& error, bool needs_unlock)
{
    if (m_signing_error != error) {
        m_signing_error = error;
        Q_EMIT signingErrorChanged();
    }
    if (m_signing_needs_unlock != needs_unlock) {
        m_signing_needs_unlock = needs_unlock;
        Q_EMIT signingNeedsUnlockChanged();
    }
}

void SignVerifyMessageModel::setSignature(const QString& signature)
{
    if (m_signature != signature) {
        m_signature = signature;
        Q_EMIT signatureChanged();
    }
}

void SignVerifyMessageModel::notifySecurityStateChanged()
{
    if (m_security_state_changed) {
        m_security_state_changed();
    }
}
