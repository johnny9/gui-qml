// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import "../../controls"
import "../../components"

Page {
    id: root
    objectName: "importWalletSuccessPage"

    signal back()
    signal done()
    signal viewSettings()

    background: null

    header: NavigationBar2 {
        leftItem: NavButton {
            iconSource: "image://images/caret-left"
            text: qsTr("Back")
            onClicked: root.back()
        }
    }

    ColumnLayout {
        width: Math.min(parent.width - 40, 560)
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: 0

        Item {
            Layout.topMargin: 72
            Layout.alignment: Qt.AlignCenter
            implicitWidth: 60
            implicitHeight: 60

            Rectangle {
                anchors.fill: parent
                radius: width / 2
                color: Theme.color.green
                opacity: 0.2
            }

            Icon {
                anchors.centerIn: parent
                source: "qrc:/icons/check"
                color: Theme.color.green
                size: 26
            }
        }

        Header {
            Layout.topMargin: 28
            Layout.fillWidth: true
            header: qsTr("Import complete")
            headerBold: true
            headerSize: 28
            description: qsTr("Your wallet was added successfully. We recommend running a health check to make sure that everything works as expected.")
        }

        ColumnLayout {
            Layout.topMargin: 36
            Layout.fillWidth: true
            spacing: 0

            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: Theme.color.neutral4
            }

            KeyValueRow {
                Layout.fillWidth: true
                Layout.topMargin: 22
                Layout.bottomMargin: 22
                key: KeyText {
                    text: qsTr("Wallet name")
                }
                value: ValueText {
                    objectName: "importWalletSuccessWalletName"
                    text: walletController.lastImportedWalletName.length > 0
                        ? walletController.lastImportedWalletName
                        : qsTr("Unknown")
                }
            }

            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: Theme.color.neutral4
            }

            KeyValueRow {
                Layout.fillWidth: true
                Layout.topMargin: 22
                Layout.bottomMargin: 22
                key: KeyText {
                    text: qsTr("Key scheme")
                }
                value: ValueText {
                    objectName: "importWalletSuccessKeyScheme"
                    text: walletController.lastImportedWalletKeyScheme.length > 0
                        ? walletController.lastImportedWalletKeyScheme
                        : qsTr("Unknown")
                }
            }

            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: Theme.color.neutral4
            }
        }

        RowLayout {
            Layout.topMargin: 38
            Layout.fillWidth: true
            spacing: 16

            OutlineButton {
                objectName: "importWalletSuccessOverviewButton"
                Layout.fillWidth: true
                Layout.preferredWidth: 0
                text: qsTr("Go to wallet overview")
                onClicked: root.done()
            }

            ContinueButton {
                objectName: "importWalletSuccessSettingsButton"
                Layout.fillWidth: true
                Layout.preferredWidth: 0
                text: qsTr("View in settings")
                onClicked: root.viewSettings()
            }
        }
    }

    component KeyText: CoreText {
        color: Theme.color.neutral9
        font.pixelSize: 18
        horizontalAlignment: Qt.AlignLeft
        verticalAlignment: Text.AlignVCenter
    }

    component ValueText: CoreText {
        color: Theme.color.neutral9
        font.pixelSize: 18
        bold: true
        horizontalAlignment: Qt.AlignRight
        verticalAlignment: Text.AlignVCenter
        wrapMode: Text.WordWrap
    }
}
