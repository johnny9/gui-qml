// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../../controls"

Page {
    id: root
    objectName: "walletOverviewPage"
    signal back()
    readonly property var selected: walletManager.selectedWallet
    CreateWallet { id: createWallet; parent: Overlay.overlay }
    RestoreWallet { id: restoreWallet; parent: Overlay.overlay }
    MigrateWallet { id: migrateWallet; parent: Overlay.overlay }
    Connections { target: walletManager; function onMigrationRequired(name) { migrateWallet.open() } }
    background: Rectangle { color: Theme.color.background }
    header: ToolBar {
        RowLayout {
            anchors.fill: parent
            Button { text: qsTr("Back"); onClicked: root.back() }
            Label { text: qsTr("Wallets"); Layout.fillWidth: true }
            Button { objectName: "createWalletButton"; text: qsTr("Create wallet"); enabled: !walletManager.busy; onClicked: createWallet.open() }
            Button { objectName: "restoreWalletButton"; text: qsTr("Restore backup"); enabled: !walletManager.busy; onClicked: restoreWallet.open() }
            Button { objectName: "walletRefreshButton"; text: qsTr("Refresh"); enabled: !walletManager.busy; onClicked: walletManager.refresh() }
        }
    }
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 16
        Label { text: root.selected ? root.selected.overview.displayName : qsTr("No wallet selected"); font.pixelSize: 26 }
        Label { objectName: "selectedWalletName"; text: root.selected ? root.selected.overview.name : "" }
        Label { text: root.selected ? root.selected.overview.balance + " BTC" : qsTr("Select an existing wallet below.") }
        Label { text: root.selected ? root.selected.overview.keyScheme : "" }
        Label {
            text: root.selected ? (root.selected.overview.encrypted ?
                (root.selected.overview.locked ? qsTr("Encrypted and locked") : qsTr("Encrypted and unlocked")) : qsTr("Not encrypted")) : ""
        }
        Button {
            objectName: "closeWalletButton"
            text: qsTr("Close selected wallet")
            visible: !!root.selected
            enabled: !walletManager.busy
            onClicked: walletManager.closeWallet(root.selected.overview.name)
        }
        Label { objectName: "walletLoadError"; text: walletManager.loadError; visible: text.length > 0; wrapMode: Text.Wrap; Layout.fillWidth: true }
        Label { text: walletManager.creation.warnings.concat(walletManager.importModel.warnings).join("\n"); visible: text.length > 0; wrapMode: Text.Wrap; Layout.fillWidth: true }
        ListView {
            objectName: "walletCatalog"
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: walletManager.catalog
            clip: true
            delegate: ItemDelegate {
                required property string walletName
                required property string displayName
                required property bool loaded
                required property string walletFormat
                width: ListView.view.width
                objectName: "selectWallet-" + walletName
                text: displayName + (loaded ? qsTr(" (open)") : "") + (walletFormat === "bdb" ? qsTr(" (migration required)") : "")
                enabled: loaded || !walletManager.busy
                onClicked: walletManager.selectWallet(walletName)
            }
        }
    }
}
