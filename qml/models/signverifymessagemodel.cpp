// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/signverifymessagemodel.h>
#include <qml/guipresentation.h>
#include <qml/secretinput.h>
#include <QPointer>
#include <mutex>

struct SignVerifyMessageModel::AddressInput {
    std::mutex mutex;
    QString sign, verify;
    quint64 revision{0};
};
struct SignVerifyMessageModel::AddressChecks {
    quint64 revision{0};
    bool sign{false}, verify{false};
};

SignVerifyMessageModel::SignVerifyMessageModel(interfaces::Wallet* wallet, QObject* parent)
    : QObject(parent), m_wallet(wallet), m_address_input(std::make_shared<AddressInput>()) {}

void SignVerifyMessageModel::setOpening(std::shared_ptr<walletopening::Session> opening, execution::SnapshotServices services)
{
    if (m_closed || (m_opening && m_opening != opening) || m_sign_active || m_verify_active) return;
    m_opening = std::move(opening);
    m_services = std::move(services);
    if (!m_services.scheduler && m_opening) m_services.scheduler = m_opening->scheduler();
    const QPointer<SignVerifyMessageModel> observer{this};
    if (!m_services.post) m_services.post = presentation::QueuedPost();
    if (!m_services.scheduler) return;
    m_address_query = std::make_shared<execution::SnapshotQuery<AddressChecks>>(m_services.scheduler,
        [input = m_address_input] {
            QString sign, verify; quint64 revision;
            { std::lock_guard lock{input->mutex}; sign = input->sign; verify = input->verify; revision = input->revision; }
            return AddressChecks{revision, walletmessage::IsLegacy(sign), walletmessage::IsLegacy(verify)};
        }, [post = m_services.post, observer] { post([observer] { if (observer) observer->publishAddressValidation(); }); }, execution::Lane::Domain);
    requestAddressValidation();
}

void SignVerifyMessageModel::closeAdmission()
{
    m_closed = true;
    if (m_address_query) m_address_query->close();
    m_signing_state = SigningClosed;
    m_verification_state = VerificationClosed;
}
void SignVerifyMessageModel::retryPublications()
{
    if (m_closed) return;
    const QPointer<SignVerifyMessageModel> guard{this};
    if (m_sign_active) publishSigning(m_sign_id, m_sign_operation_revision);
    if (!guard || m_closed) return;
    if (m_verify_active) publishVerification(m_verify_id, m_verify_operation_revision);
    if (!guard || m_closed) return;
    if (m_address_query) publishAddressValidation();
}
void SignVerifyMessageModel::setWallet(interfaces::Wallet* wallet)
{
    if (m_wallet == wallet) return;
    if (m_opening || !wallet) closeAdmission();
    m_wallet = wallet;
}
void SignVerifyMessageModel::setSecurityStateChangedFn(SecurityStateChangedFn fn) { m_security_state_changed = std::move(fn); }

void SignVerifyMessageModel::invalidateSigning()
{
    ++m_sign_revision;
    m_signature.clear(); m_signing_error.clear(); m_signing_needs_unlock = false;
    m_signing_state = m_sign_active ? SigningDiscarding : SigningIdle;
}
void SignVerifyMessageModel::invalidateVerification()
{
    ++m_verify_revision;
    m_verification_state = m_verify_active ? VerificationDiscarding : VerificationIdle;
}
bool SignVerifyMessageModel::notifySigning()
{
    const QPointer<SignVerifyMessageModel> guard{this};
    Q_EMIT signingErrorChanged(); if (!guard) return false;
    Q_EMIT signingNeedsUnlockChanged(); if (!guard) return false;
    Q_EMIT signatureChanged(); if (!guard) return false;
    Q_EMIT signingStateChanged(); return bool(guard);
}
void SignVerifyMessageModel::setSigningAddress(const QString& value)
{
    if (m_closed || m_sign_address == value) return;
    m_sign_address = value; invalidateSigning();
    const QPointer<SignVerifyMessageModel> guard{this};
    requestAddressValidation(); if (!guard) return;
    if (notifySigning()) Q_EMIT inputsChanged();
}
void SignVerifyMessageModel::setSigningMessage(const QString& value)
{
    if (m_closed || m_sign_message == value) return;
    m_sign_message = value; invalidateSigning();
    if (notifySigning()) Q_EMIT inputsChanged();
}
void SignVerifyMessageModel::setVerificationAddress(const QString& value)
{
    if (m_closed || m_verify_address == value) return;
    m_verify_address = value; invalidateVerification();
    const QPointer<SignVerifyMessageModel> guard{this};
    requestAddressValidation(); if (!guard) return;
    Q_EMIT verificationChanged(); if (guard) Q_EMIT inputsChanged();
}
void SignVerifyMessageModel::setVerificationMessage(const QString& value)
{
    if (m_closed || m_verify_message == value) return;
    m_verify_message = value; invalidateVerification();
    const QPointer<SignVerifyMessageModel> guard{this};
    Q_EMIT verificationChanged(); if (guard) Q_EMIT inputsChanged();
}
void SignVerifyMessageModel::setVerificationSignature(const QString& value)
{
    if (m_closed || m_verify_signature == value) return;
    m_verify_signature = value; invalidateVerification();
    const QPointer<SignVerifyMessageModel> guard{this};
    Q_EMIT verificationChanged(); if (guard) Q_EMIT inputsChanged();
}
void SignVerifyMessageModel::requestAddressValidation()
{
    { std::lock_guard lock{m_address_input->mutex}; m_address_input->sign = m_sign_address; m_address_input->verify = m_verify_address; m_address_input->revision = ++m_address_revision; }
    m_sign_address_valid = m_verify_address_valid = false;
    m_address_pending = bool(m_address_query);
    if (m_address_query) m_address_query->request();
    Q_EMIT addressValidationChanged();
}
void SignVerifyMessageModel::publishAddressValidation()
{
    const auto result = m_address_query->take();
    if (m_closed || !result) return;
    if (result->snapshot && result->snapshot->revision != m_address_revision) return;
    m_address_pending = false;
    m_sign_address_valid = result->snapshot && result->snapshot->sign;
    m_verify_address_valid = result->snapshot && result->snapshot->verify;
    Q_EMIT addressValidationChanged();
}
bool SignVerifyMessageModel::isLegacyP2PKHAddress(const QString& address) const
{
    return (address == m_sign_address && m_sign_address_valid) || (address == m_verify_address && m_verify_address_valid);
}

