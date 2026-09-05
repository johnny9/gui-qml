// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.bitcoincore.qt 1.0
import "../../controls"

Page {
    id: root
    objectName: "walletMessagePage"
    property var wallet: null
    property string sessionId
    property string address
    property string verificationResult
    readonly property var tool: wallet ? wallet.messages : null
    signal back()
    Component.onCompleted: { wallet = walletManager.walletBySession(sessionId); if (tool) tool.clear() }
    background: Rectangle { color: Theme.color.background }
    header: ToolBar {
        RowLayout {
            anchors.fill: parent
            Button { text: qsTr("Back"); onClicked: root.back() }
            Label { text: qsTr("Sign / verify message"); Layout.fillWidth: true }
        }
    }
    ScrollView {
        anchors.fill: parent
        anchors.margins: 24
        contentWidth: availableWidth
        ColumnLayout {
            width: parent.width
            Label { text: qsTr("Legacy P2PKH addresses only. Signing requires a local private key and does not use external signing devices."); wrapMode: Text.Wrap; Layout.fillWidth: true }
            TextField {
                id: addressInput
                objectName: "messageAddressInput"
                text: root.address
                placeholderText: qsTr("Legacy address")
                Layout.fillWidth: true
                onTextEdited: { root.verificationResult = ""; if (root.tool) root.tool.clear() }
            }
            TextArea {
                id: messageInput
                objectName: "messageTextInput"
                placeholderText: qsTr("Exact message")
                wrapMode: Text.Wrap
                Layout.fillWidth: true
                Layout.preferredHeight: 140
                onTextChanged: { root.verificationResult = ""; if (root.tool) root.tool.clear() }
            }
            TextField {
                id: passwordInput
                objectName: "messagePasswordInput"
                visible: !!root.tool && root.tool.needsUnlock
                placeholderText: qsTr("Wallet password")
                echoMode: TextInput.Password
                Layout.fillWidth: true
                onTextChanged: root.verificationResult = ""
            }
            Button {
                objectName: "signMessageButton"
                text: qsTr("Sign message")
                enabled: !!root.tool && root.tool.available && !root.tool.busy
                onClicked: { root.tool.sign(addressInput.text, messageInput.text, passwordInput.text); passwordInput.clear() }
            }
            Label { text: root.tool ? root.tool.signingError : qsTr("Wallet unavailable"); visible: text.length > 0; wrapMode: Text.Wrap; Layout.fillWidth: true }
            TextArea {
                id: signatureInput
                objectName: "messageSignatureInput"
                text: root.tool ? root.tool.signature : ""
                placeholderText: qsTr("Signature (paste here to verify)")
                wrapMode: Text.WrapAnywhere
                Layout.fillWidth: true
                onTextChanged: root.verificationResult = ""
            }
            RowLayout {
                Button { text: qsTr("Copy signature"); enabled: signatureInput.text.length > 0; onClicked: Clipboard.setText(signatureInput.text) }
                Button {
                    objectName: "verifyMessageButton"
                    text: qsTr("Verify message")
                    enabled: !!root.tool
                    onClicked: { root.tool.verify(addressInput.text, messageInput.text, signatureInput.text); root.verificationResult = root.tool.verificationStatus }
                }
            }
            Label { text: root.verificationResult; visible: text.length > 0; wrapMode: Text.Wrap; Layout.fillWidth: true }
        }
    }
}
