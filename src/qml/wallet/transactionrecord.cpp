// Copyright (c) 2024-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/wallet/transactionrecord.h>
#include <interfaces/wallet.h>
#include <key_io.h>
#include <QObject>
#include <algorithm>

QString TransactionRecord::key() const
{
    return QStringLiteral("%1:%2:%3:%4").arg(session_id).arg(txid).arg(output_index).arg(kind);
}

QString TransactionRecord::status() const
{
    if (confirmations < 0) return QObject::tr("Conflicted");
    if (abandoned) return QObject::tr("Abandoned");
    if (kind == Generated && !in_main_chain) return QObject::tr("Not accepted");
    if (blocks_to_maturity > 0) return QObject::tr("Immature");
    if (confirmations == 0) return QObject::tr("Unconfirmed");
    if (confirmations < 6) return QObject::tr("Confirming");
    return QObject::tr("Confirmed");
}

QVector<TransactionRecord> DecomposeWalletTransaction(quint64 session_id, const interfaces::WalletTx& tx,
                                                      const interfaces::WalletTxStatus& status)
{
    QVector<TransactionRecord> rows;
    if (!tx.tx) return rows;
    auto base = [&] {
        TransactionRecord row;
        row.session_id = session_id;
        row.txid = QString::fromStdString(tx.tx->GetHash().ToString());
        row.time = tx.time;
        row.confirmations = status.depth_in_main_chain;
        row.blocks_to_maturity = status.blocks_to_maturity;
        row.abandoned = status.is_abandoned;
        row.in_main_chain = status.is_in_main_chain;
        if (tx.comment) row.message = QString::fromStdString(*tx.comment);
        return row;
    };
    const bool any_from_me{!tx.is_coinbase && std::ranges::any_of(tx.txin_is_mine, [](bool mine) { return mine; })};
    const bool all_from_me{any_from_me && std::ranges::all_of(tx.txin_is_mine, [](bool mine) { return mine; })};
    if (any_from_me && !all_from_me) {
        auto row{base()};
        row.credit = tx.credit;
        row.debit = -tx.debit;
        rows.push_back(std::move(row));
        return rows;
    }
    CAmount fee{all_from_me ? std::max<CAmount>(0, tx.debit - tx.tx->GetValueOut()) : 0};
    for (size_t i = 0; i < tx.tx->vout.size(); ++i) {
        const bool mine{i < tx.txout_is_mine.size() && tx.txout_is_mine[i]};
        const bool change{i < tx.txout_is_change.size() && tx.txout_is_change[i]};
        if ((all_from_me && change) || (!all_from_me && !mine)) continue;
        auto row{base()};
        row.output_index = static_cast<int>(i);
        if (i < tx.txout_address.size() && IsValidDestination(tx.txout_address[i])) {
            row.address = QString::fromStdString(EncodeDestination(tx.txout_address[i]));
        }
        if (all_from_me) {
            row.kind = mine ? TransactionRecord::SelfPayment : TransactionRecord::Outgoing;
            row.debit = -tx.tx->vout[i].nValue - fee;
            row.fee = fee;
            fee = 0;
        } else {
            row.kind = tx.is_coinbase ? TransactionRecord::Generated : TransactionRecord::Incoming;
        }
        if (mine) row.credit = tx.tx->vout[i].nValue;
        rows.push_back(std::move(row));
    }
    if (rows.empty() && all_from_me) {
        auto row{base()};
        row.credit = tx.credit;
        row.debit = -tx.debit;
        row.fee = fee;
        rows.push_back(std::move(row));
    }
    return rows;
}
