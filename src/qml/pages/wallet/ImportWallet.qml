// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import "../../controls"
import "../../components"
import "../settings"

Page {
    id: root
    background: null

    header: NavigationBar {
        id: navbar
    }

    ColumnLayout {
        id: columnLayout
        width: Math.min(parent.width, 450)
        anchors.horizontalCenter: parent.horizontalCenter

        Item {
            Layout.alignment: Qt.AlignCenter
            Layout.preferredHeight: circle.height
            Layout.preferredWidth: circle.width
            Rectangle {
                id: circle
                width: 60
                height: width
                radius: width / 2
                color: Theme.color.blue
                opacity: 0.3
            }
            Icon {
                source: "image://images/bitcoin-circle"
                color: Theme.color.blue
                width: 30
                height: width
                anchors.centerIn: circle
            }
        }


        Header {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            header: qsTr("Import wallet")
        }

        ContinueButton {
            Layout.preferredWidth: Math.min(300, parent.width - 2 * Layout.leftMargin)
            Layout.topMargin: 40
            Layout.leftMargin: 20
            Layout.rightMargin: Layout.leftMargin
            Layout.alignment: Qt.AlignCenter
            text: qsTr("Choose a wallet file")
        }

        Item {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            Layout.topMargin: 30
            Layout.bottomMargin: 30

            Row {
                spacing: 1
                anchors.left: parent.left
                Repeater {
                    model: 90
                    Rectangle {
                        height: 1
                        width: height
                        radius: width / 2
                        color: Theme.color.neutral5
                    }
                }
            }

            Label {
                anchors.centerIn: parent
                color: Theme.color.neutral5
                text: qsTr("or")
            }

            Row {
                spacing: 1
                anchors.right: parent.right
                Repeater {
                    model: 90
                    Rectangle {
                        height: 1
                        width: height
                        radius: width / 2
                        color: Theme.color.neutral5
                    }
                }
            }
        }

        TextField {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            Layout.preferredHeight: 180
            font.family: "Inter"
            font.styleName: "Regular"
            wrapMode: TextInput.WrapAnywhere
            verticalAlignment: TextInput.AlignTop
            color: Theme.color.neutral7
            placeholderText: qsTr("Enter wallet information")
            placeholderTextColor: Theme.color.neutral5
            background: Rectangle {
                color: Theme.color.neutral2
                radius: 5
            }
        }

        CoreText {
            Layout.fillWidth: true
            Layout.topMargin: 15
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            Layout.bottomMargin: 15
            horizontalAlignment: Text.AlignHCenter
            wrapMode: TextInput.Wrap
            text: qsTr("Enter an extended public key (XPUB) or an address for view-only wallets. Enter a wallet descriptor for a full wallet.")
            color: Theme.color.neutral7
        }

        ContinueButton {
            Layout.preferredWidth: Math.min(300, parent.width - 2 * Layout.leftMargin)
            Layout.leftMargin: 20
            Layout.rightMargin: Layout.leftMargin
            Layout.alignment: Qt.AlignCenter
            text: qsTr("Next")
            borderColor: Theme.color.neutral6
            borderHoverColor: Theme.color.orangeLight1
            borderPressedColor: Theme.color.orangeLight2
            textColor: Theme.color.orange
            backgroundColor: "transparent"
            backgroundHoverColor: "transparent"
            backgroundPressedColor: "transparent"
            onClicked: {
                root.StackView.view.push("qrc:/qml/pages/wallet/ImportComplete.qml")
            }
        }
    }
}