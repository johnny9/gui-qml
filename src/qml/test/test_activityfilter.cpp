// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/test/qt_test_registry.h>
#include <qml/wallet/activitycsv.h>
#include <qml/wallet/activityfilterproxymodel.h>
#include <qml/wallet/activitytimelinemodel.h>
#include <qml/wallet/receiverequesthistorymodel.h>
#include <qml/wallet/transactionhistorymodel.h>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

class ActivityFilterTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void filtersAndDeterministicOrdering()
    {
        TransactionHistoryModel history;
        ReceiveRequestHistoryModel requests;
        ActivityTimelineModel timeline{9, history, requests};
        ActivityFilterProxyModel filter{timeline};
        TransactionRecord incoming;
        incoming.session_id = 9;
        incoming.txid = "a";
        incoming.time = 1704067200; // 2024-01-01 UTC
        incoming.kind = TransactionRecord::Incoming;
        incoming.credit = 123;
        incoming.label = "Coffee";
        auto outgoing{incoming};
        outgoing.txid = "b";
        outgoing.kind = TransactionRecord::Outgoing;
        outgoing.credit = 0;
        outgoing.debit = -1;
        outgoing.message = "Lunch";
        history.setRecords({outgoing, incoming});
        QmlRecentRequestEntry request;
        request.id = 7;
        request.date = QDateTime::fromSecsSinceEpoch(1704153600);
        request.recipient.amount = 42;
        request.recipient.address = "request-address";
        requests.setEntries({request});
        QCOMPARE(filter.rowCount(), 3);
        QCOMPARE(filter.snapshot()[0].value("rowKey").toString(), QStringLiteral("9:request:7"));
        QCOMPARE(filter.snapshot()[1].value("txid").toString(), QStringLiteral("a"));
        QCOMPARE(filter.snapshot()[2].value("txid").toString(), QStringLiteral("b"));
        filter.setSearchText("cOFfEe");
        QCOMPARE(filter.rowCount(), 2);
        filter.setSearchText("LUNCH");
        QCOMPARE(filter.rowCount(), 1);
        filter.setSearchText("");
        filter.setTypeFilter(ActivityFilterProxyModel::Requests);
        QCOMPARE(filter.rowCount(), 1);
        QVERIFY(!filter.snapshot()[0].contains("amountSat"));
        filter.setTypeFilter(ActivityFilterProxyModel::All);
        filter.setFromDate("2024-01-02");
        QCOMPARE(filter.rowCount(), 1);
        filter.setThroughDate("2024-01-01");
        QCOMPARE(filter.rowCount(), 0);
        filter.setFromDate("");
        QCOMPARE(filter.rowCount(), 2);
        filter.setThroughDate("bad-date");
        QCOMPARE(filter.rowCount(), 0);
        filter.setThroughDate("");
        const auto snapshot{filter.snapshot()};
        history.setRecords({});
        requests.setEntries({});
        QCOMPARE(snapshot.size(), 3); // Captured export revision is independent.
        QCOMPARE(filter.rowCount(), 0);
        const auto csv{SerializeActivityCsv(snapshot)};
        QVERIFY(csv.indexOf("9:request:7") < csv.indexOf(incoming.key().toUtf8()));
        QVERIFY(csv.contains("\"\",\"42\""));
        QVERIFY(csv.contains("\"-1\",\"\""));
        QVERIFY(csv.contains("\"received\""));
        QVERIFY(csv.contains("\"sent\""));
        const auto btc{SerializeActivityCsv(snapshot, 0)};
        QVERIFY(btc.contains("\"Amount (BTC)\""));
        QVERIFY(btc.contains("\"0.00000123\""));
        QVERIFY(btc.contains("\"Requested amount (BTC)\""));
    }

    void escapingAndAtomicWriteErrors()
    {
        QVariantMap row{{"rowKey", "one"}, {"label", "=sum(1,2)\n\"quoted\""},
                        {"amountSat", qlonglong{-8}}, {"timestamp", qlonglong{0}}, {"pendingRequest", false}};
        const QVector<QVariantMap> rows{row};
        const auto csv{SerializeActivityCsv(rows)};
        QVERIFY(csv.contains("\"'=sum(1,2)\n\"\"quoted\"\"\""));
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QString error;
        const QString path{dir.filePath("activity.csv")};
        QVERIFY(WriteActivityCsv(path, rows, error));
        QVERIFY(error.isEmpty());
        QFile file{path};
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), csv);
        QVERIFY(!WriteActivityCsv(dir.path(), rows, error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!WriteActivityCsv("", rows, error));
    }
};
BITCOINQML_REGISTER_QT_TEST(ActivityFilterTests)
#include <test_activityfilter.moc>
