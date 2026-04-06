// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import "../controls"

Item {
    id: root

    property string labelText: qsTr("Send to")
    property string text: ""

    Layout.fillWidth: true
    implicitHeight: Math.max(label.implicitHeight + 6, addressText.contentHeight)

    CoreText {
        id: label
        width: 110
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.topMargin: 6
        horizontalAlignment: Text.AlignLeft
        text: root.labelText
        font.pixelSize: 18
        color: Theme.color.neutral9
    }

    TextArea {
        id: addressText
        anchors.left: label.right
        anchors.right: parent.right
        anchors.top: parent.top
        readOnly: true
        text: root.text
        wrapMode: Text.WrapAnywhere
        leftPadding: 0
        topPadding: 0
        rightPadding: 0
        bottomPadding: 0
        height: Math.max(contentHeight, 32)
        font.family: "Inter"
        font.styleName: "Regular"
        font.pixelSize: 18
        color: Theme.color.neutral9
        background: Item {}
        selectByMouse: true
    }
}
