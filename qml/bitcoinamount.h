// Copyright (c) 2024-2025 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_BITCOINAMOUNT_H
#define BITCOIN_QML_BITCOINAMOUNT_H

#include <consensus/amount.h>

#include <QObject>
#include <QString>

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

    explicit BitcoinAmount(QObject *parent = nullptr);

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
    Q_INVOKABLE bool commitTo(BitcoinAmount* target);
    Q_INVOKABLE void resetInput();
    Q_INVOKABLE void restoreInput() { Q_EMIT inputChanged(); }
    Q_INVOKABLE void format();

    static QString satsToBtcString(qint64 sat);

public Q_SLOTS:
    void flipUnit();
    void clear();

Q_SIGNALS:
    void inputDirtyChanged();
    void inputChanged();
    void unitChanged();
    void amountChanged();
    void displayChanged();
    void displayWithUnitChanged();

private:
    void assignCanonical(qint64 value, bool is_set, bool reset_input = false);
    QString formattedInput() const;

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
