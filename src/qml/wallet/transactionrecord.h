// Copyright (c) 2024-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_WALLET_TRANSACTIONRECORD_H
#define BITCOIN_QML_WALLET_TRANSACTIONRECORD_H
#include <consensus/amount.h>
#include <QString>
#include <QVector>
namespace interfaces { struct WalletTx; struct WalletTxStatus; }

/** Immutable row values. Output plus direction distinguishes all relevant subentries. */
struct TransactionRecord {
    enum Kind { Incoming, Outgoing, SelfPayment, Generated, Other };
    quint64 session_id{0};
    QString txid;
    int output_index{-1};
    Kind kind{Other};
    QString address, label, message;
    CAmount credit{0}, debit{0}, fee{0};
    qint64 time{0};
    int confirmations{0}, blocks_to_maturity{0};
    bool abandoned{false}, in_main_chain{false};
    QString key() const;
    QString status() const;
    CAmount netAmount() const { return credit + debit; }
    bool qualifiesAsReceipt() const { return credit > 0 && !abandoned && confirmations >= 0; }
};
QVector<TransactionRecord> DecomposeWalletTransaction(quint64 session_id, const interfaces::WalletTx& tx,
                                                      const interfaces::WalletTxStatus& status);
#endif // BITCOIN_QML_WALLET_TRANSACTIONRECORD_H
