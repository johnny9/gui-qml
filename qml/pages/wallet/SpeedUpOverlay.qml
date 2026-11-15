// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0

import "../../controls"
import "../../components"

Popup {
    id: root
    objectName: "speedUpOverlay"

    property string txid: ""
    property BumpTransactionModel bumpModel: null
    property BumpTransactionModel originalModel: null
    property double ownedAttemptId: 0
    readonly property bool ownsAttempt: ownedAttemptId !== 0 && originalModel !== null
        && bumpModel === originalModel && originalModel.attemptId === ownedAttemptId
    property int bumpState: root.bumpModel ? root.bumpModel.state : BumpTransactionModel.Idle
    property string bumpErrorText: root.bumpModel ? root.bumpModel.errorText : ""
    property string newTxid: ""
    property int walletRecordState: BumpTransactionModel.WalletRecordUnknown
    property int walletSubmissionState: BumpTransactionModel.WalletSubmissionUnknown
    property bool readyToConfirm: root.ownsAttempt && root.bumpModel
        && root.bumpModel.state === BumpTransactionModel.NeedsConfirmation
    readonly property bool readyToPrepareAgain: root.ownsAttempt && root.bumpModel
        && root.bumpModel.state === BumpTransactionModel.NeedsRevalidation
    readonly property bool uncertainOutcome: root.ownsAttempt && root.originalModel.state === BumpTransactionModel.Uncertain

    signal done()
    signal viewNewTransaction(string newTxid)

    readonly property color modalOverlayColor: Qt.rgba(0, 0, 0, 0.4)
    property real verticalOffset: 0
    parent: Overlay.overlay
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) + verticalOffset : verticalOffset
    width: Math.min(640, parent ? parent.width - 40 : 640)
    modal: true
    focus: true
    leftPadding: width < 480 ? 20 : 40
    rightPadding: leftPadding
    topPadding: 30
    bottomPadding: 30
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 300; easing.type: Easing.OutCubic }
        NumberAnimation { property: "verticalOffset"; from: -30; to: 0; duration: 300; easing.type: Easing.OutCubic }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 250; easing.type: Easing.InCubic }
        NumberAnimation { property: "verticalOffset"; from: 0; to: -30; duration: 250; easing.type: Easing.InCubic }
    }
    Overlay.modal: Rectangle {
        color: root.modalOverlayColor
        opacity: root.opacity
    }

    onAboutToShow: {
        root.newTxid = ""
        root.originalModel = root.bumpModel
        root.ownedAttemptId = root.originalModel ? root.originalModel.requestReviewFeeBump(root.txid, 1) : 0
        if (!root.ownedAttemptId) root.close()
    }

    onBumpModelChanged: if (root.visible && root.originalModel && root.bumpModel !== root.originalModel) root.close()

    onAboutToHide: {
        if (root.originalModel && root.originalModel.attemptId === root.ownedAttemptId) root.originalModel.reset()
    }
    onClosed: {
        root.ownedAttemptId = 0
        root.originalModel = null
    }

    background: Rectangle {
        color: Theme.color.neutral1
        radius: 10
        border.color: Theme.color.neutral3
        border.width: 1
        SurfaceGradientBorder {
            anchors.fill: parent
            surfaceColor: parent.color
            cornerRadius: parent.radius
        }
    }

    signal bumpSucceeded(string newTxid, int walletRecordState, int walletSubmissionState)

    function presentNativeState() {
        if (!root.opened || !root.ownsAttempt) return
        if (root.originalModel.state === BumpTransactionModel.Succeeded) {
            root.newTxid = root.originalModel.newTxid
            root.walletRecordState = root.originalModel.walletRecordState
            root.walletSubmissionState = root.originalModel.walletSubmissionState
            root.close()
            root.bumpSucceeded(root.newTxid, root.walletRecordState, root.walletSubmissionState)
        } else if (root.originalModel.needsUnlock && !root.originalModel.busy) {
            speedUpPassphrasePopup.open()
        }
    }
    onOpened: root.presentNativeState()

    Connections {
        target: root.originalModel
        function onStateChanged() {
            root.presentNativeState()
        }
    }

    contentItem: ColumnLayout {
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            Header {
                Layout.fillWidth: true
                header: qsTr("Speed up transaction")
                headerBold: true
                center: false
            }
            CloseButton {
                objectName: "speedUpCloseButton"
                onClicked: root.close()
            }
        }

        CoreText {
            Layout.fillWidth: true
            Layout.topMargin: 15
            Layout.bottomMargin: 25
            horizontalAlignment: Text.AlignLeft
            text: qsTr("Set a higher transaction fee if you want your transaction to be confirmed faster.")
            font.pixelSize: 15
            color: Theme.color.neutral7
            wrap: true
        }

        FormSection {
            objectName: "speedUpFeeSection"
            Layout.fillWidth: true
            backgroundColor: Theme.color.neutral2
            ValueRow {
                objectName: "speedUpOriginalFeeRow"
                amountValue: true
                Layout.fillWidth: true
                title: qsTr("Original fee")
                value: root.bumpModel ? root.bumpModel.oldFee : ""
                valueTextStyle: Theme.text.monoCaption
                valueMaximumWidth: root.availableWidth * 0.6
                dividerColor: Theme.color.neutral3
            }
            ValueRow {
                objectName: "speedUpNewFeeRow"
                amountValue: true
                Layout.fillWidth: true
                title: qsTr("New fee")
                value: root.bumpModel ? root.bumpModel.newFee : ""
                valueTextStyle: Theme.text.monoCaption
                valueMaximumWidth: root.availableWidth * 0.6
                showDivider: false
            }
        }

        CoreText {
            objectName: "speedUpErrorText"
            visible: root.bumpModel && (root.bumpModel.state === BumpTransactionModel.Failed
                || root.bumpModel.state === BumpTransactionModel.NeedsRevalidation
                || root.bumpModel.state === BumpTransactionModel.Uncertain)
            text: root.bumpModel ? root.bumpModel.errorText : ""
            font.pixelSize: 15
            color: Theme.color.red
            Layout.fillWidth: true
            Layout.topMargin: 10
            wrap: true
        }

        GridLayout {
            Layout.fillWidth: true
            Layout.topMargin: 24
            columns: root.width < 480 ? 1 : 2
            columnSpacing: 12
            rowSpacing: 12

            OutlineButton {
                text: qsTr("Cancel")
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                onClicked: root.close()
            }

            ContinueButton {
                objectName: "updateTransactionButton"
                text: root.uncertainOutcome ? qsTr("Check transaction status")
                    : root.readyToPrepareAgain ? qsTr("Prepare again") : qsTr("Update transaction")
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                enabled: root.uncertainOutcome ? root.originalModel.canRecheckOutcome : root.readyToConfirm || root.readyToPrepareAgain
                onClicked: {
                    if (!root.ownsAttempt) return
                    if (root.uncertainOutcome) {
                        root.originalModel.requestRecheckFeeBumpOutcome()
                        return
                    }
                    if (root.readyToPrepareAgain) {
                        const nextAttempt = root.originalModel.requestPrepareFeeBump(root.txid, 1)
                        if (nextAttempt) root.ownedAttemptId = nextAttempt
                        return
                    }
                    root.originalModel.requestConfirmFeeBump()
                }
            }
            OutlineButton {
                objectName: "viewObservedReplacementButton"
                text: qsTr("View transaction")
                visible: root.uncertainOutcome && root.originalModel.walletRecordState === BumpTransactionModel.WalletRecordRecorded
                    && root.originalModel.newTxid.length > 0
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                onClicked: {
                    if (!root.ownsAttempt) return
                    const id = root.originalModel.newTxid
                    root.close()
                    root.viewNewTransaction(id)
                }
            }
        }
    }

    WalletPassphrasePopup {
        id: speedUpPassphrasePopup
        parent: Overlay.overlay
        width: Math.min(420, root.width - 40)
        popupObjectName: "speedUpPassphrasePopup"
        passphraseFieldObjectName: "speedUpPassphraseField"
        errorTextObjectName: "speedUpPassphraseErrorText"
        cancelButtonObjectName: "speedUpPassphraseCancelButton"
        confirmButtonObjectName: "speedUpPassphraseConfirmButton"
        titleText: qsTr("Enter wallet password")
        descriptionText: qsTr("Enter your wallet password to update this transaction.")
        confirmText: qsTr("Unlock and update")
        busyConfirmText: qsTr("Unlocking...")
        property BumpTransactionModel contextModel: null
        property double contextAttempt: 0
        readonly property bool contextValid: contextModel !== null && root.opened && root.ownsAttempt
            && root.bumpModel === contextModel && root.ownedAttemptId === contextAttempt
            && (contextModel.state === BumpTransactionModel.NeedsConfirmation
                || contextModel.state === BumpTransactionModel.Committing)
        onAboutToShow: {
            contextModel = root.bumpModel
            contextAttempt = root.ownedAttemptId
        }
        onClosed: {
            contextModel = null
            contextAttempt = 0
        }
        onContextValidChanged: if (!contextValid) close()
        busy: contextValid && root.bumpModel.busy
        errorText: contextValid ? root.bumpModel.unlockErrorText : ""
        onSubmitted: (passphrase) => {
            if (contextValid && !contextModel.busy) contextModel.requestConfirmFeeBumpWithPassphrase(passphrase)
        }
    }

}