quint64 SignVerifyMessageModel::requestSignMessage(const QString& address, const QString& message)
{
    if (m_closed || m_sign_active) return 0;
    const QPointer<SignVerifyMessageModel> guard{this};
    setSigningAddress(address); if (!guard || m_closed) return 0;
    setSigningMessage(message); return guard ? sign({}) : 0;
}
quint64 SignVerifyMessageModel::requestSignMessageWithPassphrase(const QString& address, const QString& message, const QString& passphrase)
{
    if (m_closed || m_sign_active) return 0;
    const QPointer<SignVerifyMessageModel> guard{this};
    setSigningAddress(address); if (!guard || m_closed) return 0;
    setSigningMessage(message); if (!guard || m_closed) return 0;
    if (!m_wallet && !m_opening) return sign({});
    if (!m_opening || !m_opening->available() || !m_services.scheduler) return 0;
    const auto captured = secretinput::Capture(m_opening ? m_opening->scheduler() : nullptr, passphrase);
    if (!captured) {
        m_signing_state = SigningFailed; m_signing_error = tr("The password could not be queued within the available capacity.");
        m_signing_needs_unlock = false; m_signature.clear(); notifySigning(); return 0;
    }
    return sign({}, captured.input);
}
quint64 SignVerifyMessageModel::sign(std::optional<SecureString> passphrase, secretinput::Captured captured)
{
    if (m_closed || m_sign_active) return 0;
    if (!m_wallet && !m_opening) {
        const auto revision = ++m_sign_revision;
        m_signing_state = SigningFailed;
        m_signing_error = tr("No wallet is selected.");
        m_signing_needs_unlock = false;
        m_signature.clear();
        const QPointer<SignVerifyMessageModel> observer{this};
        const auto current = [observer, revision] {
            return observer && !observer->m_closed && !observer->m_sign_active &&
                !observer->m_wallet && !observer->m_opening && observer->m_sign_revision == revision;
        };
        Q_EMIT signingErrorChanged(); if (!current()) return 0;
        Q_EMIT signingNeedsUnlockChanged(); if (!current()) return 0;
        Q_EMIT signatureChanged(); if (!current()) return 0;
        Q_EMIT signingStateChanged();
        return 0;
    }
    if (!m_opening || !m_opening->available() || !m_services.scheduler) return 0;
    const auto action = m_opening->startAction();
    if (!action) return 0;
    const auto id = m_sign_id + 1, revision = m_sign_revision;
    const QPointer<SignVerifyMessageModel> observer{this};
    auto request = std::make_shared<walletmessage::SignRequest>(walletmessage::SignRequest{m_sign_address, m_sign_message, std::move(passphrase), std::move(captured)});
    auto operation = std::make_shared<execution::OwnedCommand<walletmessage::SignResult>>(m_services.scheduler,
        [post = m_services.post, observer, id, revision] { post([observer, id, revision] { if (observer) observer->publishSigning(id, revision); }); });
    const auto bytes = (static_cast<size_t>(m_sign_address.size()) + m_sign_message.size()) * sizeof(QChar) + 4096
        + secretinput::Bytes(request->captured_passphrase) + (request->passphrase ? request->passphrase->capacity() : 0);
    if (!operation->startWith([opening = m_opening, action, request, bytes](auto done, auto failed) {
        return opening->perform(action, [request](interfaces::Wallet& wallet) { return walletmessage::Sign(wallet, std::move(*request)); }, std::move(done), std::move(failed), bytes);
    })) {
        m_signing_state = SigningFailed; m_signature.clear(); m_signing_needs_unlock = false;
        m_signing_error = tr("The signing request could not be queued."); notifySigning(); return 0;
    }
    m_sign_id = id; m_sign_operation_revision = revision;
    m_sign_operation = std::move(operation);
    m_sign_active = true; m_signing_state = SigningPending; m_signature.clear(); m_signing_error.clear(); m_signing_needs_unlock = false;
    notifySigning();
    return id;
}
void SignVerifyMessageModel::publishSigning(quint64 id, quint64 revision)
{
    if (!m_sign_active || id != m_sign_id || !m_sign_operation) return;
    const auto result = m_sign_operation->result();
    if (!result) return;
    m_sign_active = false;
    if (m_closed) return;
    if (revision != m_sign_revision) { m_signing_state = SigningIdle; notifySigning(); return; }
    auto value = result->value.value_or(walletmessage::SignResult{});
    if (result->error) value.error = walletmessage::Error::Uncertain;
    m_signing_needs_unlock = value.error == walletmessage::Error::NeedsUnlock || value.error == walletmessage::Error::IncorrectPassphrase;
    m_signing_error = value.message;
    m_signature.clear();
    switch (value.error) {
    case walletmessage::Error::None: m_signature = value.signature; m_signing_state = SigningSucceeded; break;
    case walletmessage::Error::NeedsUnlock: m_signing_state = SigningNeedsUnlock; m_signing_error = tr("Enter your wallet password to sign this message."); break;
    case walletmessage::Error::IncorrectPassphrase: m_signing_state = SigningNeedsUnlock; m_signing_error = tr("The wallet password you entered was incorrect."); break;
    case walletmessage::Error::InvalidAddress: m_signing_state = SigningFailed; m_signing_error = tr("Enter a legacy P2PKH bitcoin address."); break;
    case walletmessage::Error::RelockFailed: m_signing_state = SigningUncertain; m_signing_error = tr("The wallet lock could not be restored. Check its security state before retrying."); break;
    case walletmessage::Error::Uncertain: m_signing_state = SigningUncertain; m_signing_error = tr("The signing outcome could not be confirmed."); break;
    case walletmessage::Error::Signing: m_signing_state = SigningFailed; break;
    }
    if (notifySigning() && m_opening && m_opening->available() && m_security_state_changed) m_security_state_changed();
}
std::optional<walletmessage::SignResult> SignVerifyMessageModel::signingResult() const
{
    if (!m_sign_operation) return {};
    const auto result = m_sign_operation->result();
    return result ? result->value : std::nullopt;
}

