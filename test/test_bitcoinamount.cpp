// Copyright (c) 2025 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <QLocale>
#include <QScopeGuard>

#include <qml/bitcoinamount.h>

class BitcoinAmountTests : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void localizedInputKeepsExactSatoshis_data();
    void localizedInputKeepsExactSatoshis();
    void inputStatesGuardCommits();
    void canonicalAndUnitIntentsPreserveValue();
    void boundedInputAndDirtyNotifications();
    void satsToBtcString_basic();
    void satsToBtcString_negative();
    void btcToSats_roundtrip();
    void rejectMalformedInputWithoutChangingCanonicalValue();
    void display_flow_btc();
    void display_flow_mbtc_and_ubtc();
    void display_flow_sat();
    void displayWithUnit_formatsAmountAndUnit();
    void localizedDisplayWithUnit_usesLocaleAndKeepsCanonicalDisplay();
    void flipUnit_changesLabelAndDisplaySignal();
};

void BitcoinAmountTests::localizedInputKeepsExactSatoshis_data()
{
    QTest::addColumn<QString>("locale"); QTest::addColumn<QString>("text");
    QTest::newRow("german-comma") << "de_DE" << "0,02";
    QTest::newRow("french-comma") << "fr_FR" << "0,02";
    QTest::newRow("german-dot-control") << "de_DE" << "0.02";
    QTest::newRow("english-dot-control") << "en_US" << "0.02";
}

void BitcoinAmountTests::localizedInputKeepsExactSatoshis()
{
    QFETCH(QString, locale); QFETCH(QString, text);
    const QLocale previous;
    const auto restore = qScopeGuard([previous] { QLocale::setDefault(previous); });
    QLocale::setDefault(QLocale(locale));
    BitcoinAmount amount;
    amount.fromDisplay(text);
    QCOMPARE(amount.satoshi(), qint64{2'000'000});
}

void BitcoinAmountTests::inputStatesGuardCommits()
{
    const QLocale previous;
    const auto restore = qScopeGuard([previous] { QLocale::setDefault(previous); });
    QLocale::setDefault(QLocale::c());
    BitcoinAmount amount, target;
    QCOMPARE(amount.inputState(), BitcoinAmount::InputState::Empty);
    QVERIFY(amount.inputAcceptable()); QVERIFY(amount.inputError().isEmpty());
    amount.setSatoshi(2'000'000); target.setSatoshi(1234);
    amount.editInput("1.");
    QCOMPARE(amount.inputState(), BitcoinAmount::InputState::Incomplete);
    QCOMPARE(amount.satoshi(), qint64{2'000'000});
    QVERIFY(!amount.inputAcceptable()); QVERIFY(!amount.inputError().isEmpty());
    QVERIFY(!amount.commitTo(&target)); QCOMPARE(target.satoshi(), qint64{1234});
    amount.format(); QCOMPARE(amount.inputText(), QString("1."));
    amount.editInput("1.2.3");
    QCOMPARE(amount.inputState(), BitcoinAmount::InputState::Invalid);
    amount.setUnit(BitcoinAmount::Unit::SAT);
    QCOMPARE(amount.inputText(), QString("1.2.3"));
    QVERIFY(amount.inputError().contains("sats"));
    amount.setUnit(BitcoinAmount::Unit::BTC);
    QVERIFY(amount.inputError().contains("BTC"));
    QVERIFY(!amount.commitTo(&target));
    amount.setSatoshi(-1); QVERIFY(!amount.commitTo(&target));
    amount.setSatoshi(MAX_MONEY + 1); QVERIFY(!amount.commitTo(&target));
    amount.editInput("0.02"); QVERIFY(amount.inputAcceptable());
    QVERIFY(!amount.commitTo(nullptr));
    QVERIFY(amount.commitTo(&target));
    QCOMPARE(target.satoshi(), qint64{2'000'000});
    QVERIFY(!amount.inputDirty()); QCOMPARE(amount.inputText(), QString("0.02000000"));
    amount.editInput("0.03"); QVERIFY(amount.commitTo(&amount));
    QCOMPARE(amount.satoshi(), qint64{3'000'000}); QVERIFY(!amount.inputDirty());
    amount.editInput(""); QVERIFY(amount.inputAcceptable()); QVERIFY(amount.inputDirty());
    QVERIFY(amount.commitTo(&target)); QVERIFY(!target.isSet()); QCOMPARE(target.satoshi(), qint64{0});
    amount.editInput("junk"); amount.clear();
    QCOMPARE(amount.inputState(), BitcoinAmount::InputState::Empty);
    QVERIFY(amount.inputText().isEmpty()); QVERIFY(!amount.inputDirty());
}

