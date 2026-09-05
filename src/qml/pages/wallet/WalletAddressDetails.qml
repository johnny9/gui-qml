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
    objectName: "walletAddressDetailsPage"
    property var wallet: null
    property string sessionId
    property string address
    property var details: ({})
    signal back()
    signal navigateRequested(string route, var parameters)
    function refresh() { details = wallet ? wallet.addresses.details(address) : ({}) }
    Component.onCompleted: { wallet = walletManager.walletBySession(sessionId); refresh() }
    Connections { target: root.wallet ? root.wallet.addresses : null; function onChanged() { root.refresh() } }
    background: Rectangle { color: Theme.color.background }
    header: ToolBar {
        RowLayout {
            anchors.fill: parent
            Button { text: qsTr("Back"); onClicked: root.back() }
            Label { text: qsTr("Address details"); Layout.fillWidth: true }
        }
    }
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        TextArea { text: root.address; readOnly: true; wrapMode: Text.WrapAnywhere; Layout.fillWidth: true }
        Button { text: qsTr("Copy address"); enabled: root.address.length > 0; onClicked: Clipboard.setText(root.address) }
        Label { text: (root.details.category || "") + "   " + (root.details.scriptType || "") }
        Label { text: qsTr("Unspent amount (sat, including immature): %1").arg(root.details.balanceSat || 0) }
        TextField {
            id: labelInput
            objectName: "addressLabelInput"
            text: root.details.label || ""
            enabled: !!root.wallet && root.details.canEditLabel === true && !root.wallet.addresses.busy
            placeholderText: qsTr("Label")
            Layout.fillWidth: true
        }
        Button {
            text: qsTr("Save label")
            enabled: labelInput.enabled
            onClicked: root.wallet.addresses.setLabel(root.address, labelInput.text)
        }
        Button {
            text: qsTr("Create payment request")
            enabled: !!root.wallet && root.details.canCreateRequest === true
            onClicked: root.navigateRequested("wallet-receive", {sessionId: root.sessionId, address: root.address})
        }
        Button {
            text: qsTr("Sign message with this address")
            enabled: !!root.wallet && root.details.canSign === true
            onClicked: root.navigateRequested("wallet-message", {sessionId: root.sessionId, address: root.address})
        }
        Label { text: root.wallet ? root.wallet.addresses.error : qsTr("Wallet unavailable"); visible: text.length > 0; wrapMode: Text.Wrap; Layout.fillWidth: true }
        Item { Layout.fillHeight: true }
    }
}
