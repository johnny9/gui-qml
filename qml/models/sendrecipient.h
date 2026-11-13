// Copyright (c) 2025-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_SENDRECIPIENT_H
#define BITCOIN_QML_MODELS_SENDRECIPIENT_H

#include <qml/bitcoinamount.h>
#include <qml/models/bitcoinaddress.h>
#include <qml/snapshotquery.h>
#include <qml/walletopening.h>
#include <qml/senddraft.h>

#include <QObject>
#include <QString>
#include <atomic>

class WalletQmlModel;

class SendRecipient : public QObject
{
    Q_OBJECT
    Q_PROPERTY(BitcoinAddress* address READ address CONSTANT)
    Q_PROPERTY(QString label READ label WRITE setLabel NOTIFY labelChanged)
    Q_PROPERTY(QString message READ message WRITE setMessage NOTIFY messageChanged)
    Q_PROPERTY(QString paymentRequestLabel READ paymentRequestLabel NOTIFY paymentRequestChanged)
    Q_PROPERTY(bool hasPaymentRequest READ hasPaymentRequest NOTIFY paymentRequestChanged)
    Q_PROPERTY(BitcoinAmount* amount READ amount CONSTANT)

    Q_PROPERTY(QString addressError READ addressError NOTIFY addressErrorChanged)
    Q_PROPERTY(QString amountError READ amountError NOTIFY amountErrorChanged)
    Q_PROPERTY(bool isValid READ isValid NOTIFY isValidChanged)
    Q_PROPERTY(bool validationPending READ validationPending NOTIFY isValidChanged)
    Q_PROPERTY(quint64 validationRevision READ validationRevision NOTIFY isValidChanged)

public:
    using Snapshot = senddraft::Recipient;
    explicit SendRecipient(WalletQmlModel* wallet, QObject* parent = nullptr);
    ~SendRecipient() override;
    void setOpening(std::shared_ptr<walletopening::Session> opening, execution::SnapshotServices services);
    void closeAdmission();
    void setOwnerAdmissionGate(std::shared_ptr<std::atomic_bool> gate) { m_owner_closed = std::move(gate); m_address->setOwnerAdmissionGate(m_owner_closed); m_amount->setOwnerAdmissionGate(m_owner_closed); }
    void retryPublications();
    quint64 identity() const { return m_identity; }
    bool validationPending() const { return !admissionClosed() && (m_pending || m_address->normalizationPending()); }
    quint64 validationRevision() const { return m_revision; }
    std::shared_ptr<const Snapshot> validationSnapshot() const { return !admissionClosed() && !validationPending() && m_validated && m_validated->revision == m_revision ? m_validated : nullptr; }
    Snapshot draft() const;
    Q_INVOKABLE void applyParsedPaymentRequest(const QVariantMap& parsed);

    BitcoinAddress* address() const;
    void setAddress(const QString& address);
    QString addressError() const;
    void setAddressError(const QString& error);

    QString label() const;
    void setLabel(const QString& label);

    BitcoinAmount* amount() const;
    void setAmount(const QString& amount);
    QString amountError() const;
    void setAmountError(const QString& error);

    QString message() const;
    void setMessage(const QString& message);

    CAmount cAmount() const;

    bool isValid() const;

    QString paymentRequestLabel() const { return m_request_label; }
    bool hasPaymentRequest() const { return !m_request_address.isEmpty(); }
    Q_INVOKABLE void applyPaymentRequest(const QString& address, const QString& pay_to, const QString& message);
    Q_INVOKABLE void clear();

Q_SIGNALS:
    void paymentRequestChanged();
    void addressChanged();
    void addressErrorChanged();
    void amountErrorChanged();
    void labelChanged();
    void messageChanged();
    void isValidChanged();
    void draftChanged();

private:
    bool admissionClosed() const { return m_closed || (m_owner_closed && m_owner_closed->load()); }
    struct Retired;
    std::shared_ptr<Retired> m_retired;
    std::shared_ptr<std::atomic_bool> m_owner_closed;
    struct Input;
    Snapshot editorDraft() const;
    void requestValidation();
    void publishValidation();
    void validateAddress();
    void validateAmount();

    const WalletQmlModel* m_wallet;
    const quint64 m_identity;
    quint64 m_revision{0}, m_setup_revision{0};
    bool m_pending{true}, m_closed{false};
    std::shared_ptr<walletopening::Session> m_opening;
    execution::SnapshotServices m_services;
    std::shared_ptr<Input> m_input;
    std::shared_ptr<execution::SnapshotQuery<std::shared_ptr<const Snapshot>>> m_query;
    std::shared_ptr<const Snapshot> m_validated;
    BitcoinAddress* m_address;
    QString m_addressError{""};
    QString m_label{""};
    QString m_message{""};
    QString m_request_label;
    QString m_request_address;
    BitcoinAmount* m_amount;
    QString m_amountError{""};
};

#endif // BITCOIN_QML_MODELS_SENDRECIPIENT_H
