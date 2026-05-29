// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0

import "../../controls"
import "../../components"

Popup {
    id: root
    objectName: "speedUpOverlay"

    property string txid: ""
    property var bumpModel: walletController.selectedWallet
        ? walletController.selectedWallet.bumpModel : null
    property int bumpState: root.bumpModel ? root.bumpModel.state : BumpTransactionModel.Idle
    property string bumpErrorText: root.bumpModel ? root.bumpModel.errorText : ""
    property bool readyToConfirm: root.bumpModel
        && root.bumpModel.state === BumpTransactionModel.NeedsConfirmation

    signal done()
    signal viewNewTransaction(string newTxid)

    modal: true
    leftPadding: 40
    rightPadding: 40
    topPadding: 30
    bottomPadding: 30
    width: 500
    anchors.centerIn: parent

    onOpened: {
        if (root.bumpModel) {
            root.bumpModel.prepareFeeBump(root.txid, 1)
        }
    }

    onClosed: {
        if (root.bumpModel) {
            root.bumpModel.reset()
        }
    }

    background: Rectangle {
        color: Theme.color.neutral0
        radius: 10
        border.color: Theme.color.neutral4
        border.width: 1
    }

    property string newTxid: ""

    signal bumpSucceeded()

    Connections {
        target: root.bumpModel
        function onStateChanged() {
            if (root.bumpModel && root.bumpModel.state === BumpTransactionModel.Succeeded) {
                root.newTxid = root.bumpModel.newTxid
                root.close()
                root.bumpSucceeded()
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        CoreText {
            text: qsTr("Speed up transaction")
            font.pixelSize: 21
            bold: true
            horizontalAlignment: Text.AlignLeft
            Layout.fillWidth: true
        }

        CoreText {
            Layout.fillWidth: true
            Layout.topMargin: 15
            Layout.bottomMargin: 25
            horizontalAlignment: Text.AlignLeft
            text: qsTr("Set a higher transaction fee if you want your transaction to be confirmed faster.")
            font.pixelSize: 15
            color: Theme.color.neutral7
            wrap: true
        }

        RowLayout {
            Layout.fillWidth: true
            CoreText {
                text: qsTr("Original fee")
                font.pixelSize: 15
                color: Theme.color.neutral7
            }
            CoreText {
                text: root.bumpModel ? root.bumpModel.oldFee : ""
                font.pixelSize: 15
                color: Theme.color.neutral9
                horizontalAlignment: Text.AlignRight
                Layout.fillWidth: true
            }
            //FIXME: Add label to show estimated confirmation duration (~20 min)
        }

        Separator {
            Layout.fillWidth: true
            Layout.topMargin: 10
            Layout.bottomMargin: 10
        }

        RowLayout {
            Layout.fillWidth: true
            CoreText {
                text: qsTr("New fee")
                font.pixelSize: 15
                color: Theme.color.neutral7
            }
            CoreText {
                text: root.bumpModel ? root.bumpModel.newFee : ""
                font.pixelSize: 15
                color: Theme.color.neutral9
                horizontalAlignment: Text.AlignRight
                Layout.fillWidth: true
            }
        }

        CoreText {
            objectName: "speedUpErrorText"
            visible: root.bumpModel && root.bumpModel.state === BumpTransactionModel.Failed
            text: root.bumpModel ? root.bumpModel.errorText : ""
            font.pixelSize: 15
            color: Theme.color.red
            Layout.fillWidth: true
            Layout.topMargin: 10
            wrap: true
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 25
            spacing: 15

            OutlineButton {
                text: qsTr("Cancel")
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                onClicked: root.close()
            }

            ContinueButton {
                objectName: "updateTransactionButton"
                text: qsTr("Update transaction")
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                enabled: root.readyToConfirm
                onClicked: {
                    if (!root.bumpModel) {
                        return
                    }
                    if (!root.bumpModel.confirmFeeBump() && root.bumpModel.needsUnlock) {
                        speedUpPassphrasePopup.busy = false
                        speedUpPassphrasePopup.errorText = root.bumpModel.errorText
                        speedUpPassphrasePopup.open()
                    }
                }
            }
        }
    }

    WalletPassphrasePopup {
        id: speedUpPassphrasePopup
        parent: Overlay.overlay
        width: Math.min(420, root.width - 40)
        popupObjectName: "speedUpPassphrasePopup"
        passphraseFieldObjectName: "speedUpPassphraseField"
        errorTextObjectName: "speedUpPassphraseErrorText"
        cancelButtonObjectName: "speedUpPassphraseCancelButton"
        confirmButtonObjectName: "speedUpPassphraseConfirmButton"
        titleText: qsTr("Enter wallet password")
        descriptionText: qsTr("Enter your wallet password to update this transaction.")
        confirmText: qsTr("Unlock and update")
        busyConfirmText: qsTr("Unlocking...")
        onSubmitted: (passphrase) => {
            if (!root.bumpModel) {
                return
            }
            speedUpPassphrasePopup.busy = true
            if (root.bumpModel.confirmFeeBumpWithPassphrase(passphrase)) {
                speedUpPassphrasePopup.busy = false
                speedUpPassphrasePopup.close()
                return
            }
            speedUpPassphrasePopup.busy = false
            speedUpPassphrasePopup.errorText = root.bumpModel.errorText
        }
    }

}
