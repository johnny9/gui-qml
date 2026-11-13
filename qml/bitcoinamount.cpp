// Copyright (c) 2024-2025 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/bitcoinamount.h>
#include <qml/amountparse.h>

#include <qml/bitcoinunits.h>

#include <cassert>
#include <algorithm>

#include <QLocale>
#include <QPointer>

namespace {
std::shared_ptr<std::atomic_bool>& DefaultAdmissionGate()
{
    static std::shared_ptr<std::atomic_bool> gate;
    return gate; // GUI composition and construction only.
}
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
    : QObject(parent), m_app_gate(DefaultAdmissionGate())
{
}

void BitcoinAmount::setDefaultAdmissionGate(std::shared_ptr<std::atomic_bool> gate)
{
    DefaultAdmissionGate() = std::move(gate);
}

qint64 BitcoinAmount::satoshi() const
{
    return m_satoshi;
}

void BitcoinAmount::assignCanonical(qint64 value, bool is_set, bool reset_input)
{
    if (admissionClosed()) return;
    const QPointer<BitcoinAmount> observer{this};
    const bool changed = m_satoshi != value || m_isSet != is_set;
    const bool label_changes = (m_unit == Unit::SAT) && ((qAbs(m_satoshi) == 1) != (qAbs(value) == 1));
    m_satoshi = value;
    m_isSet = is_set;
    if (reset_input) resetInput();
    if (!observer || admissionClosed()) return;
    if (!changed) return;
    if (label_changes) Q_EMIT unitChanged();
    if (!observer || admissionClosed()) return;
    Q_EMIT amountChanged();
    if (!observer || admissionClosed()) return;
    Q_EMIT displayChanged();
    if (!observer || admissionClosed()) return;
    Q_EMIT displayWithUnitChanged();
}

void BitcoinAmount::setSatoshi(qint64 value)
{
    if (admissionClosed()) return;
    // External canonical intent also retires an invalid or incomplete edit.
    assignCanonical(value, true, true);
}

void BitcoinAmount::clear()
{
    if (admissionClosed()) return;
    assignCanonical(0, false, true);
}

BitcoinAmount::Unit BitcoinAmount::unit() const
{
    return m_unit;
}

void BitcoinAmount::setUnit(const Unit unit)
{
    if (admissionClosed()) return;
    if (unit < Unit::BTC || unit > Unit::SAT || m_unit == unit) return;
    ++m_input_revision;
    m_unit = unit;
    if (inputAcceptable()) {
        m_input_text = formattedInput();
        if (!m_input_dirty) m_baseline_text = m_input_text;
    }
    const QPointer<BitcoinAmount> observer{this};
    Q_EMIT inputChanged();
    if (!observer || admissionClosed()) return;
    Q_EMIT unitChanged();
    if (!observer || admissionClosed()) return;
    Q_EMIT displayChanged();
    if (!observer || admissionClosed()) return;
    Q_EMIT displayWithUnitChanged();
}

QString BitcoinAmount::unitLabel() const
{
    return QmlBitcoinUnits::label(ToQmlUnit(m_unit), m_satoshi);
}

void BitcoinAmount::flipUnit()
{
    if (admissionClosed()) return;
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
    if (admissionClosed()) return;
    ++m_input_revision;
    const QPointer<BitcoinAmount> observer{this};
    const bool was_dirty = m_input_dirty;
    m_input_text = formattedInput();
    m_baseline_text = m_input_text;
    m_input_state = m_input_text.isEmpty() ? InputState::Empty : InputState::Valid;
    m_input_dirty = false;
    if (was_dirty) Q_EMIT inputDirtyChanged();
    if (!observer || admissionClosed()) return;
    Q_EMIT inputChanged();
}

void BitcoinAmount::setZeroIsEmpty(bool empty)
{
    if (admissionClosed()) return;
    if (m_zero_is_empty == empty) return;
    ++m_input_revision;
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
    if (admissionClosed()) return;
    ++m_input_revision;
    const QPointer<BitcoinAmount> observer{this};
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
    if (!observer || admissionClosed()) return;
    if (was_dirty != m_input_dirty) Q_EMIT inputDirtyChanged();
    if (!observer || admissionClosed()) return;
    Q_EMIT inputChanged();
}

quint64 BitcoinAmount::beginInputRequest(qint64 expected_revision)
{
    if (admissionClosed()) return 0;
    const auto ticket = ++m_input_operation_id;
    m_input_operation_error.clear(); m_input_operation_state = InputOperationState::InputIdle;
    if (expected_revision >= 0 && static_cast<quint64>(expected_revision) != m_input_revision) {
        m_input_operation_state = InputOperationState::InputStale;
        m_input_operation_error = tr("The amount editor changed before the paste was applied.");
        Q_EMIT inputOperationChanged(); return ticket;
    }
    return ticket;
}
quint64 BitcoinAmount::applyInputRequest(const QString& text, bool over_budget, int cursor, quint64 ticket)
{
    if (admissionClosed()) return 0;
    const auto parsed = over_budget ? amountinput::Result{amountinput::State::Invalid}
        : amountinput::Parse(text, UnitDecimals(m_unit), QLocale().decimalPoint().at(0));
    m_input_operation_state = parsed.state == amountinput::State::Valid || parsed.state == amountinput::State::Empty
        ? InputOperationState::InputApplied : InputOperationState::InputRejected;
    if (over_budget) m_input_operation_error = tr("Amount input exceeds the 128-character limit.");
    else if (parsed.state == amountinput::State::Incomplete) m_input_operation_error = tr("Complete the amount.");
    else if (parsed.state == amountinput::State::Invalid) m_input_operation_error = tr("Enter a valid amount.");
    m_input_cursor = std::clamp(cursor, 0, static_cast<int>(text.size()));
    const QPointer<BitcoinAmount> observer{this};
    fromDisplay(text);
    if (observer && !admissionClosed() && m_input_operation_id == ticket) Q_EMIT inputOperationChanged();
    return ticket;
}
quint64 BitcoinAmount::requestEditInput(const QString& text, qint64 expected_revision, int cursor)
{
    const QPointer<BitcoinAmount> observer{this};
    const auto ticket = beginInputRequest(expected_revision);
    if (!observer || !ticket) return 0;
    if (ticket != m_input_operation_id || m_input_operation_state == InputOperationState::InputStale) return ticket;
    const bool over_budget = text.size() > 128;
    // The length check uses the exact captured source. Only this bounded view
    // crosses to the editor, and its extra character keeps it unambiguously invalid.
    const auto view = text.left(129);
    const int position = cursor < 0 ? static_cast<int>(view.size()) : std::min(cursor, static_cast<int>(view.size()));
    return applyInputRequest(view, over_budget, position, ticket);
}
quint64 BitcoinAmount::requestPaste(const QString& literal, int selection_start, int selection_end, qint64 expected_revision)
{
    const QPointer<BitcoinAmount> observer{this};
    const auto ticket = beginInputRequest(expected_revision);
    if (!observer || !ticket) return 0;
    if (ticket != m_input_operation_id || m_input_operation_state == InputOperationState::InputStale) return ticket;
    if (selection_start < 0 || selection_end < selection_start || selection_end > m_input_text.size()) {
        m_input_operation_state = InputOperationState::InputStale;
        m_input_operation_error = tr("The amount selection changed before the paste was applied.");
        Q_EMIT inputOperationChanged(); return ticket;
    }
    const auto remainder = m_input_text.size() - (selection_end - selection_start);
    const bool over_budget = remainder > 128 || literal.size() > 128 - remainder;
    QString view = m_input_text.left(selection_start);
    view += literal.left(129 - view.size());
    if (view.size() < 129) view += m_input_text.mid(selection_end, 129 - view.size());
    const int position = selection_start + static_cast<int>(std::min<qsizetype>(literal.size(), 129 - selection_start));
    return applyInputRequest(view, over_budget, position, ticket);
}
bool BitcoinAmount::cancelInput(quint64 ticket)
{
    // This port intentionally admits only bounded, immediately terminal work.
    // Matching cancellation cannot erase an already-applied amount or refusal,
    // and an old component can never invalidate a newer request.
    if (!ticket || ticket != m_input_operation_id) return false;
    return false;
}
void BitcoinAmount::closeAdmission()
{
    m_input_closed = true; ++m_input_revision;
    m_input_operation_state = InputOperationState::InputClosed;
}

bool BitcoinAmount::commitTo(BitcoinAmount* target)
{
    if (admissionClosed() || !target || target->admissionClosed() || !inputAcceptable() || !MoneyRange(m_satoshi)) return false;
    const QPointer<BitcoinAmount> observer{this}, receiver{target};
    if (m_input_state == InputState::Empty) target->clear();
    else target->setSatoshi(m_satoshi);
    if (!observer || !receiver || admissionClosed() || target->admissionClosed()) return false;
    resetInput();
    return observer && receiver && !admissionClosed() && !target->admissionClosed();
}

void BitcoinAmount::format()
{
    if (admissionClosed()) return;
    if (!inputAcceptable()) return;
    const QPointer<BitcoinAmount> observer{this};
    resetInput();
    if (!observer || admissionClosed()) return;
    Q_EMIT displayChanged();
    if (!observer || admissionClosed()) return;
    Q_EMIT displayWithUnitChanged();
}