quint64 SignVerifyMessageModel::requestVerifyMessage(const QString& address, const QString& message, const QString& signature)
{
    if (m_closed || m_verify_active || !m_services.scheduler) return 0;
    const QPointer<SignVerifyMessageModel> guard{this};
    setVerificationAddress(address); if (!guard || m_closed) return 0;
    setVerificationMessage(message); if (!guard || m_closed) return 0;
    setVerificationSignature(signature); if (!guard || m_closed) return 0;
    const auto id = m_verify_id + 1, revision = m_verify_revision;
    auto operation = std::make_shared<execution::OwnedCommand<walletmessage::VerifyResult>>(m_services.scheduler,
        [post = m_services.post, guard, id, revision] { post([guard, id, revision] { if (guard) guard->publishVerification(id, revision); }); });
    const auto bytes = (static_cast<size_t>(address.size()) + message.size() + signature.size()) * sizeof(QChar) + 1024;
    if (!operation->start([address, message, signature] { return walletmessage::Verify(address, message, signature); },
        {execution::Lane::Domain, bytes, {}, execution::WorkClass::Observation})) {
        m_verification_state = VerificationFailed; Q_EMIT verificationChanged(); return 0;
    }
    m_verify_id = id; m_verify_operation_revision = revision;
    m_verify_operation = std::move(operation);
    m_verify_active = true; m_verification_state = VerificationPending;
    Q_EMIT verificationChanged();
    return id;
}
void SignVerifyMessageModel::publishVerification(quint64 id, quint64 revision)
{
    if (!m_verify_active || id != m_verify_id || !m_verify_operation) return;
    const auto result = m_verify_operation->result();
    if (!result) return;
    m_verify_active = false;
    if (m_closed) return;
    if (revision != m_verify_revision) m_verification_state = VerificationIdle;
    else if (result->error || !result->value || result->value->failed) m_verification_state = VerificationFailed;
    else m_verification_state = result->value->valid ? VerificationValid : VerificationInvalid;
    Q_EMIT verificationChanged();
}
QString SignVerifyMessageModel::verificationText() const
{
    if (m_verification_state == VerificationValid) return tr("Message verified successfully.");
    if (m_verification_state == VerificationInvalid) return tr("Message verification failed.");
    if (m_verification_state == VerificationFailed) return tr("Message verification could not be completed.");
    return {};
}
void SignVerifyMessageModel::clear() { const QPointer<SignVerifyMessageModel> guard{this}; clearSigningStatus(); if (guard) clearVerification(); }
void SignVerifyMessageModel::clearSigningStatus()
{
    if (m_closed) return;
    invalidateSigning(); notifySigning();
}
void SignVerifyMessageModel::clearVerification()
{
    if (m_closed) return;
    invalidateVerification(); Q_EMIT verificationChanged();
}
