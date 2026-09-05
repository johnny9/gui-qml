// Copyright (c) 2024-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../../controls"

Page {
    id: root
    objectName: "walletActivityPage"
    property var wallet: null
    property string sessionId
    signal back()
    signal transactionSelected(string key)
    readonly property bool available: !!wallet && wallet.overview.available
    Component.onCompleted: { wallet = walletManager.walletBySession(sessionId) }
    background: Rectangle { color: Theme.color.background }
    header: ToolBar {
        RowLayout {
            anchors.fill: parent
            Button { text: qsTr("Back"); onClicked: root.back() }
            Label { text: qsTr("Activity"); Layout.fillWidth: true }
            Button { text: qsTr("Refresh"); enabled: root.available; onClicked: { root.wallet.history.reload(); root.wallet.receive.reload() } }
        }
    }
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        Label { text: root.wallet ? root.wallet.overview.displayName : qsTr("Wallet unavailable") }
        Label { text: root.wallet ? root.wallet.history.error : ""; visible: text.length > 0 }
        TextField {
            objectName: "activitySearchInput"
            Layout.fillWidth: true
            placeholderText: qsTr("Search address, label, message or transaction ID")
            text: root.wallet ? root.wallet.activityFilter.searchText : ""
            enabled: !!root.wallet
            onTextEdited: root.wallet.activityFilter.searchText = text
        }
        RowLayout {
            Layout.fillWidth: true
            ComboBox {
                objectName: "activityTypeFilter"
                model: [qsTr("All activity"), qsTr("Received"), qsTr("Sent"), qsTr("Self payment"), qsTr("Generated"), qsTr("Pending requests")]
                currentIndex: root.wallet ? root.wallet.activityFilter.typeFilter : 0
                enabled: !!root.wallet
                onActivated: root.wallet.activityFilter.typeFilter = currentIndex
            }
            TextField {
                Layout.fillWidth: true
                placeholderText: qsTr("From YYYY-MM-DD (UTC)")
                text: root.wallet ? root.wallet.activityFilter.fromDate : ""
                enabled: !!root.wallet
                onTextEdited: root.wallet.activityFilter.fromDate = text
            }
            TextField {
                Layout.fillWidth: true
                placeholderText: qsTr("Through YYYY-MM-DD (UTC)")
                text: root.wallet ? root.wallet.activityFilter.throughDate : ""
                enabled: !!root.wallet
                onTextEdited: root.wallet.activityFilter.throughDate = text
            }
            Button {
                text: qsTr("Export CSV")
                enabled: !!root.wallet
                onClicked: root.wallet.activityFilter.chooseExportFile()
            }
        }
        Label { text: root.wallet ? root.wallet.activityFilter.error : ""; visible: text.length > 0; wrapMode: Text.Wrap; Layout.fillWidth: true }
        Binding {
            target: root.wallet ? root.wallet.activity : null
            property: "displayUnit"
            value: optionsModel.displayUnit
        }
        ListView {
            objectName: "walletActivityList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.wallet ? root.wallet.activityFilter : null
            delegate: ItemDelegate {
                required property string rowKey
                required property string label
                required property string address
                required property string amountDisplay
                required property string status
                width: ListView.view.width
                objectName: "activityRow-" + rowKey
                text: (label || address) + "   " + amountDisplay + "   " + status
                onClicked: root.transactionSelected(rowKey)
            }
            Label { anchors.centerIn: parent; visible: parent.count === 0; text: qsTr("No activity yet") }
        }
    }
}
