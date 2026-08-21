// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15

Rectangle {
    property color backgroundColor: "#2d2d2d"
    property color borderColor: "#444444"
    property color gradientColor: "#3ca3de"
    property color lineColor: "#3ca3de"
    property color markerLineColor: "#5c5c5c"
    property int maxSamples: 30
    property real maxValue: 100
    property var valueList: [15, 28, 23, 48, 41, 67, 54, 81]

    color: backgroundColor
    border.color: borderColor
    border.width: 1

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        height: 2
        color: parent.lineColor
        rotation: -8
    }
}
// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
