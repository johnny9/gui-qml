// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/wallet/sendrecipientslistmodel.h>
#include <qml/test/qt_test_registry.h>
#include <key_io.h>
#include <policy/policy.h>
#include <test/util/setup_common.h>
#include <QAbstractItemModelTester>
#include <QTest>

class SendRecipientsTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void recipientCountIsBoundedAndCanRecover()
    {
        BasicTestingSetup setup{ChainType::REGTEST};
        SendRecipientsListModel recipients(CFeeRate{DUST_RELAY_TX_FEE});
        QAbstractItemModelTester consistent(&recipients, QAbstractItemModelTester::FailureReportingMode::QtTest);
        for (int i = 1; i < 25; ++i) { QVERIFY(recipients.canAdd()); recipients.add(); }
        QCOMPARE(recipients.rowCount(), 25);
        QVERIFY(!recipients.canAdd());
        recipients.add();
        QCOMPARE(recipients.rowCount(), 25);
        recipients.remove(24);
        QVERIFY(recipients.canAdd());
        recipients.add();
        QCOMPARE(recipients.rowCount(), 25);
        recipients.clear();
        QCOMPARE(recipients.rowCount(), 1);
        QVERIFY(recipients.canAdd());
    }

    void validationAndExactValues()
    {
        BasicTestingSetup setup{ChainType::REGTEST};
        SendRecipientsListModel recipients(CFeeRate{DUST_RELAY_TX_FEE});
        QAbstractItemModelTester consistent(&recipients, QAbstractItemModelTester::FailureReportingMode::QtTest);
        QVERIFY(!recipients.valid());
        recipients.setAddress(0, QString::fromStdString(EncodeDestination(WitnessV0KeyHash(uint160{}))));
        for (const QString amount : {"", "-1", "0", "1e-3", "0.123456789", "21000001", "0.00000001"}) {
            recipients.setAmount(0, amount);
            QVERIFY2(!recipients.valid(), qPrintable(amount));
        }
        recipients.setAmount(0, "1.00000001");
        QVERIFY(recipients.valid());
        QCOMPARE(recipients.snapshot().front().amount, CAmount{100000001});
        recipients.setSubtractFee(0, true);
        QVERIFY(recipients.snapshot().front().subtract_fee);
        recipients.setAmount(99, "2");
        QCOMPARE(recipients.rowCount(), 1);
        QCOMPARE(recipients.snapshot().front().amount, CAmount{100000001});
    }

    void duplicateMaximumAndSnapshotIsolation()
    {
        BasicTestingSetup setup{ChainType::REGTEST};
        SendRecipientsListModel recipients(CFeeRate{DUST_RELAY_TX_FEE});
        const QString first = QString::fromStdString(EncodeDestination(WitnessV0KeyHash(uint160{})));
        recipients.setAddress(0, first);
        recipients.setAmount(0, "2");
        const auto captured = recipients.snapshot();
        recipients.setAmount(0, "3");
        QCOMPARE(captured.front().amount, 2 * COIN);
        recipients.add();
        recipients.setAddress(1, first.toUpper());
        recipients.setAmount(1, "1");
        QVERIFY(recipients.error().contains("Duplicate"));
        recipients.setAddress(1, QString::fromStdString(EncodeDestination(PKHash(uint160{}))));
        QVERIFY(recipients.valid());
        recipients.setMaximum(1, true);
        QVERIFY(recipients.valid());
        QVERIFY(recipients.snapshot()[1].subtract_fee);
        recipients.setMaximum(0, true);
        QVERIFY(!recipients.valid());
        recipients.remove(0);
        QVERIFY(recipients.valid());
        recipients.remove(0);
        QCOMPARE(recipients.rowCount(), 1);
        QVERIFY(!recipients.valid());
    }
};

BITCOINQML_REGISTER_QT_TEST(SendRecipientsTests)
#include <test_sendrecipients.moc>
