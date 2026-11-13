// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0

import "../../controls"
import "../../components"

Page {
    id: root
    objectName: "sendTransactionReviewPage"
    background: Rectangle { color: Theme.color.background }
    title: qsTr("Review Transaction")

    property WalletQmlModel wallet: null
    readonly property WalletQmlModelTransaction transaction: wallet ? wallet.currentTransaction : null
    readonly property var flowDetails: wallet ? wallet.currentTransactionFlowDetails : null
    readonly property bool detailsContextValid: wallet !== null && wallet.available && transaction !== null
        && flowDetails !== null && flowDetails.context === transaction
        && flowDetails.revision === wallet.currentTransactionFlowRevision
    readonly property bool detailsReady: detailsContextValid && !flowDetails.pending && flowDetails.error.length === 0
    readonly property bool importedPsbt: wallet && wallet.currentTransactionIsImportedPsbt === true
    readonly property bool canSend: wallet && wallet.currentTransactionCanSend
    readonly property bool feePending: wallet && wallet.transactionFeePending
    readonly property bool canBroadcast: wallet && wallet.currentTransactionCanBroadcast
    readonly property bool showBroadcast: importedPsbt && canBroadcast
    readonly property bool showSend: !showBroadcast && canSend
    readonly property string reviewMessage: wallet ? wallet.currentTransactionReviewMessage : ""
    readonly property bool showWarning: reviewMessage.length > 0
        || (wallet && wallet.keySchemeKind === WalletQmlModel.WatchOnly && !canBroadcast)
    readonly property real sidePadding: width < 640 ? 16 : 40
    readonly property real reviewContentWidth: Math.max(0, Math.min(1100, width - sidePadding * 2))
    readonly property bool sendBusy: wallet !== null && (wallet.sendBusy || wallet.psbtActionBusy)
    property WalletQmlModelTransaction sendReview: null
    property double sendTicket: 0
    property bool sending: false
    property double psbtTicket: 0
    property WalletQmlModelTransaction psbtReview: null
    property bool psbtSaving: false
    property string saveStatus: ""
    property bool saveError: false

    onVisibleChanged: {
        if (!visible) {
            sweepAlert.close()
            reviewEntryPopup.close()
            sendPassphrasePopup.close()
            sending = false
            sendTicket = 0
            saveStatus = ""
            saveError = false
        }
    }

    signal back()
    signal transactionSent(string txid, int submissionState)

    function focusInitialControl() {
        reviewCloseButton.forceActiveFocus(Qt.TabFocusReason)
    }

    Component {
        id: readyStatus
        StatusPill {
            objectName: "sendTransactionReviewReadyPill"
            text: root.showBroadcast ? qsTr("Ready to broadcast") : qsTr("Ready to send")
            accentColor: Theme.color.orange
        }
    }

    onWalletChanged: { psbtTicket = 0; psbtReview = null; sendTicket = 0; sendReview = null; sweepAlert.close(); sendPassphrasePopup.close() }
    onTransactionChanged: { sweepAlert.close(); reviewEntryPopup.close() }
    onDetailsContextValidChanged: if (!detailsContextValid) reviewEntryPopup.close()

    function requestSend() {
        if (sending || sendBusy || !wallet || !detailsReady) return
        if (wallet.currentTransactionSweepsWallet) {
            sweepAlert.reviewedTransaction = wallet.currentTransaction
            sweepAlert.open()
        } else {
            commitSend()
        }
    }

    function commitSend() {
        if (sending || sendBusy || !wallet || !detailsReady) return
        sendReview = wallet.currentTransaction
        sendTicket = 0
        sendTicket = wallet.requestSendTransaction()
        handleSendOutcome()
    }

    // The send confirmations authorize the transaction that was shown when
    // they opened. A new review or PSBT import replaces the transaction
    // object, which closes them and makes a pending submit stale; signing
    // updates the same object.
    function isCurrentTransaction(reviewed) {
        return visible && wallet !== null && wallet.available && reviewed !== null && reviewed === wallet.currentTransaction
    }

    function handleSendOutcome() {
            if (!root.sendTicket || root.wallet.sendOperationId !== root.sendTicket || !root.isCurrentTransaction(root.sendReview) || root.sending) return
            if (root.wallet.sendState === WalletQmlModel.SendNeedsUnlock) {
                sendPassphrasePopup.reviewedTransaction = root.sendReview
                sendPassphrasePopup.errorText = root.wallet.transactionError
                if (!sendPassphrasePopup.opened) sendPassphrasePopup.open()
            } else if (root.wallet.sendState === WalletQmlModel.SendCommitted) {
                sendPassphrasePopup.close()
                root.sending = true
                root.transactionSent(root.wallet.sendResultTxid, root.wallet.sendSubmission)
            } else if (sendPassphrasePopup.opened && !root.sendBusy) {
                sendPassphrasePopup.errorText = root.wallet.transactionError
            }
    }

    Connections {
        target: root.wallet
        function onSendOperationChanged() { root.handleSendOutcome() }
        function onPsbtActionChanged() { root.handlePsbtOutcome() }
        function onCurrentTransactionChanged() {
            if (!root.isCurrentTransaction(sweepAlert.reviewedTransaction)) sweepAlert.close()
            if (!root.isCurrentTransaction(sendPassphrasePopup.reviewedTransaction)) sendPassphrasePopup.close()
        }
    }

    function handlePsbtOutcome() {
        if (!psbtTicket || !wallet || wallet.psbtActionId !== psbtTicket || wallet.psbtActionBusy) return
        psbtTicket = 0
        if (!root.isCurrentTransaction(psbtReview)) return
        if (psbtSaving) {
            saveError = wallet.psbtActionState !== WalletQmlModel.PsbtSaved
            saveStatus = saveError ? wallet.psbtActionError : qsTr("Saved.")
        } else if (wallet.psbtActionState === WalletQmlModel.PsbtSubmitted && !sending) {
            sending = true
            transactionSent(wallet.psbtSubmittedTxid, WalletQmlModel.SubmissionAccepted)
        }
    }
    function commitBroadcast() {
        if (sending || sendBusy || !wallet || !detailsReady || wallet.psbtActionBusy) return
        psbtSaving = false
        psbtReview = transaction
        psbtTicket = 0
        psbtTicket = wallet.requestBroadcastCurrentTransaction()
        handlePsbtOutcome()
    }

    function defaultSavePsbtFileUrl() {
        return "file://" + ApplicationModels.walletController.homePath() + "/transaction-"
            + Qt.formatDateTime(new Date(), "yyyy-MM-dd-HHmm") + ".psbt"
    }

    function savePsbt(path) {
        if (!wallet || !String(path).length || wallet.psbtActionBusy) return
        psbtSaving = true
        psbtReview = transaction
        psbtTicket = 0
        psbtTicket = wallet.requestSaveCurrentTransactionAsPsbt(String(path))
        if (!psbtTicket) { saveStatus = qsTr("Another wallet action is still running."); saveError = true }
        else handlePsbtOutcome()
    }

    function startSavePsbt() {
        saveStatus = ""
        saveError = false
        if (savePsbtAutomationPath.text.length) {
            const path = savePsbtAutomationPath.text
            savePsbtAutomationPath.text = ""
            savePsbt(path)
        } else {
            savePsbtDialog.currentFile = defaultSavePsbtFileUrl()
            savePsbtDialog.targetWallet = wallet
            savePsbtDialog.open()
        }
    }

    header: NavigationBar2 {
        implicitWidth: 0
        leftPadding: (root.width - root.reviewContentWidth) / 2
        rightPadding: leftPadding
        topPadding: 16
        bottomPadding: 12
        centerItem: CoreText {
            objectName: "sendTransactionReviewTitle"
            text: root.title
            font: Theme.text.heading.font
            wrap: false
        }
        rightItem: CloseButton {
            id: reviewCloseButton
            objectName: "sendTransactionReviewCloseButton"
            size: 45
            iconSize: 15
            onClicked: root.back()
        }
    }

    AppFileDialog {
        id: savePsbtDialog
        objectName: "sendTransactionReviewSaveDialog"
        title: qsTr("Save transaction as PSBT")
        fileMode: AppFileDialog.SaveFile
        currentFolder: "file://" + ApplicationModels.walletController.homePath()
        defaultSuffix: "psbt"
        nameFilters: [qsTr("Partially Signed Bitcoin Transactions (*.psbt)"), qsTr("All files (*)")]
        property var targetWallet: null
        onAccepted: {
            const target = targetWallet
            targetWallet = null
            if (target && target === root.wallet) root.savePsbt(selectedFile.toString())
        }
        onRejected: targetWallet = null
    }

    Item {
        id: savePsbtAutomationPath
        objectName: "sendTransactionReviewSavePsbtPathField"
        property string text: ""
        width: 0
        height: 0
    }

    ScrollView {
        id: scroll
        anchors.fill: parent
        clip: true
        contentWidth: availableWidth
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

        ColumnLayout {
            id: content
            x: (scroll.availableWidth - width) / 2
            width: root.reviewContentWidth
            spacing: 24

            Item { Layout.preferredHeight: 8 }

            TransactionSummary {
                objectName: "sendTransactionReviewSummary"
                Layout.fillWidth: true
                amount: root.transaction ? root.transaction.total : ""
                statusComponent: root.showSend || root.showBroadcast ? readyStatus : null
                iconComponent: ActivityIcon {
                    objectName: "sendTransactionReviewIcon"
                    activityType: TransactionActivityModel.Send
                    incoming: false
                    pending: true
                    iconSize: 48
                }
            }

            InfoBanner {
                objectName: "sendTransactionReviewWarning"
                visible: root.showWarning
                Layout.fillWidth: true
                backgroundColor: Theme.color.neutral1
                radius: 16
                contentMargin: 20
                messageFontPixelSize: Theme.text.description.font.pixelSize
                messageLineHeight: Theme.text.description.lineHeight
                message: root.reviewMessage.length ? root.reviewMessage
                    : qsTr("This wallet does not have the keys to sign this transaction.")
            }

            FormSection {
                objectName: "sendTransactionReviewRecipients"
                Layout.fillWidth: true
                title: qsTr("Recipients")
                ListView {
                    id: recipientRows
                    objectName: "sendTransactionReviewRecipientRows"
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.min(360, Math.max(72, contentHeight))
                    model: root.detailsReady ? root.flowDetails.recipientRows : null
                    cacheBuffer: 0
                    reuseItems: true
                    clip: true
                    ScrollBar.vertical: ScrollBar {}
                    delegate: ColumnLayout {
                        id: recipientCell
                        objectName: "sendReviewRecipientPreview"
                        required property int index
                        required property var entry
                        width: recipientRows.width
                        spacing: 0
                        FormRow {
                            id: recipientRow
                            objectName: "sendTransactionReviewRecipient" + recipientCell.index
                            Layout.fillWidth: true
                            showDivider: recipientCell.index < recipientRows.count - 1
                            minimumRowHeight: 72
                            title: recipientCell.entry.label || ""
                            description: recipientCell.entry.formattedAddress || ""
                            titleTextStyle: Theme.text.subheading
                            descriptionTextStyle: Theme.text.monoCaption
                            descriptionColor: Theme.color.neutral9
                            showLeadingItem: !!recipientCell.entry.hasPaymentRequest
                            leadingItem: ActivityRequestBadge {
                                objectName: "sendTransactionReviewRecipient" + recipientCell.index + "RequestBadge"
                                inactive: true
                                surfaceColor: Theme.color.neutral1
                                statusText: qsTr("Payment request recipient")
                            }
                            trailingItem: BitcoinAmountDisplayLabel {
                                objectName: "sendTransactionReviewRecipient" + recipientCell.index + "Amount"
                                text: recipientCell.entry.amount || ""
                                font: Theme.text.monoDescription.font
                                color: Theme.color.neutral9
                            }
                        }
                        TextButton {
                            objectName: "sendTransactionReviewRecipientFields" + recipientCell.index
                            text: recipientCell.entry.textTruncated ? qsTr("View complete recipient") : qsTr("View recipient fields")
                            onClicked: root.openReviewEntry("recipients", recipientCell.index)
                        }
                    }
                }
                BusyIndicator { visible: root.detailsContextValid && root.flowDetails.pending; running: visible }
                CoreText {
                    Layout.fillWidth: true
                    visible: root.detailsContextValid && root.flowDetails.error.length > 0
                    text: root.flowDetails ? root.flowDetails.error : ""
                    color: Theme.color.red
                    wrap: true
                }
            }

            FormSection {
                objectName: "sendTransactionReviewNetworkFee"
                visible: root.wallet && !root.importedPsbt
                Layout.fillWidth: true
                title: qsTr("Network fee")
                ValueRow {
                    objectName: "sendTransactionReviewTargetBlocks"
                    Layout.fillWidth: true
                    title: qsTr("Targeting blocks")
                    value: root.transaction ? (root.transaction.reviewTargetBlocks >= 50
                        ? qsTr("50+ blocks") : qsTr("%1 blocks").arg(root.transaction.reviewTargetBlocks)) : ""
                }
                ValueRow {
                    objectName: "sendTransactionReviewFeeRate"
                    Layout.fillWidth: true
                    title: qsTr("Fee rate")
                    value: root.transaction && root.transaction.reviewFeeRate.length > 0
                        ? qsTr("%1 sat/vB").arg(root.transaction.reviewFeeRate) : "—"
                    showDivider: false
                }
            }

            FormSection {
                objectName: "sendTransactionReviewTotals"
                Layout.fillWidth: true
                ValueRow {
                    objectName: "sendTransactionReviewFee"
                    amountValue: true
                    Layout.fillWidth: true
                    title: qsTr("Total fees")
                    value: root.transaction ? root.transaction.fee : ""
                    dividerColor: Theme.color.neutral2
                }
                ValueRow {
                    objectName: "sendTransactionReviewTotal"
                    amountValue: true
                    Layout.fillWidth: true
                    title: qsTr("Total amount")
                    value: root.transaction ? root.transaction.total : ""
                    showDivider: false
                }
            }

            TransactionFlow {
                objectName: "sendTransactionReviewFlow"
                Layout.fillWidth: true
                flow: root.wallet && root.wallet.currentTransactionFlow ? root.wallet.currentTransactionFlow : ({})
                transactionId: root.transaction ? root.transaction.txid : ""
                displayUnit: ApplicationModels.optionsModel.displayUnit
                walletName: root.wallet && root.wallet.name ? root.wallet.name : qsTr("Your wallet")
                interactive: false
            }

            TransactionEntriesList {
                objectName: "sendReviewAllInputs"
                Layout.fillWidth: true
                title: qsTr("All inputs")
                entryModel: root.detailsReady ? root.flowDetails.inputRows : null
                input: true
                walletName: root.wallet && root.wallet.name ? root.wallet.name : qsTr("Your wallet")
                onEntryRequested: (row) => root.openReviewEntry("inputs", row)
            }
            TransactionEntriesList {
                objectName: "sendReviewAllOutputs"
                Layout.fillWidth: true
                title: qsTr("All outputs")
                entryModel: root.detailsReady ? root.flowDetails.outputRows : null
                walletName: root.wallet && root.wallet.name ? root.wallet.name : qsTr("Your wallet")
                onEntryRequested: (row) => root.openReviewEntry("outputs", row)
            }

            Separator { Layout.fillWidth: true }

            ExternalSignerReviewActions {
                id: externalSignerActions
                enabled: root.detailsReady
                statusObjectName: "sendTransactionReviewSignerStatus"
                buttonObjectName: "sendTransactionReviewSignerButton"
                visible: root.showSend && root.wallet && root.wallet.hasExternalSigner
                Layout.fillWidth: true
                wallet: root.wallet
                canSend: root.canSend
                onSendRequested: root.requestSend()
            }

            Flow {
                Layout.fillWidth: true
                spacing: 12
                NeutralButton {
                    objectName: "sendTransactionReviewSaveButton"
                    buttonSize: NeutralButton.Large
                    enabled: !!root.transaction && !root.sending && !root.sendBusy
                    width: parent.width < 540 || !root.showSend && !root.showBroadcast || externalSignerActions.visible
                        ? parent.width : (parent.width - 12) / 2
                    height: 46
                    text: qsTr("Save transaction")
                    onClicked: root.startSavePsbt()
                }
                ContinueButton {
                    objectName: "sendTransactionReviewSendButton"
                    visible: (root.showSend && !externalSignerActions.visible) || root.showBroadcast
                    enabled: root.detailsReady && !root.sending && !root.sendBusy
                    width: parent.width < 540 ? parent.width : (parent.width - 12) / 2
                    height: 46
                    text: root.showBroadcast ? qsTr("Broadcast transaction") : qsTr("Send transaction")
                    onClicked: root.showBroadcast ? root.commitBroadcast() : root.requestSend()
                }
            }

            ToastBanner {
                visible: root.saveStatus.length > 0
                Layout.fillWidth: true
                tintColor: root.saveError ? Theme.color.red : Theme.color.green
                iconSource: root.saveError ? "image://images/info-filled" : "image://images/check"
                text: root.saveStatus
                dismissAfter: 3
                onDismissed: root.saveStatus = ""
            }

            CoreText {
                visible: text.length > 0
                Layout.fillWidth: true
                text: root.wallet ? root.wallet.transactionError : ""
                color: Theme.color.red
                font: Theme.text.description.font
                wrap: true
            }

            Item { Layout.preferredHeight: 24 }
        }
    }

    function openReviewEntry(side, row) {
        if (!root.detailsReady) return
        if (root.flowDetails.requestEntryDetails(side, row, root.transaction, root.wallet.currentTransactionFlowRevision)) reviewEntryPopup.open()
    }
    Popup {
        id: reviewEntryPopup
        objectName: "sendReviewEntryPopup"
        parent: Overlay.overlay
        anchors.centerIn: Overlay.overlay
        width: Math.min(720, Overlay.overlay ? Overlay.overlay.width - 32 : 720)
        height: Math.min(600, Overlay.overlay ? Overlay.overlay.height - 32 : 600)
        modal: true
        padding: 20
        contentItem: ColumnLayout {
            spacing: 12
            CoreText { text: qsTr("Complete entry"); font: Theme.text.subheading.font }
            BusyIndicator { visible: !!root.flowDetails && root.flowDetails.entryDetailsBusy; running: visible }
            CoreText {
                Layout.fillWidth: true
                visible: !!root.flowDetails && root.flowDetails.entryDetailsError.length > 0
                text: root.flowDetails ? root.flowDetails.entryDetailsError : ""
                color: Theme.color.red
                wrap: true
            }
            ListView {
                id: documentRows
                objectName: "sendReviewEntryDocument"
                Layout.fillWidth: true
                Layout.fillHeight: true
                model: reviewEntryPopup.opened && root.detailsContextValid ? root.flowDetails.entryDocument : null
                cacheBuffer: 0
                reuseItems: true
                clip: true
                spacing: 12
                ScrollBar.vertical: ScrollBar {}
                delegate: ColumnLayout {
                    id: documentRow
                    required property var entry
                    width: documentRows.width
                    CoreText { Layout.fillWidth: true; text: documentRow.entry.field; font: Theme.text.captionStrong.font; wrap: true }
                    TextArea {
                        Layout.fillWidth: true
                        text: documentRow.entry.content
                        readOnly: true
                        selectByMouse: true
                        textFormat: TextEdit.PlainText
                        wrapMode: TextEdit.WrapAnywhere
                        font: Theme.text.monoCaption.font
                        color: Theme.color.neutral9
                        padding: 0
                        background: null
                    }
                }
            }
            TextButton { text: qsTr("Close"); onClicked: reviewEntryPopup.close() }
        }
    }

    AlertPopup {
        id: sweepAlert
        objectName: "sendSweepAlert"
        parent: Overlay.overlay
        property WalletQmlModelTransaction reviewedTransaction: null
        //: Final confirmation before sending a transaction that uses all available wallet funds.
        title: qsTr("Send all available funds?")
        //: Warns that a full-wallet send cannot be undone after confirmation on the network.
        //: %1 is the prepared transaction's recipient amount with its unit, excluding the fee.
        message: qsTr("This will send all available funds (%1) from this wallet. Once confirmed, the transaction cannot be undone.").arg(root.transaction ? root.transaction.amountAmount.displayWithUnit : "—")
        AlertAction {
            //: Dismiss the full-wallet send confirmation and keep the prepared transaction open for review.
            text: qsTr("Back to review")
            role: AlertAction.Cancel
            buttonObjectName: "sendSweepCancelButton"
        }
        AlertAction {
            //: Confirm and send the prepared transaction using all available wallet funds.
            text: qsTr("Send all funds")
            buttonObjectName: "sendSweepConfirmButton"
            onTriggered: {
                if (root.isCurrentTransaction(sweepAlert.reviewedTransaction)) root.commitSend()
            }
        }
    }

    WalletPassphrasePopup {
        id: sendPassphrasePopup
        popupObjectName: "sendTransactionPassphrasePopup"
        parent: Overlay.overlay
        width: Math.min(420, root.width - 40)
        titleText: qsTr("Enter wallet password")
        descriptionText: qsTr("Enter your wallet password to send this transaction.")
        confirmText: qsTr("Unlock and send")
        busyConfirmText: qsTr("Unlocking...")
        property WalletQmlModel actionWallet: null
        property WalletQmlModelTransaction reviewedTransaction: null
        readonly property bool contextValid: root.visible && actionWallet !== null
            && actionWallet === root.wallet && actionWallet.available
            && reviewedTransaction !== null && reviewedTransaction === actionWallet.currentTransaction
        onAboutToShow: actionWallet = root.wallet
        onContextValidChanged: if (!contextValid) close()
        busy: actionWallet !== null && actionWallet.sendBusy
        onAboutToHide: clearPassphraseField()
        onSubmitted: (passphrase) => {
            if (!opened || !contextValid) {
                close()
                return
            }
            root.sendReview = reviewedTransaction
            root.sendTicket = 0
            root.sendTicket = actionWallet.requestSendTransactionWithPassphrase(passphrase)
            root.handleSendOutcome()
        }
    }
}
