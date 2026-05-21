// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_SIGNVERIFYMESSAGEMODEL_H
#define BITCOIN_QML_MODELS_SIGNVERIFYMESSAGEMODEL_H

#include <interfaces/wallet.h>

#include <functional>
#include <optional>

#include <QObject>
#include <QString>

class SignVerifyMessageModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString signingError READ signingError NOTIFY signingErrorChanged)
    Q_PROPERTY(bool signingNeedsUnlock READ signingNeedsUnlock NOTIFY signingNeedsUnlockChanged)
    Q_PROPERTY(QString signature READ signature NOTIFY signatureChanged)

public:
    using SecurityStateChangedFn = std::function<void()>;

    explicit SignVerifyMessageModel(interfaces::Wallet* wallet = nullptr, QObject* parent = nullptr);

    QString signingError() const { return m_signing_error; }
    bool signingNeedsUnlock() const { return m_signing_needs_unlock; }
    QString signature() const { return m_signature; }

    void setWallet(interfaces::Wallet* wallet);
    void setSecurityStateChangedFn(SecurityStateChangedFn fn);

    Q_INVOKABLE bool isLegacyP2PKHAddress(const QString& address) const;
    Q_INVOKABLE bool signMessage(const QString& address, const QString& message);
    Q_INVOKABLE bool signMessageWithPassphrase(const QString& address, const QString& message, const QString& passphrase);
    Q_INVOKABLE bool verifyMessage(const QString& address, const QString& message, const QString& signature) const;
    Q_INVOKABLE void clear();
    Q_INVOKABLE void clearSigningStatus();

Q_SIGNALS:
    void signingErrorChanged();
    void signingNeedsUnlockChanged();
    void signatureChanged();

private:
    bool signMessageInternal(const QString& address, const QString& message, const std::optional<QString>& passphrase);
    bool unlockForSigning(const std::optional<QString>& passphrase, bool& relock);
    void setSigningStatus(const QString& error, bool needs_unlock = false);
    void setSignature(const QString& signature);
    void notifySecurityStateChanged();

    interfaces::Wallet* m_wallet{nullptr};
    SecurityStateChangedFn m_security_state_changed;
    QString m_signing_error;
    bool m_signing_needs_unlock{false};
    QString m_signature;
};

#endif // BITCOIN_QML_MODELS_SIGNVERIFYMESSAGEMODEL_H
