// Copyright (c) 2025 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <QLocale>
#include <QScopeGuard>
#include <QPointer>
#include <memory>

#include <qml/bitcoinamount.h>
#include <qml/amountparse.h>

class BitcoinAmountTests : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void sharedAdmissionGatesFenceAllEditorIntakes_data();
    void sharedAdmissionGatesFenceAllEditorIntakes();
    void reentrantOwnerCloseStopsFurtherPresentation();
    void capturedLiteralBudget_data();
    void capturedLiteralBudget();
    void capturedSelectionAndRevision_data();
    void capturedSelectionAndRevision();
    void capturedInputPublicationCanCloseOrSupersede_data();
    void capturedInputPublicationCanCloseOrSupersede();
    void notificationCanDestroyTheAmount_data();
    void notificationCanDestroyTheAmount();
    void commitDoesNotAccessASourceDestroyedByTheTarget();
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

void BitcoinAmountTests::sharedAdmissionGatesFenceAllEditorIntakes_data()
{
    QTest::addColumn<bool>("application");
    QTest::newRow("application-quiet") << true;
    QTest::newRow("owner-quiet") << false;
}
void BitcoinAmountTests::sharedAdmissionGatesFenceAllEditorIntakes()
{
    QFETCH(bool, application);
    auto gate = std::make_shared<std::atomic_bool>(false);
    const auto reset = qScopeGuard([] { BitcoinAmount::setDefaultAdmissionGate({}); });
    if (application) BitcoinAmount::setDefaultAdmissionGate(gate);
    BitcoinAmount amount, target;
    if (!application) amount.setOwnerAdmissionGate(gate);
    amount.setSatoshi(123); target.setSatoshi(456);
    const auto input = amount.inputText(); const auto revision = amount.inputRevision();
    const auto unit = amount.unit(); const auto ticket = amount.inputOperationId();
    QSignalSpy changed{&amount, &BitcoinAmount::amountChanged};
    QSignalSpy input_changed{&amount, &BitcoinAmount::inputChanged};
    QSignalSpy operation_changed{&amount, &BitcoinAmount::inputOperationChanged};
    *gate = true;
    amount.setSatoshi(999); amount.clear(); amount.fromDisplay("1"); amount.editInput("2");
    amount.setUnit(BitcoinAmount::Unit::SAT); amount.flipUnit(); amount.setZeroIsEmpty(true);
    amount.resetInput(); amount.restoreInput(); amount.format();
    QCOMPARE(amount.requestPaste("3", 0, 0), quint64{0});
    QCOMPARE(amount.requestEditInput("4"), quint64{0});
    QVERIFY(!amount.commitTo(&target));
    QCOMPARE(amount.satoshi(), qint64{123}); QCOMPARE(target.satoshi(), qint64{456});
    QCOMPARE(amount.inputText(), input); QCOMPARE(amount.inputRevision(), revision);
    QCOMPARE(amount.unit(), unit); QCOMPARE(amount.inputOperationId(), ticket); QVERIFY(!amount.zeroIsEmpty());
    QCOMPARE(changed.count(), 0); QCOMPARE(input_changed.count(), 0); QCOMPARE(operation_changed.count(), 0);
}
void BitcoinAmountTests::reentrantOwnerCloseStopsFurtherPresentation()
{
    auto gate = std::make_shared<std::atomic_bool>(false); BitcoinAmount amount;
    amount.setOwnerAdmissionGate(gate);
    QSignalSpy changed{&amount, &BitcoinAmount::amountChanged};
    QSignalSpy display{&amount, &BitcoinAmount::displayChanged};
    connect(&amount, &BitcoinAmount::amountChanged, &amount, [gate] { *gate = true; });
    amount.setSatoshi(123);
    QCOMPARE(amount.satoshi(), qint64{123}); QCOMPARE(changed.count(), 1); QCOMPARE(display.count(), 0);
    amount.setSatoshi(999); QCOMPARE(amount.satoshi(), qint64{123});
}

