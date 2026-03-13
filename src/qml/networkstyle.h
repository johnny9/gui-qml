// Copyright (c) 2014-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_NETWORKSTYLE_H
#define BITCOIN_QML_NETWORKSTYLE_H

#include <util/chaintype.h>

#include <QIcon>
#include <QPixmap>
#include <QString>

/* Coin network-specific GUI style information. */
class NetworkStyle
{
public:
    /** Get style associated with provided network id, or null if not known. */
    static const NetworkStyle* instantiate(ChainType networkId);

    const QString& getAppName() const { return appName; }
    const QIcon& getAppIcon() const { return appIcon; }
    const QIcon& getTrayAndWindowIcon() const { return trayAndWindowIcon; }

private:
    NetworkStyle(const QString& appName,
                 int iconColorHueShift,
                 int iconColorSaturationReduction);

    QString appName;
    QIcon appIcon;
    QIcon trayAndWindowIcon;
};

#endif // BITCOIN_QML_NETWORKSTYLE_H
