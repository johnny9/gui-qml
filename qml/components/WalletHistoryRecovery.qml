// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../controls"

Pane {
    id: root
    objectName: "walletHistoryRecoveryBanner"

    property var recovery: null
    readonly property string recoveryState: recovery ? recovery.state : "disabled"
    readonly property bool recoveryIncomplete: recovery ? recovery.incomplete : false
    readonly property bool retryAvailable: recoveryState === "error" || recoveryState === "cancelled"
    readonly property bool unlockAvailable: recovery !== null && recovery.needsUnlock
        && recoveryIncomplete && recoveryState !== "cancelled"
    property bool ready: false

    Component.onCompleted: ready = true
    onRecoveryChanged: if (ready) unlockPopup.close()

    visible: recovery !== null && recovery.visible
    leftPadding: width < 640 ? 16 : 40
    rightPadding: leftPadding
    topPadding: 12
    bottomPadding: 12

    background: Rectangle {
        color: Theme.color.neutral1
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: Theme.color.neutral2
        }
    }

    contentItem: ColumnLayout {
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 16

            CoreText {
                id: statusLabel
                objectName: "recoveryStatusText"
                Layout.fillWidth: true
                text: root.recovery ? root.recovery.statusText : ""
                font: Theme.text.captionStrong.font
                horizontalAlignment: Text.AlignLeft
                Accessible.role: Accessible.StaticText
                Accessible.name: text
            }

            NeutralButton {
                objectName: "recoveryCancelButton"
                visible: root.recoveryIncomplete && (!root.retryAvailable || root.unlockAvailable)
                text: qsTr("Cancel")
                onClicked: root.recovery.cancel()
            }

            NeutralButton {
                objectName: "recoveryUnlockButton"
                visible: root.unlockAvailable
                enabled: root.recovery !== null && !root.recovery.active
                text: qsTr("Unlock")
                onClicked: unlockPopup.open()
            }

            NeutralButton {
                objectName: "recoveryRetryButton"
                visible: root.retryAvailable && !root.unlockAvailable
                enabled: root.recovery !== null && !root.recovery.active
                text: qsTr("Retry")
                onClicked: root.recovery.retry()
            }
        }

        ProgressBar {
            id: progressBar
            objectName: "recoveryProgressBar"
            Layout.fillWidth: true
            visible: !root.retryAvailable
            from: 0
            to: 1
            value: root.recovery ? root.recovery.progress : 0
            indeterminate: root.recovery ? root.recovery.indeterminate : true
            Accessible.name: qsTr("Wallet history recovery")
            Accessible.description: statusLabel.text

            property real indicatorPosition: 0
            NumberAnimation on indicatorPosition {
                from: -0.25
                to: 1
                duration: 1200
                loops: Animation.Infinite
                running: root.visible && progressBar.visible && progressBar.indeterminate
            }

            background: Rectangle {
                implicitHeight: 6
                radius: 3
                color: Theme.color.neutral3
            }

            contentItem: Item {
                implicitHeight: 6
                clip: true
                Rectangle {
                    width: parent.width * (progressBar.indeterminate ? 0.25 : progressBar.visualPosition)
                    height: parent.height
                    x: progressBar.indeterminate ? progressBar.indicatorPosition * parent.width : 0
                    radius: 3
                    color: Theme.color.orange
                }
            }
        }

        CoreText {
            objectName: "recoveryIncompleteText"
            Layout.fillWidth: true
            visible: root.recoveryIncomplete && root.retryAvailable
            text: qsTr("Wallet history and balance are incomplete.")
            color: Theme.color.neutral7
            font: Theme.text.caption.font
            horizontalAlignment: Text.AlignLeft
        }

        CoreText {
            objectName: "recoveryErrorText"
            Layout.fillWidth: true
            visible: text.length > 0
            text: root.recovery ? root.recovery.errorText : ""
            color: Theme.color.red
            font: Theme.text.caption.font
            horizontalAlignment: Text.AlignLeft
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
        }
    }

    WalletPassphrasePopup {
        id: unlockPopup
        parent: Overlay.overlay
        width: parent ? Math.min(420, parent.width - 32) : 420
        popupObjectName: "recoveryUnlockPopup"
        titleText: qsTr("Unlock wallet")
        descriptionText: qsTr("Enter your wallet password to recover its transaction history.")
        confirmText: qsTr("Recover history")
        passphraseFieldObjectName: "recoveryPassphraseInput"
        cancelButtonObjectName: "recoveryUnlockCancelButton"
        confirmButtonObjectName: "recoveryUnlockConfirmButton"
        onSubmitted: (passphrase) => {
            root.recovery.unlockAndRetry(passphrase)
            unlockPopup.close()
        }
    }
}
