// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_WALLET_BUMPFEEMODEL_H
#define BITCOIN_QML_WALLET_BUMPFEEMODEL_H

#include <qml/wallet/transactionreviewmodel.h>
#include <QObject>
#include <optional>

class WalletSession;
namespace interfaces { class Node; }

/** Core replacement policy, independent of ordinary send drafts and previews. */
class BumpFeeModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool eligible READ eligible NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool canSubmit READ canSubmit NOTIFY changed)
    Q_PROPERTY(bool accepted READ accepted NOTIFY changed)
    Q_PROPERTY(quint64 revision READ revision NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString feeIncrease READ feeIncrease NOTIFY changed)
    Q_PROPERTY(QString replacementTransactionId READ replacementTransactionId NOTIFY changed)
    Q_PROPERTY(TransactionReviewModel* review READ review CONSTANT)
public:
    BumpFeeModel(WalletSession& session, interfaces::Node& node, QObject* parent = nullptr);
    bool eligible() const { return m_session_available && m_eligible; }
    bool busy() const;
    bool canSubmit() const;
    bool accepted() const { return m_accepted; }
    quint64 revision() const { return m_revision; }
    QString error() const { return m_error; }
    QString feeIncrease() const;
    QString replacementTransactionId() const { return m_replacement_id; }
    TransactionReviewModel* review() { return &m_review; }
    Q_INVOKABLE void inspect(const QString& transaction_id);
    Q_INVOKABLE void prepare(const QString& custom_rate = {});
    Q_INVOKABLE void submit(const QString& passphrase = {});
    Q_INVOKABLE void clear();
    Q_INVOKABLE void discardReview();
Q_SIGNALS:
    void changed();
private:
    void checkEligibility();
    WalletSession& m_session;
    interfaces::Node& m_node;
    TransactionReviewModel m_review;
    std::optional<Txid> m_original;
    std::optional<CMutableTransaction> m_candidate;
    quint64 m_revision{1};
    bool m_session_available{true};
    bool m_pending{false};
    bool m_eligible{false};
    bool m_recorded{false};
    bool m_accepted{false};
    QString m_error;
    QString m_replacement_id;
};

#endif // BITCOIN_QML_WALLET_BUMPFEEMODEL_H
