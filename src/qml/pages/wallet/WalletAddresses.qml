// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../../controls"

Page {
    id: root
    objectName: "walletAddressesPage"
    property var wallet: null
    property string sessionId
    signal back()
    signal navigateRequested(string route, var parameters)
    Component.onCompleted: wallet = walletManager.walletBySession(sessionId)
    background: Rectangle { color: Theme.color.background }
    header: ToolBar {
        RowLayout {
            anchors.fill: parent
            Button { text: qsTr("Back"); onClicked: root.back() }
            Label { text: qsTr("Addresses"); Layout.fillWidth: true }
            Button {
                text: qsTr("Sign / verify message")
                enabled: !!root.wallet
                onClicked: root.navigateRequested("wallet-message", {sessionId: root.sessionId})
            }
            Button { text: qsTr("Refresh"); enabled: !!root.wallet && !root.wallet.addresses.busy; onClicked: root.wallet.addresses.reload() }
        }
    }
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        Label { text: root.wallet ? root.wallet.addresses.error : qsTr("Wallet unavailable"); visible: text.length > 0; wrapMode: Text.Wrap; Layout.fillWidth: true }
        ListView {
            objectName: "walletAddressList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.wallet ? root.wallet.addresses : null
            delegate: ItemDelegate {
                required property string address
                required property string label
                required property string category
                required property string scriptType
                width: ListView.view.width
                text: (label || address) + "   " + category + "   " + scriptType
                onClicked: root.navigateRequested("wallet-address", {sessionId: root.sessionId, address: address})
            }
        }
    }
}
