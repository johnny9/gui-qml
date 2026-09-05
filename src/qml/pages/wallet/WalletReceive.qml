// Copyright (c) 2024-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.bitcoincore.qt 1.0
import "../../controls"

Page {
    id: root
    objectName: "walletReceivePage"
    property var wallet: null
    property string sessionId
    property string requestId
    property string address
    function openRequest() {
        if (receive && !receive.busy && requestId.length > 0) {
            const id = requestId
            requestId = ""
            if (!receive.edit(id)) requestId = id
        } else if (receive && !receive.busy && address.length > 0) {
            const selectedAddress = address
            address = ""
            if (!receive.useAddress(selectedAddress)) address = selectedAddress
        }
    }
    readonly property var receive: wallet ? wallet.receive : null
    readonly property var draft: receive ? receive.draft : null
    readonly property bool qrAvailable: typeof walletQrAvailable !== "undefined" && walletQrAvailable
    signal back()
    Component.onCompleted: { if (!wallet) wallet = walletManager.walletBySession(sessionId); openRequest() }
    Connections {
        target: root.receive
        function onChanged() { root.openRequest() }
    }
    background: Rectangle { color: Theme.color.background }
    header: ToolBar {
        RowLayout {
            anchors.fill: parent
            Button { objectName: "receiveBackButton"; text: qsTr("Back"); onClicked: root.back() }
            Label { text: qsTr("Receive"); Layout.fillWidth: true }
            Button { text: qsTr("New request"); enabled: !!root.receive && !root.receive.busy; onClicked: root.receive.clear() }
        }
    }
    ScrollView {
        anchors.fill: parent
        anchors.margins: 20
        contentWidth: availableWidth
        ColumnLayout {
            width: parent.width
            spacing: 12
            Label { text: root.wallet ? root.wallet.overview.displayName : qsTr("Wallet unavailable") }
            Label { text: qsTr("Amount (BTC, optional)") }
            TextField {
                objectName: "receiveAmountInput"
                Layout.fillWidth: true
                text: root.draft ? root.draft.amount : ""
                enabled: !!root.receive && !root.receive.busy
                onTextEdited: root.draft.amount = text
            }
            Label { text: qsTr("Label") }
            TextField {
                objectName: "receiveLabelInput"
                Layout.fillWidth: true
                text: root.draft ? root.draft.label : ""
                enabled: !!root.receive && !root.receive.busy
                onTextEdited: root.draft.label = text
            }
            Label { text: qsTr("Message (shared with the payer)") }
            TextField {
                objectName: "receiveMessageInput"
                Layout.fillWidth: true
                text: root.draft ? root.draft.message : ""
                enabled: !!root.receive && !root.receive.busy
                onTextEdited: root.draft.message = text
            }
            Label { text: qsTr("Private note") }
            TextField {
                objectName: "receiveNoteInput"
                Layout.fillWidth: true
                text: root.draft ? root.draft.noteSelf : ""
                enabled: !!root.receive && !root.receive.busy
                onTextEdited: root.draft.noteSelf = text
            }
            Label { text: qsTr("Address type") }
            ComboBox {
                objectName: "receiveAddressType"
                model: root.receive ? root.receive.addressTypes : []
                enabled: !!root.receive && !root.receive.busy && !!root.draft && root.draft.address.length === 0
                currentIndex: root.receive ? root.receive.addressTypes.indexOf(root.draft.addressType || root.receive.defaultAddressType) : -1
                onActivated: { root.draft.addressType = currentText; root.receive.defaultAddressType = currentText }
            }
            TextField {
                id: password
                objectName: "receivePasswordInput"
                visible: !!root.receive && root.receive.needsUnlock
                placeholderText: qsTr("Wallet password")
                echoMode: TextInput.Password
                Layout.fillWidth: true
            }
            Button {
                objectName: "saveReceiveRequestButton"
                text: root.draft && root.draft.id.length ? qsTr("Save changes") : qsTr("Create request")
                enabled: !!root.receive && !root.receive.busy && (!!root.draft && root.draft.address.length > 0 || root.receive.available)
                onClicked: { root.receive.save(password.text); password.clear() }
            }
            Label {
                objectName: "receiveError"
                Layout.fillWidth: true
                text: root.receive ? root.receive.error : ""
                visible: text.length > 0
                wrapMode: Text.Wrap
            }
            Label { objectName: "receiveRequestId"; text: root.draft ? root.draft.id : ""; visible: text.length > 0 }
            TextArea {
                objectName: "receiveAddress"
                Layout.fillWidth: true
                text: root.draft ? root.draft.address : ""
                readOnly: true
                wrapMode: Text.WrapAnywhere
            }
            TextArea {
                objectName: "receiveUri"
                Layout.fillWidth: true
                text: root.draft ? root.draft.uri : ""
                readOnly: true
                wrapMode: Text.WrapAnywhere
            }
            RowLayout {
                Button { text: qsTr("Copy address"); enabled: !!root.draft && root.draft.address.length > 0; onClicked: Clipboard.setText(root.draft.address) }
                Button { text: qsTr("Copy request"); enabled: !!root.draft && root.draft.uri.length > 0; onClicked: Clipboard.setText(root.draft.uri) }
            }
            Image {
                objectName: "receiveQrImage"
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: 240
                Layout.preferredHeight: 240
                smooth: false
                source: root.qrAvailable && root.draft ? root.draft.qrImageSource : ""
                visible: source.toString().length > 0
            }
            Label { text: qsTr("Request history"); font.bold: true }
            Label {
                text: qsTr("This request is too long for a QR code. Copy the request instead.")
                visible: root.qrAvailable && !!root.draft && root.draft.uri.length > 0 && root.draft.qrImageSource.length === 0
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            Repeater {
                model: root.receive ? root.receive.history : null
                delegate: Frame {
                    required property string requestId
                    required property string address
                    required property string label
                    required property string amountDisplay
                    Layout.fillWidth: true
                    ColumnLayout {
                        anchors.fill: parent
                        Label { text: label || address; elide: Text.ElideMiddle; Layout.fillWidth: true }
                        Label { text: amountDisplay.length ? amountDisplay + " BTC" : qsTr("No amount requested") }
                        RowLayout {
                            Button { objectName: "editRequest-" + requestId; text: qsTr("View / edit"); enabled: !root.receive.busy; onClicked: root.receive.edit(requestId) }
                            Button { text: qsTr("Use as template"); enabled: !root.receive.busy; onClicked: root.receive.useAsTemplate(requestId) }
                            Button { text: qsTr("Reuse address"); enabled: !root.receive.busy; onClicked: root.receive.useAsTemplate(requestId, true) }
                            Button { text: qsTr("Delete"); enabled: !root.receive.busy; onClicked: root.receive.remove(requestId) }
                        }
                    }
                }
            }
        }
    }
}
