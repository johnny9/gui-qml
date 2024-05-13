// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import org.bitcoincore.qt 1.0

import "../../controls"

Button {
    id: root

    property color bgActiveColor: Theme.color.neutral2
    property color textColor: Theme.color.neutral7
    property color textHoverColor: Theme.color.neutral9
    property color textActiveColor: Theme.color.orange
    property color iconColor: "transparent"
    property string iconSource: ""
    property bool showBalance: true
    property bool showIcon: true

    checkable: true
    hoverEnabled: AppMode.isDesktop
    implicitHeight: 60
    implicitWidth: 220
    bottomPadding: 0
    topPadding: 0

    contentItem: RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 10
        spacing: 5
        Icon {
            id: icon
            visible: root.showIcon
            source: "image://images/singlesig-wallet"
            color: Theme.color.neutral8
            Layout.preferredWidth: 30
            Layout.preferredHeight: 30
        }
        Column {
            Layout.fillWidth: true
            spacing: 2
            CoreText {
                id: buttonText
                font.pixelSize: 13
                text: root.text
                color: root.textColor
                bold: true
                visible: root.text !== ""
            }
            CoreText {
                id: balanceText
                visible: root.showBalance
                text: "<font color=\""+Theme.color.neutral9+"\">₿</font> 0.00 <font color=\""+Theme.color.neutral9+"\">167 599</font>"
                color: Theme.color.neutral7
            }
        }

    }

    background: Rectangle {
        id: bg
        height: root.height
        width: root.width
        radius: 5
        color: Theme.color.neutral3
        visible: root.hovered || root.checked

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
            PropertyChanges { target: buttonText; color: root.textActiveColor }
            PropertyChanges { target: icon; color: root.textActiveColor }
        },
        State {
            name: "HOVER"; when: root.hovered
            PropertyChanges { target: buttonText; color: root.textHoverColor }
            PropertyChanges { target: icon; color: root.textHoverColor }
            PropertyChanges { target: balanceText; color: root.textHoverColor }
        }
    ]
}