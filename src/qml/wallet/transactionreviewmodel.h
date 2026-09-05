// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_WALLET_TRANSACTIONREVIEWMODEL_H
#define BITCOIN_QML_WALLET_TRANSACTIONREVIEWMODEL_H

#include <primitives/transaction.h>
#include <QObject>
#include <QVariantList>
#include <optional>
#include <vector>

struct ReviewOutput {
    QString address;
    CAmount amount{0};
    bool change{false};
};

struct ReviewSnapshot {
    quint64 session_id{0};
    quint64 session_generation{0};
    quint64 revision{0};
    CTransactionRef transaction;
    CAmount fee{0};
    std::optional<CAmount> previous_fee;
    std::vector<ReviewOutput> outputs;
};

/** Read-only presentation of one exact transaction. Never signs or broadcasts. */
class TransactionReviewModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool hasReview READ hasReview NOTIFY changed)
    Q_PROPERTY(QString transactionId READ transactionId NOTIFY changed)
    Q_PROPERTY(QString feeText READ feeText NOTIFY changed)
    Q_PROPERTY(QString previousFeeText READ previousFeeText NOTIFY changed)
    Q_PROPERTY(QVariantList outputs READ outputs NOTIFY changed)
    Q_PROPERTY(int displayUnit READ displayUnit WRITE setDisplayUnit NOTIFY changed)
public:
    explicit TransactionReviewModel(QObject* parent = nullptr) : QObject(parent) {}
    void setSnapshot(ReviewSnapshot snapshot);
    void clear();
    const std::optional<ReviewSnapshot>& snapshot() const { return m_snapshot; }
    bool hasReview() const { return m_snapshot.has_value(); }
    QString transactionId() const;
    QString feeText() const;
    QString previousFeeText() const;
    QVariantList outputs() const;
    int displayUnit() const { return m_display_unit; }
    void setDisplayUnit(int unit);
Q_SIGNALS:
    void changed();
private:
    QString format(CAmount amount) const;
    std::optional<ReviewSnapshot> m_snapshot;
    int m_display_unit{0};
};

#endif // BITCOIN_QML_WALLET_TRANSACTIONREVIEWMODEL_H
