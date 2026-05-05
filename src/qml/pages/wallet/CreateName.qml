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
    objectName: "createWalletNamePage"
    signal back
    signal next
    enum WalletType { SingleSig, ExternalSigner }
    property string walletName: ""
    property int walletType: CreateName.WalletType.SingleSig
    property string initialWalletName: ""
    readonly property bool externalSignerWallet: walletType === CreateName.WalletType.ExternalSigner
    property bool loading: false
    property string walletNameError: ""
    background: null

    Component.onCompleted: {
        walletController.clearWalletLoadStatus()
        if (walletNameInput.text.length === 0 && initialWalletName.length > 0) {
            walletNameInput.text = initialWalletName
            root.walletName = initialWalletName
        }
    }

    header: NavigationBar2 {
        navigationStack: root.StackView.view
    }

    ColumnLayout {
        id: columnLayout
        width: Math.min(parent.width, 450)
        spacing: 30
        anchors.horizontalCenter: parent.horizontalCenter

        Header {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            header: qsTr("Choose a wallet name")
            headerBold: true
            description: root.externalSignerWallet
                ? qsTr("This wallet will use the connected external signer.")
                : ""
        }

        CoreTextField {
            id: walletNameInput
            objectName: "createWalletNameInput"
            focus: true
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            placeholderText: root.externalSignerWallet
                ? qsTr("Eg. Hardware wallet...")
                : qsTr("Eg. My bitcoin wallet...")
            onTextChanged: {
                walletController.clearWalletLoadStatus()
                root.walletName = walletNameInput.text
                root.walletNameError = ""
                continueButton.enabled = walletNameInput.text.length > 0
            }
        }

        CoreText {
            objectName: "walletNameError"
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            opacity: root.walletNameError.length > 0 || walletController.walletLoadError.length > 0 ? 1 : 0
            text: root.walletNameError.length > 0 ? root.walletNameError : walletController.walletLoadError
            color: Theme.color.red
            horizontalAlignment: Text.AlignLeft
            font: Theme.text.caption.font
        }

        ContinueButton {
            id: continueButton
            objectName: "createWalletNameContinueButton"
            Layout.preferredWidth: Math.min(300, parent.width - 2 * Layout.leftMargin)
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            Layout.alignment: Qt.AlignCenter
            enabled: walletNameInput.text.length > 0 && !root.loading
            text: {
                if (root.loading) return qsTr("Initializing...")
                if (root.externalSignerWallet) return qsTr("Create wallet")
                return qsTr("Continue")
            }
            onClicked: {
                const availabilityError = walletController.walletNameAvailabilityError(walletNameInput.text)
                if (availabilityError.length > 0) {
                    root.walletNameError = availabilityError
                    return
                }
                root.walletName = walletNameInput.text
                root.next()
            }
        }
    }
}
