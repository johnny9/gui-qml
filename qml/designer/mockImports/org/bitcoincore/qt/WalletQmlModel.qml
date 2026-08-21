// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQml 2.15
import QtQml.Models 2.15

QtObject {
    id: root
    enum KeyScheme { SingleKey, WatchOnly, MultiKey, ExternalSigner }
    enum PsbtImportResult { PsbtUnsupported, WalletCanSign, WalletCannotSign, TransactionAlreadyKnown }

    property string name: "designer-wallet"
    property string displayName: qsTr("Designer Wallet")
    property string balance: "0.01250000 BTC"
    property double balanceSatoshi: 1250000
    property bool hasExternalSigner: false
    property ListModel activityListModel: ListModel {}
    property AddressListModel addressListModel: AddressListModel {}
    property ListModel coinsListModel: ListModel {}
    property QtObject recipients: QtObject {
        property int count: 1
        property double totalAmountSatoshi: 50000
        property SendRecipient current: SendRecipient {}
        function add() {}
        function remove() {}
        function clear() {}
    }
    property QtObject signVerifyMessageModel: QtObject {
        property string address: ""
        property string message: ""
        property string signature: ""
        property string error: ""
        property bool result: false
        function signMessage() { return true }
        function verifyMessage() { return true }
        function clear() {}
    }
    property PaymentRequest currentPaymentRequest: PaymentRequest {}
    property PaymentRequest detailPaymentRequest: PaymentRequest { isEditing: false }
    property ListModel receiveRequests: ListModel {}
    property WalletQmlModelTransaction currentTransaction: WalletQmlModelTransaction {}
    property int targetBlocks: 6
    property string estimatedFee: "0.00001000 BTC"
    property bool sendAmountExhaustsBalance: false
    property bool customFeeEnabled: false
    property string customFeeRate: "2.0"
    property bool customFeeRateValid: true
    property bool feeEstimatePending: false
    property int feeEstimateRevision: 0
    property BumpTransactionModel bumpModel: BumpTransactionModel {}
    property bool isWalletLoaded: true
    property int displayUnit: BitcoinAmount.BTC
    property bool isEncrypted: true
    property bool isLocked: false
    property string keyScheme: qsTr("Single-key")
    property int keySchemeKind: WalletQmlModel.SingleKey
    property string privateKeysStatus: qsTr("Enabled")
    property string externalSignerStatus: qsTr("Not configured")
    property bool canManagePassphrase: true
    property string transactionError: ""
    property bool transactionNeedsUnlock: false
    property bool currentTransactionCanSend: true
    property bool currentTransactionCanBroadcast: true
    property string currentTransactionReviewMessage: ""
    property string settingsError: ""
    property QtObject importedPsbt: QtObject {
        property string matchedTxid: ""
        property string error: ""
    }

    function commitPaymentRequest() { return true }
    function commitPaymentRequestWithPassphrase() { return true }
    function reloadReceiveRequests() {}
    function removeReceiveRequest() { return true }
    function loadPaymentRequest() { return true }
    function loadPaymentRequestDetail() { return true }
    function usePaymentRequestAsTemplate() {}
    function prepareTransaction() { return true }
    function approveExternalSignerTransaction() {}
    function prepareTransactionWithPassphrase() { return true }
    function sendTransaction() { return true }
    function sendTransactionWithPassphrase() { return true }
    function broadcastCurrentTransaction() { return true }
    function availableReceiveAddressTypes() { return ["bech32", "bech32m"] }
    function defaultReceiveAddressType() { return "bech32" }
    function estimatedFeeForTarget() { return estimatedFee }
    function feeTargetIndex() { return 0 }
    function scheduleFeeEstimates() {}
    function setCurrentPaymentRequestAddress(value) {
        currentPaymentRequest.address = value
        return true
    }
    function encryptWallet() { isEncrypted = true; return true }
    function changeWalletPassphrase() { return true }
    function backupWallet() { return true }
    function clearSettingsError() { settingsError = "" }
    function setDefaultReceiveAddressType() {}
    function receiveAddressTypeLabel(value) { return value }
    function importPsbtFromFile() { return WalletQmlModel.WalletCanSign }
    function saveCurrentTransactionAsPsbt() { return "" }
    function discardCurrentTransaction() {}
}
// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
