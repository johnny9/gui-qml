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
                source: "image://images/visible"
                color: Theme.color.green
                width: 30
                height: width
                anchors.centerIn: circle
            }
        }


        Header {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            header: qsTr("Import complete")
        }

        Setting {
            Layout.fillWidth: true
            header: qsTr("Wallet name")
            actionItem: CoreText {
                text: "Savings"
            }
            onClicked: openPopup(loadedItem.link)
        }
        Separator { Layout.fillWidth: true }
        Setting {
            id: versionLink
            Layout.fillWidth: true
            header: qsTr("Wallet type")
            actionItem: CoreText {
                text: "Single-key"
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
                root.StackView.view.finished()
            }
        }
    }
}