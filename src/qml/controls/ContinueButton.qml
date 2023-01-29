// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15

Button {
    id: root
    font.family: "Inter"
    font.styleName: "Semi Bold"
    font.pixelSize: 18
    contentItem: Text {
        text: parent.text
        font: parent.font
        color: Theme.color.white
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
    background: Rectangle {
        id: bg
        implicitHeight: 46
        implicitWidth: 300
        color: Theme.color.orange
        radius: 5
        state:"DEFAULT"

        states: [
            State {
                name: "DEFAULT"
                PropertyChanges { target: bg; color: Theme.color.orange }
            },
            State {
                name: "HOVER"
                PropertyChanges { target: bg; color: Theme.color.orangeLight1 }
            },
            State {
                name: "PRESSED"
                PropertyChanges { target: bg; color: Theme.color.orangeLight2 }
            }
        ]

        Behavior on color {
            ColorAnimation { duration: 150 }
        }
    }
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        onEntered: {
            root.background.state = "HOVER"
        }
        onExited: {
            root.background.state = "DEFAULT"
        }
        onPressed: {
            root.background.state = "PRESSED"
        }
        onReleased: {
            root.background.state = "DEFAULT"
            root.clicked()
        }
    }
}