void BitcoinAmountTests::capturedLiteralBudget_data()
{
    QTest::addColumn<bool>("paste"); QTest::addColumn<QString>("literal"); QTest::addColumn<bool>("accepted");
    for (bool paste : {false, true}) {
        const auto prefix = paste ? QStringLiteral("paste-") : QStringLiteral("native-edit-");
        QTest::newRow(qPrintable(prefix + "boundary128")) << paste << (QStringLiteral("0.1") + QString(125, u' ')) << true;
        QTest::newRow(qPrintable(prefix + "boundary129")) << paste << (QStringLiteral("0.1") + QString(126, u' ')) << false;
        QTest::newRow(qPrintable(prefix + "hidden-numeric-tail")) << paste << (QStringLiteral("0.1") + QString(200000, u' ') + "9") << false;
        QTest::newRow(qPrintable(prefix + "oversized-leading-space")) << paste << (QString(200000, u' ') + "0.1") << false;
    }
}
void BitcoinAmountTests::capturedLiteralBudget()
{
    QFETCH(bool, paste); QFETCH(QString, literal); QFETCH(bool, accepted);
    BitcoinAmount amount, target;
    amount.setSatoshi(2 * COIN); target.setSatoshi(123);
    const auto before = amount.satoshi();
    const auto revision = amount.inputRevision();
    const auto old_text = amount.inputText();
    QSignalSpy completed{&amount, &BitcoinAmount::inputOperationChanged};
    const auto ticket = paste ? amount.requestPaste(literal, 0, static_cast<int>(old_text.size()), revision)
        : amount.requestEditInput(literal, revision);
    QVERIFY(ticket); QCOMPARE(amount.inputOperationId(), ticket); QCOMPARE(completed.size(), 1);
    QVERIFY(!amount.inputPending()); QVERIFY(amount.inputRevision() != revision);
    if (accepted) {
        QCOMPARE(amount.inputOperationState(), BitcoinAmount::InputOperationState::InputApplied);
        QCOMPARE(amount.satoshi(), qint64{COIN / 10}); QVERIFY(amount.commitTo(&target));
        QCOMPARE(target.satoshi(), qint64{COIN / 10});
    } else {
        QCOMPARE(amount.inputOperationState(), BitcoinAmount::InputOperationState::InputRejected);
        QCOMPARE(amount.inputState(), BitcoinAmount::InputState::Invalid);
        QCOMPARE(amount.inputText().size(), qsizetype{129});
        QCOMPARE(amount.satoshi(), before); QVERIFY(!amount.inputOperationError().isEmpty());
        QVERIFY(!amount.commitTo(&target)); QCOMPARE(target.satoshi(), qint64{123});
        QVERIFY(amount.inputCursorPosition() <= 129);
        if (literal.startsWith("0.1")) {
            // Truncating/then trimming this original would manufacture a valid
            // amount. The exact-source request must still refuse it.
            QCOMPARE(amountinput::Parse(QStringView{literal}.left(128), 8, u'.').state, amountinput::State::Valid);
        }
    }
}
void BitcoinAmountTests::capturedSelectionAndRevision_data()
{
    QTest::addColumn<int>("change");
    QTest::newRow("keyboard-edit") << 0;
    QTest::newRow("unit") << 1;
    QTest::newRow("canonical") << 2;
    QTest::newRow("reset") << 3;
    QTest::newRow("clear") << 4;
}
void BitcoinAmountTests::capturedSelectionAndRevision()
{
    QFETCH(int, change);
    BitcoinAmount amount;
    amount.editInput("123");
    const auto applied = amount.requestPaste("4", 1, 2, amount.inputRevision());
    QVERIFY(applied); QCOMPARE(amount.inputText(), QStringLiteral("143")); QCOMPARE(amount.satoshi(), qint64{143 * COIN});
    const auto captured_revision = amount.inputRevision();
    if (change == 0) amount.editInput("12");
    if (change == 1) amount.setUnit(BitcoinAmount::Unit::SAT);
    if (change == 2) amount.setSatoshi(456);
    if (change == 3) amount.resetInput();
    if (change == 4) amount.clear();
    const auto retained = amount.satoshi(); const auto text = amount.inputText(); const auto state = amount.inputState(); const auto unit = amount.unit();
    const auto stale = amount.requestPaste("9", 0, 1, captured_revision);
    QVERIFY(stale); QCOMPARE(amount.inputOperationState(), BitcoinAmount::InputOperationState::InputStale);
    QCOMPARE(amount.satoshi(), retained); QCOMPARE(amount.inputText(), text); QCOMPARE(amount.inputState(), state); QCOMPARE(amount.unit(), unit);
    const auto latest = amount.requestEditInput("2", amount.inputRevision());
    QVERIFY(latest > stale); QVERIFY(!amount.cancelInput(stale)); QVERIFY(!amount.cancelInput(latest));
    QCOMPARE(amount.inputOperationId(), latest); QCOMPARE(amount.inputOperationState(), BitcoinAmount::InputOperationState::InputApplied);
    QCOMPARE(amount.satoshi(), change == 1 ? qint64{2} : qint64{2 * COIN});
    QSignalSpy quiet{&amount, &BitcoinAmount::inputOperationChanged};
    amount.closeAdmission(); QCOMPARE(quiet.size(), 0);
    QCOMPARE(amount.requestPaste("1", 0, 1), quint64{0}); QCOMPARE(amount.requestEditInput("1"), quint64{0});
    QCOMPARE(quiet.size(), 0);
}
void BitcoinAmountTests::capturedInputPublicationCanCloseOrSupersede_data()
{
    QTest::addColumn<int>("action");
    QTest::newRow("destroy") << 0;
    QTest::newRow("newer-request") << 1;
    QTest::newRow("quiet-close") << 2;
}
void BitcoinAmountTests::capturedInputPublicationCanCloseOrSupersede()
{
    QFETCH(int, action);
    auto amount = std::make_unique<BitcoinAmount>(); amount->editInput("1");
    const QPointer<BitcoinAmount> held{amount.get()};
    bool acted{false}; quint64 newer{0};
    if (action == 0) connect(amount.get(), &BitcoinAmount::inputOperationChanged, this, [&] { amount.reset(); });
    if (action == 1) connect(amount.get(), &BitcoinAmount::amountChanged, this, [&] {
        if (acted) return;
        acted = true; newer = amount->requestEditInput("3", amount->inputRevision());
    });
    if (action == 2) connect(amount.get(), &BitcoinAmount::inputChanged, this, [&] { amount->closeAdmission(); });
    const auto original = amount->requestPaste("2", 0, 1, amount->inputRevision());
    QVERIFY(original);
    if (action == 0) QVERIFY(!held);
    else if (action == 1) { QVERIFY(newer > original); QCOMPARE(amount->inputOperationId(), newer); QCOMPARE(amount->satoshi(), qint64{3 * COIN}); }
    else { QCOMPARE(amount->inputOperationState(), BitcoinAmount::InputOperationState::InputClosed); QCOMPARE(amount->satoshi(), qint64{2 * COIN}); }
}

