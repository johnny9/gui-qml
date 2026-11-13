// Copyright (c) 2024-2025 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_BITCOINAMOUNT_H
#define BITCOIN_QML_BITCOINAMOUNT_H

#include <consensus/amount.h>

#include <QObject>
#include <QString>
#include <atomic>
#include <memory>

class BitcoinAmount : public QObject
{
    Q_OBJECT
    Q_PROPERTY(Unit unit READ unit WRITE setUnit NOTIFY unitChanged)
    Q_PROPERTY(QString unitLabel READ unitLabel NOTIFY displayWithUnitChanged)
    Q_PROPERTY(QString display READ toDisplay WRITE fromDisplay NOTIFY displayChanged)
    Q_PROPERTY(QString displayWithUnit READ displayWithUnit NOTIFY displayWithUnitChanged)
    Q_PROPERTY(QString localizedDisplay READ localizedDisplay NOTIFY displayChanged)
    Q_PROPERTY(QString localizedDisplayWithUnit READ localizedDisplayWithUnit NOTIFY displayWithUnitChanged)
    Q_PROPERTY(QString inputText READ inputText NOTIFY inputChanged)
    Q_PROPERTY(InputState inputState READ inputState NOTIFY inputChanged)
    Q_PROPERTY(bool inputAcceptable READ inputAcceptable NOTIFY inputChanged)
    Q_PROPERTY(bool inputDirty READ inputDirty NOTIFY inputDirtyChanged)
    Q_PROPERTY(QString inputError READ inputError NOTIFY inputChanged)
    Q_PROPERTY(bool zeroIsEmpty READ zeroIsEmpty WRITE setZeroIsEmpty NOTIFY inputChanged)
    Q_PROPERTY(quint64 inputRevision READ inputRevision NOTIFY inputChanged)
    Q_PROPERTY(bool inputPending READ inputPending CONSTANT)
    Q_PROPERTY(quint64 inputOperationId READ inputOperationId NOTIFY inputOperationChanged)
    Q_PROPERTY(InputOperationState inputOperationState READ inputOperationState NOTIFY inputOperationChanged)
    Q_PROPERTY(QString inputOperationError READ inputOperationError NOTIFY inputOperationChanged)
    Q_PROPERTY(int inputCursorPosition READ inputCursorPosition NOTIFY inputOperationChanged)
    Q_PROPERTY(qint64 satoshi READ satoshi WRITE setSatoshi NOTIFY amountChanged)

public:
    // QML only exposes enum names that begin with an uppercase letter.
    enum class Unit {
        BTC,
        MilliBTC,
        MicroBTC,
        SAT
    };
    Q_ENUM(Unit)

    enum class InputState { Empty, Incomplete, Invalid, Valid };
    Q_ENUM(InputState)

    enum class InputOperationState { InputIdle, InputApplied, InputRejected, InputStale, InputClosed };
    Q_ENUM(InputOperationState)
    explicit BitcoinAmount(QObject *parent = nullptr);
    static void setDefaultAdmissionGate(std::shared_ptr<std::atomic_bool> gate);
    void setOwnerAdmissionGate(std::shared_ptr<std::atomic_bool> gate) { m_owner_gate = std::move(gate); }

    Unit unit() const;
    void setUnit(Unit unit);
    QString unitLabel() const;

    QString toDisplay() const;
    QString displayWithUnit() const;
    QString localizedDisplay() const;
    QString localizedDisplayWithUnit() const;
    void fromDisplay(const QString& new_amount);
    qint64 satoshi() const;
    void setSatoshi(qint64 new_amount);

    bool isSet() const { return m_isSet; }

    QString inputText() const { return m_input_text; }
    InputState inputState() const { return m_input_state; }
    bool inputAcceptable() const { return m_input_state == InputState::Empty || m_input_state == InputState::Valid; }
    bool inputDirty() const { return m_input_dirty; }
    QString inputError() const;
    bool zeroIsEmpty() const { return m_zero_is_empty; }
    void setZeroIsEmpty(bool empty);
    Q_INVOKABLE void editInput(const QString& text) { fromDisplay(text); }
    // Parse has an existing 128-character ORIGINAL-input budget. Long literals
    // are refused before any trim/prefix parse; the 129-character view is an
    // invalid overflow sentinel, never a silently accepted numeric prefix.
    Q_INVOKABLE quint64 requestPaste(const QString& literal, int selection_start, int selection_end, qint64 expected_revision = -1);
    Q_INVOKABLE quint64 requestEditInput(const QString& text, qint64 expected_revision = -1, int cursor = -1);
    Q_INVOKABLE bool cancelInput(quint64 ticket);
    void closeAdmission();
    quint64 inputRevision() const { return m_input_revision; }
    bool inputPending() const { return false; }
    quint64 inputOperationId() const { return m_input_operation_id; }
    InputOperationState inputOperationState() const { return m_input_operation_state; }
    QString inputOperationError() const { return m_input_operation_error; }
    int inputCursorPosition() const { return m_input_cursor; }
    Q_INVOKABLE bool commitTo(BitcoinAmount* target);
    Q_INVOKABLE void resetInput();
    Q_INVOKABLE void restoreInput() { if (!admissionClosed()) Q_EMIT inputChanged(); }
    Q_INVOKABLE void format();

    static QString satsToBtcString(qint64 sat);

public Q_SLOTS:
    void flipUnit();
    void clear();

Q_SIGNALS:
    void inputOperationChanged();
    void inputDirtyChanged();
    void inputChanged();
    void unitChanged();
    void amountChanged();
    void displayChanged();
    void displayWithUnitChanged();

private:
    bool admissionClosed() const { return m_input_closed || (m_app_gate && m_app_gate->load()) || (m_owner_gate && m_owner_gate->load()); }
    std::shared_ptr<std::atomic_bool> m_app_gate, m_owner_gate;
    void assignCanonical(qint64 value, bool is_set, bool reset_input = false);
    QString formattedInput() const;

    quint64 beginInputRequest(qint64 expected_revision);
    quint64 applyInputRequest(const QString& bounded_text, bool over_budget, int cursor, quint64 ticket);
    quint64 m_input_revision{0}, m_input_operation_id{0};
    InputOperationState m_input_operation_state{InputOperationState::InputIdle};
    QString m_input_operation_error;
    int m_input_cursor{0};
    bool m_input_closed{false};
    QString m_input_text;
    QString m_baseline_text;
    InputState m_input_state{InputState::Empty};
    bool m_input_dirty{false};
    bool m_zero_is_empty{false};
    qint64 m_satoshi{0};
    bool m_isSet{false};
    Unit m_unit{Unit::BTC};
};

#endif // BITCOIN_QML_BITCOINAMOUNT_H
