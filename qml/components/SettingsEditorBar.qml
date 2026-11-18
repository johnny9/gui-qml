// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
import QtQuick
import QtQuick.Layouts
import org.bitcoincore.qt 1.0
import "../controls"
ColumnLayout {
    id: root
    required property SettingsEditorModel editor
    spacing: 8
    CoreText { Layout.fillWidth: true; text: root.editor ? root.editor.error : qsTr("The settings editor is unavailable."); visible: text.length > 0; color: Theme.color.dangerText; wrap: true }
    CoreText { objectName: "settingsRestartNotice"; Layout.fillWidth: true; text: root.editor ? root.editor.restartExplanation : ""; visible: root.editor && root.editor.restartNoticeVisible; wrap: true }
    PrimaryButton { text: qsTr("Acknowledge restart notice"); visible: root.editor && root.editor.restartNoticeVisible; onClicked: root.editor.acknowledgeRestart() }
    CoreText { Layout.fillWidth: true; text: qsTr("Returning to full storage after pruning may require rebuilding and downloading the blockchain again."); visible: root.editor && root.editor.redownloadWarning; wrap: true }
    PrimaryButton { objectName: "settingsConfirmRedownload"; text: qsTr("I understand the storage recovery requirement"); visible: root.editor && root.editor.redownloadWarning; onClicked: root.editor.confirmRedownload() }
    CoreText { objectName: "settingsActualSaveResult"; Layout.fillWidth: true; text: root.editor && root.editor.saveResult.present && root.editor.saveResult.persisted ? qsTr("Saved values:\n%1").arg(root.editor.saveResult.actualValues) : ""; visible: text.length > 0; wrap: true }
    RowLayout {
        PrimaryButton { objectName: "settingsSaveBatchButton"; text: qsTr("Save settings"); enabled: root.editor && root.editor.canSave; onClicked: root.editor.save() }
        PrimaryButton { objectName: "settingsDiscardBatchButton"; text: qsTr("Discard edits"); enabled: root.editor && root.editor.canDiscard && root.editor.dirty; onClicked: root.editor.discard() }
        PrimaryButton { objectName: "settingsRefreshBatchButton"; text: qsTr("Refresh"); enabled: root.editor && !root.editor.busy; onClicked: root.editor.refresh() }
        PrimaryButton { objectName: "settingsQueryBatchButton"; text: qsTr("Check whether it was saved"); visible: root.editor && root.editor.phase === SettingsEditorModel.Uncertain; onClicked: root.editor.reconcile() }
    }
    // Leaving with edits asks here. Save is the one highlighted action;
    // Escape keeps editing.
    AlertPopup {
        id: question
        objectName: "settingsCloseQuestion"
        shown: root.editor !== null && root.editor.closeQuestion
        onCloseRequested: root.answer(TextEditClosePrompt.KeepEditing)
        title: qsTr("Save the edited settings before leaving?")
        escapeAction: keepAction
        AlertAction {
            id: keepAction
            text: qsTr("Keep editing")
            role: AlertAction.Cancel
            buttonObjectName: "settingsCloseKeep"
        }
        AlertAction {
            text: qsTr("Discard")
            role: AlertAction.Neutral
            buttonObjectName: "settingsCloseDiscard"
            closesPopup: false
            onTriggered: root.answer(TextEditClosePrompt.Discard)
        }
        AlertAction {
            text: qsTr("Save")
            buttonObjectName: "settingsCloseSave"
            closesPopup: false
            onTriggered: root.answer(TextEditClosePrompt.Save)
        }
    }
    function answer(choice) { if (root.editor && root.editor.closePrompt) root.editor.closePrompt.answer(choice) }
}
