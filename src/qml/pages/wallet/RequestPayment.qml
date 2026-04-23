// Copyright (c) 2024-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import Qt.labs.settings 1.0
import org.bitcoincore.qt 1.0

import "../../controls"
import "../../controls" as Controls
import "../../components"

PageStack {
    id: root

    signal viewPreviousRequests()

    property WalletQmlModel wallet: walletController.selectedWallet
    property PaymentRequest request: wallet ? wallet.currentPaymentRequest : null
    property var receiveHistory: wallet ? wallet.receiveRequests : null
    property string requestError: ""
    property var availableAddressTypes: wallet && typeof wallet.availableReceiveAddressTypes === "function"
        ? wallet.availableReceiveAddressTypes() : []
    readonly property bool requestReady: root.request !== null
    readonly property bool requestPersisted: root.requestReady && root.request.id !== ""
    readonly property bool requestHasAddress: root.requestReady && root.request.address !== ""

    function selectedReceiveAddressType() {
        if (root.request && root.request.address === "" && root.request.addressType !== undefined && root.request.addressType.length > 0) {
            return root.request.addressType
        }
        return root.wallet && typeof root.wallet.defaultReceiveAddressType === "function"
            ? root.wallet.defaultReceiveAddressType() : ""
    }

    function addressTypeIndex(addressType) {
        for (let i = 0; i < root.availableAddressTypes.length; ++i) {
            if (root.availableAddressTypes[i].id === addressType) {
                return i
            }
        }
        return root.availableAddressTypes.length > 0 ? 0 : -1
    }

    function ensureAddressTypeSelected() {
        if (!root.request || root.request.address !== "" || root.request.addressType === undefined || root.request.addressType.length > 0) {
            return
        }
        root.request.addressType = root.selectedReceiveAddressType()
    }

    function createPaymentRequest() {
        if (!root.requestReady || !root.wallet) {
            return false
        }

        root.requestError = ""
        root.ensureAddressTypeSelected()
        if (root.wallet.commitPaymentRequest()) {
            return true
        }

        if (root.request && root.request.needsUnlock) {
            commitPassphrasePopup.errorText = ""
            commitPassphrasePopup.open()
            return false
        }

        root.requestError = root.requestHasAddress
            ? qsTr("The payment request could not be created.")
            : qsTr("The new payment address could not be created.")
        return false
    }

    function clearPaymentRequest() {
        root.requestError = ""
        if (root.request) {
            root.request.clear()
            root.ensureAddressTypeSelected()
        }
    }

    Component.onCompleted: root.ensureAddressTypeSelected()

    onWalletChanged: {
        root.requestError = ""
        root.ensureAddressTypeSelected()
    }
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

    Connections {
        target: walletController
        function onSelectedWalletChanged() {
            root.pop(null)
            root.clearPaymentRequest()
        }
    }

    initialItem: Page {
        id: formPage
        objectName: "requestPaymentPage"
        background: null

        Item {
            id: requestHistoryCount
            objectName: "requestHistoryCount"
            property int count: root.receiveHistory ? root.receiveHistory.count : 0
            visible: false
        }

        ScrollView {
            id: scrollView
            clip: true
            anchors.fill: parent
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

            ColumnLayout {
                id: outerColumn
                width: scrollView.availableWidth
                spacing: 0

                ColumnLayout {
                    id: formColumn

                    enabled: walletController.initialized && root.request !== null

                    Layout.topMargin: 20
                    Layout.bottomMargin: 20
                    Layout.leftMargin: 20
                    Layout.rightMargin: 20
                    Layout.maximumWidth: 470
                    Layout.alignment: Qt.AlignHCenter

                    spacing: 10

                    Item {
                        id: titleRow
                        Layout.fillWidth: true
                        Layout.bottomMargin: 20
                        Layout.preferredHeight: Math.max(title.implicitHeight, menuButton.implicitHeight)

                        CoreText {
                            id: title
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            text: root.requestPersisted
                                ? qsTr("Payment request #") + root.request.id
                                : qsTr("Request bitcoin")
                            font.pixelSize: 21
                            color: Theme.color.neutral9
                            bold: true
                        }

                        IconButton {
                            id: menuButton
                            objectName: "receiveOptionsButton"
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            checked: receiveOptionsPopup.opened
                            iconSource: "image://images/ellipsis"
                            onClicked: receiveOptionsPopup.open()
                        }

                        Controls.ReceiveOptionsPopup {
                            id: receiveOptionsPopup
                            x: menuButton.x - width + menuButton.width
                            y: menuButton.y + menuButton.height
                        }
                    }

                    Item {
                        Layout.fillWidth: true
                        implicitHeight: amountInput.height

                        CoreText {
                            id: amountLabel
                            width: 110
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            horizontalAlignment: Text.AlignLeft
                            text: qsTr("Amount")
                            font.family: "BitcoinCoreSans"
                            font.styleName: "Regular"
                            font.pixelSize: 18
                        }

                        TextField {
                            id: amountInput
                            objectName: "requestPaymentAmountInput"
                            anchors.left: amountLabel.right
                            anchors.right: unitFlipItem.left
                            anchors.verticalCenter: parent.verticalCenter
                            leftPadding: 0
                            font.family: "Inter"
                            font.styleName: "Regular"
                            font.pixelSize: 18
                            color: Theme.color.neutral9
                            placeholderTextColor: enabled ? Theme.color.neutral7 : Theme.color.neutral4
                            background: Item {}
                            placeholderText: !root.request || root.request.amount.unit === BitcoinAmount.BTC
                                ? "0.00000000" : "0"
                            selectByMouse: true
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
                                cursorShape: Qt.PointingHandCursor
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
                        function onAmountChanged() {
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

                    Separator {
                        Layout.fillWidth: true
                        color: Theme.color.neutral5
                    }

                    LabeledTextInput {
                        id: labelInput
                        objectName: "requestPaymentLabelInput"
                        Layout.fillWidth: true
                        labelText: qsTr("Note to self")
                        placeholderText: qsTr("Enter note...")
                        text: root.request ? root.request.label : ""
                        onTextEdited: {
                            root.requestError = ""
                            if (root.request) {
                                root.request.label = labelInput.text
                            }
                        }
                    }

                    Connections {
                        target: root.request
                        function onLabelChanged() {
                            if (labelInput.text !== root.request.label) {
                                labelInput.text = root.request.label
                            }
                        }
                        function onMessageChanged() {
                            if (messageInput.text !== root.request.message) {
                                messageInput.text = root.request.message
                            }
                        }
                    }

                    Separator {
                        Layout.fillWidth: true
                        color: Theme.color.neutral5
                    }

                    LabeledTextInput {
                        id: messageInput
                        objectName: "requestPaymentMessageInput"
                        Layout.fillWidth: true
                        labelText: qsTr("Message")
                        placeholderText: qsTr("Enter message...")
                        text: root.request ? root.request.message : ""
                        onTextEdited: {
                            root.requestError = ""
                            if (root.request) {
                                root.request.message = messageInput.text
                            }
                        }
                    }

                    Separator {
                        Layout.fillWidth: true
                        color: Theme.color.neutral5
                        visible: settings.addressFormatEnabled
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
                                property string selectedLabel: selectedIndex >= 0 && root.availableAddressTypes.length > 0
                                    ? root.availableAddressTypes[selectedIndex].label : ""

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
                                            return
                                        }
                                        root.request.addressType = delegate.modelData.id
                                        if (root.wallet) {
                                            root.wallet.setDefaultReceiveAddressType(delegate.modelData.id)
                                        }
                                        addressTypePopup.close()
                                    }
                                }
                            }
                        }
                    }

                    Separator {
                        Layout.fillWidth: true
                        color: Theme.color.neutral5
                        visible: root.requestHasAddress
                    }

                    Item {
                        Layout.fillWidth: true
                        Layout.minimumHeight: addressLabel.height + copyLabel.height
                        Layout.topMargin: 10
                        height: addressLabel.height + copyLabel.height
                        visible: root.requestHasAddress

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
                        id: createButton
                        objectName: "requestPaymentCreateButton"
                        Layout.alignment: Qt.AlignHCenter
                        Layout.topMargin: 30
                        Layout.preferredWidth: Math.min(implicitWidth + 80, formColumn.width)
                        text: qsTr("Create payment request")
                        enabled: root.request !== null && root.request.amount.satoshi > 0
                        onClicked: {
                            if (root.createPaymentRequest()) {
                                root.push(detailPage)
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

                    ContinueButton {
                        id: clearRequest
                        Layout.alignment: Qt.AlignHCenter
                        Layout.topMargin: 10
                        Layout.preferredWidth: Math.min(createButton.implicitWidth + 80, formColumn.width)
                        visible: root.requestPersisted
                        borderColor: Theme.color.neutral6
                        borderHoverColor: Theme.color.orangeLight1
                        borderPressedColor: Theme.color.orangeLight2
                        backgroundColor: "transparent"
                        backgroundHoverColor: "transparent"
                        backgroundPressedColor: "transparent"
                        text: qsTr("Clear")
                        onClicked: root.clearPaymentRequest()
                    }

                    ContinueButton {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.topMargin: 15
                        Layout.preferredWidth: Math.min(createButton.implicitWidth + 80, formColumn.width)
                        text: qsTr("View previous requests")
                        textColor: Theme.color.neutral9
                        backgroundColor: "transparent"
                        backgroundHoverColor: Theme.color.neutral2
                        backgroundPressedColor: Theme.color.neutral3
                        onClicked: root.viewPreviousRequests()
                    }
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
                root.push(detailPage)
                return
            }
            commitPassphrasePopup.busy = false
            commitPassphrasePopup.errorText = root.request && root.request.unlockError.length > 0
                ? root.request.unlockError
                : qsTr("The payment request could not be created.")
        }
    }

    Component {
        id: detailPage
        PaymentRequestDetail {
            onDone: root.pop()
        }
    }
}
