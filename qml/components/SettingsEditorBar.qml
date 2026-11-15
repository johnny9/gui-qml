// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.bitcoincore.qt 1.0
import "../controls"
ColumnLayout {
    id: root
    required property SettingsEditorModel editor
    spacing: 8
    CoreText { Layout.fillWidth: true; text: root.editor ? root.editor.error : qsTr("The settings editor is unavailable."); visible: text.length > 0; color: Theme.color.red; wrap: true }
    CoreText { objectName: "settingsRestartNotice"; Layout.fillWidth: true; text: root.editor ? root.editor.restartExplanation : ""; visible: root.editor && root.editor.restartNoticeVisible; wrap: true }
    ContinueButton { text: qsTr("Acknowledge restart notice"); visible: root.editor && root.editor.restartNoticeVisible; onClicked: root.editor.acknowledgeRestart() }
    CoreText { Layout.fillWidth: true; text: qsTr("Returning to full storage after pruning may require rebuilding and downloading the blockchain again."); visible: root.editor && root.editor.redownloadWarning; wrap: true }
    ContinueButton { objectName: "settingsConfirmRedownload"; text: qsTr("I understand the storage recovery requirement"); visible: root.editor && root.editor.redownloadWarning; onClicked: root.editor.confirmRedownload(root.editor.redownloadToken, true) }
    CoreText { objectName: "settingsActualSaveResult"; Layout.fillWidth: true; text: root.editor && root.editor.saveResult.present && root.editor.saveResult.persisted ? qsTr("Saved values:\n%1").arg(root.editor.saveResult.actualValues) : ""; visible: text.length > 0; wrap: true }
    RowLayout {
        ContinueButton { objectName: "settingsSaveBatchButton"; text: qsTr("Save settings"); enabled: root.editor && root.editor.canSave; onClicked: root.editor.save() }
        ContinueButton { objectName: "settingsDiscardBatchButton"; text: qsTr("Discard edits"); enabled: root.editor && root.editor.canDiscard && root.editor.dirty; onClicked: root.editor.discard() }
        ContinueButton { objectName: "settingsRefreshBatchButton"; text: qsTr("Refresh"); enabled: root.editor && !root.editor.busy; onClicked: root.editor.refresh() }
        ContinueButton { objectName: "settingsQueryBatchButton"; text: qsTr("Check original save outcome"); visible: root.editor && root.editor.phase === SettingsEditorModel.Uncertain; onClicked: root.editor.reconcile() }
    }
    Item {
        visible: false
        Popup {
        id: question
        objectName: "settingsCloseQuestion"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 380
        padding: 16
        visible: root.editor && root.editor.closeQuestion
        modal: true
        closePolicy: Popup.NoAutoClose
        contentItem: ColumnLayout {
            CoreText { Layout.fillWidth: true; text: qsTr("Save the edited settings before leaving?"); wrap: true }
            ContinueButton { objectName: "settingsCloseSave"; text: qsTr("Save"); onClicked: root.editor.answerClose(root.editor.closeToken, SettingsEditorModel.Save) }
            ContinueButton { objectName: "settingsCloseDiscard"; text: qsTr("Discard"); onClicked: root.editor.answerClose(root.editor.closeToken, SettingsEditorModel.Discard) }
            ContinueButton { objectName: "settingsCloseKeep"; text: qsTr("Keep editing"); onClicked: root.editor.answerClose(root.editor.closeToken, SettingsEditorModel.KeepEditing) }
        }
    }
    }
}
