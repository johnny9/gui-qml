// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Dialogs
import org.bitcoincore.qt 1.0

import "../../controls"
import "../../components"
import "../settings"

Page {
    id: root
    objectName: "importWalletOptions"
    signal back
    signal next
    background: null

    header: NavigationBar2 {
        leftItem: NavButton {
            iconSource: "image://images/caret-left"
            text: qsTr("Back")
            onClicked: root.back()
        }
    }

    FileDialog {
        id: fileDialog
        currentFolder: shortcuts.home
        nameFilters: [qsTr("Wallet backup files (*.bak *.dat)"), qsTr("All files (*)")]
        onAccepted: {
            if (fileDialog.selectedFile.toString().length === 0) {
                return
            }
            pathField.text = walletController.normalizeWalletPath(fileDialog.selectedFile.toString())
            walletController.importWallet(pathField.text)
        }
    }

    Connections {
        target: walletController
        function onWalletLoadSucceeded() {
            root.next()
        }
    }

    ColumnLayout {
        width: Math.min(parent.width, 450)
        anchors.horizontalCenter: parent.horizontalCenter

        Image {
            Layout.alignment: Qt.AlignCenter
            Layout.topMargin: 20
            source: "image://images/circle-file"
            sourceSize.width: 60
            sourceSize.height: 60
        }

        Header {
            Layout.topMargin: 18
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            header: qsTr("Import wallet")
            headerBold: true
            description: qsTr("Choose a wallet backup file to restore into the app.")
        }

        ContinueButton {
            id: chooseFileButton
            objectName: "importWalletChooseFileButton"
            Layout.preferredWidth: Math.min(300, parent.width - 2 * Layout.leftMargin)
            Layout.topMargin: 30
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            Layout.alignment: Qt.AlignCenter
            enabled: !walletController.walletLoadInProgress
            text: walletController.walletLoadInProgress ? qsTr("Importing...") : qsTr("Choose a backup file")
            onClicked: fileDialog.open()
        }

        Item {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            Layout.topMargin: 18
            Layout.preferredHeight: 25

            Rectangle {
                anchors.left: parent.left
                anchors.right: orLabel.left
                anchors.verticalCenter: parent.verticalCenter
                anchors.rightMargin: 8
                height: 1
                color: Theme.color.neutral3
            }

            CoreText {
                id: orLabel
                anchors.centerIn: parent
                text: qsTr("or")
                color: Theme.color.neutral7
                font.pixelSize: 18
            }

            Rectangle {
                anchors.left: orLabel.right
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 8
                height: 1
                color: Theme.color.neutral3
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            Layout.topMargin: 30
            implicitHeight: 60
            radius: 5
            color: Theme.color.neutral2

            TextField {
                id: pathField
                objectName: "importWalletPathField"
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 52
                leftPadding: 0
                rightPadding: 0
                font.family: "Inter"
                font.styleName: "Regular"
                font.pixelSize: 18
                color: Theme.color.neutral9
                placeholderTextColor: Theme.color.neutral5
                placeholderText: qsTr("Enter wallet backup path")
                background: Item {}
                selectByMouse: true
                enabled: !walletController.walletLoadInProgress
                onTextEdited: walletController.clearWalletLoadStatus()
                onAccepted: {
                    if (nextButton.enabled) {
                        walletController.importWallet(pathField.text)
                    }
                }
            }

            Icon {
                anchors.right: parent.right
                anchors.rightMargin: 14
                anchors.verticalCenter: parent.verticalCenter
                source: "image://images/copy"
                color: walletController.walletLoadInProgress ? Theme.color.neutral4 : Theme.color.neutral7
                enabled: !walletController.walletLoadInProgress
                onClicked: {
                    pathField.text = Clipboard.text()
                    pathField.cursorPosition = pathField.text.length
                    walletController.clearWalletLoadStatus()
                }
            }
        }

        CoreText {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            Layout.topMargin: 14
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: qsTr("Enter a backup file path such as wallet.dat or a .bak file.")
            color: Theme.color.neutral7
            font.pixelSize: 18
        }

        CoreText {
            id: statusText
            objectName: "importWalletStatusText"
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            Layout.topMargin: 14
            visible: text.length > 0
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: walletController.walletLoadError.length > 0 ? walletController.walletLoadError : walletController.walletLoadWarnings
            color: walletController.walletLoadError.length > 0 ? Theme.color.red : Theme.color.blue
            font.pixelSize: 16
        }

        ContinueButton {
            id: nextButton
            objectName: "importWalletNextButton"
            Layout.preferredWidth: Math.min(300, parent.width - 2 * Layout.leftMargin)
            Layout.topMargin: statusText.visible ? 20 : 40
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            Layout.alignment: Qt.AlignCenter
            enabled: pathField.text.length > 0 && !walletController.walletLoadInProgress
            text: walletController.walletLoadInProgress ? qsTr("Importing...") : qsTr("Next")
            onClicked: walletController.importWallet(pathField.text)
        }
    }
}
