// Copyright (c) 2025 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0
import "../../controls"
import "../../components"

Page {
    id: root
    signal back()
    background: null

    header: NavigationBar2 {
        leftItem: NavButton {
            iconSource: "image://images/caret-left"
            text: qsTr("Back")
            onClicked: root.back()
        }
        centerItem: Header {
            headerBold: true
            headerSize: 18
            header: qsTr("Banned peers")
        }
    }

    ListView {
        id: listView
        clip: true
        width: Math.min(parent.width - 40, 450)
        height: parent.height
        anchors.horizontalCenter: parent.horizontalCenter
        model: banTableModel
        spacing: 15

        header: ColumnLayout {
            width: listView.width
            spacing: 0
            CoreText {
                Layout.fillWidth: true
                Layout.topMargin: 10
                Layout.bottomMargin: 20
                text: qsTr("You banned these peers from connecting to your node.")
                font.pixelSize: 13
                color: Theme.color.neutral7
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
            }
        }

        delegate: ItemDelegate {
            required property string address
            required property string banUntil
            required property int index

            leftPadding: 0
            rightPadding: 0
            topPadding: 0
            bottomPadding: 14
            width: listView.width
            background: Item {
                Separator {
                    anchors.bottom: parent.bottom
                    width: parent.width
                }
            }

            contentItem: RowLayout {
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    CoreText {
                        Layout.fillWidth: true
                        text: address
                        font.pixelSize: 15
                        color: Theme.color.neutral9
                        elide: Text.ElideMiddle
                        horizontalAlignment: Text.AlignLeft
                    }
                    CoreText {
                        Layout.fillWidth: true
                        text: qsTr("Until %1").arg(banUntil)
                        font.pixelSize: 13
                        color: Theme.color.neutral7
                        horizontalAlignment: Text.AlignLeft
                    }
                }
                OutlineButton {
                    bold: false
                    horizontalPadding: 24
                    text: qsTr("Unban")
                    onClicked: banTableModel.unbanAt(index)
                }
            }
        }

        footer: Loader {
            width: listView.width
            height: 80
            active: listView.count === 0
            visible: active
            sourceComponent: CoreText {
                anchors.centerIn: parent
                text: qsTr("No banned peers.")
                color: Theme.color.neutral7
                font.pixelSize: 15
            }
        }
    }
}
