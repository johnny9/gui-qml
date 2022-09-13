// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_APPMODE_H
#define BITCOIN_QML_APPMODE_H

#include <QObject>

class AppMode : public QObject
{
    Q_OBJECT
    Q_PROPERTY(Mode mode READ mode)
    Q_PROPERTY(bool isDesktop READ isDesktop NOTIFY modeChanged)
    Q_PROPERTY(bool isMobile READ isMobile NOTIFY modeChanged)

public:
    enum Mode
    {
        DESKTOP,
        MOBILE
    };
    Q_ENUM(Mode)

    explicit AppMode(Mode mode) :
        m_mode(mode)
    {
    }

    bool isMobile() { return m_mode == MOBILE; }
    bool isDesktop() { return m_mode == DESKTOP; }
    Mode mode() const { return m_mode; }

Q_SIGNALS:
    void modeChanged();

private:
    const Mode m_mode;
};

#endif // BITCOIN_QML_APPMODE_H