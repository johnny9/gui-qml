// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import "../../controls"

Page {
    id: root
    objectName: "importWalletMigration"
    required property string walletPath
    signal back
    signal next

    state: "intro"

    function startMigration() {
        state = "updating"
        walletController.migrateWallet(walletPath)
    }

    Component.onCompleted: {
        walletController.clearWalletMigrationStatus()
        state = "intro"
    }

    background: null

    header: NavigationBar2 {
        leftItem: NavButton {
            id: backButton
            enabled: visible
            iconSource: "image://images/caret-left"
            text: qsTr("Back")
            onClicked: {
                walletController.clearWalletMigrationStatus()
                root.back()
            }
        }
    }

    Connections {
        target: walletController
        function onWalletMigrationSucceeded() {
            root.state = "success"
        }
        function onWalletMigrationFailed() {
            root.state = "failed"
        }
    }

    ColumnLayout {
        width: Math.min(parent.width, 450)
        anchors.horizontalCenter: parent.horizontalCenter

        Image {
            id: statusImage
            Layout.alignment: Qt.AlignCenter
            Layout.topMargin: 20
            source: "image://images/pending"
            sourceSize.width: 60
            sourceSize.height: 60
        }

        Header {
            id: message
            Layout.topMargin: 18
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            header: qsTr("Wallet update required")
            headerBold: true
            description: qsTr("This wallet uses an outdated file format and needs to be updated. This does not impact security or any previously made transactions.")
        }

        ContinueButton {
            id: actionButton
            objectName: "walletMigrationActionButton"
            Layout.preferredWidth: Math.min(335, parent.width - 2 * Layout.leftMargin)
            Layout.topMargin: 30
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            Layout.alignment: Qt.AlignCenter
            text: qsTr("Update wallet")
            onClicked: {
                if (root.state === "success") {
                    walletController.clearWalletMigrationStatus()
                    root.next()
                    return
                }
                if (root.state === "failed") {
                    walletController.clearWalletMigrationStatus()
                    root.back()
                    return
                }
                root.startMigration()
            }
        }
    }

    states: [
        State {
            name: "intro"
        },
        State {
            name: "updating"
            PropertyChanges {
                target: backButton
                visible: false
            }
            PropertyChanges {
                target: message
                header: qsTr("Updating wallet")
                description: qsTr("Updating your wallet now. This may take a moment.")
            }
            PropertyChanges {
                target: actionButton
                enabled: false
                text: qsTr("Updating wallet...")
            }
        },
        State {
            name: "success"
            PropertyChanges {
                target: statusImage
                source: "image://images/circle-green-check"
            }
            PropertyChanges {
                target: message
                header: qsTr("Wallet successfully migrated")
                description: qsTr("You can use your wallet as usual.")
            }
            PropertyChanges {
                target: actionButton
                text: qsTr("Done")
            }
        },
        State {
            name: "failed"
            PropertyChanges {
                target: statusImage
                source: "image://images/circle-red-cross"
            }
            PropertyChanges {
                target: message
                header: qsTr("Update failed")
                description: walletController.walletMigrationError.length > 0
                    ? walletController.walletMigrationError
                    : qsTr("The wallet could not be updated.")
            }
            PropertyChanges {
                target: actionButton
                text: qsTr("Back to overview")
            }
        }
    ]
}
