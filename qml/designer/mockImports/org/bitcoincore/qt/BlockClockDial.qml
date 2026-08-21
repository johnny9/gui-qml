// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15

Rectangle {
    property var timeRatioList: [0.1, 0.15, 0.2, 0.25, 0.3]
    property real verificationProgress: 0.82
    property bool connected: true
    property bool synced: false
    property bool paused: false
    property bool animateDial: false
    property int connectingAnimationDelayMs: 5000
    property bool showTimeTicks: true
    property bool showBlockSegments: true
    property bool useGradientArcWhenSynced: false
    property real penWidth: 4
    property color backgroundColor: "#2d2d2d"
    property var confirmationColors: ["#ff1c1c", "#f1d54a"]
    property color timeTickColor: "#949494"

    radius: Math.min(width, height) / 2
    color: backgroundColor
    border.width: penWidth
    border.color: connected ? "#f7931a" : timeTickColor

    Rectangle {
        anchors.centerIn: parent
        width: Math.max(0, parent.width - parent.penWidth * 4)
        height: Math.max(0, parent.height - parent.penWidth * 4)
        radius: Math.min(width, height) / 2
        color: "transparent"
        border.width: 1
        border.color: parent.timeTickColor
        opacity: 0.6
    }
}
// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
