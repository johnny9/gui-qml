// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_WALLET_SIGNVERIFYMESSAGEMODEL_H
#define BITCOIN_QML_WALLET_SIGNVERIFYMESSAGEMODEL_H
#include <QObject>
class WalletSession;

class SignVerifyMessageModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString signature READ signature NOTIFY changed)
    Q_PROPERTY(QString signingError READ signingError NOTIFY changed)
    Q_PROPERTY(QString verificationStatus READ verificationStatus NOTIFY changed)
    Q_PROPERTY(bool needsUnlock READ needsUnlock NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool available READ available NOTIFY changed)
public:
    explicit SignVerifyMessageModel(WalletSession& session, QObject* parent = nullptr);
    QString signature() const { return m_signature; }
    QString signingError() const { return m_error; }
    QString verificationStatus() const { return m_verification; }
    bool needsUnlock() const { return m_needs_unlock; }
    bool busy() const;
    bool available() const;
    Q_INVOKABLE bool sign(const QString& address, const QString& message, const QString& passphrase = {});
    Q_INVOKABLE bool verify(const QString& address, const QString& message, const QString& signature);
    Q_INVOKABLE void clear();
    static bool IsLegacyAddress(const QString& address);
    static bool Verify(const QString& address, const QString& message, const QString& signature);
Q_SIGNALS:
    void changed();
private:
    WalletSession& m_session;
    QString m_signature, m_error, m_verification;
    quint64 m_revision{0};
    bool m_needs_unlock{false};
};
#endif // BITCOIN_QML_WALLET_SIGNVERIFYMESSAGEMODEL_H
