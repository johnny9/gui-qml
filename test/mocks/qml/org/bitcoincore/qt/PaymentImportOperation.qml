// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
import QtQuick 2.15

// Presentation fixture only. Native/domain ownership, exact receipts and real
// Wallet adapters are exercised with the shared manual executors in C++.
QtObject {
    id: operation
    enum State { Empty, Reading, Analyzing, Inspecting, AwaitingReplacement, Applying, CreatingReview, Reconciling, Applied, ReviewOpened, Failed, Closing, Closed, AwaitingNativeHandoff }
    enum SourceKind { UnknownSource, PaymentSource, PsbtSource }
    enum NativeDisposition { NoHandoff, LiteralPaste, RestoreOnly, RestoreImport }
    property int sourceKind: _candidate.psbt ? PaymentImportOperation.PsbtSource : _candidate.success ? PaymentImportOperation.PaymentSource : PaymentImportOperation.UnknownSource
    property int nativeDisposition: _candidate.nativeHandoff || PaymentImportOperation.NoHandoff
    property string nativeHandoffToken: ""
    readonly property string paymentIdentity: JSON.stringify([address, hasAmount ? amount : null, publicLabel, message])
    readonly property bool matchesExistingRequest: !!originalRecipient && originalRecipient.address.address === address
        && (!hasAmount || (originalRecipient.amountMode === 0 && originalRecipient.amount.satoshi === amount))
        && originalRecipient.paymentRequestLabel === publicLabel && originalRecipient.message === message
    property string selectedPath: ""
    property bool _inputStarted: true
    property int requestId: 0
    property var requestOwner: null
    property var originalWallet: null
    property var originalRecipient: null
    property string originalRevision: ""
    property int state: PaymentImportOperation.Reading
    property bool pending: true
    property bool canApply: false
    property bool canReview: false
    property bool canExport: false
    property bool canReconcile: false
    property int promptToken: 0
    property int resultId: 0
    readonly property string resultToken: resultId ? requestId + ":" + resultId : ""
    property string error: ""
    property string address: ""
    property string publicLabel: ""
    property string message: ""
    property bool hasAmount: false
    property double amount: 0
    property var _candidate: ({})
    property bool _closed: false
    property bool _requested: false
    property bool _ready: false
    signal changed()
    function prepare(outcome, held) { _candidate = outcome; if (!held) Qt.callLater(operation.retryPublication) }
    function retryPublication() {
        if (_closed || _ready) return
        _ready = true
        if (nativeDisposition !== PaymentImportOperation.NoHandoff) {
            nativeHandoffToken = requestId + ":1"
            state = PaymentImportOperation.AwaitingNativeHandoff
            changed()
            return
        }
        finishInspection()
    }
    function finishInspection() {
        pending = false
        address = _candidate.address || ""
        publicLabel = _candidate.label || ""
        message = _candidate.uriMessage || ""
        hasAmount = !!_candidate.hasAmount
        amount = _candidate.amountSats || 0
        error = _candidate.success ? "" : _candidate.error
        state = _candidate.success ? PaymentImportOperation.Inspecting : PaymentImportOperation.Failed
        canApply = _candidate.success && !_candidate.psbt && !!originalWallet && originalWallet.available
        canReview = !!_candidate.psbt && !!originalWallet && originalWallet.available
        if (_requested) use()
        changed()
    }
    function chooseTarget(wallet, recipient) { if (!_closed) { originalWallet = wallet; originalRecipient = recipient; originalRevision = wallet ? wallet.recipients.revision : "" } }
    function use() { _requested = true; if (_ready && !nativeHandoffToken) { if (canApply) apply(); else if (canReview) openReview() } }
    function completeNativeHandoff(token) {
        if (_closed || !token || token !== nativeHandoffToken) return
        nativeHandoffToken = ""
        if (nativeDisposition !== PaymentImportOperation.RestoreImport && !_candidate.error) { cancel(); return }
        finishInspection()
    }
    function requestFile(path) {
        if (_closed || _inputStarted) return false
        _inputStarted = true; selectedPath = path
        state = PaymentImportOperation.Reading; pending = true
        prepare({success: true, psbt: true}, BitcoinUri.holdPublications)
        if (BitcoinUri.holdPublications) BitcoinUri._heldOperations.push(operation)
        return true
    }
    function apply() {
        if (_closed || !canApply || !originalWallet || !originalWallet.available || !originalRecipient) return
        if (originalWallet.recipients.revision !== originalRevision) { cancel(); return }
        const conflicts = (_candidate.field !== BitcoinUri.AddressField && originalRecipient.address.address.length > 0 && originalRecipient.address.address !== address)
            || (hasAmount && (originalRecipient.amountMode === 1 || (_candidate.field !== BitcoinUri.AmountField && originalRecipient.amount.satoshi > 0 && originalRecipient.amount.satoshi !== amount)))
        if (conflicts) { promptToken = requestId; state = PaymentImportOperation.AwaitingReplacement; changed(); return }
        _apply()
    }
    function replace(token) { if (!_closed && token === promptToken && originalWallet && originalWallet.available) { promptToken = 0; _apply() } }
    function keepDraft(token) { if (token === promptToken) { promptToken = 0; state = PaymentImportOperation.Inspecting; changed() } }
    function _apply() {
        canApply = false
        state = PaymentImportOperation.Applying
        const publicFields = Object.assign({}, _candidate, {privateNote: ""})
        originalWallet.recipients.importPending = true
        try {
            originalRecipient.applyParsedPaymentRequest(publicFields, false)
            if (_closed) return
            if (hasAmount) originalWallet.clearMaximum()
            originalWallet.scheduleFeeEstimates()
        } finally { originalWallet.recipients.importPending = false }
        resultId = requestId
        state = PaymentImportOperation.Applied
        changed()
    }
    function openReview() {
        if (_closed || !canReview || !originalWallet || !originalWallet.available || originalWallet.recipients.revision !== originalRevision) return
        canReview = false; resultId = requestId
        originalWallet.adoptImportReviewForTest(requestId, resultId)
        state = PaymentImportOperation.ReviewOpened
        changed()
    }
    function reconcile() {}
    function cancel() { _closed = true; pending = false; canApply = false; promptToken = 0; state = PaymentImportOperation.Closed; changed() }
}
