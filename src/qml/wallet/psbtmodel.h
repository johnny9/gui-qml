// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_WALLET_PSBTMODEL_H
#define BITCOIN_QML_WALLET_PSBTMODEL_H

#include <qml/wallet/psbtdocument.h>
#include <qml/wallet/psbtinspection.h>
#include <qml/wallet/transactionreviewmodel.h>
#include <QObject>
#include <QVariantList>
#include <functional>

class WalletSession;
namespace interfaces { class Node; }

/** One PSBT document, original wallet session, and immutable review revision. */
class PsbtModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool loaded READ loaded NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(bool complete READ complete NOTIFY changed)
    Q_PROPERTY(bool canSign READ canSign NOTIFY changed)
    Q_PROPERTY(bool canUnlockForSigning READ canUnlockForSigning NOTIFY changed)
    Q_PROPERTY(bool known READ known NOTIFY changed)
    Q_PROPERTY(bool canSubmit READ canSubmit NOTIFY changed)
    Q_PROPERTY(bool accepted READ accepted NOTIFY changed)
    Q_PROPERTY(quint64 revision READ revision NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QVariantList outputs READ outputs NOTIFY changed)
    Q_PROPERTY(TransactionReviewModel* review READ review CONSTANT)
public:
    PsbtModel(WalletSession& session, interfaces::Node& node, QObject* parent = nullptr);
    bool loaded() const { return m_document.has_value(); }
    bool busy() const;
    bool available() const;
    bool complete() const { return m_inspection.complete; }
    bool canSign() const;
    bool canUnlockForSigning() const;
    bool known() const { return m_inspection.known; }
    bool canSubmit() const;
    bool accepted() const { return m_submitted; }
    QString status() const;
    QString error() const { return m_error; }
    QVariantList outputs() const;
    TransactionReviewModel* review() { return &m_review; }
    quint64 revision() const { return m_revision; }
    const std::optional<PsbtDocument>& document() const { return m_document; }
    Q_INVOKABLE void importFile(const QString& path);
    void importData(const QByteArray& data);
    Q_INVOKABLE bool importReview(TransactionReviewModel* review);
    Q_INVOKABLE void exportFile(const QString& path);
    Q_INVOKABLE void sign(const QString& passphrase = {});
    Q_INVOKABLE void submit();
    Q_INVOKABLE void clear();
Q_SIGNALS:
    void changed();
private:
    void import(std::function<std::optional<PsbtDocument>(QString&)> read);
    void publish(PsbtDocument document, PsbtInspection inspection);
    WalletSession& m_session;
    interfaces::Node& m_node;
    TransactionReviewModel m_review;
    std::optional<PsbtDocument> m_document;
    PsbtInspection m_inspection;
    quint64 m_revision{1};
    bool m_pending{false};
    bool m_submitted{false};
    QString m_error;
};

#endif // BITCOIN_QML_WALLET_PSBTMODEL_H