void BitcoinAmountTests::notificationCanDestroyTheAmount_data()
{
    QTest::addColumn<int>("action"); QTest::addColumn<QByteArray>("signal");
    QTest::newRow("canonical-input") << 0 << QByteArray{SIGNAL(inputChanged())};
    QTest::newRow("canonical-amount") << 0 << QByteArray{SIGNAL(amountChanged())};
    QTest::newRow("canonical-display") << 0 << QByteArray{SIGNAL(displayChanged())};
    QTest::newRow("clear-input") << 1 << QByteArray{SIGNAL(inputChanged())};
    QTest::newRow("clear-amount") << 1 << QByteArray{SIGNAL(amountChanged())};
    QTest::newRow("unit-input") << 2 << QByteArray{SIGNAL(inputChanged())};
    QTest::newRow("unit-unit") << 2 << QByteArray{SIGNAL(unitChanged())};
    QTest::newRow("text-amount") << 3 << QByteArray{SIGNAL(amountChanged())};
    QTest::newRow("reset-dirty") << 4 << QByteArray{SIGNAL(inputDirtyChanged())};
    QTest::newRow("format-input") << 5 << QByteArray{SIGNAL(inputChanged())};
}

void BitcoinAmountTests::notificationCanDestroyTheAmount()
{
    QFETCH(int, action); QFETCH(QByteArray, signal);
    auto* amount = new BitcoinAmount;
    amount->setUnit(BitcoinAmount::Unit::SAT); amount->setSatoshi(1); amount->editInput("2");
    const QPointer<BitcoinAmount> held{amount};
    // QObject deletion is direct; deleteLater would not exercise continuation
    // after a signal's synchronous receiver destroys its sender.
    // A separate observer deletes the sender synchronously at the selected edge.
    auto destroy = [amount] { delete amount; };
    if (signal == SIGNAL(inputChanged())) connect(amount, &BitcoinAmount::inputChanged, destroy);
    else if (signal == SIGNAL(amountChanged())) connect(amount, &BitcoinAmount::amountChanged, destroy);
    else if (signal == SIGNAL(displayChanged())) connect(amount, &BitcoinAmount::displayChanged, destroy);
    else if (signal == SIGNAL(unitChanged())) connect(amount, &BitcoinAmount::unitChanged, destroy);
    else connect(amount, &BitcoinAmount::inputDirtyChanged, destroy);
    switch (action) {
    case 0: amount->setSatoshi(3); break;
    case 1: amount->clear(); break;
    case 2: amount->setUnit(BitcoinAmount::Unit::BTC); break;
    case 3: amount->editInput("3"); break;
    case 4: amount->resetInput(); break;
    case 5: amount->format(); break;
    }
    QVERIFY(!held);
}

void BitcoinAmountTests::commitDoesNotAccessASourceDestroyedByTheTarget()
{
    auto* source = new BitcoinAmount; source->setSatoshi(12);
    const QPointer<BitcoinAmount> held{source}; BitcoinAmount target;
    connect(&target, &BitcoinAmount::amountChanged, [source] { delete source; });
    QVERIFY(!source->commitTo(&target)); QVERIFY(!held);
    QCOMPARE(target.satoshi(), qint64{12});
}

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
