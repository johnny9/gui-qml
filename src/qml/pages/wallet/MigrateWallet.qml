// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: root
    objectName: "migrateWalletDialog"
    title: qsTr("Migrate legacy wallet")
    modal: true
    anchors.centerIn: parent
    width: Math.min(parent.width - 48, 520)
    closePolicy: walletManager.migration.busy ? Popup.NoAutoClose : Popup.CloseOnEscape
    onClosed: password.clear()
    ColumnLayout {
        width: parent.width
        Label { text: walletManager.migration.walletName; font.bold: true }
        Label { text: qsTr("Convert this legacy wallet to descriptors. Core keeps a legacy backup and may create separate watch-only or solvable wallets."); wrapMode: Text.Wrap; Layout.fillWidth: true }
        TextField { id: password; objectName: "migrateWalletPasswordInput"; placeholderText: qsTr("Wallet passphrase"); echoMode: TextInput.Password; visible: walletManager.migration.needsPassphrase; Layout.fillWidth: true }
        Label { text: walletManager.migration.error; wrapMode: Text.Wrap; Layout.fillWidth: true; visible: text.length > 0 }
        Label { text: walletManager.migration.backupPath; wrapMode: Text.Wrap; Layout.fillWidth: true; visible: text.length > 0 }
        Label { text: walletManager.migration.companions.join("\n"); wrapMode: Text.Wrap; Layout.fillWidth: true; visible: text.length > 0 }
        RowLayout {
            Button { text: qsTr("Close"); enabled: !walletManager.migration.busy; onClicked: root.close() }
            Button { objectName: "migrateWalletSubmitButton"; text: qsTr("Migrate"); enabled: !walletManager.busy && walletManager.migration.backupPath.length === 0; onClicked: { walletManager.migration.migrate(password.text); password.clear() } }
        }
    }
}
