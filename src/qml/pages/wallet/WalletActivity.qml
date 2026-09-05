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
            model: root.wallet ? root.wallet.activity : null
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
