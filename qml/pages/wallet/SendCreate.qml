// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0

import "../../controls"
import "../../components"

PageStack {
    id: root
    objectName: "sendPage"
    vertical: true

    property WalletQmlModel wallet: null
    property SendRecipient recipient: wallet ? wallet.recipients.current : null
    property string prepareTransactionErrorText: ""
    readonly property bool manualCoinSelection: root.wallet !== null
        && root.wallet.coinsListModel !== null && root.wallet.coinsListModel.manualInputSelection
    readonly property bool externalSignerWallet: wallet !== null && wallet.hasExternalSigner
    readonly property string recipientValidationError: wallet ? wallet.recipients.budgetError || wallet.recipients.validationError : ""
    readonly property bool selectedInputsActive: wallet !== null
        && wallet.coinsListModel !== null
        && (wallet.coinsListModel.manualInputSelection || wallet.coinsListModel.selectedCoinsCount > 0)
    readonly property bool hasLockedCoins: wallet !== null
        && wallet.coinsListModel !== null
        && wallet.coinsListModel.lockedSatoshi > 0
    readonly property string baseAvailableBalanceErrorText: qsTr("Amount plus fee exceeds available balance")
    readonly property string walletBalanceErrorText: qsTr("The wallet does not have enough balance for this transaction.")
    readonly property string availableBalanceErrorText: hasLockedCoins
        ? qsTr("Amount plus fee exceeds available balance. Some of your coins are locked.")
        : baseAvailableBalanceErrorText
    readonly property string selectedInputsBalanceErrorText: qsTr("Selected inputs do not cover the amount plus fee")
    readonly property string feeBalanceErrorText: wallet && wallet.sendAmountExhaustsBalance
        ? (selectedInputsActive ? selectedInputsBalanceErrorText : availableBalanceErrorText)
        : ""
    readonly property string formErrorText: wallet && wallet.fees && !wallet.fees.estimatePending && wallet.fees.errorText.length > 0
        ? wallet.fees.errorText : recipientValidationError.length > 0
        ? recipientValidationError
        : (wallet && wallet.coinsListModel && wallet.coinsListModel.selectionReconciliationRequired
           ? qsTr("Some selected coins are no longer available. Choose Automatic inputs or update the selection.")
           : (feeBalanceErrorText.length > 0 ? feeBalanceErrorText : prepareTransactionErrorText))

    signal viewTransactionInActivity(string txid)

    property double psbtImportTicket: 0
    property WalletQmlModel psbtImportWallet: null
    property int psbtImportHiddenCount: -1
    property string psbtImportDraftRevision: ""
    property string psbtImportSelectionToken: ""
    function requestPsbtImport(path) {
        if (!root.wallet || root.wallet.psbtActionBusy) return
        psbtImportWallet = root.wallet
        psbtImportHiddenCount = root.hiddenCount
        psbtImportDraftRevision = root.draftRevision
        psbtImportSelectionToken = root.wallet.coinsListModel.selectionToken
        psbtImportTicket = 0
        psbtImportTicket = root.wallet.requestImportPsbtFromFile(path)
        handlePsbtOutcome()
    }
    function handlePsbtOutcome() {
        if (!psbtImportTicket || !root.wallet || psbtImportWallet !== root.wallet || root.wallet.psbtActionId !== psbtImportTicket || root.wallet.psbtActionBusy) return
        psbtImportTicket = 0
        if (!root.visible || psbtImportHiddenCount !== root.hiddenCount || psbtImportDraftRevision !== root.draftRevision
            || root.wallet.coinsListModel.selectionPending || psbtImportSelectionToken !== root.wallet.coinsListModel.selectionToken
            || root.wallet.psbtActionState === WalletQmlModel.PsbtStale) return
        sendPage.handlePsbtImportResult(root.wallet.psbtImportResult)
    }

    function confirmTransactionReview() {
        if (!root.wallet || !root.wallet.available || sendPage.uriActionPending) return
        if (root.wallet.sendDraftSweepsWallet) {
            reviewSweepAlert.draftRevision = root.draftRevision
            reviewSweepAlert.open()
        }
        else root.prepareTransactionForReview()
    }

    property bool preparingReview: false
    property double preparationTicket: 0
    property WalletQmlModel preparationWallet: null
    property string preparationDraftRevision: ""
    property int preparationHiddenCount: -1

    function prepareTransactionForReview() {
        if (!root.wallet || !root.wallet.available || root.wallet.preparationBusy || sendPage.uriActionPending) return
        root.clearPrepareTransactionError()
        preparationWallet = root.wallet
        preparationDraftRevision = root.draftRevision
        preparationHiddenCount = root.hiddenCount
        preparingReview = true
        preparationTicket = 0
        preparationTicket = root.wallet.requestPrepareTransaction()
        if (!preparationTicket) {
            preparingReview = false
            root.prepareTransactionErrorText = qsTr("Another wallet action is still running.")
        } else root.handlePreparationOutcome()
    }

    function handlePreparationOutcome() {
        if (!preparingReview || !preparationTicket || preparationWallet !== root.wallet || !root.wallet) return
        if (root.wallet.preparationId !== preparationTicket) { preparingReview = false; return }
        if (root.wallet.preparationBusy) return
        preparingReview = false
        if (!root.visible || preparationDraftRevision !== root.draftRevision || preparationHiddenCount !== root.hiddenCount) return
        if (root.wallet.preparationState === WalletQmlModel.PreparationReady) {
            reviewPassphrasePopup.close()
            root.openTransactionReview()
        } else if (root.wallet.preparationState === WalletQmlModel.PreparationNeedsUnlock) {
            reviewPassphrasePopup.open()
        } else if (root.wallet.preparationState !== WalletQmlModel.PreparationStale) {
            root.prepareTransactionErrorText = root.wallet.transactionError.length > 0
                ? root.displayTransactionError(root.wallet.transactionError)
                : root.selectedInputsActive ? root.selectedInputsBalanceErrorText : root.availableBalanceErrorText
        }
    }

    function openTransactionReview() {
        transactionReviewPopup.reviewWallet = root.wallet
        transactionReviewPopup.importedReview = root.wallet.currentTransactionIsImportedPsbt === true
        transactionReviewPopup.open()
    }

    function returnToSendForm() {
        if (root.depth > 1) {
            root.pop(null, StackView.Immediate)
        }
    }

    function openPaymentRequestImport() {
        root.returnToSendForm()
        root.openWhileShown(sendPage.openPaymentRequestImport)
    }

    function acceptDesktopImport(mailbox) {
        if (!mailbox || !root.visible || !root.wallet || !root.wallet.available || !root.recipient
                || root.wallet.recipients.importPending) return false
        const originalWallet = root.wallet
        const originalRecipient = root.recipient
        sendPage.cancelUriAction()
        if (root.wallet !== originalWallet || root.recipient !== originalRecipient || !root.visible) return false
        const operation = mailbox.takeInspection(sendPage)
        if (!operation) return false
        sendPage.m_importOperation = operation
        sendPage.m_importWallet = originalWallet
        sendPage.m_importSource = qsTr("desktop")
        if (root.wallet !== originalWallet || root.recipient !== originalRecipient || !root.visible || !originalWallet.available) {
            operation.cancel()
            return false
        }
        operation.chooseTarget(originalWallet, originalRecipient)
        operation.use()
        return true
    }

    function openPsbtFileImport() {
        root.returnToSendForm()
        root.openWhileShown(sendPage.openPsbtFileImport)
    }

    // The draft owner stamps captured user intent. Validation echoes and
    // automatic Maximum observations do not create another user revision.
    readonly property string draftRevision: root.wallet ? root.wallet.recipients.revision : ""
    onDraftRevisionChanged: if (sendPage && reviewSweepAlert) root.draftChanged()

    function draftChanged() {
        if (!sendPage.m_applyingUri && !(root.wallet && root.wallet.recipients.importPending) && !(sendPage.m_uriPasteOwner && (sendPage.m_uriPasteOwner.literalActive || sendPage.m_uriPasteOwner.mutating)))
            sendPage.cancelUriIntents()
        root.preparingReview = false
        reviewSweepAlert.close()
        sendPage.checkClipboard()
    }

    // Counts how often this page was hidden. A deferred open is dropped if
    // the page was hidden in between, even if it is shown again by then.
    property int hiddenCount: 0

    function openWhileShown(open) {
        const hiddenCount = root.hiddenCount
        Qt.callLater(function() {
            if (root.visible && hiddenCount === root.hiddenCount) open()
        })
    }

    function clearPrepareTransactionError() {
        if (prepareTransactionErrorText.length > 0) {
            prepareTransactionErrorText = ""
        }
    }

    function resetSendForm() {
        if (!root.wallet) return
        root.wallet.recipients.clear()
        root.wallet.coinsListModel.setManualInputSelection(false)
        root.wallet.coinsListModel.update()
        root.clearPrepareTransactionError()
        sendPage.expandedRecipient = 0
        sendPage.m_legacyPaymentRequestStatus = ""
        sendPage.m_legacyPaymentRequestIsError = false
        sendPage.m_legacyPaymentRequestMessage = ""
        sendPage.clearPendingPaymentUriPaste()
    }

    function displayTransactionError(error) {
        if (hasLockedCoins && !selectedInputsActive
                && (error === baseAvailableBalanceErrorText || error === walletBalanceErrorText)) {
            return availableBalanceErrorText
        }
        return error
    }

    function scheduleFeeEstimates() {
        if (root.wallet) {
            root.wallet.scheduleFeeEstimates()
        }
    }

    function updateExpandedRecipient(index) {
        sendPage.expandedRecipient = index
    }

    // Re-check the clipboard whenever the Send tab becomes visible so the
    // banner appears even if the URI was copied before navigating here.
    onVisibleChanged: {
        if (visible) {
            sendPage.checkClipboard()
        } else {
            root.hiddenCount++
            sendPage.cancelUriIntents()
            reviewSweepAlert.close()
        }
    }
    onWalletChanged: {
        sendPage.cancelUriIntents()
        reviewSweepAlert.close()
        if (transactionReviewPopup.opened) {
            transactionReviewPopup.close()
        }
        root.pop()
        // Clear URI import state so stale results from the previous wallet
        // are not shown when this page is pointed at another wallet.
        sendPage.m_legacyPaymentRequestStatus = ""
        sendPage.m_legacyPaymentRequestIsError = false
        sendPage.m_legacyPaymentRequestMessage = ""
        sendPage.m_applyingUri = false
        sendPage.clearPendingPaymentUriPaste()
        paymentUriOverwritePopup.close()
    }

    Connections {
        target: root.wallet ? root.wallet.recipients : null
        function onListCleared() {
            root.draftChanged()
            root.clearPrepareTransactionError()
            sendPage.expandedRecipient = 0
            if (root.wallet) {
                root.wallet.scheduleFeeEstimates()
            }
        }
        function onValidationChanged() { if (!root.wallet.recipients.importPending) sendPage.checkClipboard() }
        function onCountChanged() {
            root.draftChanged()
            root.clearPrepareTransactionError()
            root.scheduleFeeEstimates()
        }
        function onTotalAmountChanged() {
            // Only the bounded warning presentation follows this observation.
            // A validation echo does not retire the original import intent.
            if (root.wallet && !root.wallet.maximumRecipient) reviewSweepAlert.close()
        }
        function onCurrentRecipientChanged() {
            if (sendPage.m_importOperation) sendPage.cancelUriAction()
            root.draftChanged()
            root.clearPrepareTransactionError()
            root.scheduleFeeEstimates()
            sendPage.m_legacyPaymentRequestStatus = ""
            sendPage.m_legacyPaymentRequestIsError = false
            sendPage.m_legacyPaymentRequestMessage = ""
            sendPage.expandedRecipient = root.wallet.recipients.currentIndex - 1
            sendPage.checkClipboard()
            sendPage.clearPendingPaymentUriPaste()
            paymentUriOverwritePopup.close()
        }
    }

    Connections {
        target: root.wallet ? root.wallet.coinsListModel : null
        function onSelectionSnapshotChanged() {
            root.draftChanged()
            root.clearPrepareTransactionError()
        }
    }

    Connections {
        target: root.wallet
        function onCurrentTransactionChanged() { reviewSweepAlert.close(); sendPage.presentImportedReview() }
        function onPreparationChanged() { root.handlePreparationOutcome() }
        function onAvailableChanged() { if (!root.wallet.available) sendPage.cancelUriIntents() }
        function onPsbtActionChanged() { root.handlePsbtOutcome() }
        function onMaximumRecipientChanged() { root.draftChanged() }
        function onCustomFeeEnabledChanged() {
            root.clearPrepareTransactionError()
        }
        function onCustomFeeRateChanged() {
            root.clearPrepareTransactionError()
        }
    }

    Binding {
        target: root.recipient ? root.recipient.amount : null
        property: "unit"
        value: ApplicationModels.optionsModel.displayUnit
        when: root.recipient !== null
    }

    BitcoinAmount {
        id: recipientsTotalAmount
        satoshi: root.wallet && root.wallet.recipients
            ? root.wallet.recipients.totalAmountSatoshi
            : 0
        unit: ApplicationModels.optionsModel.displayUnit
    }

    initialItem: Page {
        id: sendPage
        objectName: "walletSendPage"
        background: null
        readonly property real pageSidePadding: width < 640 ? 16 : 40

        function handlePsbtImportResult(result) {
            sendOptionsPopup.close()
            if (result === WalletQmlModel.WalletCanSign) {
                root.openTransactionReview()
            } else if (result === WalletQmlModel.WalletCannotSign || result === WalletQmlModel.ReviewPending) {
                root.openTransactionReview()
            } else if (result === WalletQmlModel.TransactionAlreadyKnown) {
                knownPsbtTxPopup.txid = root.wallet.importedPsbt.matchedTxid
                knownPsbtTxPopup.open()
            } else if (result === WalletQmlModel.PsbtUnsupported) {
                unsupportedPsbtPopup.message = root.wallet.importedPsbt.error.length > 0
                    ? root.wallet.importedPsbt.error
                    : qsTr("This PSBT is not supported yet.")
                unsupportedPsbtPopup.open()
            }
        }

        function openPaymentRequestImport() {
            uriImportInput.text = ""
            sendUriImportPopup.open()
        }

        function openPsbtFileImport() {
            sendOptionsPopup.close()
            cancelUriAction()
            m_importWallet = root.wallet
            m_importSource = qsTr("file")
            m_importOperation = BitcoinUri.captureImport(root.wallet, root.recipient, sendPage)
            if (m_importOperation) psbtOpenDialog.open()
        }

        Popup {
            id: transactionReviewPopup
            objectName: "transactionReviewPopup"
            parent: Overlay.overlay
            x: 0
            y: 0
            property real verticalOffset: 0
            modal: true
            focus: true
            dim: false
            closePolicy: Popup.NoAutoClose
            padding: 0
            width: Overlay.overlay ? Overlay.overlay.width : 0
            height: Overlay.overlay ? Overlay.overlay.height : 0
            onOpened: {
                const review = reviewStack.currentItem as SendTransactionReview
                const complete = reviewStack.currentItem as SendComplete
                if (review) review.focusInitialControl()
                else if (complete) complete.focusInitialControl()
            }

            enter: Transition {
                NumberAnimation {
                    property: "verticalOffset"
                    from: transactionReviewPopup.height
                    to: 0
                    duration: 350
                    easing.type: Easing.OutCubic
                }
            }
            exit: Transition {
                NumberAnimation {
                    property: "verticalOffset"
                    from: 0
                    to: transactionReviewPopup.height
                    duration: 280
                    easing.type: Easing.InCubic
                }
            }

            property WalletQmlModel reviewWallet: null
            property bool importedReview: false
            // Set once the payment is sent, until the form is cleared. The
            // popup can also close without Done, such as when the page is
            // hidden, and the sent payment must not stay in the form.
            property bool sent: false

            function clearSentForm() {
                if (!sent) return
                sent = false
                if (reviewWallet === root.wallet) root.resetSendForm()
            }

            onClosed: {
                clearSentForm()
                const wallet = reviewWallet
                const wasImported = importedReview
                reviewStack.pop(null, StackView.Immediate)
                reviewWallet = null
                importedReview = false
                if (wallet && wasImported && !wallet.sendBusy) {
                    wallet.discardCurrentTransaction()
                }
            }

            background: null

            contentItem: PageStack {
                id: reviewStack
                objectName: "sendTransactionReviewStack"
                width: transactionReviewPopup.availableWidth
                height: transactionReviewPopup.availableHeight
                vertical: true
                initialItem: reviewPage
                transform: Translate { y: transactionReviewPopup.verticalOffset }
                pushEnter: Transition {
                    NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 180; easing.type: Easing.OutCubic }
                }
                pushExit: Transition {
                    // Keep Review visible under the fading Complete page until
                    // the Complete background is fully opaque.
                    NumberAnimation { property: "opacity"; from: 1; to: 1; duration: 180 }
                }

                Component {
                    id: reviewPage
                    SendTransactionReview {
                        wallet: transactionReviewPopup.reviewWallet
                        onBack: transactionReviewPopup.close()
                        onTransactionSent: (txid, submissionState, recordState) => {
                            transactionReviewPopup.sent = true
                            reviewStack.push(sendCompletePage, {
                                "txid": txid,
                                "submissionState": submissionState,
                                "recordState": recordState,
                                "targetBlocks": !transactionReviewPopup.importedReview && wallet && wallet.currentTransaction.reviewFeeSource === FeeSelectionModel.Estimated
                                    ? wallet.currentTransaction.reviewTargetBlocks : 0
                            })
                        }
                    }
                }

                Component {
                    id: sendCompletePage
                    SendComplete {
                        onDone: {
                            transactionReviewPopup.clearSentForm()
                            transactionReviewPopup.close()
                        }
                        onViewNewTransaction: (txid) => {
                            transactionReviewPopup.clearSentForm()
                            transactionReviewPopup.close()
                            root.viewTransactionInActivity(txid)
                        }
                    }
                }
            }
        }

        AppFileDialog {
            id: psbtOpenDialog
            objectName: "sendPsbtOpenDialog"
            title: qsTr("Import PSBT")
            fileMode: AppFileDialog.OpenFile
            nameFilters: [qsTr("Partially Signed Bitcoin Transactions (*.psbt)"), qsTr("All files (*)")]
            contextObject: sendPage.m_importOperation
            enabled: root.visible && root.wallet !== null && root.wallet.available && !root.wallet.psbtActionBusy
            onChosen: (context, file) => {
                const original = context as PaymentImportOperation
                if (original && original === sendPage.m_importOperation && original.requestFile(file)) original.use()
            }
        }

        // Kept hidden so functional tests can inject a PSBT path until the
        // native file dialog is automatable through the QML test bridge.
        TextField {
            id: psbtAutomationPathField
            objectName: "psbtImportPathField"
            visible: false
        }

        AlertPopup {
            id: unsupportedPsbtPopup
            objectName: "unsupportedPsbtPopup"
            title: qsTr("Not supported")
            messageObjectName: "unsupportedPsbtPopupMessage"

            onClosed: root.wallet.importedPsbt.clear()

            AlertAction {
                text: qsTr("OK")
                buttonObjectName: "unsupportedPsbtPopupOkButton"
            }
        }

        AlertPopup {
            id: knownPsbtTxPopup
            objectName: "knownPsbtTxPopup"
            title: qsTr("Cannot import transaction")
            message: qsTr("This transaction is already in your wallet spent history. View it in Activity")
            messageObjectName: "knownPsbtTxPopupMessage"

            property string txid: ""

            onClosed: root.wallet.importedPsbt.clear()

            AlertAction {
                text: qsTr("View transaction")
                buttonObjectName: "knownPsbtTxViewButton"
                onTriggered: root.viewTransactionInActivity(knownPsbtTxPopup.txid)
            }

            AlertAction {
                text: qsTr("Close")
                role: AlertAction.Cancel
                buttonObjectName: "knownPsbtTxCloseButton"
            }
        }

        // URI import state
        property string m_legacyPaymentRequestStatus: ""
        property bool m_legacyPaymentRequestIsError: false
        property string m_legacyPaymentRequestMessage: ""
        readonly property string paymentRequestStatus: m_importOperation
            ? (m_importOperation.error.length > 0 ? m_importOperation.error
                : m_importOperation.state === PaymentImportOperation.Applied ? qsTr("Payment request imported from %1").arg(m_importSource) : "")
            : m_legacyPaymentRequestStatus
        readonly property bool paymentRequestIsError: m_importOperation ? m_importOperation.error.length > 0 : m_legacyPaymentRequestIsError
        readonly property string paymentRequestMessage: m_importOperation
            ? (m_importOperation.state === PaymentImportOperation.Applied ? m_importOperation.message : "") : m_legacyPaymentRequestMessage

        // Native clipboard capture and immutable Domain probe outcomes belong
        // to the original wallet, recipient, draft and editor selection.
        readonly property bool showClipboardUriBanner: root.visible && m_clipboardOperation !== null
            && m_clipboardWallet !== null && m_clipboardWallet.available
            && m_clipboardOperation.state === PaymentImportOperation.Inspecting
            && m_clipboardOperation.sourceKind === PaymentImportOperation.PaymentSource
            && m_clipboardOperation.canApply && !m_clipboardOperation.matchesExistingRequest
            && root.wallet === m_clipboardWallet && root.recipient === m_clipboardRecipient
            && root.draftRevision === m_clipboardRevision && root.hiddenCount === m_clipboardHidden
            && Clipboard.revision === m_clipboardCounter
        readonly property string m_pendingClipboardUri: showClipboardUriBanner ? m_clipboardText : ""
        readonly property double m_pendingClipboardCounter: m_clipboardCounter
        property PaymentImportOperation m_importOperation: null
        property WalletQmlModel m_importWallet: null
        property string m_importSource: ""
        property string m_importPresentedResult: ""
        readonly property string importError: m_importOperation ? m_importOperation.error : ""
        property PaymentImportOperation m_clipboardOperation: null
        property string m_nativeHandoffHandled: ""
        property WalletQmlModel m_uriWallet: null
        property SendRecipient m_uriRecipient: null
        property SendRecipient m_uriCurrentRecipient: null
        property CapturedPaste m_uriPasteOwner: null
        property bool m_uriHadPasteOwner: false
        property var m_uriPayload: null
        property string m_uriRevision: ""
        property int m_uriHidden: -1
        property bool m_uriApplying: false
        property WalletQmlModel m_clipboardWallet: null
        property SendRecipient m_clipboardRecipient: null
        property string m_clipboardRevision: ""
        property int m_clipboardHidden: -1
        property double m_clipboardCounter: -1
        property string m_clipboardText: ""
        property bool m_applyingUri: false
        readonly property bool uriActionPending: (m_importOperation !== null && m_importOperation.pending) || m_uriApplying
            || (m_uriPasteOwner !== null && m_uriPasteOwner.pending)

        Connections {
            target: sendPage.m_importOperation
            function onChanged() {
                sendPage.performNativeHandoff()
                const operation = sendPage.m_importOperation
                if (!operation) return
                if (operation.promptToken) paymentUriOverwritePopup.open()
                if (operation.state === PaymentImportOperation.Applied || operation.state === PaymentImportOperation.Failed
                        || operation.state === PaymentImportOperation.Closed) sendPage.finishPasteOwner(false)
                sendPage.presentImportedReview()
            }
        }
        function presentImportedReview() {
            const operation = m_importOperation
            if (!operation || operation.state !== PaymentImportOperation.ReviewOpened || !m_importWallet
                    || root.wallet !== m_importWallet || !m_importWallet.available
                    || m_importWallet.importedReviewToken !== operation.resultToken || m_importPresentedResult === operation.resultToken) return
            m_importPresentedResult = operation.resultToken
            root.openTransactionReview()
        }
        function finishPasteOwner(cancel) {
            if (!m_uriPasteOwner || !m_uriPayload) return
            if (cancel && m_uriPasteOwner.intent === m_uriPayload) m_uriPasteOwner.cancel()
            else if (!(m_uriPasteOwner.literalActive && m_uriPasteOwner.effectPending)) m_uriPasteOwner.complete(m_uriPayload)
        }
        function clearPendingPaymentUriPaste() { finishPasteOwner(true) }
        function cancelUriAction() {
            const operation = m_importOperation
            const pasteOwner = m_uriPasteOwner
            const payload = m_uriPayload
            m_importOperation = null
            m_importWallet = null
            m_importPresentedResult = ""
            m_uriPasteOwner = null
            m_uriPayload = null
            m_uriHadPasteOwner = false
            m_nativeHandoffHandled = ""
            paymentUriOverwritePopup.close()
            if (pasteOwner && payload && pasteOwner.intent === payload) pasteOwner.cancel()
            if (operation) operation.cancel()
        }
        function cancelUriIntents() {
            cancelUriAction()
            if (m_clipboardOperation) m_clipboardOperation.cancel()
            m_clipboardOperation = null
        }
        function uriContextValid() {
            return root.visible && m_uriWallet && m_uriWallet.available && root.wallet === m_uriWallet
                && m_uriRecipient && root.recipient === m_uriCurrentRecipient
                && root.draftRevision === m_uriRevision && root.hiddenCount === m_uriHidden
                && (!m_uriHadPasteOwner || (m_uriPasteOwner && m_uriPasteOwner.owns(m_uriPayload)))
        }
        function requestUriAction(text, source, field, payload, recipient) {
            cancelUriAction()
            if (!root.visible || !root.wallet || !root.wallet.available || !(recipient || root.recipient)) {
                if (payload && payload.origin) payload.origin.cancel()
                return
            }
            m_uriWallet = root.wallet
            m_uriCurrentRecipient = root.recipient
            m_uriRecipient = recipient || root.recipient
            m_uriRevision = root.draftRevision
            m_uriHidden = root.hiddenCount
            m_uriPayload = payload || null
            m_uriPasteOwner = payload ? payload.origin : null
            m_uriHadPasteOwner = m_uriPasteOwner !== null
            m_importWallet = m_uriWallet
            m_importSource = source
            m_importOperation = BitcoinUri.requestImportPaste(text, field, !!(payload && payload.edited),
                payload && payload.edited ? payload.previous : "", payload && payload.edited ? payload.current : "",
                m_uriWallet, m_uriRecipient, sendPage)
            if (!m_importOperation) {
                m_legacyPaymentRequestStatus = qsTr("Payment URI services are unavailable. Try again.")
                m_legacyPaymentRequestIsError = true
                finishPasteOwner(true)
                return
            }
            performNativeHandoff()
        }
        function performNativeHandoff() {
            const operation = m_importOperation
            if (!operation || !operation.nativeHandoffToken || m_nativeHandoffHandled === operation.nativeHandoffToken) return
            if (!uriContextValid()) { cancelUriAction(); return }
            const token = operation.nativeHandoffToken
            m_nativeHandoffHandled = token
            m_uriApplying = true
            try {
                if (m_uriPasteOwner) {
                    if (operation.nativeDisposition === PaymentImportOperation.LiteralPaste) m_uriPasteOwner.apply(m_uriPayload)
                    else m_uriPasteOwner.restore(m_uriPayload)
                }
                if (operation !== m_importOperation) return
                operation.completeNativeHandoff(token)
                if (operation.nativeDisposition === PaymentImportOperation.RestoreImport) operation.use()
            } finally { m_uriApplying = false }
        }
        function handlePaymentUriPaste(text, sourceField) {
            requestUriAction(text, qsTr("clipboard"), sourceField, null, root.recipient)
        }
        function handleClipboardPaste(field, editor, payload) {
            if (!payload) {
                const text = Clipboard.text()
                const component = !editor ? null : field === BitcoinUri.AddressField ? editor.addressField : field === BitcoinUri.AmountField ? editor.amountField : editor.noteField
                payload = component ? component.capturePaste(text, false) : {text: text, origin: null, edited: false}
            }
            if (!payload || !payload.text.length) {
                if (payload && payload.origin) payload.origin.complete(payload)
                return
            }
            requestUriAction(payload.text, qsTr("clipboard"), field, payload, editor ? editor.recipient : root.recipient)
        }
        function checkClipboard() {
            if (m_clipboardOperation) m_clipboardOperation.cancel()
            m_clipboardOperation = null
            if (!root.visible || !root.wallet || !root.wallet.available || !root.recipient || root.wallet.recipients.importPending) return
            m_clipboardWallet = root.wallet
            m_clipboardRecipient = root.recipient
            m_clipboardRevision = root.draftRevision
            m_clipboardHidden = root.hiddenCount
            m_clipboardText = Clipboard.text()
            m_clipboardCounter = Clipboard.revision
            m_clipboardOperation = BitcoinUri.requestImportText(m_clipboardText, m_clipboardWallet, m_clipboardRecipient, sendPage)
        }

        // Parse a URI from text and apply to form.
        function applyPaymentRequestFromText(text, source) {
            cancelUriAction()
            m_importWallet = root.wallet; m_importSource = source
            m_importOperation = BitcoinUri.requestImportText(text, m_importWallet, root.recipient, sendPage)
            if (m_importOperation) m_importOperation.use()
            else { m_legacyPaymentRequestStatus = qsTr("Payment URI services are unavailable. Try again."); m_legacyPaymentRequestIsError = true }
        }

        function applyPaymentRequestFromFile(path) {
            cancelUriAction()
            m_importWallet = root.wallet; m_importSource = qsTr("file")
            m_importOperation = BitcoinUri.requestImportFile(path, m_importWallet, root.recipient, sendPage)
            if (m_importOperation) m_importOperation.use()
            else { m_legacyPaymentRequestStatus = qsTr("Payment URI services are unavailable. Try again."); m_legacyPaymentRequestIsError = true }
        }

        AlertPopup {
            id: paymentUriOverwritePopup
            objectName: "sendPaymentUriOverwritePopup"
            parent: Overlay.overlay
            title: qsTr("Replace payment details?")
            message: qsTr("The payment request differs from the current draft. Replace the payment details?")
            messageObjectName: "sendPaymentUriOverwriteMessage"

            AlertAction {
                text: qsTr("Cancel")
                role: AlertAction.Cancel
                buttonObjectName: "sendPaymentUriOverwriteCancelButton"
                closesPopup: false
                onTriggered: {
                    sendPage.cancelUriAction()
                }
            }

            AlertAction {
                text: qsTr("Replace")
                buttonObjectName: "sendPaymentUriOverwriteConfirmButton"
                closesPopup: false
                onTriggered: {
                    if (sendPage.m_importOperation) sendPage.m_importOperation.replace(sendPage.m_importOperation.promptToken)
                    paymentUriOverwritePopup.close()
                }
            }
        }

        Connections {
            target: Clipboard
            function onDataChanged() { sendPage.checkClipboard() }
        }

        // Capture a fresh observation after local input. Public metadata
        // equality is decided by the original target on Domain.
        Connections {
            target: root.recipient.address
            function onAddressChanged() {
                if (!sendPage.m_applyingUri) {
                    if (root.recipient.address.address === "") {
                        sendPage.m_legacyPaymentRequestStatus = ""
                        sendPage.m_legacyPaymentRequestIsError = false
                        sendPage.m_legacyPaymentRequestMessage = ""
                    }
                    sendPage.checkClipboard()
                }
            }
        }
        Connections {
            target: root.recipient.amount
            function onAmountChanged() {
                if (!sendPage.m_applyingUri) sendPage.checkClipboard()
            }
        }
        Connections {
            target: root.recipient
            function onLabelChanged() { if (!sendPage.m_applyingUri) sendPage.checkClipboard() }
            function onMessageChanged() { if (!sendPage.m_applyingUri) sendPage.checkClipboard() }
            function onPaymentRequestChanged() { if (!sendPage.m_applyingUri) sendPage.checkClipboard() }
        }

        Component.onCompleted: sendPage.checkClipboard()

        Connections {
            target: sendOptionsPopup
            function onOpenPaymentRequest() {
                sendPage.openPaymentRequestImport()
            }
        }

        // Manual URI entry popup
        Popup {
            id: sendUriImportPopup
            objectName: "sendUriImportPopup"
            anchors.centerIn: Overlay.overlay
            width: Math.min(sendPage.width - 40, 420)
            modal: true
            dim: true
            padding: 20

            Overlay.modal: Rectangle {
                color: Qt.rgba(0, 0, 0, 0.5)
            }

            background: Rectangle {
                color: Theme.color.neutral1
                border.color: Theme.color.neutral2
                radius: 5
                border.width: 1
                SurfaceGradientBorder {
                    anchors.fill: parent
                    surfaceColor: parent.color
                    cornerRadius: parent.radius
                }
            }

            contentItem: ColumnLayout {
                spacing: 12

                CoreText {
                    text: qsTr("Open payment request")
                    font: Theme.text.subheading.font
                    color: Theme.color.neutral9
                    Layout.fillWidth: true
                }

                CoreTextField {
                    id: uriImportInput
                    objectName: "sendUriImportInput"
                    Layout.fillWidth: true
                    placeholderText: qsTr("bitcoin:address?amount=…")
                    background: Rectangle {
                        color: Theme.color.neutral2
                        radius: 5
                        border.width: 0
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    OutlineButton {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 0
                        text: qsTr("Cancel")
                        onClicked: sendUriImportPopup.close()
                    }

                    ContinueButton {
                        objectName: "sendUriImportApplyButton"
                        Layout.fillWidth: true
                        Layout.preferredWidth: 0
                        text: qsTr("Apply")
                        enabled: uriImportInput.text.length > 0
                        onClicked: {
                            sendUriImportPopup.close()
                            sendPage.applyPaymentRequestFromText(uriImportInput.text, qsTr("manual entry"))
                        }
                    }
                }
            }
        }

        // Drag-and-drop support
        DropArea {
            objectName: "sendDropArea"
            anchors.fill: parent
            keys: ["text/uri-list", "text/plain"]
            onDropped: (drop) => {
                sendPage.cancelUriAction()
                sendPage.m_importWallet = root.wallet
                sendPage.m_importSource = qsTr("drag and drop")
                sendPage.m_importOperation = BitcoinUri.requestImportDrop(drop.hasUrls ? drop.urls.length : 0,
                    drop.hasUrls && drop.urls.length ? drop.urls[0] : "", drop.hasUrls ? "" : drop.text, root.wallet, root.recipient, sendPage)
                if (sendPage.m_importOperation) sendPage.m_importOperation.use()
            }
        }

        function removeRecipient(index) {
            root.wallet.recipients.removeAt(index)
            expandedRecipient = root.wallet.recipients.currentIndex - 1
        }

        property int expandedRecipient: root.wallet ? root.wallet.recipients.currentIndex - 1 : 0
        readonly property var currentEditor: {
            const revision = root.wallet ? root.wallet.recipients.currentIndex : 0
            const row = recipientRepeater.itemAt(expandedRecipient) as SendRecipientCard
            return row ? row.editorItem : null
        }
        readonly property var addressField: currentEditor ? currentEditor.addressField : null
        readonly property var label: currentEditor ? currentEditor.noteField : null
        readonly property var amountInput: currentEditor ? currentEditor.amountField : null

        ScrollView {
            id: sendScroll
            objectName: "sendFormScrollView"
            anchors.fill: parent
            contentWidth: availableWidth
            contentHeight: formColumn.height + 64
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            Item {
                width: sendScroll.availableWidth
                height: formColumn.height + 64
                ColumnLayout {
                    id: formColumn
                    width: Math.max(0, Math.min(1100, sendScroll.availableWidth - sendPage.pageSidePadding * 2))
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.top: parent.top; anchors.topMargin: 28
                    spacing: 24
                    enabled: ApplicationModels.walletController.initialized
                    RowLayout {
                        Layout.fillWidth: true
                        ColumnLayout {
                            Layout.fillWidth: true; spacing: 6
                            //: Main heading of the transaction creation form.
                            CoreText { objectName: "walletSendTitle"; Layout.fillWidth: true; text: qsTr("Send bitcoin"); font: Theme.text.headline.font; horizontalAlignment: Text.AlignLeft }
                            RowLayout {
                                id: availableBalanceRow
                                Layout.fillWidth: true
                                spacing: 4
                                //: Available wallet balance excludes locked coins.
                                CoreText {
                                    id: availableBalanceCaption
                                    objectName: "sendAvailableCaption"
                                    text: qsTr("Available")
                                    font: Theme.text.description.font
                                    color: Theme.color.neutral7
                                    horizontalAlignment: Text.AlignLeft
                                    Layout.alignment: Qt.AlignBaseline
                                }
                                BitcoinAmountDisplayLabel {
                                    objectName: "sendAvailableAmount"
                                    amount: availableAmount.localizedDisplay
                                    unit: availableAmount.unitLabel
                                    font: Theme.text.description.font
                                    color: Theme.color.neutral7
                                    horizontalAlignment: Text.AlignLeft
                                    Layout.alignment: Qt.AlignBaseline
                                    animationAvailableWidth: Math.max(0, availableBalanceRow.parent.width
                                        - availableBalanceCaption.width - availableBalanceRow.spacing)
                                }
                                BitcoinAmount {
                                    id: availableAmount
                                    satoshi: {
                                        const coinCount = root.wallet ? root.wallet.coinsListModel.coinCount : 0
                                        return root.wallet ? root.wallet.availableSendBalanceSatoshi : 0
                                    }
                                    unit: ApplicationModels.optionsModel.displayUnit
                                }
                            }
                        }
                        OverflowMenuButton {
                            id: menuButton; objectName: "sendOptionsButton"
                            checked: sendOptionsPopup.opened
                            onClicked: sendOptionsPopup.opened ? sendOptionsPopup.close() : sendOptionsPopup.open()
                            SendCreateOptionsPopup {
                                id: sendOptionsPopup
                                x: menuButton.width - width; y: menuButton.height + 6
                                onClearFormRequested: root.resetSendForm()
                                onImportPsbtFromFileRequested: {
                                    if (psbtAutomationPathField.text.length > 0) {
                                        const automatedPath = psbtAutomationPathField.text
                                        psbtAutomationPathField.text = ""
                                        root.requestPsbtImport(automatedPath)
                                    } else sendPage.openPsbtFileImport()
                                }
                            }
                        }
                    }
                    //: Reminder for wallets that sign on a separate hardware device.
                    CoreText { visible: root.externalSignerWallet; Layout.fillWidth: true; text: qsTr("Have your external signer ready to approve this transaction."); horizontalAlignment: Text.AlignLeft; font: Theme.text.description.font; color: Theme.color.neutral7 }
                    GridLayout {
                        id: sendSections
                        Layout.fillWidth: true
                        columns: width >= 900 ? 2 : 1
                        columnSpacing: 24
                        rowSpacing: 24

                        FormSection {
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignTop
                            Layout.preferredWidth: sendSections.columns === 2 ? sendSections.width * 0.6 - 12 : sendSections.width
                            title: root.wallet.recipients.count > 1 ? qsTr("Recipients") : qsTr("Recipient")
                            showBackground: false
                            rowSpacing: 12

                            // Keep changing recipient delegates out of Qt Quick Layouts.
                            // Qt 6.4 can read a freed delegate during a layout pass.
                            Column {
                                id: recipientColumn
                                Layout.fillWidth: true
                                spacing: 12

                                Repeater {
                                    id: recipientRepeater
                                    model: root.wallet.recipients
                                    delegate: SendRecipientCard {
                                        id: recipientDelegate
                                        wallet: root.wallet
                                        width: recipientColumn.width
                                        expanded: sendPage.expandedRecipient === recipientDelegate.index
                                        clipboardRequest: sendPage.showClipboardUriBanner
                                        clipboardText: sendPage.m_pendingClipboardUri
                                        clipboardRevision: sendPage.m_pendingClipboardCounter
                                        onToggleExpanded: {
                                            const wasExpanded = recipientDelegate.expanded
                                            root.wallet.recipients.setCurrentIndex(recipientDelegate.index)
                                            sendPage.expandedRecipient = wasExpanded ? -1 : recipientDelegate.index
                                        }
                                        onPasteRequested: function(field, payload) {
                                            sendPage.handleClipboardPaste(field, recipientDelegate.editorItem, payload)
                                        }
                                        onEdited: {
                                            root.draftChanged()
                                            root.clearPrepareTransactionError()
                                            root.scheduleFeeEstimates()
                                        }
                                        onRemoveRequested: sendPage.removeRecipient(recipientDelegate.index)
                                    }
                                }
                            }
                            CoreText {
                                objectName: "sendPaymentRequestStatusText"
                                visible: sendPage.importError.length > 0 || (sendPage.paymentRequestIsError && sendPage.paymentRequestStatus.length > 0)
                                Layout.leftMargin: 16; Layout.rightMargin: 16
                                Layout.fillWidth: true; text: sendPage.importError.length > 0 ? sendPage.importError : sendPage.paymentRequestStatus; horizontalAlignment: Text.AlignLeft; color: Theme.color.red; font: Theme.text.description.font
                            }

                            NeutralButton {
                                objectName: "sendAddRecipientButton"
                                Layout.fillWidth: true
                                text: qsTr("+ Add another")
                                enabled: root.wallet.recipients.canAddRecipient
                                onClicked: { root.wallet.recipients.add(); sendPage.expandedRecipient = root.wallet.recipients.currentIndex - 1 }
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignTop
                            Layout.preferredWidth: sendSections.columns === 2 ? sendSections.width * 0.4 - 12 : sendSections.width
                            spacing: 24

                            FormSection {
                                objectName: "sendNetworkFeeSection"
                                Layout.fillWidth: true
                                title: qsTr("Network fee")
                                footerText: !root.wallet ? ""
                                    : root.wallet.feeEstimatePending ? qsTr("Estimating…")
                                    : root.wallet.estimatedFeeSatoshi >= 0 ? ""
                                    : root.wallet.recipients.allValid ? qsTr("Fee estimate unavailable")
                                    : qsTr("Enter a valid recipient address and amount to estimate the fee")
                                SendFeeSelection { Layout.fillWidth: true; walletModel: root.wallet; onFeeChanged: root.clearPrepareTransactionError() }
                            }

                            FormSection {
                                Layout.fillWidth: true
                                title: qsTr("Coin control")

                                FormRow {
                                    Layout.fillWidth: true
                                    title: qsTr("Selection")
                                    showDivider: root.manualCoinSelection || root.selectedInputsActive
                                    trailingItem: SegmentedPicker {
                                        objectName: "sendCoinControlPicker"
                                        implicitWidth: 190; implicitHeight: 36
                                        model: [qsTr("Automatic"), qsTr("Manual")]
                                        currentIndex: root.manualCoinSelection || root.selectedInputsActive ? 1 : 0
                                        onSelected: function(index, option) {
                                            if (root.wallet && root.wallet.coinsListModel)
                                                root.wallet.coinsListModel.setManualInputSelection(index === 1)
                                        }
                                    }
                                }

                                FormRow {
                                    visible: root.manualCoinSelection || root.selectedInputsActive
                                    Layout.fillWidth: true
                                    title: qsTr("Inputs")
                                    description: root.wallet.coinsListModel.selectedCoinsCount === 0 ? qsTr("No inputs selected")
                                        : root.wallet.coinsListModel.selectedCoinsCount === 1 ? qsTr("1 input selected") : qsTr("%1 inputs selected").arg(root.wallet.coinsListModel.selectedCoinsCount)
                                    showDivider: false
                                    trailingItem: NeutralButton {
                                        objectName: "sendSelectInputsButton"
                                        buttonSize: NeutralButton.Medium
                                        text: qsTr("Select coins")
                                        onClicked: coinSelectionPopup.open()
                                    }
                                }

                                CoreText {
                                    objectName: "sendInputsSelectedText"
                                    visible: false
                                    //: Status in the Send form showing how many transaction inputs are selected.
                                    text: root.manualCoinSelection && root.wallet.coinsListModel.selectedCoinsCount === 0 ? qsTr("No inputs selected")
                                        : root.selectedInputsActive ? (root.wallet.coinsListModel.selectedCoinsCount === 1 ? qsTr("1 input selected") : qsTr("%1 inputs selected").arg(root.wallet.coinsListModel.selectedCoinsCount))
                                        : qsTr("%1 inputs selected automatically").arg(root.wallet.estimatedInputCount)
                                }
                                OutlineButton {
                                    objectName: "sendUseAutomaticInputsButton"
                                    visible: false
                                    text: qsTr("Use automatic selection")
                                    onClicked: { root.wallet.coinsListModel.setManualInputSelection(false); root.wallet.coinsListModel.update(); root.scheduleFeeEstimates() }
                                }
                            }

                            FormSection {
                                Layout.fillWidth: true
                                title: qsTr("Summary")
                                BitcoinAmount { id: summaryFee; satoshi: Math.max(0, root.wallet.estimatedFeeSatoshi); unit: ApplicationModels.optionsModel.displayUnit }
                                BitcoinAmount { id: summaryTotal; satoshi: root.wallet.sendTotalSatoshi; unit: ApplicationModels.optionsModel.displayUnit }
                                ValueRow { objectName: "sendTotalFeesValue"; Layout.fillWidth: true; amountValue: true; title: qsTr("Total fees"); value: !root.wallet.feeEstimatePending && root.wallet.estimatedFeeSatoshi >= 0 ? summaryFee.localizedDisplay : "—"; valueUnit: summaryFee.unitLabel; valueTextStyle: Theme.text.monoDescription }
                                ValueRow { objectName: "sendTotalAmountValue"; Layout.fillWidth: true; amountValue: true; title: qsTr("Total to send"); showDivider: false; value: !root.wallet.feeEstimatePending && root.wallet.estimatedFeeSatoshi >= 0 ? summaryTotal.localizedDisplay : recipientsTotalAmount.localizedDisplay; valueUnit: !root.wallet.feeEstimatePending && root.wallet.estimatedFeeSatoshi >= 0 ? summaryTotal.unitLabel : recipientsTotalAmount.unitLabel; valueTextStyle: Theme.text.monoBody }
                            }

                            CoreText { objectName: "sendPrepareTransactionErrorText"; Layout.fillWidth: true; visible: root.formErrorText.length > 0; text: root.formErrorText; horizontalAlignment: Text.AlignLeft; font: Theme.text.description.font; color: Theme.color.red }
                            ContinueButton {
                                objectName: "sendReviewButton"
                                Layout.fillWidth: true
                                text: qsTr("Review Transaction")
                                enabled: !sendPage.uriActionPending && root.wallet && root.wallet.available && root.wallet.recipients.allValid && !root.wallet.feeEstimatePending && !root.wallet.preparationBusy && !root.wallet.fees.policyPending && !root.wallet.customFeeTargetPending && !root.wallet.coinSelectionPending && !root.wallet.coinSelectionBlocked
                                    && !root.wallet.sendAmountExhaustsBalance && (!root.wallet.maximumRecipient || root.wallet.fees.errorText.length === 0) && (!root.wallet.customFeeEnabled || root.wallet.customFeeRateValid)
                                onClicked: root.confirmTransactionReview()
                            }
                        }
                    }
                }
            }
        }

        // Automation-only hooks for functional tests. Only loaded when the app
        // is built with -DENABLE_TEST_AUTOMATION=ON (testAutomationEnabled is a
        // C++ context property set at startup). In production builds the Loader
        // is inactive and no hook items exist in the QML object tree.
        Loader {
            active: ApplicationModels.testAutomationEnabled
            anchors.fill: parent
            sourceComponent: Component {
                Item {
                    anchors.fill: parent

                    CoreTextField {
                        id: fileImportPathInput
                        objectName: "sendImportPaymentRequestFilePathInput"
                        visible: false
                    }

                    Button {
                        objectName: "sendApplyPaymentRequestFilePathButton"
                        visible: false
                        onClicked: sendPage.applyPaymentRequestFromFile(fileImportPathInput.text)
                    }

                    CoreTextField {
                        id: dropUriInput
                        objectName: "sendDropUriInput"
                        visible: false
                    }

                    Button {
                        objectName: "sendApplyDropUriButton"
                        visible: false
                        onClicked: sendPage.applyPaymentRequestFromText(dropUriInput.text, qsTr("drag and drop"))
                    }

                    // Exercises the DropArea hasUrls + file:// branch.
                    // Passes the raw URL to applyPaymentRequestFromFile so C++
                    // handles the platform-correct toLocalFile() conversion.
                    CoreTextField {
                        id: dropFileUrlInput
                        objectName: "sendDropFileUrlInput"
                        visible: false
                    }

                    Button {
                        objectName: "sendApplyDropFileUrlButton"
                        visible: false
                        onClicked: {
                            const url = dropFileUrlInput.text
                            if (url.startsWith("file://")) {
                                sendPage.applyPaymentRequestFromFile(url)
                            } else {
                                sendPage.applyPaymentRequestFromText(url, qsTr("drag and drop"))
                            }
                        }
                    }
                }
            }
        }

    }

    AlertPopup {
        id: reviewSweepAlert
        objectName: "sendReviewSweepAlert"
        parent: Overlay.overlay
        property string draftRevision: ""
        //: Confirmation before reviewing a transaction that sends the wallet's entire available balance.
        title: qsTr("Use all available funds?")
        //: %1 is the total recipient amount with its unit, excluding the transaction fee.
        message: qsTr("You’re preparing to send all available funds (%1) from this wallet. Do you want to proceed?").arg(recipientsTotalAmount.displayWithUnit)
        AlertAction {
            //: Stay on the Send form without changing the recipients, amounts, or selected coins.
            text: qsTr("Cancel")
            role: AlertAction.Cancel
            buttonObjectName: "sendReviewSweepCancelButton"
        }
        AlertAction {
            //: Confirm using all available funds before unlocking the wallet and reviewing; this does not send it.
            text: qsTr("Use maximum")
            buttonObjectName: "sendReviewSweepConfirmButton"
            onTriggered: {
                if (reviewSweepAlert.draftRevision === root.draftRevision) root.prepareTransactionForReview()
            }
        }
    }

    SendCoinSelection { id: coinSelectionPopup; wallet: root.wallet }

    WalletPassphrasePopup {
        id: reviewPassphrasePopup
        parent: Overlay.overlay
        width: Math.min(480, root.width - 40)
        popupObjectName: "reviewPassphrasePopup"
        passphraseFieldObjectName: "reviewPassphraseField"
        errorTextObjectName: "reviewPassphraseErrorText"
        cancelButtonObjectName: "reviewPassphraseCancelButton"
        confirmButtonObjectName: "reviewPassphraseConfirmButton"
        titleText: qsTr("Enter wallet password")
        descriptionText: qsTr("Enter your wallet password to prepare this transaction for review.")
        confirmText: qsTr("Unlock and continue")
        busyConfirmText: qsTr("Unlocking...")
        property WalletQmlModel actionWallet: null
        property string actionDraftRevision: ""
        readonly property bool contextValid: root.visible && actionWallet !== null
            && actionWallet === root.wallet && actionWallet.available && actionDraftRevision === root.draftRevision
        onAboutToShow: { actionWallet = root.wallet; actionDraftRevision = root.draftRevision }
        onContextValidChanged: if (!contextValid) close()
        busy: opened && contextValid && actionWallet.preparationBusy
        errorText: contextValid && actionWallet.preparationState === WalletQmlModel.PreparationFailed ? actionWallet.transactionError : ""
        onAboutToHide: clearPassphraseField()
        onSubmitted: (passphrase) => {
            if (!opened || !contextValid || actionWallet.preparationBusy) return
            root.preparationWallet = actionWallet
            root.preparationDraftRevision = root.draftRevision
            root.preparationHiddenCount = root.hiddenCount
            root.preparingReview = true
            root.preparationTicket = 0
            root.preparationTicket = actionWallet.requestPrepareTransactionWithPassphrase(passphrase)
            if (!root.preparationTicket) root.preparingReview = false
            else root.handlePreparationOutcome()
        }
    }

}
