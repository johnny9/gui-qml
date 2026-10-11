// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Dialogs
import org.bitcoincore.qt 1.0
import "../controls"

Pane {
    id: root
    property var wallet
    property var clipboard: Clipboard
    property var request: wallet ? wallet.receivingAddress : null
    property string errorText: ""
    property bool pendingNext: false
    property string pendingType: ""
    property int contextRevision: 0
    property int addressOperationRevision: -1
    property int qrRevision: 0
    property int imageSaveRevision: -1
    property bool ensureAfterPending: false
    readonly property bool ready: !!request && request.address !== "" && !request.paymentReceived
    padding: 24
    implicitHeight: contentItem.implicitHeight + topPadding + bottomPadding
    background: Rectangle { color: Theme.color.neutral1; radius: 16 }

    function invalidateQR() {
        ++qrRevision
        qrMenu.close()
        saveDialog.close()
    }
    function invalidateContext() {
        ++contextRevision
        addressOperationRevision = -1
        ensureAfterPending = false
        pendingNext = false
        pendingType = ""
        errorText = ""
        passphrasePopup.close()
        passphrasePopup.busy = false
        passphrasePopup.errorText = ""
        addressTypePicker.ownerRevision = -1
        addressTypePicker.close()
        invalidateQR()
    }
    onWalletChanged: invalidateContext()
    onRequestChanged: invalidateContext()
    onVisibleChanged: if (!visible) invalidateContext()

    function ensureAddressLater() {
        const revision = contextRevision
        Qt.callLater(function() {
            if (root.visible && root.contextRevision === revision)
                root.ensureAddress(root.pendingNext, root.pendingType)
        })
    }
    function ensureAddress(next, type) {
        if (!wallet || !visible) return false
        pendingNext = !!next
        pendingType = type || ""
        if (wallet.receiveOperationPending) {
            ensureAfterPending = true
            return false
        }
        ensureAfterPending = false
        errorText = ""
        const accepted = wallet.ensureReceivingAddress(pendingNext, pendingType)
        addressOperationRevision = accepted ? contextRevision : -1
        return accepted
    }
    function captureQR(callback) {
        if (!visible || !ready || qrImage.status !== Image.Ready) {
            errorText = qsTr("The QR code is not ready. Please try again.")
            return false
        }
        const revision = qrRevision
        const address = request.address
        return qrPanel.grabToImage(function(result) {
            if (root.visible && root.qrRevision === revision && root.ready && root.request.address === address)
                callback(result)
        }, Qt.size(784, 784))
    }
    function copyQR() {
        return captureQR(function(result) {
            if (!root.clipboard.setImage(result.image)) root.errorText = qsTr("The QR code could not be copied. Please try again.")
        })
    }
    function saveQRToFile(fileUrl) {
        return captureQR(function(result) {
            if (imageSave.save(result.image, fileUrl)) root.imageSaveRevision = root.qrRevision
            else root.errorText = qsTr("The QR code could not be saved. Please try again.")
        })
    }
    ImageSaveModel {
        id: imageSave
        objectName: "receivingAddressImageSave"
        onFinished: function(success) {
            if (!success && root.visible && root.imageSaveRevision === root.qrRevision)
                root.errorText = qsTr("The QR code could not be saved. Please try again.")
            root.imageSaveRevision = -1
        }
    }
    // Keep the handlers reachable while no wallet is selected and the QML
    // engine may collect garbage before the first request becomes available.
    readonly property Connections requestConnections: Connections {
        target: root.request
        function onAddressChanged() { root.invalidateQR() }
        function onPaymentReceivedChanged() {
            if (root.request.paymentReceived) {
                root.invalidateQR()
                root.pendingNext = false
                root.pendingType = ""
                root.ensureAddressLater()
            }
        }
    }
    readonly property Connections walletCompletion: Connections {
        target: root.wallet
        function onReceiveOperationFinished(operation, success) {
            if (root.ensureAfterPending) {
                root.ensureAfterPending = false
                root.addressOperationRevision = -1
                passphrasePopup.busy = false
                root.ensureAddressLater()
                return
            } else if (operation === "rememberAddress" && root.visible && root.request.address === "") {
                root.ensureAddressLater()
            }
            if (operation !== "address" || root.addressOperationRevision !== root.contextRevision || !root.visible) return
            root.addressOperationRevision = -1
            passphrasePopup.busy = false
            root.errorText = root.wallet.receiveOperationError
            if (success) passphrasePopup.close()
            else if (root.request.needsUnlock) {
                passphrasePopup.ownerRevision = root.contextRevision
                passphrasePopup.open()
            } else if (passphrasePopup.visible) passphrasePopup.errorText = root.request.unlockError
        }
    }
    contentItem: ColumnLayout {
        enabled: !root.wallet || !root.wallet.receiveOperationPending
        spacing: 24
        CoreText {
            Layout.fillWidth: true
            text: qsTr("Receiving address")
            font: Theme.text.title.font
        }
        Rectangle {
            Layout.alignment: Qt.AlignHCenter
            implicitWidth: readyLabel.implicitWidth + 24
            implicitHeight: 30
            radius: 15
            color: root.ready ? Qt.rgba(Theme.color.green.r, Theme.color.green.g, Theme.color.green.b, 0.16)
                : Theme.color.neutral2
            CoreText {
                id: readyLabel
                anchors.centerIn: parent
                text: root.ready ? qsTr("Ready to receive") : qsTr("Preparing address")
                font: Theme.text.captionStrong.font
                color: root.ready ? Theme.color.green : Theme.color.neutral6
            }
        }
        Rectangle {
            id: qrPanel
            visible: root.ready
            Layout.preferredWidth: 196
            Layout.preferredHeight: 196
            Layout.alignment: Qt.AlignHCenter
            color: "white"
            radius: 10
            QRImage {
                id: qrImage
                objectName: "receivingAddressQRImage"
                anchors.fill: parent
                anchors.margins: 16
                code: root.ready ? root.request.address : ""
                backgroundColor: "white"
                foregroundColor: "black"
            }
            MouseArea {
                objectName: "receivingAddressQRContextArea"
                anchors.fill: parent
                acceptedButtons: Qt.RightButton
                enabled: root.ready
                onClicked: function(mouse) {
                    qrMenu.x = mouse.x
                    qrMenu.y = mouse.y
                    qrMenu.open()
                }
            }
            ContextMenu {
                id: qrMenu
                objectName: "receivingAddressQRContextMenu"
                property int ownerRevision: -1
                onAboutToShow: ownerRevision = root.qrRevision
                onAboutToHide: ownerRevision = -1
                function acceptAction() {
                    const current = root.visible && ownerRevision === root.qrRevision
                    close()
                    return current
                }
                ContextMenuButton {
                    objectName: "receivingAddressQRContextCopy"
                    text: qsTr("Copy QR code")
                    enabled: root.ready
                    autoClose: false
                    onTriggered: if (qrMenu.acceptAction()) root.copyQR()
                }
                ContextMenuButton {
                    objectName: "receivingAddressQRContextSave"
                    text: qsTr("Save QR code")
                    enabled: root.ready
                    autoClose: false
                    onTriggered: if (qrMenu.acceptAction() && root.ready) saveDialog.open()
                }
            }
        }
        AddressLabel {
            objectName: "receivingAddressText"
            Layout.fillWidth: true
            address: root.ready ? root.request.address : ""
            textStyle: Theme.text.monoDescription
            embedded: true
            interactive: root.ready
        }
        FormRow {
            Layout.fillWidth: true
            title: qsTr("Address type")
            showDivider: false
            leftPadding: 0
            rightPadding: 0
            trailingItem: PopupPicker {
                id: addressTypePicker
                objectName: "requestPaymentAddressTypeDropdown"
                property int ownerRevision: -1
                model: root.wallet ? root.wallet.receiveAddressTypes : []
                textRole: "label"
                valueRole: "id"
                subtitleRole: "description"
                embedded: true
                currentValue: root.request ? root.request.addressType.toLowerCase() : "bech32m"
                onAboutToShow: ownerRevision = root.contextRevision
                onAboutToHide: ownerRevision = -1
                onActivated: function(value) {
                    if (!acceptingSelection || ownerRevision !== root.contextRevision || !root.visible) return
                    ownerRevision = -1
                    root.ensureAddress(false, value)
                }
            }
        }
        CoreText {
            visible: root.errorText !== ""
            Layout.fillWidth: true
            text: root.errorText
            color: Theme.color.red
            font: Theme.text.caption.font
            wrap: true
        }
        TextButton {
            visible: !root.ready
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Try again")
            onClicked: root.ensureAddress(root.pendingNext, root.pendingType)
        }
    }
    FileDialog {
        id: saveDialog
        objectName: "receivingAddressSaveQRDialog"
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("PNG files (*.png)")]
        defaultSuffix: "png"
        property int ownerRevision: -1
        onVisibleChanged: if (visible) ownerRevision = root.qrRevision
        onRejected: ownerRevision = -1
        onAccepted: {
            if (ownerRevision === root.qrRevision && root.visible) root.saveQRToFile(selectedFile)
            ownerRevision = -1
        }
    }
    WalletPassphrasePopup {
        id: passphrasePopup
        popupObjectName: "receivingAddressPassphrasePopup"
        passphraseFieldObjectName: "receivingAddressPassphraseField"
        property int ownerRevision: -1
        onAboutToHide: {
            clearPassphraseField()
            ownerRevision = -1
            root.addressOperationRevision = -1
        }
        parent: Overlay.overlay
        width: Math.min(420, parent ? parent.width - 32 : 420)
        titleText: qsTr("Enter wallet password")
        descriptionText: qsTr("Enter your wallet password to create a receiving address.")
        confirmText: qsTr("Unlock and create address")
        onSubmitted: function(passphrase) {
            if (!root.visible || !root.wallet || passphrasePopup.ownerRevision !== root.contextRevision) return
            const accepted = root.wallet.ensureReceivingAddressWithPassphrase(passphrase, root.pendingNext, root.pendingType)
            root.addressOperationRevision = accepted ? root.contextRevision : -1
            passphrasePopup.busy = accepted
        }
    }
}
