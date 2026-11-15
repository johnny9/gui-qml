// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0
import "../controls"

Item {
    id: root
    required property WalletOperationModel operation
    required property WalletOperationPresentationModel presenter
    required property bool active
    property bool showResult: true
    property real maximumWidth: 420

    // Each delegate captures an immutable prompt, including its original ID.
    // A delayed Popup close or input callback cannot borrow a successor token.
    Repeater {
        model: root.operation.prompts
        delegate: Item {
            id: session
            required property WalletOperationPrompt modelData
            readonly property WalletOperationPrompt prompt: modelData
            WalletMigrationPopup {
                parent: Overlay.overlay
                width: Math.min(root.maximumWidth, parent ? parent.width - 40 : root.maximumWidth)
                popupObjectName: root.active && root.operation.confirmationPrompt === session.prompt ? "walletMigrationPopup" : ""
                errorTextObjectName: root.active && root.operation.confirmationPrompt === session.prompt ? "walletMigrationErrorText" : ""
                cancelButtonObjectName: root.active && root.operation.confirmationPrompt === session.prompt ? "walletMigrationCancelButton" : ""
                confirmButtonObjectName: root.active && root.operation.confirmationPrompt === session.prompt ? "walletMigrationConfirmButton" : ""
                titleText: session.prompt.title
                descriptionText: session.prompt.message
                errorText: session.prompt.error.message
                errorDetailsModel: root.operation.errorDetails
                busy: session.prompt.busy
                visible: root.active && root.operation.prompt === session.prompt && session.prompt.kind === WalletOperationPrompt.Confirmation
                onConfirmed: session.prompt.answer(session.prompt.promptId, WalletOperationPrompt.Confirm)
                onClosed: root.presenter.reportClosed(root.operation.operationId, session.prompt.promptId)
            }
            WalletPassphrasePopup {
                parent: Overlay.overlay
                width: Math.min(root.maximumWidth, parent ? parent.width - 40 : root.maximumWidth)
                popupObjectName: root.active && root.operation.passphrasePrompt === session.prompt ? "walletMigrationPassphrasePopup" : ""
                passphraseFieldObjectName: root.active && root.operation.passphrasePrompt === session.prompt ? "walletMigrationPassphraseField" : ""
                errorTextObjectName: root.active && root.operation.passphrasePrompt === session.prompt ? "walletMigrationPassphraseErrorText" : ""
                cancelButtonObjectName: root.active && root.operation.passphrasePrompt === session.prompt ? "walletMigrationPassphraseCancelButton" : ""
                confirmButtonObjectName: root.active && root.operation.passphrasePrompt === session.prompt ? "walletMigrationPassphraseConfirmButton" : ""
                titleText: session.prompt.title
                descriptionText: session.prompt.message
                errorText: session.prompt.error.message
                errorDetailsModel: root.operation.errorDetails
                busy: session.prompt.busy
                visible: root.active && root.operation.prompt === session.prompt && session.prompt.kind === WalletOperationPrompt.Passphrase
                onSubmitted: function(secret) { session.prompt.submitSecret(session.prompt.promptId, secret) }
                onClosed: root.presenter.reportClosed(root.operation.operationId, session.prompt.promptId)
            }
        }
    }
    Popup {
        id: resultPopup
        objectName: root.active ? "walletMigrationResultPopup" : ""
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(root.maximumWidth, parent ? parent.width - 40 : root.maximumWidth)
        height: Math.min(620, parent ? parent.height - 40 : 620)
        modal: true
        visible: root.showResult && root.active && root.operation.terminal
        onClosed: { if (root.active && root.operation.terminal) root.presenter.dismiss(root.operation.operationId) }
        background: Rectangle { color: Theme.color.background; radius: 10; border.color: Theme.color.neutral4 }
        contentItem: ColumnLayout {
            CoreText {
                Layout.fillWidth: true
                text: root.operation.phase === WalletOperationModel.Succeeded ? (root.operation.kind === WalletOperationModel.Open ? qsTr("Wallet opened with warnings") : qsTr("Wallet update complete")) : root.operation.errorTitle
                font: Theme.text.subheading.font
                wrapMode: Text.WordWrap
            }
            CoreText { Layout.fillWidth: true; text: root.operation.errorDescription; textFormat: Text.PlainText; wrapMode: Text.WordWrap }
            CoreText { Layout.fillWidth: true; text: root.operation.error.recovery; textFormat: Text.PlainText; wrapMode: Text.WordWrap }
            CoreText { Layout.fillWidth: true; text: root.operation.result.recoveryPaths.join("\n"); textFormat: Text.PlainText; wrapMode: Text.WordWrap }
            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                reuseItems: true
                cacheBuffer: 0
                model: root.operation.errorDetails
                delegate: CoreText { required property string content; width: ListView.view.width; text: content; textFormat: Text.PlainText; wrapMode: Text.WordWrap }
            }
            WalletOperationWarnings { Layout.fillWidth: true; operation: root.operation }
            Repeater {
                model: root.operation.resultWallets
                delegate: TextButton {
                    required property WalletQmlModel modelData
                    Layout.fillWidth: true
                    text: qsTr("Open %1").arg(modelData.displayName)
                    visible: modelData !== root.operation.resultWallet
                    enabled: modelData.available
                    onClicked: { if (ApplicationModels.walletController.selectWallet(modelData)) root.presenter.dismiss(root.operation.operationId) }
                }
            }
            ContinueButton {
                objectName: root.active ? "walletMigrationOpenResultButton" : ""
                Layout.fillWidth: true
                text: root.operation.canOpenResult ? qsTr("Open wallet") : qsTr("Done")
                onClicked: {
                    if (!root.operation.canOpenResult || ApplicationModels.walletController.selectWallet(root.operation.resultWallet)) root.presenter.dismiss(root.operation.operationId)
                }
            }
        }
    }
}
