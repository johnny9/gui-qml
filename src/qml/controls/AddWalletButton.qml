// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import org.bitcoincore.qt 1.0

Button {
    id: root

    property color bgActiveColor: Theme.color.neutral2
    property color textColor: Theme.color.neutral7
    property color textHoverColor: Theme.color.orange
    property color textActiveColor: Theme.color.orange

    hoverEnabled: AppMode.isDesktop
    implicitHeight: 40
    implicitWidth: 220

    MouseArea {
        anchors.fill: parent
        enabled: false
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
    }

    contentItem: Item {
        anchors.fill: parent
        RowLayout {
            anchors.centerIn: parent
            spacing: 3
            Icon {
                id: addIcon
                Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                source: "image://images/plus-filled"
                color: Theme.color.neutral7
                size: 24
                Layout.minimumWidth: 24
                Layout.preferredWidth: 24
                Layout.maximumWidth: 24
                Layout.preferredHeight: 24
            }
            CoreText {
                id: addText
                Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                text: qsTr("Add Wallet")
                color: Theme.color.neutral7
                font.pixelSize: 15
                bold: true
            }
        }
    }

    background: Rectangle {
        id: bg
        height: 30
        width: 220
        radius: 5
        color: Theme.color.background

        FocusBorder {
            visible: root.visualFocus
        }

        Behavior on color {
            ColorAnimation { duration: 150 }
        }
    }

    states: [
        State {
            name: "CHECKED"; when: root.checked
        },
        State {
            name: "HOVER"; when: root.hovered
            PropertyChanges { target: bg; color: bgActiveColor }
            PropertyChanges { target: addText; color: textHoverColor }
            PropertyChanges { target: addIcon; color: textHoverColor }
        }
    ]
}
