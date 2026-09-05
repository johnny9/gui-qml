// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/test/qt_test_registry.h>
#include <qml/wallet/activitytimelinemodel.h>
#include <qml/wallet/transactionhistorymodel.h>
#include <qml/wallet/receiverequesthistorymodel.h>
#include <QTest>

class ActivityTimelineTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void distinctRequestsReceiptsAndDeletion()
    {
        TransactionHistoryModel history;
        ReceiveRequestHistoryModel requests;
        ActivityTimelineModel timeline{7, history, requests};
        QmlRecentRequestEntry first;
        first.id = 1;
        first.recipient.address = "shared";
        first.recipient.amount = 100;
        first.date = QDateTime::fromSecsSinceEpoch(50);
        auto second{first};
        second.id = 2;
        second.recipient.amount = 200;
        requests.setEntries({first, second});
        QCOMPARE(timeline.count(), 2);
        QVERIFY(timeline.details("7:request:1").value("pendingRequest").toBool());
        QVERIFY(!timeline.details("7:request:1").contains("amountSat"));
        TransactionRecord receipt;
        receipt.session_id = 7;
        receipt.txid = "transaction";
        receipt.output_index = 0;
        receipt.kind = TransactionRecord::Incoming;
        receipt.address = "shared";
        receipt.credit = 3; // Partial receipt associates, never allocates invoice amounts.
        auto output{receipt};
        output.output_index = 1;
        history.setRecords({receipt, output});
        QCOMPARE(timeline.count(), 2);
        QCOMPARE(timeline.details(receipt.key()).value("requestIds").toStringList(), QStringList({"1", "2"}));
        const auto snapshot{timeline.rows()};
        requests.setEntries({first, second});
        history.setRecords({receipt, output});
        QCOMPARE(timeline.rows(), snapshot);
        QVERIFY(requests.removeByRequestId("1"));
        QCOMPARE(timeline.count(), 2);
        QCOMPARE(history.count(), 2);
        QCOMPARE(timeline.details(receipt.key()).value("requestIds").toStringList(), QStringList({"2"}));
        QCOMPARE(history.records().front().credit, CAmount{3});
    }

    void conflictsAbandonmentAndReorgRecomputePending()
    {
        TransactionHistoryModel history;
        ReceiveRequestHistoryModel requests;
        ActivityTimelineModel timeline{8, history, requests};
        QmlRecentRequestEntry request;
        request.id = 41;
        request.recipient.address = "destination";
        requests.setEntries({request});
        TransactionRecord receipt;
        receipt.session_id = 8;
        receipt.txid = "receipt";
        receipt.address = "destination";
        receipt.credit = 1;
        receipt.kind = TransactionRecord::Incoming;
        history.setRecords({receipt});
        QCOMPARE(timeline.count(), 1); // Unconfirmed qualifies.
        receipt.confirmations = -1;
        history.setRecords({receipt});
        QCOMPARE(timeline.count(), 2);
        QVERIFY(!timeline.details("8:request:41").isEmpty());
        receipt.confirmations = 0;
        receipt.abandoned = true;
        history.setRecords({receipt});
        QCOMPARE(timeline.count(), 2);
        receipt.abandoned = false;
        receipt.kind = TransactionRecord::Generated;
        history.setRecords({receipt});
        QCOMPARE(timeline.count(), 2); // Orphaned coinbase does not qualify.
        receipt.in_main_chain = true;
        history.setRecords({receipt});
        QCOMPARE(timeline.count(), 1);
        history.setRecords({});
        QCOMPARE(timeline.count(), 1);
        QVERIFY(timeline.rows().front().value("pendingRequest").toBool());
        QCOMPARE(requests.count(), 1);
    }
};
BITCOINQML_REGISTER_QT_TEST(ActivityTimelineTests)
#include <test_activitytimeline.moc>