void BitcoinAmountTests::canonicalAndUnitIntentsPreserveValue()
{
    BitcoinAmount amount;
    amount.setSatoshi(2'000'000);
    amount.editInput("12"); QVERIFY(amount.inputDirty());
    amount.setUnit(BitcoinAmount::Unit::SAT);
    QCOMPARE(amount.satoshi(), qint64{1'200'000'000});
    QCOMPARE(amount.inputText(), QString("1200000000")); QVERIFY(amount.inputDirty());
    amount.format(); QVERIFY(!amount.inputDirty());
    amount.flipUnit(); QCOMPARE(amount.satoshi(), qint64{1'200'000'000});
    amount.setUnit(BitcoinAmount::Unit::BTC);
    amount.setUnit(static_cast<BitcoinAmount::Unit>(-1));
    amount.setUnit(static_cast<BitcoinAmount::Unit>(4));
    QCOMPARE(amount.unit(), BitcoinAmount::Unit::BTC);
    amount.setZeroIsEmpty(true); amount.setSatoshi(0);
    QVERIFY(amount.zeroIsEmpty());
    QCOMPARE(amount.inputState(), BitcoinAmount::InputState::Empty);
    QVERIFY(amount.inputText().isEmpty());
    amount.setZeroIsEmpty(false); QVERIFY(!amount.inputText().isEmpty());
    QVERIFY(!amount.zeroIsEmpty());
    amount.editInput("2"); amount.setZeroIsEmpty(true);
    QCOMPARE(amount.inputText(), QString("2")); QVERIFY(amount.inputDirty());
    amount.setZeroIsEmpty(false); QCOMPARE(amount.inputText(), QString("2"));
    amount.setZeroIsEmpty(false);
    amount.editInput("oops"); amount.setSatoshi(amount.satoshi());
    QCOMPARE(amount.inputState(), BitcoinAmount::InputState::Valid); QVERIFY(!amount.inputDirty());
    amount.setUnit(BitcoinAmount::Unit::SAT); amount.setSatoshi(1); amount.setSatoshi(-1);
    QCOMPARE(amount.unitLabel(), QString("sat"));
    amount.setSatoshi(2); QCOMPARE(amount.unitLabel(), QString("sats"));
}

void BitcoinAmountTests::boundedInputAndDirtyNotifications()
{
    BitcoinAmount amount;
    QSignalSpy dirty{&amount, &BitcoinAmount::inputDirtyChanged};
    QSignalSpy input{&amount, &BitcoinAmount::inputChanged};
    amount.setSatoshi(2'000'000); amount.resetInput();
    QCOMPARE(dirty.count(), 0);
    amount.editInput("0.02"); QCOMPARE(dirty.count(), 1);
    amount.editInput("0.03"); QCOMPARE(dirty.count(), 1);
    amount.format(); QCOMPARE(dirty.count(), 2);
    amount.resetInput(); amount.setSatoshi(amount.satoshi()); QCOMPARE(dirty.count(), 2);
    const auto count = input.count(); amount.restoreInput(); QCOMPARE(input.count(), count + 1);
    amount.editInput(QString(1000, u'9'));
    QCOMPARE(amount.inputState(), BitcoinAmount::InputState::Invalid);
    QCOMPARE(amount.inputText().size(), qsizetype{129});
    QCOMPARE(amount.satoshi(), qint64{3'000'000});
    amount.format(); QVERIFY(!amount.inputAcceptable());
    amount.clear(); amount.clear(); QVERIFY(amount.inputText().isEmpty());
}

void BitcoinAmountTests::satsToBtcString_basic()
{
    QCOMPARE(BitcoinAmount::satsToBtcString(0), QString("0.00000000"));
    QCOMPARE(BitcoinAmount::satsToBtcString(1), QString("0.00000001"));
    QCOMPARE(BitcoinAmount::satsToBtcString(COIN), QString("1.00000000"));
    QCOMPARE(BitcoinAmount::satsToBtcString(21 * COIN), QString("21.00000000"));
    QCOMPARE(BitcoinAmount::satsToBtcString(100000123), QString("1.00000123"));
}

void BitcoinAmountTests::satsToBtcString_negative()
{
    QCOMPARE(BitcoinAmount::satsToBtcString(-1), QString("-0.00000001"));
}

void BitcoinAmountTests::btcToSats_roundtrip()
{
    BitcoinAmount amt;
    amt.setUnit(BitcoinAmount::Unit::BTC);
    amt.fromDisplay("1.23456789");
    QCOMPARE(amt.toDisplay(), QString("1.23456789"));
}

void BitcoinAmountTests::rejectMalformedInputWithoutChangingCanonicalValue()
{
    BitcoinAmount amt;
    amt.setUnit(BitcoinAmount::Unit::BTC);
    amt.fromDisplay("1.2");
    amt.fromDisplay("abc1.2.3xyz");
    QCOMPARE(amt.toDisplay(), QString("1.20000000"));
    QCOMPARE(amt.inputState(), BitcoinAmount::InputState::Invalid);
    QVERIFY(!amt.inputAcceptable());
    amt.format();
    QCOMPARE(amt.inputText(), QString("abc1.2.3xyz"));
}

void BitcoinAmountTests::display_flow_btc()
{
    BitcoinAmount amt;
    amt.setUnit(BitcoinAmount::Unit::BTC);
    amt.setSatoshi(2 * COIN);
    QCOMPARE(amt.toDisplay(), QString("2.00000000"));
}

void BitcoinAmountTests::display_flow_mbtc_and_ubtc()
{
    BitcoinAmount amt;
    amt.setUnit(BitcoinAmount::Unit::MilliBTC);
    amt.setSatoshi(COIN);
    QCOMPARE(amt.toDisplay(), QString("1000.00000"));
    QCOMPARE(amt.unitLabel(), QString("mBTC"));

    amt.fromDisplay("1.23456");
    amt.fromDisplay("1.23456789");
    QCOMPARE(amt.inputState(), BitcoinAmount::InputState::Invalid);
    QCOMPARE(amt.satoshi(), qint64{123456});
    QCOMPARE(amt.toDisplay(), QString("1.23456"));

    amt.setUnit(BitcoinAmount::Unit::MicroBTC);
    QCOMPARE(amt.toDisplay(), QString("1234.56"));
    QCOMPARE(amt.unitLabel(), QString("bits"));

    amt.fromDisplay("1.23");
    amt.fromDisplay("1.239");
    QCOMPARE(amt.inputState(), BitcoinAmount::InputState::Invalid);
    QCOMPARE(amt.satoshi(), qint64{123});
    QCOMPARE(amt.toDisplay(), QString("1.23"));
}

void BitcoinAmountTests::display_flow_sat()
{
    BitcoinAmount amt;
    amt.setUnit(BitcoinAmount::Unit::SAT);
    amt.setSatoshi(123);
    QCOMPARE(amt.toDisplay(), QString("123"));
}

void BitcoinAmountTests::displayWithUnit_formatsAmountAndUnit()
{
    BitcoinAmount amt;
    QVERIFY(amt.displayWithUnit().isEmpty());

    amt.setSatoshi(COIN);
    QCOMPARE(amt.displayWithUnit(), QStringLiteral("1.00000000 BTC"));

    amt.setUnit(BitcoinAmount::Unit::SAT);
    QCOMPARE(amt.displayWithUnit(), QStringLiteral("100000000 sats"));

    amt.setSatoshi(1);
    QCOMPARE(amt.displayWithUnit(), QStringLiteral("1 sat"));
}

void BitcoinAmountTests::localizedDisplayWithUnit_usesLocaleAndKeepsCanonicalDisplay()
{
    const QLocale previous;
    const auto restore_locale = qScopeGuard([previous] { QLocale::setDefault(previous); });
    QLocale::setDefault(QLocale{"de_DE"});

    BitcoinAmount amount;
    QVERIFY(amount.localizedDisplayWithUnit().isEmpty());
    QVERIFY(amount.localizedDisplay().isEmpty());
    amount.setSatoshi(123'456'789);
    QCOMPARE(amount.localizedDisplayWithUnit(), QStringLiteral("1,23456789 BTC"));
    QCOMPARE(amount.localizedDisplay(), QStringLiteral("1,23456789"));
    QCOMPARE(amount.displayWithUnit(), QStringLiteral("1.23456789 BTC"));

    amount.setUnit(BitcoinAmount::Unit::SAT);
    QCOMPARE(amount.localizedDisplayWithUnit(), QStringLiteral("123.456.789 sats"));
    QCOMPARE(amount.localizedDisplay(), QStringLiteral("123.456.789"));
    QCOMPARE(amount.displayWithUnit(), QStringLiteral("123456789 sats"));
}

void BitcoinAmountTests::flipUnit_changesLabelAndDisplaySignal()
{
    BitcoinAmount amt;
    amt.setSatoshi(100);
    QSignalSpy spy(&amt, &BitcoinAmount::displayChanged);
    amt.flipUnit();
    QVERIFY(spy.count() >= 1);
}

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(BitcoinAmountTests)
#else
QTEST_MAIN(BitcoinAmountTests)
#endif
#include <test_bitcoinamount.moc>
