// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_BUILDINFO_H
#define BITCOIN_QML_BUILDINFO_H

#include <QObject>

class BuildInfo : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool isDebug READ isDebug CONSTANT)

public:
    explicit BuildInfo(QObject* parent = nullptr) : QObject(parent) {}

    bool isDebug() const
    {
#ifndef NDEBUG
        return true;
#else
        return false;
#endif
    }
};

#endif // BITCOIN_QML_BUILDINFO_H
