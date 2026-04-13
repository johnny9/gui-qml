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
    property string fullText: ""
    property string expandedObjectName: ""
    property bool expanded: false
    property int labelPixelSize: 18
    property color labelColor: Theme.color.neutral9
    readonly property bool showLabel: labelText.length > 0
    readonly property bool expandable: fullText.length > 0 && fullText !== text

    Layout.fillWidth: true
    implicitHeight: Math.max(showLabel ? label.implicitHeight + 6 : 0, addressContainer.implicitHeight)

    onExpandableChanged: {
        if (!expandable) {
            expanded = false
        }
    }

    function click() {
        if (expandable) {
            expanded = !expanded
        }
    }

    CoreText {
        id: label
        visible: root.showLabel
        width: 110
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.topMargin: 6
        horizontalAlignment: Text.AlignLeft
        text: root.labelText
        font.pixelSize: root.labelPixelSize
        color: root.labelColor
    }

    Item {
        id: addressContainer
        anchors.left: root.showLabel ? label.right : parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        implicitHeight: addressColumn.implicitHeight
        height: implicitHeight

        HoverHandler {
            enabled: root.expandable
            cursorShape: Qt.PointingHandCursor
        }

        TapHandler {
            enabled: root.expandable
            onTapped: root.expanded = !root.expanded
        }

        Column {
            id: addressColumn
            width: parent.width
            spacing: root.expandable && root.expanded ? 6 : 0

            TextArea {
                id: addressText
                width: parent.width
                readOnly: true
                text: root.text
                wrapMode: Text.WrapAnywhere
                leftPadding: 0
                topPadding: 0
                rightPadding: 0
                bottomPadding: 0
                height: Math.max(contentHeight, 32)
                font.family: "Roboto Mono"
                font.styleName: "Regular"
                font.pixelSize: 18
                color: Theme.color.neutral9
                background: Item {}
            }

            TextArea {
                id: fullAddressText
                objectName: root.expandedObjectName
                width: parent.width
                visible: root.expandable && root.expanded
                readOnly: true
                text: root.fullText
                wrapMode: Text.WrapAnywhere
                leftPadding: 0
                topPadding: 0
                rightPadding: 0
                bottomPadding: 0
                height: visible ? Math.max(contentHeight, 32) : 0
                font.family: "Roboto Mono"
                font.styleName: "Regular"
                font.pixelSize: 18
                color: Theme.color.neutral7
                background: Item {}
            }
        }
    }
}
