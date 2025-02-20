// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/walletqmlmodel.h>

#include <qml/models/sendrecipient.h>
#include <qml/models/walletqmlmodeltransaction.h>

#include <consensus/amount.h>
#include <key_io.h>
#include <outputtype.h>
#include <qt/bitcoinunits.h>
#include <wallet/coincontrol.h>
#include <wallet/wallet.h>

#include <QTimer>

WalletQmlModel::WalletQmlModel(std::unique_ptr<interfaces::Wallet> wallet, QObject *parent)
    : QObject(parent)
{
    m_wallet = std::move(wallet);
    m_current_recipient = new SendRecipient(this);
}

WalletQmlModel::WalletQmlModel(QObject *parent)
    : QObject(parent)
{
    m_current_recipient = new SendRecipient(this);
}

QString WalletQmlModel::balance() const
{
    if (!m_wallet) {
        return "0";
    }
    return BitcoinUnits::format(BitcoinUnits::Unit::BTC, m_wallet->getBalance());
}

QString WalletQmlModel::name() const
{
    if (!m_wallet) {
        return QString();
    }
    return QString::fromStdString(m_wallet->getWalletName());
}

bool WalletQmlModel::prepareTransaction()
{
    if (!m_wallet || !m_current_recipient) {
        return false;
    }

    CScript scriptPubKey = GetScriptForDestination(DecodeDestination(m_current_recipient->address().toStdString()));
    wallet::CRecipient recipient = {scriptPubKey, m_current_recipient->cAmount(), m_current_recipient->subtractFeeFromAmount()};
    wallet::CCoinControl coinControl;
    coinControl.m_feerate = CFeeRate(1000);

    CAmount balance = m_wallet->getBalance();
    if (balance < recipient.nAmount) {
        return false;
    }

    std::vector<wallet::CRecipient> vecSend{recipient};
    int nChangePosRet = -1;
    CAmount nFeeRequired = 0;
    const auto& res = m_wallet->createTransaction(vecSend, coinControl, true, nChangePosRet, nFeeRequired);
    if (res) {
        if (m_current_transaction) {
            delete m_current_transaction;
        }
        CTransactionRef newTx = *res;
        m_current_transaction = new WalletQmlModelTransaction(m_current_recipient, this);
        m_current_transaction->setWtx(newTx);
        m_current_transaction->setTransactionFee(nFeeRequired);
        Q_EMIT currentTransactionChanged();
        return true;
    } else {
        return false;
    }
}

void WalletQmlModel::sendTransaction()
{
    if (!m_wallet || !m_current_transaction) {
        return;
    }

    CTransactionRef newTx = m_current_transaction->getWtx();
    if (!newTx) {
        return;
    }

    interfaces::WalletValueMap value_map;
    interfaces::WalletOrderForm order_form;
    m_wallet->commitTransaction(newTx, value_map, order_form);
}
