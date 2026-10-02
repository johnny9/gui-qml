// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_PSBTQMLMODEL_H
#define BITCOIN_QML_MODELS_PSBTQMLMODEL_H

#include <common/messages.h>
#include <psbt.h>

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

class BackendExecutor;

class PsbtQmlModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool pending READ pending NOTIFY changed)
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
    explicit PsbtQmlModel(std::shared_ptr<interfaces::Wallet> wallet, interfaces::Node* node, QObject* parent = nullptr, std::shared_ptr<BackendExecutor> executor = {});

    bool pending() const { return m_pending; }
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
    void detachWallet();
    void setNode(interfaces::Node* node) { m_node = node; }
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
    void operationFinished(bool success);
    void changed();

private:
    enum class Operation { Load, Refresh, Sign, Broadcast, Copy, Save };
    bool startOperation(Operation operation, const QString& path = {}, const QString& status = {});
    static QStringList buildSummary(const PartiallySignedTransaction& psbt, interfaces::Wallet* wallet);

    std::shared_ptr<interfaces::Wallet> m_wallet;
    std::shared_ptr<BackendExecutor> m_executor;
    quint64 m_generation{0};
    bool m_pending{false};
    interfaces::Node* m_node;
    std::unique_ptr<PartiallySignedTransaction> m_psbt;
    QString m_status;
    QString m_error;
    QStringList m_summary;
    bool m_can_sign{false};
    bool m_can_broadcast{false};
    bool m_complete{false};
    int m_unsigned_inputs{0};
    int m_could_sign_inputs{0};
    QString m_matched_txid;
};

#endif // BITCOIN_QML_MODELS_PSBTQMLMODEL_H
