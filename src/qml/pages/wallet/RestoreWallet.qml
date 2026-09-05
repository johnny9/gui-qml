// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Dialog {
    id: root
    objectName: "restoreWalletDialog"
    title: qsTr("Restore wallet backup")
    modal: true
    anchors.centerIn: parent
    width: Math.min(parent.width - 48, 520)
    closePolicy: walletManager.importModel.busy ? Popup.NoAutoClose : Popup.CloseOnEscape
    onOpened: walletManager.importModel.clear()
    ColumnLayout {
        width: parent.width
        Label { text: qsTr("Restore a wallet backup under a new name. The backup is not modified."); wrapMode: Text.Wrap; Layout.fillWidth: true }
        TextField { id: path; objectName: "restoreWalletPathInput"; placeholderText: qsTr("Wallet backup path"); Layout.fillWidth: true }
        Button { text: qsTr("Choose backup…"); onClicked: file.open() }
        TextField { id: name; objectName: "restoreWalletNameInput"; placeholderText: qsTr("New wallet name"); Layout.fillWidth: true }
        Label { text: walletManager.importModel.error; wrapMode: Text.Wrap; Layout.fillWidth: true; visible: text.length > 0 }
        RowLayout {
            Button { text: qsTr("Cancel"); enabled: !walletManager.importModel.busy; onClicked: root.close() }
            Button { objectName: "restoreWalletSubmitButton"; text: qsTr("Restore"); enabled: !walletManager.busy; onClicked: walletManager.importModel.restore(path.text, name.text) }
        }
    }
    FileDialog { id: file; title: qsTr("Choose wallet backup"); onAccepted: path.text = selectedFile.toString() }
    Connections { target: walletManager.importModel; function onSucceeded() { root.close() } }
}
