// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_COMPONENTS_BLOCKCLOCKDIAL_H
#define BITCOIN_QML_COMPONENTS_BLOCKCLOCKDIAL_H

#include <QQuickPaintedItem>
#include <QPainter>

class BlockClockDial : public QQuickPaintedItem
{
    Q_OBJECT

public:
    explicit BlockClockDial(QQuickItem * parent = nullptr);
    void paint(QPainter * painter) override;
};

#endif // BITCOIN_QML_COMPONENTS_BLOCKCLOCKDIAL_H
