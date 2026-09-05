// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: root
    objectName: "createWalletDialog"
    title: qsTr("Create a standard wallet")
    modal: true
    width: Math.min(parent.width - 48, 480)
    anchors.centerIn: parent
    closePolicy: walletManager.creation.busy ? Popup.NoAutoClose : Popup.CloseOnEscape
    onOpened: { walletManager.creation.clear(); walletName.forceActiveFocus() }
    onClosed: { password.clear(); confirmation.clear() }
    ColumnLayout {
        width: parent.width
        TextField { id: walletName; objectName: "createWalletNameInput"; placeholderText: qsTr("Wallet name"); Layout.fillWidth: true; enabled: !walletManager.creation.busy }
        Label { text: qsTr("Optional encryption protects your wallet's private keys. Keep the passphrase safe: it cannot be recovered."); wrapMode: Text.Wrap; Layout.fillWidth: true }
        TextField { id: password; objectName: "createWalletPasswordInput"; placeholderText: qsTr("Passphrase (optional)"); echoMode: TextInput.Password; Layout.fillWidth: true; enabled: !walletManager.creation.busy }
        TextField { id: confirmation; objectName: "createWalletPasswordConfirmation"; placeholderText: qsTr("Repeat passphrase"); echoMode: TextInput.Password; Layout.fillWidth: true; enabled: !walletManager.creation.busy }
        Label { objectName: "createWalletError"; text: walletManager.creation.error; wrapMode: Text.Wrap; Layout.fillWidth: true; visible: text.length > 0 }
        RowLayout {
            Button { text: qsTr("Cancel"); enabled: !walletManager.creation.busy; onClicked: root.close() }
            Button {
                objectName: "createWalletSubmitButton"
                text: walletManager.creation.busy ? qsTr("Creating…") : qsTr("Create wallet")
                enabled: !walletManager.busy
                onClicked: {
                    walletManager.creation.create(walletName.text, password.text, confirmation.text)
                    password.clear()
                    confirmation.clear()
                }
            }
        }
    }
    Connections { target: walletManager.creation; function onSucceeded() { root.close() } }
}
