// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

AbstractButton {
    id: root
    required property string parentState
    required property string link
    property string description: ""
    property int descriptionSize: 18
    property url iconSource: ""
    property int iconWidth: 18
    property int iconHeight: 18
    property color iconColor
    property color textColor
    state: root.parentState

    states: [
        State {
            name: "FILLED"
            PropertyChanges {
                target: root
                iconColor: Theme.color.neutral9
                textColor: Theme.color.neutral7
            }
        },
        State {
            name: "HOVER"
            PropertyChanges {
                target: root
                iconColor: Theme.color.orangeLight1
                textColor: Theme.color.orangeLight1
            }
        },
        State {
            name: "ACTIVE"
            PropertyChanges {
                target: root
                iconColor: Theme.color.orange
                textColor: Theme.color.orange
            }
        }
    ]

    contentItem: RowLayout {
        spacing: 0
        width: parent.width
        Loader {
            Layout.fillWidth: true
            active: root.description.length > 0
            visible: active
            sourceComponent: Text {
                font.family: "Inter"
                font.styleName: "Regular"
                font.pixelSize: root.descriptionSize
                color: root.textColor
                textFormat: Text.RichText
                text: root.description

                Behavior on color {
                    ColorAnimation { duration: 150 }
                }
            }
        }
        Loader {
            Layout.fillWidth: true
            active: root.iconSource.toString().length > 0
            visible: active
            sourceComponent: Button {
                icon.source: root.iconSource
                icon.color: root.iconColor
                icon.height: root.iconHeight
                icon.width: root.iconWidth
                background: null
                onClicked: root.clicked()

                Behavior on icon.color {
                    ColorAnimation { duration: 150 }
                }
            }
        }
    }
    onClicked: Qt.openUrlExternally(link)

    background: Rectangle {
        visible: root.visualFocus
        anchors.fill: parent
        anchors.margins: -4
        border.width: 2
        border.color: Theme.color.purple
        radius: 9
        color: "transparent"
    }
}
