// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_WALLET_WALLETSENDMODEL_H
#define BITCOIN_QML_WALLET_WALLETSENDMODEL_H

#include <qml/wallet/sendrecipientslistmodel.h>
#include <qml/wallet/feeselectionmodel.h>
#include <qml/wallet/coinselectionmodel.h>
#include <qml/wallet/transactionreviewmodel.h>
#include <QObject>

class WalletSession;
namespace interfaces { class Node; }

/** Per-session send coordinator. Selecting another wallet never retargets a draft. */
class WalletSendModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(SendRecipientsListModel* recipients READ recipients CONSTANT)
    Q_PROPERTY(FeeSelectionModel* fees READ fees CONSTANT)
    Q_PROPERTY(CoinSelectionModel* coins READ coins CONSTANT)
    Q_PROPERTY(TransactionReviewModel* review READ review CONSTANT)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool canPrepare READ canPrepare NOTIFY changed)
    Q_PROPERTY(bool needsPassphrase READ needsPassphrase NOTIFY changed)
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(quint64 revision READ revision NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
public:
    WalletSendModel(WalletSession& session, CFeeRate dust_relay_fee, QObject* parent = nullptr);
    SendRecipientsListModel* recipients() { return &m_recipients; }
    FeeSelectionModel* fees() { return &m_fees; }
    CoinSelectionModel* coins() { return &m_coins; }
    TransactionReviewModel* review() { return &m_review; }
    bool busy() const;
    bool canPrepare() const;
    bool needsPassphrase() const;
    bool available() const;
    quint64 revision() const { return m_revision; }
    QString error() const;
    SendDraftSnapshot snapshot() const;
    Q_INVOKABLE void discard();
    Q_INVOKABLE bool prepare(const QString& passphrase);
    Q_INVOKABLE void invalidateReview();
Q_SIGNALS:
    void changed();
private:
    void draftEdited();
    WalletSession& m_session;
    SendRecipientsListModel m_recipients;
    CoinSelectionModel m_coins;
    FeeSelectionModel m_fees;
    TransactionReviewModel m_review;
    quint64 m_revision{1};
    quint64 m_prepare_id{0};
    bool m_preparing{false};
    QString m_error;
    struct Prepared { SendDraftSnapshot inputs; ReviewSnapshot review; };
    std::optional<Prepared> m_prepared;
};

#endif // BITCOIN_QML_WALLET_WALLETSENDMODEL_H
