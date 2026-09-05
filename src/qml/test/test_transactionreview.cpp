// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/wallet/transactionreviewmodel.h>
#include <qml/test/qt_test_registry.h>
#include <QSignalSpy>
#include <QTest>

class TransactionReviewTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void formattingCannotChangeReviewedValues()
    {
        TransactionReviewModel model;
        QVERIFY(!model.hasReview());
        CMutableTransaction transaction;
        transaction.vout.emplace_back(1234, CScript{});
        ReviewSnapshot value{5, 6, 7, MakeTransactionRef(transaction), 88, {}, {{"destination", 1234, false}}};
        model.setSnapshot(value);
        const QString id = model.transactionId();
        value.outputs[0].amount = 9999;
        model.setDisplayUnit(3);
        QCOMPARE(model.feeText(), QString("88 sat"));
        QCOMPARE(model.outputs()[0].toMap()["amount"].toString(), QString("1234 sat"));
        QCOMPARE(model.snapshot()->outputs[0].amount, CAmount{1234});
        QCOMPARE(model.transactionId(), id);
        QCOMPARE(model.snapshot()->revision, quint64{7});
        QCOMPARE(model.snapshot()->transaction->vout[0].nValue, CAmount{1234});
        model.clear();
        QVERIFY(!model.hasReview());
        QVERIFY(model.outputs().isEmpty());
        QVERIFY(model.transactionId().isEmpty());
    }
};

BITCOINQML_REGISTER_QT_TEST(TransactionReviewTests)
#include <test_transactionreview.moc>
