// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Dialog {
    id: root
    objectName: "walletSettingsDialog"
    property var wallet: null
    title: qsTr("Wallet settings")
    modal: true
    anchors.centerIn: parent
    width: Math.min(parent.width - 48, 560)
    onOpened: aliasInput.text = wallet.overview.displayName
    onClosed: { oldPassword.clear(); password.clear(); confirmation.clear(); wallet = null }
    Connections {
        target: walletManager
        function onSelectedWalletChanged() { if (root.wallet !== walletManager.selectedWallet) root.close() }
    }
    ColumnLayout {
        width: parent.width
        Label { text: root.wallet ? root.wallet.overview.name : ""; font.bold: true }
        TextField { id: aliasInput; objectName: "walletAliasInput"; placeholderText: qsTr("Display name"); Layout.fillWidth: true }
        Button { objectName: "saveWalletAliasButton"; text: qsTr("Save display name"); enabled: !!root.wallet; onClicked: root.wallet.storage.setAlias(aliasInput.text) }
        Label { text: root.wallet ? root.wallet.storage.location : ""; wrapMode: Text.Wrap; Layout.fillWidth: true }
        RowLayout {
            Button { text: qsTr("Open wallet location"); enabled: !!root.wallet; onClicked: root.wallet.storage.openLocation() }
            Button { objectName: "backupWalletButton"; text: qsTr("Back up wallet…"); enabled: !!root.wallet && !root.wallet.storage.busy; onClicked: backupFile.open() }
        }
        Label { text: root.wallet ? root.wallet.storage.error : ""; wrapMode: Text.Wrap; Layout.fillWidth: true; visible: text.length > 0 }
        Label { text: root.wallet && root.wallet.security.encrypted ? qsTr("Change passphrase") : qsTr("Encrypt wallet"); font.bold: true }
        Label { text: qsTr("Encryption is unavailable for watch-only and external-signer wallets."); visible: !!root.wallet && !root.wallet.security.supported; wrapMode: Text.Wrap; Layout.fillWidth: true }
        TextField { id: oldPassword; placeholderText: qsTr("Current passphrase"); echoMode: TextInput.Password; visible: !!root.wallet && root.wallet.security.encrypted; Layout.fillWidth: true }
        TextField { id: password; placeholderText: qsTr("New passphrase"); echoMode: TextInput.Password; enabled: !!root.wallet && root.wallet.security.supported; Layout.fillWidth: true }
        TextField { id: confirmation; placeholderText: qsTr("Repeat new passphrase"); echoMode: TextInput.Password; enabled: password.enabled; Layout.fillWidth: true }
        Button {
            objectName: "walletSecuritySubmitButton"
            text: root.wallet && root.wallet.security.encrypted ? qsTr("Change passphrase") : qsTr("Encrypt wallet")
            enabled: !!root.wallet && root.wallet.security.supported && !root.wallet.security.busy
            onClicked: {
                if (root.wallet.security.encrypted) root.wallet.security.changePassphrase(oldPassword.text, password.text, confirmation.text)
                else root.wallet.security.encrypt(password.text, confirmation.text)
                oldPassword.clear(); password.clear(); confirmation.clear()
            }
        }
        Label { text: root.wallet ? root.wallet.security.error : ""; wrapMode: Text.Wrap; Layout.fillWidth: true; visible: text.length > 0 }
        Button { text: qsTr("Done"); onClicked: root.close() }
    }
    FileDialog {
        id: backupFile
        fileMode: FileDialog.SaveFile
        title: qsTr("Choose a new wallet backup file")
        defaultSuffix: "bak"
        onAccepted: if (root.wallet) root.wallet.storage.backup(selectedFile.toString())
    }
}
