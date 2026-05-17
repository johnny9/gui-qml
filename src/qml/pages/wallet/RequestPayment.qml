// Copyright (c) 2024-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import Qt.labs.settings 1.0
import org.bitcoincore.qt 1.0

import "../../controls"
import "../../components"
import "../../components" as Components

Page {
    id: root
    objectName: "requestPaymentPage"
    background: null

    property WalletQmlModel wallet: walletController.selectedWallet
    property PaymentRequest request: wallet ? wallet.currentPaymentRequest : null
    property string requestError: ""
    property string selectedReceiveAddressType: ""
    property var availableAddressTypes: {
        if (!root.wallet) return []
        if (typeof root.wallet.availableReceiveAddressTypes === "function") {
            return root.wallet.availableReceiveAddressTypes()
        }
        return root.wallet.receiveAddressTypes !== undefined ? root.wallet.receiveAddressTypes : []
    }
    readonly property bool hasAddress: root.requestValue("address") !== ""
    readonly property bool hasAddressType: receiveOptionsPopup.showAddressType && root.hasAddress && root.requestValue("addressType") !== ""
    readonly property bool showAddressTypeSelector: receiveOptionsPopup.showAddressType && root.request !== null && root.requestIsEditing() && !root.hasAddress
    readonly property bool hasSavedRequest: root.requestValue("id") !== ""

    function requestValue(name) {
        if (!root.request || root.request[name] === undefined || root.request[name] === null) {
            return ""
        }
        return root.request[name]
    }

    function requestIsEditing() {
        return !root.request || root.request.isEditing === undefined ? true : root.request.isEditing
    }

    function resetSelectedReceiveAddressType() {
        if (!root.wallet) {
            root.selectedReceiveAddressType = "bech32"
            return
        }
        if (typeof root.wallet.defaultReceiveAddressType === "function") {
            root.selectedReceiveAddressType = root.wallet.defaultReceiveAddressType()
        } else if (root.wallet.defaultReceiveAddressType !== undefined && root.wallet.defaultReceiveAddressType !== "") {
            root.selectedReceiveAddressType = root.wallet.defaultReceiveAddressType
        } else {
            root.selectedReceiveAddressType = "bech32"
        }
    }

    function addressTypeIndex(addressType) {
        for (let i = 0; i < root.availableAddressTypes.length; ++i) {
            if (root.availableAddressTypes[i].id === addressType) {
                return i
            }
        }
        return root.availableAddressTypes.length > 0 ? 0 : -1
    }

    function commitCurrentRequest() {
        if (!root.request || !root.wallet) return false
        root.requestError = ""

        if (!root.hasAddress && root.request.addressType !== undefined && root.selectedReceiveAddressType !== "") {
            root.request.addressType = root.selectedReceiveAddressType
        }

        if (root.wallet.commitPaymentRequest()) {
            root.request.isEditing = false
            return true
        }

        if (root.request.needsUnlock) {
            commitPassphrasePopup.errorText = ""
            commitPassphrasePopup.open()
            return false
        }

        root.requestError = root.hasAddress
            ? qsTr("The payment request could not be created.")
            : qsTr("The new payment address could not be created.")
        return false
    }

    Component.onCompleted: resetSelectedReceiveAddressType()
    onWalletChanged: {
        root.requestError = ""
        resetSelectedReceiveAddressType()
    }

    Binding {
        target: root.request ? root.request.amount : null
        property: "unit"
        value: optionsModel.displayUnit
    }

    Settings {
        id: receiveSettings
        property alias receiveShowName: receiveOptionsPopup.showName
        property alias receiveShowMessage: receiveOptionsPopup.showMessage
        property alias receiveShowNoteSelf: receiveOptionsPopup.showNoteSelf
        property alias receiveShowAddressType: receiveOptionsPopup.showAddressType
    }

    Item {
        id: requestHistoryCount
        objectName: "requestHistoryCount"
        visible: false
        property int count: root.wallet && root.wallet.receiveRequests !== undefined ? root.wallet.receiveRequests.count : 0
    }

    ScrollView {
        clip: true
        width: parent.width
        height: parent.height
        contentWidth: width

        ColumnLayout {
            width: 520
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 10
            enabled: walletController.initialized

            Item {
                id: titleRow
                Layout.fillWidth: true
                Layout.topMargin: 30
                Layout.bottomMargin: 20

                CoreText {
                    id: titleText
                    objectName: "requestPaymentTitle"
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.hasSavedRequest
                        ? qsTr("Payment request #%1").arg(root.requestValue("id"))
                        : qsTr("Request a payment")
                    font.pixelSize: 21
                    color: Theme.color.neutral9
                    bold: true
                }

                IconButton {
                    id: receiveOptionsButton
                    objectName: "receiveOptionsButton"
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    checked: receiveOptionsPopup.opened
                    iconSource: "image://images/ellipsis"
                    Accessible.name: qsTr("Receive options")
                    onClicked: {
                        if (receiveOptionsPopup.opened) {
                            receiveOptionsPopup.close()
                        } else {
                            receiveOptionsPopup.open()
                        }
                    }
                }

                Components.ReceiveOptionsPopup {
                    id: receiveOptionsPopup
                    x: receiveOptionsButton.x - width + receiveOptionsButton.width
                    y: receiveOptionsButton.y + receiveOptionsButton.height
                }
            }

            ColumnLayout {
                Layout.fillWidth: true

                Item {
                    Layout.fillWidth: true
                    height: amountInput.height

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
                        Accessible.name: qsTr("Payment amount")
                        anchors.left: amountLabel.right
                        anchors.right: unitFlipItem.left
                        anchors.verticalCenter: parent.verticalCenter
                        leftPadding: 0
                        font.family: "BitcoinCoreSans"
                        font.styleName: "Regular"
                        font.pixelSize: 18
                        color: Theme.color.neutral9
                        placeholderTextColor: enabled ? Theme.color.neutral7 : Theme.color.neutral4
                        background: Item {}
                        placeholderText: !root.request || root.request.amount.unit === BitcoinAmount.BTC
                            ? "0.00000000" : "0"
                        selectByMouse: true
                        enabled: root.requestIsEditing()
                        text: root.request ? root.request.amount.display : ""
                        onTextEdited: {
                            root.requestError = ""
                            if (root.request) {
                                root.request.amount.display = text
                            }
                        }
                        onEditingFinished: {
                            if (root.request) {
                                root.request.amount.format()
                            }
                        }
                        onActiveFocusChanged: {
                            if (!activeFocus && root.request) {
                                root.request.amount.format()
                            }
                        }
                        validator: RegularExpressionValidator {
                            regularExpression: !root.request || root.request.amount.unit === BitcoinAmount.BTC
                                ? /^(0|[1-9]\d{0,7})(\.\d{0,8})?$/
                                : /^(0|[1-9]\d{0,15})$/
                        }
                        maximumLength: !root.request || root.request.amount.unit === BitcoinAmount.BTC ? 17 : 16
                    }

                    Item {
                        id: unitFlipItem
                        width: unitLabel.width + flipIcon.width
                        height: Math.max(unitLabel.height, flipIcon.height)
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter

                        MouseArea {
                            anchors.fill: parent
                            enabled: root.requestIsEditing()
                            cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                            onClicked: {
                                if (root.request) {
                                    root.request.amount.flipUnit()
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

                Connections {
                    target: root.request ? root.request.amount : null
                    function onDisplayChanged() {
                        amountInput.text = root.request ? root.request.amount.display : ""
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

            Separator {
                Layout.fillWidth: true
            }

            LabeledTextInput {
                id: nameInput
                objectName: "requestPaymentLabelInput"
                inputObjectName: "requestPaymentYourNameInput"
                Layout.fillWidth: true
                visible: receiveOptionsPopup.showName
                labelText: qsTr("Name")
                placeholderText: qsTr("Enter name...")
                enabled: root.requestIsEditing()
                text: root.requestValue("label")
                onTextEdited: {
                    root.requestError = ""
                    if (root.request) {
                        root.request.label = nameInput.text
                    }
                }
            }

            Separator {
                Layout.fillWidth: true
                visible: receiveOptionsPopup.showName && (receiveOptionsPopup.showMessage || receiveOptionsPopup.showNoteSelf || root.showAddressTypeSelector || root.hasAddressType || root.hasAddress)
            }

            LabeledTextInput {
                id: messageInput
                objectName: "requestPaymentMessageInput"
                Layout.fillWidth: true
                visible: receiveOptionsPopup.showMessage
                labelText: qsTr("Message")
                placeholderText: qsTr("Enter message...")
                enabled: root.requestIsEditing()
                text: root.requestValue("message")
                onTextEdited: {
                    root.requestError = ""
                    if (root.request) {
                        root.request.message = messageInput.text
                    }
                }
            }

            Separator {
                Layout.fillWidth: true
                visible: receiveOptionsPopup.showMessage && (receiveOptionsPopup.showNoteSelf || root.showAddressTypeSelector || root.hasAddressType || root.hasAddress)
            }

            LabeledTextInput {
                id: noteSelfInput
                objectName: "requestPaymentNoteSelfInput"
                Layout.fillWidth: true
                visible: receiveOptionsPopup.showNoteSelf
                labelText: qsTr("Note to self")
                placeholderText: qsTr("Enter private note...")
                enabled: root.requestIsEditing()
                text: root.requestValue("noteSelf")
                onTextEdited: {
                    root.requestError = ""
                    if (root.request && root.request.noteSelf !== undefined) {
                        root.request.noteSelf = noteSelfInput.text
                    }
                }
            }

            Separator {
                Layout.fillWidth: true
                visible: receiveOptionsPopup.showNoteSelf && (root.showAddressTypeSelector || root.hasAddressType || root.hasAddress)
            }

            ColumnLayout {
                id: addressFormatRow
                Layout.fillWidth: true
                visible: root.showAddressTypeSelector
                spacing: 0

                RowLayout {
                    Layout.fillWidth: true
                    height: 40

                    CoreText {
                        id: addressTypePickerLabel
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
                        property int selectedIndex: root.addressTypeIndex(root.selectedReceiveAddressType)
                        property string selectedLabel: selectedIndex >= 0 && root.availableAddressTypes.length > 0
                            ? root.availableAddressTypes[selectedIndex].label : ""

                        enabled: root.request !== null && root.requestIsEditing() && root.availableAddressTypes.length > 0
                        hoverEnabled: AppMode.isDesktop
                        Layout.fillWidth: true
                        leftPadding: 10
                        rightPadding: 4
                        topPadding: 2
                        bottomPadding: 2
                        height: 28
                        onPressed: addressTypePopup.open()

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
                                root.selectedReceiveAddressType = delegate.modelData.id
                                addressTypePopup.close()
                            }
                        }
                    }
                }
            }

            Item {
                Layout.fillWidth: true
                visible: root.hasAddressType
                implicitHeight: addressTypeLabel.implicitHeight + 16
                height: implicitHeight

                CoreText {
                    id: addressTypeLabel
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    width: 110
                    horizontalAlignment: Text.AlignLeft
                    text: qsTr("Type")
                    font.pixelSize: 18
                }

                CoreText {
                    anchors.left: addressTypeLabel.right
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    horizontalAlignment: Text.AlignLeft
                    text: root.requestValue("addressType")
                    font.pixelSize: 18
                    color: Theme.color.neutral9
                }
            }

            Separator {
                Layout.fillWidth: true
                visible: root.hasAddressType && root.hasAddress
            }

            Item {
                Layout.fillWidth: true
                visible: root.hasAddress
                Layout.topMargin: root.hasAddressType ? 0 : 10
                implicitHeight: addressLabel.height + copyLabel.height
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
                    text: qsTr("Copy")
                    font.pixelSize: 18
                    color: Theme.color.orange
                }

                CoreText {
                    id: addressValue
                    objectName: "requestPaymentAddressText"
                    anchors.left: addressLabel.right
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    horizontalAlignment: Text.AlignLeft
                    font.pixelSize: 18
                    wrapMode: Text.WordWrap
                    text: root.request ? root.request.addressFormatted : ""
                }

                MouseArea {
                    anchors.left: parent.left
                    anchors.top: addressLabel.bottom
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (root.request) {
                            Clipboard.setText(root.request.address)
                            copiedToast.show()
                        }
                    }
                }
            }

            CopiedToast {
                id: copiedToast
                objectName: "requestPaymentCopiedToast"
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: 4
            }

            ContinueButton {
                id: generateButton
                objectName: "requestPaymentGenerateButton"
                Layout.fillWidth: true
                Layout.topMargin: 30
                text: {
                    if (!root.request || root.requestIsEditing()) {
                        return root.hasSavedRequest
                            ? qsTr("Update payment request")
                            : qsTr("Generate payment request")
                    }
                    return qsTr("New request")
                }
                onClicked: {
                    if (!root.request) return
                    if (root.requestIsEditing()) {
                        root.commitCurrentRequest()
                    } else {
                        root.request.clear()
                        root.requestError = ""
                        root.resetSelectedReceiveAddressType()
                    }
                }

                Item {
                    objectName: "requestPaymentCreateButton"
                    anchors.fill: parent
                    enabled: false
                }
            }

            OutlineButton {
                objectName: "requestPaymentCancelButton"
                Layout.fillWidth: true
                Layout.topMargin: 10
                visible: root.request ? root.requestIsEditing() && root.hasSavedRequest : false
                text: qsTr("Cancel")
                onClicked: {
                    if (root.request && root.wallet && root.wallet.loadPaymentRequest(root.request.id)) {
                        root.request.isEditing = false
                    }
                }
            }

            RowLayout {
                id: generatedActions
                objectName: "requestPaymentGeneratedActions"
                Layout.fillWidth: true
                Layout.topMargin: 10
                visible: root.request ? !root.requestIsEditing() : false
                spacing: 10

                Button {
                    id: editButton
                    objectName: "requestPaymentEditButton"
                    Layout.fillWidth: true
                    Layout.preferredWidth: 0
                    hoverEnabled: AppMode.isDesktop
                    implicitHeight: 46
                    Accessible.name: qsTr("Edit payment request")

                    contentItem: RowLayout {
                        spacing: 6
                        Item { Layout.fillWidth: true }
                        Icon {
                            source: "qrc:/icons/edit"
                            color: Theme.color.neutral9
                            size: 24
                        }
                        CoreText {
                            text: qsTr("Edit")
                            bold: true
                            font.pixelSize: 18
                            color: Theme.color.neutral9
                        }
                        Item { Layout.fillWidth: true }
                    }

                    background: Rectangle {
                        implicitHeight: 46
                        color: Theme.color.background
                        radius: 5
                        border.width: 1
                        border.color: editButton.pressed ? Theme.color.orangeLight2 : editButton.hovered ? Theme.color.neutral9 : Theme.color.neutral6
                        Behavior on border.color { ColorAnimation { duration: 150 } }
                    }

                    onClicked: {
                        if (root.request) root.request.edit()
                    }
                }

                Button {
                    id: copyButton
                    objectName: "requestPaymentCopyButton"
                    Layout.fillWidth: true
                    Layout.preferredWidth: 0
                    hoverEnabled: AppMode.isDesktop
                    implicitHeight: 46
                    Accessible.name: qsTr("Copy payment request")

                    contentItem: RowLayout {
                        spacing: 6
                        Item { Layout.fillWidth: true }
                        Icon {
                            source: "qrc:/icons/copy"
                            color: Theme.color.neutral9
                            size: 24
                        }
                        CoreText {
                            text: qsTr("Copy")
                            bold: true
                            font.pixelSize: 18
                            color: Theme.color.neutral9
                        }
                        Item { Layout.fillWidth: true }
                    }

                    background: Rectangle {
                        implicitHeight: 46
                        color: Theme.color.background
                        radius: 5
                        border.width: 1
                        border.color: copyButton.pressed ? Theme.color.orangeLight2 : copyButton.hovered ? Theme.color.neutral9 : Theme.color.neutral6
                        Behavior on border.color { ColorAnimation { duration: 150 } }
                    }

                    onClicked: {
                        if (root.request) {
                            Clipboard.setText(root.request.qrPayload)
                            copiedToast.show()
                        }
                    }
                }

                Button {
                    id: qrButton
                    objectName: "requestPaymentQRButton"
                    Layout.fillWidth: true
                    Layout.preferredWidth: 0
                    hoverEnabled: AppMode.isDesktop
                    implicitHeight: 46
                    Accessible.name: qsTr("Show QR code")

                    contentItem: RowLayout {
                        spacing: 6
                        Item { Layout.fillWidth: true }
                        Icon {
                            source: "qrc:/icons/qr-code"
                            color: Theme.color.neutral9
                            size: 24
                        }
                        CoreText {
                            text: qsTr("QR Code")
                            bold: true
                            font.pixelSize: 18
                            color: Theme.color.neutral9
                        }
                        Item { Layout.fillWidth: true }
                    }

                    background: Rectangle {
                        implicitHeight: 46
                        color: Theme.color.background
                        radius: 5
                        border.width: 1
                        border.color: qrButton.pressed ? Theme.color.orangeLight2 : qrButton.hovered ? Theme.color.neutral9 : Theme.color.neutral6
                        Behavior on border.color { ColorAnimation { duration: 150 } }
                    }

                    onClicked: qrPopup.open()
                }

            }

            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: 20
            }

            Connections {
                target: root.request && root.request.isEditing !== undefined ? root.request : null
                function onIsEditingChanged() {
                    if (root.request) {
                        amountInput.text = root.request.amount.display
                        nameInput.text = root.requestValue("label")
                        messageInput.text = root.requestValue("message")
                        noteSelfInput.text = root.requestValue("noteSelf")
                    }
                }
            }

            Connections {
                target: walletController
                function onSelectedWalletChanged() {
                    if (root.request) {
                        root.request.clear()
                    }
                    root.requestError = ""
                    root.resetSelectedReceiveAddressType()
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
                root.request.isEditing = false
                commitPassphrasePopup.busy = false
                commitPassphrasePopup.close()
                return
            }
            commitPassphrasePopup.busy = false
            commitPassphrasePopup.errorText = root.requestValue("unlockError") !== ""
                ? root.requestValue("unlockError")
                : qsTr("The payment request could not be created.")
        }
    }

    QRCodePopup {
        id: qrPopup
        objectName: "requestPaymentQRPopup"
        code: root.request ? root.request.qrPayload : ""
        label: root.requestValue("label")
        onCopyRequested: {
            if (root.request) {
                Clipboard.setText(root.request.qrPayload)
                copiedToast.show()
            }
            qrPopup.close()
        }
    }
}
