// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_PSBTQMLMODEL_H
#define BITCOIN_QML_MODELS_PSBTQMLMODEL_H

#include <common/messages.h>
#include <psbt.h>
#include <qml/models/psbtfeeservices.h>

#include <cstddef>
#include <memory>
#include <optional>
#include <utility>

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>

namespace interfaces {
class Node;
class Wallet;
} // namespace interfaces

class PsbtQmlModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool loaded READ loaded NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QStringList summary READ summary NOTIFY changed)
    Q_PROPERTY(bool canSign READ canSign NOTIFY changed)
    Q_PROPERTY(bool canBroadcast READ canBroadcast NOTIFY changed)
    Q_PROPERTY(bool complete READ complete NOTIFY changed)
    Q_PROPERTY(int unsignedInputCount READ unsignedInputCount NOTIFY changed)
    Q_PROPERTY(int couldSignInputCount READ couldSignInputCount NOTIFY changed)
    Q_PROPERTY(QString matchedTxid READ matchedTxid NOTIFY changed)

public:
    void detachWallet() { m_fee_request.reset(); m_fee_pending = false; m_detached = true; m_wallet = nullptr; m_can_sign = m_can_broadcast = false; }
    explicit PsbtQmlModel(interfaces::Wallet* wallet, interfaces::Node* node, QObject* parent = nullptr);

    bool loaded() const { return m_psbt != nullptr; }
    QString status() const { return m_status; }
    QString error() const { return m_error; }
    QStringList summary() const { return m_summary; }
    bool canSign() const { return m_can_sign; }
    bool canBroadcast() const { return m_can_broadcast; }
    bool complete() const { return m_complete; }
    int unsignedInputCount() const { return m_unsigned_inputs; }
    int couldSignInputCount() const { return m_could_sign_inputs; }
    QString matchedTxid() const { return m_matched_txid; }

    Q_INVOKABLE void clear();
    Q_INVOKABLE void sign();
    Q_INVOKABLE void broadcast();
    Q_INVOKABLE void copyToClipboard();
    Q_INVOKABLE QString saveToFile(const QString& path);

    QString loadFromFile(const QString& path);
    const PartiallySignedTransaction* psbt() const { return m_psbt.get(); }
    void setError(const QString& error);
    void setMatchedTxid(const QString& txid);
    void setNode(interfaces::Node* node);
    void setFeeServices(psbtfee::Services services) { m_fee_services = std::move(services); }
    void refreshState(const QString& status_override = {});

    static QString LocalFilePath(const QString& path);
    static QString LoadPsbtFromFile(const QString& path, PartiallySignedTransaction& psbt);
    static QByteArray SerializePsbtRaw(const PartiallySignedTransaction& psbt);
    static QString SavePsbtToFile(const PartiallySignedTransaction& psbt, const QString& path);
    static QString PsbtErrorText(common::PSBTError error);
    static bool IsMultisigPsbtInput(const PartiallySignedTransaction& psbt, std::size_t index);
    // Returns (required, total) signers for a multisig PSBT input, or nullopt if not multisig.
    static std::optional<std::pair<int, int>> MultisigPsbtInputSigInfo(const PartiallySignedTransaction& psbt, std::size_t index);

Q_SIGNALS:
    void changed();

private:
    QStringList buildSummary(const PartiallySignedTransaction& psbt) const;

    interfaces::Wallet* m_wallet;
    interfaces::Node* m_node;
    std::shared_ptr<const PartiallySignedTransaction> m_psbt;
    psbtfee::Services m_fee_services;
    std::shared_ptr<const psbtfee::Analysis> m_fee_proof;
    std::shared_ptr<const psbtfee::RequestIdentity> m_fee_request;
    bool m_detached{false};
    bool m_fee_pending{false};
    bool m_fee_attempted{false};
    void requestFeeProof();
    QString m_status;
    QString m_error;
    QStringList m_summary;
    bool m_can_sign{false};
    bool m_can_broadcast{false};
    bool m_complete{false};
    std::optional<CAmount> m_reviewed_fee;
    int m_unsigned_inputs{0};
    int m_could_sign_inputs{0};
    QString m_matched_txid;
};

#endif // BITCOIN_QML_MODELS_PSBTQMLMODEL_H
