// Copyright (c) 2011-2025 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_WALLETQMLMODELTRANSACTION_H
#define BITCOIN_QML_MODELS_WALLETQMLMODELTRANSACTION_H

#include <qml/bitcoinamount.h>
#include <qml/models/sendrecipientslistmodel.h>

#include <consensus/amount.h>
#include <primitives/transaction.h>


class WalletQmlModelTransaction : public QObject
{
    Q_OBJECT
    Q_PROPERTY(BitcoinAmount* amountAmount READ amountAmount CONSTANT)
    Q_PROPERTY(BitcoinAmount* feeAmount READ feeAmount CONSTANT)
    Q_PROPERTY(BitcoinAmount* totalAmount READ totalAmount CONSTANT)
public:
    explicit WalletQmlModelTransaction(const SendRecipientsListModel* recipient, QObject* parent = nullptr);

    BitcoinAmount* amountAmount() const;
    BitcoinAmount* feeAmount() const;
    BitcoinAmount* totalAmount() const;

    CTransactionRef& getWtx();
    void setWtx(const CTransactionRef&);

    void setTransactionFee(const CAmount& newFee);
    void reassignAmounts(int nChangePosRet); // needed for the subtract-fee-from-amount feature

private:
    CAmount m_amount;
    CAmount m_fee;
    BitcoinAmount* m_amount_amount;
    BitcoinAmount* m_fee_amount;
    BitcoinAmount* m_total_amount;
    CTransactionRef m_wtx;
};

#endif // BITCOIN_QML_MODELS_WALLETQMLMODELTRANSACTION_H
