// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import "../../controls"

Page {
    id: root
    objectName: "walletPsbtPage"
    required property string sessionId
    property var wallet: null
    readonly property var psbt: wallet ? wallet.psbt : null
    property string acknowledgedRevision: ""
    Component.onCompleted: if (!wallet) wallet = walletManager.walletBySession(sessionId)
    signal back()
    background: Rectangle { color: Theme.color.background }
    header: ToolBar {
        RowLayout {
            anchors.fill: parent
            Button { text: qsTr("Back"); onClicked: root.back() }
            Label { text: qsTr("Inspect partially signed transaction"); Layout.fillWidth: true }
        }
    }
    Binding { target: root.psbt ? root.psbt.review : null; property: "displayUnit"; value: optionsModel.displayUnit }
    Connections {
        target: root.psbt
        ignoreUnknownSignals: true
        function onChanged() {
            if (confirmation.checked && root.acknowledgedRevision !== String(root.psbt.revision))
                confirmation.checked = false
        }
    }
    FileDialog {
        id: openDialog
        title: qsTr("Import PSBT")
        nameFilters: [qsTr("PSBT files (*.psbt)"), qsTr("All files (*)")]
        onAccepted: if (root.psbt) root.psbt.importFile(selectedFile.toString())
    }
    FileDialog {
        id: saveDialog
        title: qsTr("Export PSBT")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "psbt"
        nameFilters: [qsTr("PSBT files (*.psbt)"), qsTr("All files (*)")]
        onAccepted: if (root.psbt) root.psbt.exportFile(selectedFile.toString())
    }
    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        ColumnLayout {
            width: parent.width
            spacing: 12
            Label { text: root.wallet ? root.wallet.overview.displayName : qsTr("Wallet unavailable") }
            RowLayout {
                Layout.fillWidth: true
                TextField { id: importPath; objectName: "psbtImportPath"; placeholderText: qsTr("PSBT file path"); Layout.fillWidth: true }
                Button { text: qsTr("Browse…"); enabled: !!root.psbt && !root.psbt.busy; onClicked: openDialog.open() }
                Button { objectName: "psbtImport"; text: qsTr("Import"); enabled: !!root.psbt && root.psbt.available && !root.psbt.busy && importPath.text.length > 0; onClicked: root.psbt.importFile(importPath.text) }
            }
            Label { objectName: "psbtStatus"; text: root.psbt ? root.psbt.status : ""; Layout.fillWidth: true; wrapMode: Text.Wrap }
            Label { objectName: "psbtError"; text: root.psbt ? root.psbt.error : ""; visible: text.length > 0; Layout.fillWidth: true; wrapMode: Text.Wrap }
            Repeater {
                model: root.psbt ? root.psbt.outputs : []
                delegate: GroupBox {
                    required property var modelData
                    Layout.fillWidth: true
                    ColumnLayout {
                        anchors.fill: parent
                        Label { text: modelData.address; Layout.fillWidth: true; wrapMode: Text.WrapAnywhere }
                        Label { text: modelData.amount }
                        Label { text: qsTr("Owned by this wallet"); visible: modelData.owned }
                    }
                }
            }
            Label { objectName: "psbtFee"; text: root.psbt && root.psbt.review.hasReview ? qsTr("Fee: %1").arg(root.psbt.review.feeText) : qsTr("Fee unavailable") }
            Button {
                objectName: "psbtExport"
                text: qsTr("Export PSBT…")
                enabled: !!root.psbt && root.psbt.loaded && !root.psbt.busy
                onClicked: saveDialog.open()
            }
            RowLayout {
                visible: !!root.psbt && (root.psbt.canSign || root.psbt.canUnlockForSigning)
                TextField {
                    id: passphrase
                    objectName: "psbtPassphrase"
                    placeholderText: qsTr("Wallet password, if locked")
                    echoMode: TextInput.Password
                    inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText
                    onVisibleChanged: if (!visible) clear()
                    Component.onDestruction: clear()
                }
                Button {
                    objectName: "psbtSign"
                    text: root.psbt && root.psbt.canUnlockForSigning ? qsTr("Unlock and try local signing") : qsTr("Add local signatures")
                    enabled: !!root.psbt && (root.psbt.canSign || root.psbt.canUnlockForSigning)
                    onClicked: { const secret = passphrase.text; passphrase.clear(); root.psbt.sign(secret) }
                }
            }
            CheckBox {
                id: confirmation
                objectName: "psbtReviewConfirmation"
                text: qsTr("I have checked every output and the fee.")
                visible: !!root.psbt && root.psbt.complete && !root.psbt.known
                enabled: !!root.psbt && root.psbt.canSubmit
                onToggled: root.acknowledgedRevision = checked ? String(root.psbt.revision) : ""
            }
            Button {
                objectName: "psbtSubmit"
                text: qsTr("Submit reviewed transaction")
                visible: !!root.psbt && root.psbt.complete && !root.psbt.known
                enabled: !!root.psbt && root.psbt.canSubmit && confirmation.checked && root.acknowledgedRevision === String(root.psbt.revision)
                onClicked: root.psbt.submit()
            }
        }
    }
}
