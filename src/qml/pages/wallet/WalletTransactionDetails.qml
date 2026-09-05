// Copyright (c) 2024-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../../controls"

Page {
    id: root
    objectName: "walletTransactionDetailsPage"
    property var wallet: null
    property string sessionId
    property string rowKey
    property var details: ({})
    signal back()
    function refresh() { details = wallet ? wallet.history.details(rowKey) : ({}) }
    Component.onCompleted: { wallet = walletManager.walletBySession(sessionId); refresh() }
    Connections {
        target: root.wallet ? root.wallet.history : null
        function onRecordsChanged() { root.refresh() }
    }
    background: Rectangle { color: Theme.color.background }
    header: ToolBar {
        RowLayout {
            anchors.fill: parent
            Button { text: qsTr("Back"); onClicked: root.back() }
            Label { text: qsTr("Transaction details"); Layout.fillWidth: true }
        }
    }
    ScrollView {
        anchors.fill: parent
        anchors.margins: 24
        ColumnLayout {
            width: parent.width
            Label { text: root.details.status || qsTr("Transaction unavailable") }
            Label { text: qsTr("Transaction ID") }
            TextArea { objectName: "transactionId"; text: root.details.txid || ""; readOnly: true; wrapMode: Text.WrapAnywhere; Layout.fillWidth: true }
            Label { text: qsTr("Address") }
            TextArea { text: root.details.address || ""; readOnly: true; wrapMode: Text.WrapAnywhere; Layout.fillWidth: true }
            Label { text: qsTr("Label: %1").arg(root.details.label || "") }
            Label { text: qsTr("Confirmations: %1").arg(root.details.confirmations || 0) }
            Label { text: qsTr("Credit (sat): %1").arg(root.details.creditSat || 0) }
            Label { text: qsTr("Debit (sat): %1").arg(root.details.debitSat || 0) }
            Label { text: qsTr("Fee (sat): %1").arg(root.details.feeSat || 0) }
            Label { text: root.details.message || ""; wrapMode: Text.Wrap; Layout.fillWidth: true }
        }
    }
}
