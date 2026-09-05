// Copyright (c) 2024-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/wallet/transactionhistorymodel.h>
#include <qml/test/qt_test_registry.h>
#include <interfaces/wallet.h>
#include <primitives/transaction.h>
#include <QSignalSpy>
#include <QTest>

class TransactionHistoryTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void keepsEveryOwnedOutputAndStableIdentity()
    {
        CMutableTransaction tx;
        tx.vin.resize(1);
        tx.vout.emplace_back(11, CScript{});
        tx.vout.emplace_back(22, CScript{});
        interfaces::WalletTx wallet_tx{};
        wallet_tx.tx = MakeTransactionRef(tx);
        wallet_tx.txin_is_mine = {false};
        wallet_tx.txout_is_mine = {true, true};
        wallet_tx.txout_is_change = {false, true};
        wallet_tx.time = 1234;
        interfaces::WalletTxStatus status{};
        const auto rows{DecomposeWalletTransaction(17, wallet_tx, status)};
        QCOMPARE(rows.size(), 2);
        QVERIFY(rows[0].key() != rows[1].key());
        QCOMPARE(rows[0].credit, CAmount{11});
        QCOMPARE(rows[1].credit, CAmount{22}); // A receipt to change is still incoming.
        QVERIFY(rows[0].qualifiesAsReceipt());
        const auto other_session{DecomposeWalletTransaction(18, wallet_tx, status)};
        QVERIFY(rows[0].key() != other_session[0].key());
        status.depth_in_main_chain = -1;
        const auto conflict{DecomposeWalletTransaction(17, wallet_tx, status)};
        QCOMPARE(conflict[0].key(), rows[0].key());
        QVERIFY(!conflict[0].qualifiesAsReceipt());
    }

    void outgoingAndSelfPaymentAccountForFeeOnce()
    {
        CMutableTransaction tx;
        tx.vin.resize(1);
        tx.vout.emplace_back(20, CScript{});
        tx.vout.emplace_back(30, CScript{});
        tx.vout.emplace_back(40, CScript{});
        interfaces::WalletTx wallet_tx{};
        wallet_tx.tx = MakeTransactionRef(tx);
        wallet_tx.txin_is_mine = {true};
        wallet_tx.txout_is_mine = {false, true, true};
        wallet_tx.txout_is_change = {false, false, true};
        wallet_tx.debit = 100;
        wallet_tx.credit = 70;
        interfaces::WalletTxStatus status{};
        const auto rows{DecomposeWalletTransaction(1, wallet_tx, status)};
        QCOMPARE(rows.size(), 2);
        QCOMPARE(rows[0].kind, TransactionRecord::Outgoing);
        QCOMPARE(rows[0].netAmount(), CAmount{-30});
        QCOMPARE(rows[0].fee, CAmount{10});
        QCOMPARE(rows[1].kind, TransactionRecord::SelfPayment);
        QCOMPARE(rows[1].netAmount(), CAmount{0});
        QCOMPARE(rows[1].fee, CAmount{0});
    }

    void detailsAndStatusUpdateWithoutLosingRows()
    {
        TransactionHistoryModel history;
        TransactionRecord row;
        row.session_id = 1;
        row.txid = QString(64, QLatin1Char('a'));
        row.credit = 100;
        row.kind = TransactionRecord::Incoming;
        const auto key{row.key()};
        history.setRecords({row});
        QCOMPARE(history.rowCount(), 1);
        QCOMPARE(history.details(key).value(QStringLiteral("status")).toString(), QStringLiteral("Unconfirmed"));
        row.confirmations = 6;
        row.label = QStringLiteral("Updated label");
        history.setRecords({row});
        QCOMPARE(history.details(key).value(QStringLiteral("status")).toString(), QStringLiteral("Confirmed"));
        QCOMPARE(history.data(history.index(0), TransactionHistoryModel::LabelRole).toString(), row.label);
        QVERIFY(history.details(QStringLiteral("missing")).isEmpty());
        history.setDisplayUnit(3);
        QVERIFY(!history.data(history.index(0), TransactionHistoryModel::AmountDisplayRole).toString().isEmpty());
    }
};
BITCOINQML_REGISTER_QT_TEST(TransactionHistoryTests)
#include <test_transactionhistory.moc>
