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
    property bool manualCoinSelection: false
    readonly property bool externalSignerWallet: wallet !== null && wallet.hasExternalSigner
    readonly property string recipientValidationError: wallet ? wallet.recipients.validationError : ""
    readonly property bool selectedInputsActive: wallet !== null
        && wallet.coinsListModel !== null
        && wallet.coinsListModel.selectedCoinsCount > 0
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
    readonly property string formErrorText: recipientValidationError.length > 0
        ? recipientValidationError
        : (feeBalanceErrorText.length > 0 ? feeBalanceErrorText : prepareTransactionErrorText)

    signal viewTransactionInActivity(string txid)

    property double psbtImportTicket: 0
    property WalletQmlModel psbtImportWallet: null
    property int psbtImportHiddenCount: -1
    property int psbtImportDraftRevision: -1
    function requestPsbtImport(path) {
        if (!root.wallet || root.wallet.psbtActionBusy) return
        psbtImportWallet = root.wallet
        psbtImportHiddenCount = root.hiddenCount
        psbtImportDraftRevision = root.draftRevision
        psbtImportTicket = 0
        psbtImportTicket = root.wallet.requestImportPsbtFromFile(path)
        handlePsbtOutcome()
    }
    function handlePsbtOutcome() {
        if (!psbtImportTicket || !root.wallet || psbtImportWallet !== root.wallet || root.wallet.psbtActionId !== psbtImportTicket || root.wallet.psbtActionBusy) return
        psbtImportTicket = 0
        if (!root.visible || psbtImportHiddenCount !== root.hiddenCount || psbtImportDraftRevision !== root.draftRevision || root.wallet.psbtActionState === WalletQmlModel.PsbtStale) return
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
    property int preparationDraftRevision: -1
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
        } else if (root.wallet.transactionNeedsUnlock) {
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

    function openPsbtFileImport() {
        root.returnToSendForm()
        root.openWhileShown(sendPage.openPsbtFileImport)
    }

    // Counts edits to the payment draft. A confirmation applies only to the
    // draft it was shown for, so any edit closes it, including one from
    // outside the form such as File > Open URI.
    property int draftRevision: 0

    function draftChanged() {
        root.draftRevision++
        if (!sendPage.m_applyingUri && !(sendPage.m_uriPasteOwner && (sendPage.m_uriPasteOwner.literalActive || sendPage.m_uriPasteOwner.mutating)))
            sendPage.cancelUriIntents()
        root.preparingReview = false
        reviewSweepAlert.close()
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
        root.wallet.clearSelectedCoins()
        root.wallet.coinsListModel.update()
        root.manualCoinSelection = false
        root.clearPrepareTransactionError()
        sendPage.expandedRecipient = 0
        sendPage.paymentRequestStatus = ""
        sendPage.paymentRequestIsError = false
        sendPage.paymentRequestMessage = ""
        sendPage.m_pendingClipboardUri = ""
        sendPage.m_filledUri = ""
        sendPage.m_dismissedUri = ""
        sendPage.showClipboardUriBanner = false
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
        sendPage.paymentRequestStatus = ""
        sendPage.paymentRequestIsError = false
        sendPage.paymentRequestMessage = ""
        sendPage.showClipboardUriBanner = false
        sendPage.m_pendingClipboardUri = ""
        sendPage.m_filledUri = ""
        sendPage.m_dismissedUri = ""
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
        function onCountChanged() {
            root.draftChanged()
            root.clearPrepareTransactionError()
            root.scheduleFeeEstimates()
        }
        function onTotalAmountChanged() {
            // With Use maximum the amount follows fee estimates; only a
            // fixed amount changing is an edit. Replacing the maximum
            // amount clears the maximum recipient, handled below.
            if (root.wallet && !root.wallet.maximumRecipient) root.draftChanged()
        }
        function onCurrentRecipientChanged() {
            root.draftChanged()
            root.clearPrepareTransactionError()
            root.scheduleFeeEstimates()
            sendPage.paymentRequestStatus = ""
            sendPage.paymentRequestIsError = false
            sendPage.paymentRequestMessage = ""
            sendPage.expandedRecipient = root.wallet.recipients.currentIndex - 1
            sendPage.checkClipboard()
            sendPage.clearPendingPaymentUriPaste()
            paymentUriOverwritePopup.close()
        }
    }

    Connections {
        target: root.recipient ? root.recipient.address : null
        function onFormattedAddressChanged() {
            root.draftChanged()
        }
    }

    Connections {
        target: root.wallet ? root.wallet.coinsListModel : null
        function onSelectedCoinsCountChanged() {
            root.draftChanged()
            root.clearPrepareTransactionError()
            root.scheduleFeeEstimates()
        }
    }

    Connections {
        target: root.wallet
        function onCurrentTransactionChanged() { reviewSweepAlert.close() }
        function onPreparationChanged() { root.handlePreparationOutcome() }
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
            psbtOpenDialog.targetWallet = root.wallet
            psbtOpenDialog.open()
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
                enabled: Theme.animationsEnabled
                NumberAnimation {
                    property: "verticalOffset"
                    from: transactionReviewPopup.height
                    to: 0
                    duration: 350
                    easing.type: Easing.OutCubic
                }
            }
            exit: Transition {
                enabled: Theme.animationsEnabled
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
                        onTransactionSent: (txid, submissionState) => {
                            transactionReviewPopup.sent = true
                            reviewStack.push(sendCompletePage, {
                                "txid": txid,
                                "submissionState": submissionState,
                                "targetBlocks": !transactionReviewPopup.importedReview && wallet && !wallet.customFeeEnabled
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
            property WalletQmlModel targetWallet: null
            onAccepted: {
                const wallet = targetWallet
                targetWallet = null
                if (wallet && wallet === root.wallet) {
                    root.requestPsbtImport(selectedFile.toString())
                }
            }
            onRejected: targetWallet = null
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
        property string paymentRequestStatus: ""
        property bool paymentRequestIsError: false
        property string paymentRequestMessage: ""

        // Native clipboard capture and immutable Domain probe outcomes belong
        // to the original wallet, recipient, draft and editor selection.
        property bool showClipboardUriBanner: false
        property string m_pendingClipboardUri: ""
        property double m_pendingClipboardCounter: -1
        property string m_filledUri: ""
        property string m_dismissedUri: ""
        property BitcoinUriParseOperation m_uriOperation: null
        property BitcoinUriParseOperation m_clipboardOperation: null
        property var m_uriHandled: null
        property var m_clipboardHandled: null
        property WalletQmlModel m_uriWallet: null
        property SendRecipient m_uriRecipient: null
        property SendRecipient m_uriCurrentRecipient: null
        property CapturedPaste m_uriPasteOwner: null
        property bool m_uriHadPasteOwner: false
        property var m_uriPayload: null
        property double m_uriRecipientRevision: -1
        property int m_uriRevision: -1
        property int m_uriHidden: -1
        property string m_uriText: ""
        property string m_uriSource: ""
        property string m_uriField: ""
        property string m_uriKind: ""
        property bool m_uriApplying: false
        property WalletQmlModel m_clipboardWallet: null
        property SendRecipient m_clipboardRecipient: null
        property int m_clipboardRevision: -1
        property int m_clipboardHidden: -1
        property double m_clipboardCounter: -1
        property string m_clipboardText: ""
        property bool m_applyingUri: false
        property var m_pendingPastedPaymentRequest: null
        property string m_pendingPastedPaymentRequestText: ""
        readonly property bool uriActionPending: m_uriApplying
            || (m_uriOperation !== null && m_uriHandled !== m_uriOperation)
            || m_pendingPastedPaymentRequest !== null
            || (m_uriPasteOwner !== null && m_uriPasteOwner.pending)

        Connections {
            target: sendPage.m_uriOperation
            function onFinished() { sendPage.finishUriAction() }
        }
        Connections {
            target: sendPage.m_clipboardOperation
            function onFinished() { sendPage.finishClipboardCheck() }
        }
        function probeContext(recipient, field, payload) {
            return {address: recipient ? recipient.address.address : "", amount: recipient ? recipient.amount.satoshi : 0,
                note: recipient ? recipient.label : "", requestLabel: recipient ? recipient.paymentRequestLabel : "",
                sourceField: field || "", edited: !!(payload && payload.edited),
                previousText: payload && payload.edited ? payload.previous : "",
                currentText: payload && payload.edited ? payload.current : ""}
        }
        function finishPasteOwner(cancel) {
            if (!m_uriPasteOwner || !m_uriPayload) return
            if (cancel && m_uriPasteOwner.intent === m_uriPayload) m_uriPasteOwner.cancel()
            else m_uriPasteOwner.complete(m_uriPayload)
        }
        function clearPendingPaymentUriPaste() {
            m_pendingPastedPaymentRequest = null
            m_pendingPastedPaymentRequestText = ""
        }
        function cancelUriAction() {
            if (m_uriOperation) m_uriOperation.cancel()
            m_uriOperation = null
            if (m_uriPasteOwner && m_uriPayload && m_uriPasteOwner.intent === m_uriPayload) m_uriPasteOwner.cancel()
            m_uriPasteOwner = null
            m_uriPayload = null
            m_uriHadPasteOwner = false
            clearPendingPaymentUriPaste()
            paymentUriOverwritePopup.close()
        }
        function cancelUriIntents() {
            cancelUriAction()
            if (m_clipboardOperation) m_clipboardOperation.cancel()
            m_clipboardOperation = null
            showClipboardUriBanner = false
        }
        function uriContextValid() {
            return root.visible && m_uriWallet && m_uriWallet.available && root.wallet === m_uriWallet
                && m_uriRecipient && m_uriRecipient.validationRevision === m_uriRecipientRevision && root.recipient === m_uriCurrentRecipient
                && root.draftRevision === m_uriRevision && root.hiddenCount === m_uriHidden
                && (!m_uriHadPasteOwner || (m_uriPasteOwner && m_uriPasteOwner.owns(m_uriPayload)))
        }
        function requestUriAction(text, kind, source, field, payload, recipient) {
            cancelUriAction()
            if (!root.visible || !root.wallet || !root.wallet.available || !(recipient || root.recipient)) {
                if (payload && payload.origin) payload.origin.cancel()
                return
            }
            m_uriWallet = root.wallet
            m_uriCurrentRecipient = root.recipient
            m_uriRecipient = recipient || root.recipient
            m_uriRecipientRevision = m_uriRecipient.validationRevision
            m_uriRevision = root.draftRevision
            m_uriHidden = root.hiddenCount
            m_uriText = text
            m_uriKind = kind
            m_uriSource = source
            m_uriField = field || ""
            m_uriPayload = payload || null
            m_uriPasteOwner = payload ? payload.origin : null
            m_uriHadPasteOwner = m_uriPasteOwner !== null
            m_uriHandled = null
            m_uriOperation = kind === "file" ? BitcoinUri.requestParseFile(text, sendPage)
                : BitcoinUri.requestProbe(text, probeContext(m_uriRecipient, field, payload), sendPage)
            if (!m_uriOperation) {
                paymentRequestStatus = qsTr("Payment URI services are unavailable. Try again.")
                paymentRequestIsError = true
                finishPasteOwner(true)
                return
            }
            finishUriAction()
        }
        function finishUriAction() {
            const operation = m_uriOperation
            if (!operation || operation.pending || m_uriHandled === operation) return
            m_uriApplying = true
            m_uriHandled = operation
            try {
                if (!uriContextValid()) { cancelUriAction(); return }
                const result = operation.result
                if (m_uriKind === "paste") {
                    if (!result.probeSucceeded) {
                        paymentRequestStatus = result.error
                        paymentRequestIsError = true
                        finishPasteOwner(true)
                        return
                    }
                    const nativeEdit = m_uriPayload && m_uriPayload.edited
                    if (!result.isPaymentUri || (nativeEdit && !result.pasteProvenance)) {
                        if (m_uriPasteOwner) {
                            if (nativeEdit && m_uriField === "amount" && result.containsPaymentUri) {
                                m_uriPasteOwner.restore(m_uriPayload)
                                m_uriPasteOwner.complete(m_uriPayload)
                            } else m_uriPasteOwner.apply(m_uriPayload)
                        }
                        return
                    }
                    if (m_uriPasteOwner) m_uriPasteOwner.restore(m_uriPayload)
                    if (result.success && result.conflicts) {
                        m_pendingPastedPaymentRequest = result
                        m_pendingPastedPaymentRequestText = m_uriText
                        paymentUriOverwritePopup.open()
                    } else {
                        applyPastedPaymentRequest(result, m_uriText)
                        finishPasteOwner(false)
                    }
                } else applyParsedPaymentRequest(result, m_uriSource, m_uriRecipient)
            } finally { m_uriApplying = false }
        }
        function applyPastedPaymentRequest(result, text) {
            applyParsedPaymentRequest(result, qsTr("clipboard"), m_uriRecipient)
            if (result.success) {
                m_filledUri = result.sourceId
                showClipboardUriBanner = false
            }
        }
        function handlePaymentUriPaste(text, sourceField) {
            requestUriAction(text, "paste", qsTr("clipboard"), sourceField, null, root.recipient)
        }
        function handleClipboardPaste(field, editor, payload) {
            if (!payload) {
                const text = Clipboard.text()
                const component = !editor ? null : field === "address" ? editor.addressField : field === "amount" ? editor.amountField : editor.noteField
                payload = component ? component.capturePaste(text, false) : {text: text, origin: null, edited: false}
            }
            if (!payload || !payload.text.length) {
                if (payload && payload.origin) payload.origin.complete(payload)
                return
            }
            requestUriAction(payload.text, "paste", qsTr("clipboard"), field, payload, editor ? editor.recipient : root.recipient)
        }
        function checkClipboard() {
            if (!root.visible || !root.wallet || !root.wallet.available || !root.recipient) {
                if (m_clipboardOperation) m_clipboardOperation.cancel()
                m_clipboardOperation = null
                showClipboardUriBanner = false
                return
            }
            m_clipboardWallet = root.wallet
            m_clipboardRecipient = root.recipient
            m_clipboardRevision = root.draftRevision
            m_clipboardHidden = root.hiddenCount
            m_clipboardText = Clipboard.text()
            m_clipboardCounter = Clipboard.revision
            m_clipboardHandled = null
            m_clipboardOperation = BitcoinUri.requestProbe(m_clipboardText, probeContext(root.recipient, "", null), sendPage, m_clipboardOperation)
            finishClipboardCheck()
        }
        function finishClipboardCheck() {
            const operation = m_clipboardOperation
            if (!operation || operation.pending || m_clipboardHandled === operation) return
            m_clipboardHandled = operation
            if (!root.visible || !m_clipboardWallet || !m_clipboardWallet.available || root.wallet !== m_clipboardWallet
                    || root.recipient !== m_clipboardRecipient || root.draftRevision !== m_clipboardRevision
                    || root.hiddenCount !== m_clipboardHidden || Clipboard.revision !== m_clipboardCounter) return
            const parsed = operation.result
            if (!parsed.success || parsed.sourceId === m_dismissedUri) {
                showClipboardUriBanner = false
                m_pendingClipboardUri = ""
                return
            }
            if (parsed.sourceId === m_filledUri && parsed.matchesExistingRequest) {
                showClipboardUriBanner = false
                return
            }
            m_pendingClipboardUri = m_clipboardText
            m_pendingClipboardCounter = m_clipboardCounter
            showClipboardUriBanner = true
        }

        // Apply a pre-parsed URI result to the current recipient form fields.
        function applyParsedPaymentRequest(result, source, recipient) {
            if (!result.success) {
                paymentRequestStatus = result.error
                paymentRequestIsError = true
                return
            }
            const wallet = m_uriWallet
            const target = recipient || root.recipient
            if (!root.visible || !wallet || !wallet.available || root.wallet !== wallet || !target) return
            m_applyingUri = true
            try {
                sendPage.expandedRecipient = wallet.recipients.currentIndex - 1
                target.applyParsedPaymentRequest(result)
                if (amountInput) amountInput.syncFromAmount(true)
            } finally { m_applyingUri = false }
            if (!root.visible || !wallet.available || root.wallet !== wallet) return
            root.scheduleFeeEstimates()
            paymentRequestMessage = result.hasMessage ? result.uriMessage : ""
            paymentRequestStatus = qsTr("Payment request imported from %1").arg(source)
            paymentRequestIsError = false
            m_filledUri = result.sourceId || ""
            checkClipboard()
        }

        // Parse a URI from text and apply to form.
        function applyPaymentRequestFromText(text, source) {
            requestUriAction(text, "text", source, "")
        }

        function applyPaymentRequestFromFile(path) {
            requestUriAction(path, "file", qsTr("file"), "")
        }

        AlertPopup {
            id: paymentUriOverwritePopup
            objectName: "sendPaymentUriOverwritePopup"
            parent: Overlay.overlay
            title: qsTr("Confirm paste?")
            message: qsTr("The payment request from the clipboard contains information that differs from the currently populated values. Pasting will replace the values.")
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
                text: qsTr("Paste")
                buttonObjectName: "sendPaymentUriOverwriteConfirmButton"
                closesPopup: false
                onTriggered: {
                    if (!sendPage.uriContextValid()) { sendPage.cancelUriAction(); return }
                    const result = sendPage.m_pendingPastedPaymentRequest
                    const text = sendPage.m_pendingPastedPaymentRequestText
                    sendPage.m_uriApplying = true
                    sendPage.clearPendingPaymentUriPaste()
                    paymentUriOverwritePopup.close()
                    try {
                        if (result) sendPage.applyPastedPaymentRequest(result, text)
                        sendPage.finishPasteOwner(false)
                    } finally { sendPage.m_uriApplying = false }
                }
            }
        }

        Connections {
            target: Clipboard
            function onDataChanged() { sendPage.checkClipboard() }
        }

        // Re-check the clipboard whenever any form field changes due to user
        // input. checkClipboard() compares current values against the filled URI
        // and re-shows the banner if any specified field has diverged.
        // The m_applyingUri guard prevents these from firing during a programmatic
        // fill, which would trigger a re-check before m_filledUri is set.
        Connections {
            target: root.recipient.address
            function onAddressChanged() {
                if (!sendPage.m_applyingUri) {
                    if (root.recipient.address.address === "") {
                        sendPage.paymentRequestStatus = ""
                        sendPage.paymentRequestIsError = false
                        sendPage.paymentRequestMessage = ""
                    }
                    sendPage.checkClipboard()
                }
            }
        }
        Connections {
            target: root.recipient.amount
            function onInputChanged() { root.draftChanged() }
            function onAmountChanged() {
                if (!sendPage.m_applyingUri) sendPage.checkClipboard()
            }
        }
        Connections {
            target: root.recipient
            function onLabelChanged() {
                root.draftChanged()
                if (!sendPage.m_applyingUri) sendPage.checkClipboard()
            }
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
                if (drop.hasUrls && drop.urls.length > 0) {
                    const url = drop.urls[0].toString()
                    if (url.startsWith("file://")) {
                        // Pass the raw file:// URL; C++ uses QUrl::toLocalFile()
                        // to derive the correct local path on all platforms.
                        sendPage.applyPaymentRequestFromFile(url)
                    } else {
                        sendPage.applyPaymentRequestFromText(url, qsTr("drag and drop"))
                    }
                } else if (drop.hasText) {
                    sendPage.applyPaymentRequestFromText(drop.text, qsTr("drag and drop"))
                }
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
                                visible: sendPage.paymentRequestIsError && sendPage.paymentRequestStatus.length > 0
                                Layout.leftMargin: 16; Layout.rightMargin: 16
                                Layout.fillWidth: true; text: sendPage.paymentRequestStatus; horizontalAlignment: Text.AlignLeft; color: Theme.color.red; font: Theme.text.description.font
                            }

                            NeutralButton {
                                objectName: "sendAddRecipientButton"
                                Layout.fillWidth: true
                                text: qsTr("+ Add another")
                                enabled: root.wallet.recipients.count < 25
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
                                            root.manualCoinSelection = index === 1
                                            if (index === 0) {
                                                if (root.selectedInputsActive) {
                                                    root.wallet.clearSelectedCoins()
                                                    root.wallet.coinsListModel.update()
                                                    root.scheduleFeeEstimates()
                                                }
                                            }
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
                                    text: root.selectedInputsActive ? (root.wallet.coinsListModel.selectedCoinsCount === 1 ? qsTr("1 input selected") : qsTr("%1 inputs selected").arg(root.wallet.coinsListModel.selectedCoinsCount))
                                        : qsTr("%1 inputs selected automatically").arg(root.wallet.estimatedInputCount)
                                }
                                OutlineButton {
                                    objectName: "sendUseAutomaticInputsButton"
                                    visible: false
                                    text: qsTr("Use automatic selection")
                                    onClicked: { root.wallet.clearSelectedCoins(); root.wallet.coinsListModel.update(); root.scheduleFeeEstimates() }
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
                                enabled: !sendPage.uriActionPending && root.wallet && root.wallet.available && root.wallet.recipients.allValid && !root.wallet.feeEstimatePending && !root.wallet.preparationBusy && !root.wallet.customFeeTargetPending && !root.wallet.coinSelectionPending
                                    && !root.wallet.sendAmountExhaustsBalance && (!root.wallet.customFeeEnabled || root.wallet.customFeeRateValid)
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
        property int draftRevision: -1
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
        property int actionDraftRevision: -1
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
