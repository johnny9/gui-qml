// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import Qt.labs.settings 1.0
import org.bitcoincore.qt 1.0

import "../../controls"
import "../../components"
import "../settings"

Page {
    id: root
    objectName: "requestPaymentPage"
    background: null

    property WalletQmlModel wallet: walletController.selectedWallet
    property PaymentRequest request: wallet ? wallet.currentPaymentRequest : null
    property string requestError: ""
    property var availableAddressTypes: wallet ? wallet.availableReceiveAddressTypes() : []
    readonly property bool requestReady: root.request !== null
    readonly property bool requestPersisted: root.requestReady && root.request.id !== ""
    readonly property bool requestHasAddress: root.requestReady && root.request.address !== ""

    function selectedReceiveAddressType() {
        if (root.request && root.request.addressType.length > 0) {
            return root.request.addressType;
        }
        return root.wallet ? root.wallet.defaultReceiveAddressType() : "";
    }

    function addressTypeIndex(addressType) {
        for (let i = 0; i < root.availableAddressTypes.length; ++i) {
            if (root.availableAddressTypes[i].id === addressType) {
                return i;
            }
        }
        return root.availableAddressTypes.length > 0 ? 0 : -1;
    }

    function ensureAddressTypeSelected() {
        if (!root.request || root.request.address !== "" || root.request.addressType.length > 0) {
            return;
        }
        root.request.addressType = root.selectedReceiveAddressType();
    }

    function primaryActionText() {
        if (root.requestPersisted) {
            return qsTr("Copy payment request")
        }
        if (root.requestHasAddress) {
            return qsTr("Create payment request")
        }
        return qsTr("Create bitcoin address")
    }

    function runPrimaryAction() {
        if (!root.requestReady) {
            return
        }
        root.requestError = ""
        if (!root.requestPersisted) {
            root.ensureAddressTypeSelected()
            if (!root.wallet.commitPaymentRequest()) {
                if (root.request && root.request.needsUnlock) {
                    commitPassphrasePopup.errorText = ""
                    commitPassphrasePopup.open()
                    return
                }
                root.requestError = root.requestHasAddress
                    ? qsTr("The payment request could not be created.")
                    : qsTr("The new payment address could not be created.")
            }
            return
        }
        Clipboard.setText(root.request.address)
    }

    Component.onCompleted: root.ensureAddressTypeSelected()

    onWalletChanged: root.ensureAddressTypeSelected()
    onRequestChanged: root.ensureAddressTypeSelected()

    Settings {
        id: settings
        property alias addressFormatEnabled: receiveOptionsPopup.addressFormatEnabled
    }

    Binding {
        target: root.request ? root.request.amount : null
        property: "unit"
        value: optionsModel.displayUnit
    }

    ScrollView {
        clip: true
        width: parent.width
        height: parent.height
        contentWidth: width

        Item {
            id: titleRow
            anchors.left: contentRow.left
            anchors.right: contentRow.right
            anchors.top: parent.top
            anchors.topMargin: 20
            height: Math.max(title.height, menuButton.height)

            CoreText {
                id: title
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                text: root.requestPersisted
                    ? qsTr("Payment request #") + root.request.id
                    : qsTr("Request a payment")
                font.pixelSize: 21
                bold: true
            }

            IconButton {
                id: menuButton
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                checked: receiveOptionsPopup.opened
                iconSource: "image://images/ellipsis"
                onClicked: receiveOptionsPopup.open()
            }

            ReceiveOptionsPopup {
                id: receiveOptionsPopup
                x: menuButton.x - width + menuButton.width
                y: menuButton.y + menuButton.height
            }
        }

        RowLayout {
            id: contentRow

            enabled: walletController.initialized && root.request !== null

            anchors.top: titleRow.bottom
            anchors.topMargin: 40
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 30
            ColumnLayout {
                id: columnLayout
                Layout.minimumWidth: 450
                Layout.maximumWidth: AppMode.isDesktop ? 650 : 470

                spacing: 5

                Item {
                    Layout.preferredHeight: 50
                    Layout.fillWidth: true
                    CoreText {
                        id: amountLabel
                        width: 110
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        horizontalAlignment: Text.AlignLeft
                        text: qsTr("Amount")
                        font.pixelSize: 18
                    }

                    TextField {
                        id: amountInput
                        objectName: "requestPaymentAmountInput"
                        anchors.left: amountLabel.right
                        anchors.verticalCenter: parent.verticalCenter
                        leftPadding: 0
                        font.family: "BitcoinCoreSans"
                        font.styleName: "Regular"
                        font.pixelSize: 18
                        color: Theme.color.neutral9
                        placeholderTextColor: enabled ? Theme.color.neutral7 : Theme.color.neutral4
                        background: Item {}
                        placeholderText: root.request && root.request.amount.unit === BitcoinAmount.SAT ? "0" : "0.00000000"
                        selectByMouse: true
                        text: root.request ? root.request.amount.display : ""
                        onTextEdited: {
                            root.requestError = ""
                            if (root.request) {
                                root.request.amount.display = text;
                            }
                        }
                        onEditingFinished: {
                            if (root.request) {
                                root.request.amount.format();
                            }
                        }
                        onActiveFocusChanged: {
                            if (!activeFocus && root.request) {
                                root.request.amount.format();
                            }
                        }
                        validator: RegularExpressionValidator {
                            regularExpression: !root.request || root.request.amount.unit === BitcoinAmount.BTC ? /^(0|[1-9]\d{0,7})(\.\d{0,8})?$/ : /^(0|[1-9]\d{0,15})$/
                        }
                        maximumLength: !root.request || root.request.amount.unit === BitcoinAmount.BTC ? 17 : 16
                    }
                    Item {
                        width: unitLabel.width + flipIcon.width
                        height: Math.max(unitLabel.height, flipIcon.height)
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                if (root.request) {
                                    root.request.amount.flipUnit();
                                }
                            }
                        }
                        CoreText {
                            id: unitLabel
                            anchors.right: flipIcon.left
                            anchors.verticalCenter: parent.verticalCenter
                            text: root.request ? root.request.amount.unitLabel : ""
                            font.pixelSize: 18
                            color: enabled ? Theme.color.neutral7 : Theme.color.neutral4
                        }
                        Icon {
                            id: flipIcon
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            source: "image://images/flip-vertical"
                            color: unitLabel.enabled ? Theme.color.neutral8 : Theme.color.neutral4
                            size: 30
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    visible: root.request !== null && root.request.amountError.length > 0

                    Icon {
                        source: "image://images/alert-filled"
                        size: 22
                        color: Theme.color.red
                    }

                    CoreText {
                        text: root.request ? root.request.amountError : ""
                        font.pixelSize: 15
                        color: Theme.color.red
                        horizontalAlignment: Text.AlignLeft
                        Layout.fillWidth: true
                    }
                }

                Separator {
                    Layout.fillWidth: true
                }

                LabeledTextInput {
                    id: label
                    objectName: "requestPaymentLabelInput"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 50
                    labelText: qsTr("Note to self")
                    placeholderText: qsTr("Enter note…")
                    text: root.request ? root.request.label : ""
                    onTextEdited: {
                        root.requestError = ""
                        if (root.request) {
                            root.request.label = label.text;
                        }
                    }
                }

                Separator {
                    Layout.fillWidth: true
                }

                LabeledTextInput {
                    id: message
                    objectName: "requestPaymentMessageInput"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 50
                    labelText: qsTr("Message")
                    placeholderText: qsTr("Enter message...")
                    text: root.request ? root.request.message : ""
                    onTextEdited: {
                        root.requestError = ""
                        if (root.request) {
                            root.request.message = message.text;
                        }
                    }
                }

                Separator {
                    Layout.fillWidth: true
                }

                ColumnLayout {
                    id: addressFormatRow
                    Layout.fillWidth: true
                    visible: settings.addressFormatEnabled
                    spacing: 0

                    RowLayout {
                        Layout.fillWidth: true
                        height: 40

                        CoreText {
                            id: addressTypeLabel
                            Layout.preferredWidth: 150
                            Layout.minimumWidth: 150
                            Layout.maximumWidth: 150
                            Layout.rightMargin: 10
                            horizontalAlignment: Text.AlignLeft
                            text: qsTr("Address type")
                            font.pixelSize: 18
                            wrapMode: Text.NoWrap
                        }

                        Button {
                            id: addressTypePicker
                            objectName: "receiveAddressTypePicker"
                            property int selectedIndex: root.addressTypeIndex(root.selectedReceiveAddressType())
                            property string selectedLabel: selectedIndex >= 0 && root.availableAddressTypes.length > 0 ? root.availableAddressTypes[selectedIndex].label : ""

                            enabled: root.request !== null && root.request.address === "" && count > 0
                            hoverEnabled: AppMode.isDesktop
                            Layout.fillWidth: true
                            leftPadding: 10
                            rightPadding: 4
                            topPadding: 2
                            bottomPadding: 2
                            height: 28
                            onPressed: addressTypePopup.open()

                            readonly property int count: root.availableAddressTypes.length

                            HoverHandler {
                                cursorShape: Qt.PointingHandCursor
                            }

                            contentItem: RowLayout {
                                spacing: 0

                                CoreText {
                                    text: addressTypePicker.selectedLabel
                                    font.pixelSize: 18
                                    Layout.fillWidth: true
                                    horizontalAlignment: Text.AlignRight
                                    elide: Text.ElideRight
                                }

                                Icon {
                                    source: "image://images/caret-down-medium-filled"
                                    Layout.preferredWidth: 30
                                    size: 30
                                    color: addressTypePicker.enabled ? Theme.color.orange : Theme.color.neutral4
                                }
                            }

                            background: Rectangle {
                                id: addressTypePickerBg
                                color: Theme.color.background
                                radius: 6
                                Behavior on color {
                                    ColorAnimation {
                                        duration: 150
                                    }
                                }
                            }

                            states: [
                                State {
                                    name: "HOVER"
                                    when: addressTypePicker.hovered
                                    PropertyChanges {
                                        target: addressTypePickerBg
                                        color: Theme.color.neutral2
                                    }
                                },
                                State {
                                    name: "DISABLED"
                                    when: !addressTypePicker.enabled
                                    PropertyChanges {
                                        target: addressTypePickerBg
                                        color: Theme.color.background
                                    }
                                }
                            ]
                        }
                    }

                    Popup {
                        id: addressTypePopup
                        modal: true
                        dim: false

                        background: Rectangle {
                            color: Theme.color.background
                            radius: 6
                            border.color: Theme.color.neutral4
                        }

                        width: 300
                        height: Math.min(addressTypeList.contentHeight + 10, 400)
                        x: Math.max(0, addressTypePicker.x + addressTypePicker.width - width)
                        y: addressTypePicker.y + addressTypePicker.height + 2
                        padding: 5

                        contentItem: ListView {
                            id: addressTypeList
                            model: root.availableAddressTypes
                            interactive: false
                            width: 300
                            height: contentHeight
                            spacing: 2
                            delegate: ItemDelegate {
                                id: delegate
                                required property var modelData
                                required property int index

                                width: ListView.view.width
                                leftPadding: 10
                                rightPadding: 4
                                topPadding: 6
                                bottomPadding: 6

                                background: Item {
                                    Rectangle {
                                        anchors.fill: parent
                                        radius: 6
                                        color: Theme.color.neutral2
                                        visible: delegate.hovered
                                    }
                                }

                                contentItem: RowLayout {
                                    spacing: 5

                                    Item {
                                        Layout.alignment: Qt.AlignVCenter
                                        Layout.preferredWidth: 24
                                        Layout.preferredHeight: 24

                                        Icon {
                                            anchors.fill: parent
                                            visible: delegate.index === addressTypePicker.selectedIndex
                                            source: "image://images/check"
                                            color: Theme.color.orange
                                            size: 24
                                        }
                                    }

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 2

                                        CoreText {
                                            text: delegate.modelData.label
                                            horizontalAlignment: Text.AlignLeft
                                            Layout.fillWidth: true
                                            font.pixelSize: 15
                                            elide: Text.ElideRight
                                        }

                                        CoreText {
                                            text: delegate.modelData.description
                                            horizontalAlignment: Text.AlignLeft
                                            Layout.fillWidth: true
                                            font.pixelSize: 13
                                            color: Theme.color.neutral7
                                            wrapMode: Text.WordWrap
                                        }
                                    }
                                }

                                HoverHandler {
                                    cursorShape: Qt.PointingHandCursor
                                }

                                onClicked: {
                                    if (!root.request) {
                                        return;
                                    }
                                    root.request.addressType = delegate.modelData.id;
                                    if (root.wallet) {
                                        root.wallet.setDefaultReceiveAddressType(delegate.modelData.id);
                                    }
                                    addressTypePopup.close();
                                }
                            }
                        }
                    }
                }

                Separator {
                    Layout.fillWidth: true
                    visible: settings.addressFormatEnabled
                }

                Item {
                    Layout.fillWidth: true
                    Layout.minimumHeight: addressLabel.height + copyLabel.height
                    Layout.topMargin: 10
                    height: addressLabel.height + copyLabel.height
                    CoreText {
                        id: addressLabel
                        anchors.left: parent.left
                        anchors.top: parent.top
                        horizontalAlignment: Text.AlignLeft
                        width: 110
                        text: qsTr("Address")
                        font.pixelSize: 18
                    }
                    CoreText {
                        id: copyLabel
                        anchors.left: parent.left
                        anchors.top: addressLabel.bottom
                        horizontalAlignment: Text.AlignLeft
                        width: 110
                        text: qsTr("copy")
                        font.pixelSize: 18
                        color: copyArea.enabled ? Theme.color.orange : Theme.color.neutral4
                    }

                    Rectangle {
                        anchors.left: addressLabel.right
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        color: Theme.color.neutral2
                        radius: 5
                        CoreText {
                            id: address
                            objectName: "requestPaymentAddressText"
                            anchors.fill: parent
                            anchors.leftMargin: 5
                            horizontalAlignment: Text.AlignLeft
                            font.family: "Roboto Mono"
                            font.styleName: "Regular"
                            font.pixelSize: 18
                            wrapMode: Text.WordWrap
                            text: root.request ? root.request.addressFormatted : ""
                        }
                    }

                    MouseArea {
                        id: copyArea
                        anchors.left: parent.left
                        anchors.top: addressLabel.bottom
                        anchors.right: addressLabel.right
                        anchors.bottom: parent.bottom
                        hoverEnabled: true
                        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                        enabled: root.requestHasAddress
                        onClicked: Clipboard.setText(root.request.address)
                    }

                    MouseArea {
                        id: addressArea
                        anchors.left: addressLabel.right
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        hoverEnabled: true
                        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                        enabled: root.requestHasAddress
                        onClicked: Clipboard.setText(root.request.address)
                    }
                }

                ContinueButton {
                    id: continueButton
                    objectName: "requestPaymentCreateButton"
                    Layout.fillWidth: true
                    Layout.topMargin: 30
                    text: root.primaryActionText()
                    onClicked: root.runPrimaryAction()
                }

                RowLayout {
                    Layout.fillWidth: true
                    visible: root.requestError.length > 0

                    Icon {
                        source: "image://images/alert-filled"
                        size: 22
                        color: Theme.color.red
                    }

                    CoreText {
                        text: root.requestError
                        font.pixelSize: 15
                        color: Theme.color.red
                        horizontalAlignment: Text.AlignLeft
                        Layout.fillWidth: true
                    }
                }

                ContinueButton {
                    id: clearRequest
                    Layout.fillWidth: true
                    Layout.topMargin: 10
                    visible: root.request !== null && root.request.id !== ""
                    borderColor: Theme.color.neutral6
                    borderHoverColor: Theme.color.orangeLight1
                    borderPressedColor: Theme.color.orangeLight2
                    backgroundColor: "transparent"
                    backgroundHoverColor: "transparent"
                    backgroundPressedColor: "transparent"
                    text: qsTr("Clear")
                    onClicked: {
                        if (root.request) {
                            root.requestError = ""
                            root.request.clear()
                            root.ensureAddressTypeSelected()
                        }
                    }
                }

                Connections {
                    target: walletController
                    function onSelectedWalletChanged() {
                        root.requestError = ""
                        if (root.request) {
                            root.request.clear();
                            root.ensureAddressTypeSelected();
                        }
                    }
                }
            }

            Pane {
                Layout.alignment: Qt.AlignTop
                Layout.minimumWidth: 150
                Layout.minimumHeight: 150
                padding: 0
                background: Rectangle {
                    color: Theme.color.neutral2
                    visible: qrImage.code === ""
                }
                contentItem: QRImage {
                    id: qrImage
                    backgroundColor: "transparent"
                    foregroundColor: Theme.color.neutral9
                    code: root.request ? root.request.address : ""
                }
            }
        }
    }

    WalletPassphrasePopup {
        id: commitPassphrasePopup
        parent: Overlay.overlay
        width: Math.min(420, root.width - 40)
        popupObjectName: "requestPaymentPassphrasePopup"
        passphraseFieldObjectName: "requestPaymentPassphraseField"
        errorTextObjectName: "requestPaymentPassphraseErrorText"
        cancelButtonObjectName: "requestPaymentPassphraseCancelButton"
        confirmButtonObjectName: "requestPaymentPassphraseConfirmButton"
        titleText: qsTr("Enter wallet password")
        descriptionText: qsTr("Enter your wallet password to create a new address.")
        confirmText: qsTr("Unlock and create")
        busyConfirmText: qsTr("Unlocking...")
        onSubmitted: passphrase => {
            commitPassphrasePopup.busy = true
            if (root.wallet && root.wallet.commitPaymentRequestWithPassphrase(passphrase)) {
                commitPassphrasePopup.busy = false
                commitPassphrasePopup.close()
                return
            }
            commitPassphrasePopup.busy = false
            commitPassphrasePopup.errorText = root.request ? root.request.unlockError : ""
        }
    }
}
