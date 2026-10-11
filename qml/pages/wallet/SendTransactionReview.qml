// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Dialogs
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
    property bool reviewActive: true
    property bool contextReady: false
    readonly property bool canAct: reviewActive && visible && wallet !== null && transaction !== null
    readonly property WalletQmlModelTransaction transaction: wallet ? wallet.currentTransaction : null
    readonly property bool importedPsbt: wallet !== null && wallet.currentTransactionIsImportedPsbt === true
    readonly property bool canSend: wallet !== null && wallet.currentTransactionCanSend
    readonly property bool canBroadcast: wallet !== null && wallet.currentTransactionCanBroadcast
    readonly property bool showBroadcast: importedPsbt && canBroadcast
    readonly property bool showSend: !showBroadcast && canSend
    readonly property string reviewMessage: wallet ? wallet.currentTransactionReviewMessage : ""
    readonly property bool showWarning: reviewMessage.length > 0
        || (wallet !== null && wallet.keySchemeKind === WalletQmlModel.WatchOnly && !canBroadcast)
    readonly property real sidePadding: width < 640 ? 16 : 40
    readonly property real reviewContentWidth: Math.max(0, Math.min(1100, width - sidePadding * 2))
    property bool sending: false
    property string saveStatus: ""
    property bool saveError: false

    function resetDialogs() {
        if (!contextReady) return
        sweepAlert.close()
        savePsbtDialog.ownerWallet = null
        savePsbtDialog.ownerTransaction = null
        savePsbtDialog.close()
        sendPassphrasePopup.ownerWallet = null
        sendPassphrasePopup.ownerTransaction = null
        sendPassphrasePopup.close()
        sendPassphrasePopup.clearPassphraseField()
        sendPassphrasePopup.busy = false
        sendPassphrasePopup.errorText = ""
        externalSignerActions.reset()
        sending = false
        saveStatus = ""
        saveError = false
    }
    Component.onCompleted: contextReady = true
    onVisibleChanged: if (!visible) resetDialogs()
    onReviewActiveChanged: if (!reviewActive) resetDialogs()

    signal back()
    signal transactionSent(string txid)

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

    onWalletChanged: resetDialogs()
    onTransactionChanged: resetDialogs()

    function requestSend() {
        if (sending || !canAct) return
        if (wallet.currentTransactionSweepsWallet) sweepAlert.open()
        else commitSend()
    }

    function commitSend() {
        if (sending || !canAct) return
        if (wallet.sendTransaction()) {
            sending = true
            transactionSent(transaction ? transaction.txid : "")
        } else if (wallet.transactionNeedsUnlock) {
            sendPassphrasePopup.ownerWallet = wallet
            sendPassphrasePopup.ownerTransaction = transaction
            sendPassphrasePopup.errorText = ""
            sendPassphrasePopup.open()
        }
    }

    function commitBroadcast() {
        if (sending || !canAct) return
        if (wallet.broadcastCurrentTransaction()) {
            sending = true
            transactionSent(transaction ? transaction.txid : "")
        }
    }

    function defaultSavePsbtFileUrl() {
        return "file://" + walletController.homePath() + "/transaction-"
            + Qt.formatDateTime(new Date(), "yyyy-MM-dd-HHmm") + ".psbt"
    }

    function savePsbt(path, owner, preparedTransaction) {
        if (!canAct || owner !== wallet || preparedTransaction !== transaction || !String(path).length) return
        const result = owner.saveCurrentTransactionAsPsbt(String(path))
        saveStatus = result.length ? result : qsTr("Saved.")
        saveError = result.length > 0
    }

    function startSavePsbt() {
        if (!canAct || sending) return
        saveStatus = ""
        saveError = false
        if (savePsbtAutomationPath.text.length) {
            const path = savePsbtAutomationPath.text
            savePsbtAutomationPath.text = ""
            savePsbt(path, wallet, transaction)
        } else {
            savePsbtDialog.ownerWallet = wallet
            savePsbtDialog.ownerTransaction = transaction
            savePsbtDialog.currentFile = defaultSavePsbtFileUrl()
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

    FileDialog {
        id: savePsbtDialog
        objectName: "sendTransactionReviewSaveDialog"
        property WalletQmlModel ownerWallet: null
        property WalletQmlModelTransaction ownerTransaction: null
        title: qsTr("Save transaction as PSBT")
        fileMode: FileDialog.SaveFile
        currentFolder: "file://" + walletController.homePath()
        defaultSuffix: "psbt"
        nameFilters: [qsTr("Partially Signed Bitcoin Transactions (*.psbt)"), qsTr("All files (*)")]
        onRejected: {
            ownerWallet = null
            ownerTransaction = null
        }
        onAccepted: {
            const owner = ownerWallet
            const preparedTransaction = ownerTransaction
            ownerWallet = null
            ownerTransaction = null
            root.savePsbt(selectedFile.toString(), owner, preparedTransaction)
        }
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
                Column {
                    id: recipientColumn
                    Layout.fillWidth: true
                    spacing: 0

                    Repeater {
                        model: root.transaction ? root.transaction.reviewedRecipients : []
                        delegate: FormRow {
                            required property int index
                            required property var modelData
                            objectName: "sendTransactionReviewRecipient" + index
                            width: recipientColumn.width
                            showDivider: index < (root.transaction ? root.transaction.reviewedRecipients.length - 1 : 0)
                            minimumRowHeight: 72
                            title: modelData.label
                            description: modelData.formattedAddress
                            titleTextStyle: Theme.text.subheading
                            descriptionTextStyle: Theme.text.monoCaption
                            descriptionColor: Theme.color.neutral9
                            showLeadingItem: modelData.hasPaymentRequest
                            leadingItem: ActivityRequestBadge {
                                objectName: "sendTransactionReviewRecipient" + index + "RequestBadge"
                                inactive: true
                                surfaceColor: Theme.color.neutral1
                                statusText: qsTr("Payment request recipient")
                            }
                            trailingItem: BitcoinAmountDisplayLabel {
                                objectName: "sendTransactionReviewRecipient" + index + "Amount"
                                text: modelData.amount
                                font: Theme.text.monoDescription.font
                                color: Theme.color.neutral9
                            }
                        }
                    }
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
                displayUnit: optionsModel.displayUnit
                walletName: root.wallet && root.wallet.name ? root.wallet.name : qsTr("Your wallet")
                interactive: false
            }

            Separator { Layout.fillWidth: true }

            ExternalSignerReviewActions {
                id: externalSignerActions
                statusObjectName: "sendTransactionReviewSignerStatus"
                buttonObjectName: "sendTransactionReviewSignerButton"
                visible: root.showSend && root.wallet && root.wallet.hasExternalSigner
                Layout.fillWidth: true
                wallet: root.wallet
                canSend: root.canAct && root.canSend
                onSendRequested: root.requestSend()
            }

            Flow {
                Layout.fillWidth: true
                spacing: 12
                NeutralButton {
                    objectName: "sendTransactionReviewSaveButton"
                    buttonSize: NeutralButton.Large
                    enabled: root.canAct && !root.sending
                    width: parent.width < 540 || !root.showSend && !root.showBroadcast || externalSignerActions.visible
                        ? parent.width : (parent.width - 12) / 2
                    height: 46
                    text: qsTr("Save transaction")
                    onClicked: root.startSavePsbt()
                }
                ContinueButton {
                    objectName: "sendTransactionReviewSendButton"
                    visible: (root.showSend && !externalSignerActions.visible) || root.showBroadcast
                    enabled: root.canAct && !root.sending
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

    AlertPopup {
        id: sweepAlert
        objectName: "sendSweepAlert"
        property WalletQmlModel ownerWallet: null
        property WalletQmlModelTransaction ownerTransaction: null
        onAboutToShow: {
            ownerWallet = root.wallet
            ownerTransaction = root.transaction
        }
        onAboutToHide: {
            ownerWallet = null
            ownerTransaction = null
        }
        parent: Overlay.overlay
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
            closesPopup: false
            onTriggered: {
                const current = root.canAct && sweepAlert.ownerWallet === root.wallet
                    && sweepAlert.ownerTransaction === root.transaction
                sweepAlert.close()
                if (current) root.commitSend()
            }
        }
    }

    WalletPassphrasePopup {
        id: sendPassphrasePopup
        property WalletQmlModel ownerWallet: null
        property WalletQmlModelTransaction ownerTransaction: null
        popupObjectName: "sendPassphrasePopup"
        passphraseFieldObjectName: "sendPassphraseField"
        confirmButtonObjectName: "sendPassphraseConfirmButton"
        parent: Overlay.overlay
        width: Math.min(420, root.width - 40)
        titleText: qsTr("Enter wallet password")
        descriptionText: qsTr("Enter your wallet password to send this transaction.")
        confirmText: qsTr("Unlock and send")
        busyConfirmText: qsTr("Unlocking...")
        onAboutToHide: {
            ownerWallet = null
            ownerTransaction = null
            clearPassphraseField()
            busy = false
            errorText = ""
        }
        onSubmitted: (passphrase) => {
            const owner = ownerWallet
            if (!root.canAct || !owner || owner !== root.wallet || ownerTransaction !== root.transaction) return
            busy = true
            if (owner.sendTransactionWithPassphrase(passphrase)) {
                busy = false
                close()
                root.sending = true
                root.transactionSent(root.transaction ? root.transaction.txid : "")
            } else {
                busy = false
                errorText = owner.transactionError
            }
        }
    }
}
