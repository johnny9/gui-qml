// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import "../controls"

Rectangle {
    id: root

    property url iconSource: ""
    property color iconColor: Theme.color.white
    property string text: ""
    property string textObjectName: ""
    property color textColor: Theme.color.white
    property color backgroundColor: Theme.color.neutral2
    property bool showsCloseButton: false
    property string actionText: ""

    signal actionTriggered()
    signal dismissed()

    color: backgroundColor
    radius: 5
    implicitHeight: Math.max(50, contentRow.implicitHeight + 20)

    RowLayout {
        id: contentRow
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: 20
        anchors.rightMargin: 12
        spacing: 12

        Icon {
            visible: root.iconSource != ""
            source: root.iconSource
            color: root.iconColor
            size: 18
            Layout.alignment: Qt.AlignVCenter
        }

        CoreText {
            objectName: root.textObjectName
            Layout.fillWidth: true
            text: root.text
            color: root.textColor
            font: Theme.text.description.font
            horizontalAlignment: root.iconSource != "" ? Text.AlignLeft : Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            wrapMode: Text.WordWrap
        }

        Button {
            id: actionButton
            visible: root.actionText !== ""
            text: root.actionText
            padding: 6
            background: null
            contentItem: CoreText {
                text: actionButton.text
                color: root.textColor
                font: Theme.text.description.font
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                opacity: actionButton.hovered ? 0.75 : 1.0
            }
            onClicked: root.actionTriggered()
            HoverHandler {
                cursorShape: Qt.PointingHandCursor
            }
        }

        Rectangle {
            visible: root.actionText !== "" && root.showsCloseButton
            Layout.preferredWidth: 1
            Layout.preferredHeight: 20
            color: Qt.rgba(root.textColor.r, root.textColor.g, root.textColor.b, 0.4)
        }

        Icon {
            visible: root.showsCloseButton
            source: "image://images/cross"
            color: root.textColor
            size: 14
            enabled: true
            padding: 6
            onClicked: root.dismissed()
            HoverHandler {
                cursorShape: Qt.PointingHandCursor
            }
        }
    }
}
