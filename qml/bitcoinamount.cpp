// Copyright (c) 2024-2025 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/bitcoinamount.h>
#include <qml/amountparse.h>

#include <qml/bitcoinunits.h>

#include <cassert>

#include <QLocale>

namespace {
int UnitDecimals(BitcoinAmount::Unit unit)
{
    switch (unit) {
    case BitcoinAmount::Unit::BTC: return 8;
    case BitcoinAmount::Unit::MilliBTC: return 5;
    case BitcoinAmount::Unit::MicroBTC: return 2;
    case BitcoinAmount::Unit::SAT: return 0;
    }
    assert(false);
}

QmlBitcoinUnits::Unit ToQmlUnit(BitcoinAmount::Unit unit)
{
    switch (unit) {
    case BitcoinAmount::Unit::BTC: return QmlBitcoinUnits::Unit::BTC;
    case BitcoinAmount::Unit::MilliBTC: return QmlBitcoinUnits::Unit::mBTC;
    case BitcoinAmount::Unit::MicroBTC: return QmlBitcoinUnits::Unit::uBTC;
    case BitcoinAmount::Unit::SAT: return QmlBitcoinUnits::Unit::SAT;
    }
    assert(false);
}
} // namespace

BitcoinAmount::BitcoinAmount(QObject* parent)
    : QObject(parent)
{
}

qint64 BitcoinAmount::satoshi() const
{
    return m_satoshi;
}

void BitcoinAmount::assignCanonical(qint64 value, bool is_set, bool reset_input)
{
    const bool changed = m_satoshi != value || m_isSet != is_set;
    const bool label_changes = (m_unit == Unit::SAT) && ((qAbs(m_satoshi) == 1) != (qAbs(value) == 1));
    m_satoshi = value;
    m_isSet = is_set;
    if (reset_input) resetInput();
    if (!changed) return;
    if (label_changes) Q_EMIT unitChanged();
    Q_EMIT amountChanged();
    Q_EMIT displayChanged();
    Q_EMIT displayWithUnitChanged();
}

void BitcoinAmount::setSatoshi(qint64 value)
{
    // External canonical intent also retires an invalid or incomplete edit.
    assignCanonical(value, true, true);
}

void BitcoinAmount::clear()
{
    assignCanonical(0, false, true);
}

BitcoinAmount::Unit BitcoinAmount::unit() const
{
    return m_unit;
}

void BitcoinAmount::setUnit(const Unit unit)
{
    if (unit < Unit::BTC || unit > Unit::SAT || m_unit == unit) return;
    m_unit = unit;
    if (inputAcceptable()) {
        m_input_text = formattedInput();
        if (!m_input_dirty) m_baseline_text = m_input_text;
    }
    Q_EMIT inputChanged();
    Q_EMIT unitChanged();
    Q_EMIT displayChanged();
    Q_EMIT displayWithUnitChanged();
}

QString BitcoinAmount::unitLabel() const
{
    return QmlBitcoinUnits::label(ToQmlUnit(m_unit), m_satoshi);
}

void BitcoinAmount::flipUnit()
{
    setUnit(m_unit == Unit::SAT ? Unit::BTC : Unit::SAT);
}

QString BitcoinAmount::satsToBtcString(qint64 sat)
{
    const bool negative = sat < 0;
    qint64 absSat = negative ? -sat : sat;

    const qint64 wholePart = absSat / COIN;
    const qint64 fracInt = absSat % COIN;
    QString fracPart = QString("%1").arg(fracInt, 8, 10, QLatin1Char('0'));

    QString result = QString::number(wholePart) + '.' + fracPart;
    if (negative) {
        result.prepend('-');
    }
    return result;
}

QString BitcoinAmount::toDisplay() const
{
    if (!m_isSet) {
        return "";
    }
    return QmlBitcoinUnits::format(ToQmlUnit(m_unit), m_satoshi, false, QmlBitcoinUnits::SeparatorStyle::NEVER);
}

QString BitcoinAmount::displayWithUnit() const
{
    const QString display{toDisplay()};
    return display.isEmpty() ? QString{} : display + QStringLiteral(" ") + unitLabel();
}

QString BitcoinAmount::localizedDisplay() const
{
    if (!m_isSet) return {};
    return QmlBitcoinUnits::formatForDisplay(ToQmlUnit(m_unit), m_satoshi);
}

QString BitcoinAmount::localizedDisplayWithUnit() const
{
    if (!m_isSet) return {};
    const auto unit = ToQmlUnit(m_unit);
    return localizedDisplay() + QLatin1Char(' ')
        + QmlBitcoinUnits::label(unit, m_satoshi);
}

QString BitcoinAmount::formattedInput() const
{
    if (!m_isSet || (m_zero_is_empty && m_satoshi == 0)) return {};
    return toDisplay().replace(QLatin1Char('.'), QLocale().decimalPoint());
}

void BitcoinAmount::resetInput()
{
    const bool was_dirty = m_input_dirty;
    m_input_text = formattedInput();
    m_baseline_text = m_input_text;
    m_input_state = m_input_text.isEmpty() ? InputState::Empty : InputState::Valid;
    m_input_dirty = false;
    if (was_dirty) Q_EMIT inputDirtyChanged();
    Q_EMIT inputChanged();
}

void BitcoinAmount::setZeroIsEmpty(bool empty)
{
    if (m_zero_is_empty == empty) return;
    m_zero_is_empty = empty;
    if (!m_input_dirty) resetInput();
    else Q_EMIT inputChanged();
}

QString BitcoinAmount::inputError() const
{
    if (m_input_state == InputState::Incomplete) return tr("Complete the amount.");
    if (m_input_state != InputState::Invalid) return {};
    return m_unit == Unit::SAT
        ? tr("Enter a whole number of sats, up to 2,100,000,000,000,000.")
        : tr("Enter a valid amount, up to 21,000,000 BTC.");
}

void BitcoinAmount::fromDisplay(const QString& text)
{
    const auto parsed = amountinput::Parse(text, UnitDecimals(m_unit), QLocale().decimalPoint().at(0));
    static_assert(static_cast<int>(InputState::Empty) == static_cast<int>(amountinput::State::Empty));
    static_assert(static_cast<int>(InputState::Incomplete) == static_cast<int>(amountinput::State::Incomplete));
    static_assert(static_cast<int>(InputState::Invalid) == static_cast<int>(amountinput::State::Invalid));
    static_assert(static_cast<int>(InputState::Valid) == static_cast<int>(amountinput::State::Valid));
    m_input_state = static_cast<InputState>(parsed.state);
    m_input_text = text.left(129);
    const bool was_dirty = m_input_dirty;
    m_input_dirty = m_input_text != m_baseline_text;
    if (parsed.state == amountinput::State::Valid) assignCanonical(parsed.satoshi, true);
    else if (parsed.state == amountinput::State::Empty) assignCanonical(0, false);
    if (was_dirty != m_input_dirty) Q_EMIT inputDirtyChanged();
    Q_EMIT inputChanged();
}

bool BitcoinAmount::commitTo(BitcoinAmount* target)
{
    if (!target || !inputAcceptable() || !MoneyRange(m_satoshi)) return false;
    if (m_input_state == InputState::Empty) target->clear();
    else target->setSatoshi(m_satoshi);
    resetInput();
    return true;
}

void BitcoinAmount::format()
{
    if (!inputAcceptable()) return;
    resetInput();
    Q_EMIT displayChanged();
    Q_EMIT displayWithUnitChanged();
}
