// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import org.bitcoincore.qt 1.0
import "../../controls"
import "../../components"

PageStack {
    id: stackView
    objectName: "activityStack"
    property var wallet: null

    replaceEnter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 250; easing.type: Easing.OutCubic }
    }
    replaceExit: Transition {
        NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 250; easing.type: Easing.OutCubic }
    }

    function openPaymentRequest(requestId) {
        stackView.pop(null)
        paymentRequestModal.openRequest(requestId)
    }

    function navigateToTransaction(txid, outputIndex) {
        if (!wallet || !wallet.available || !txid) return
        const details = { transactionId: txid, wallet: stackView.wallet,
            outputIndex: outputIndex === undefined ? -1 : outputIndex }
        let page
        if (stackView.currentItem && stackView.currentItem.objectName === "activityDetailsPage")
            page = stackView.replace(stackView.currentItem, "TransactionDetail.qml", details, StackView.ReplaceTransition)
        else {
            stackView.pop(null)
            page = stackView.push("TransactionDetail.qml", details)
        }
        page.showTransaction.connect(stackView.navigateToTransaction)
    }

    onWalletChanged: { paymentRequestModal.close(); stackView.pop(null) }

    Connections {
        target: ApplicationModels.walletController
        function onClosePaymentRequestDetailRequested(wallet) {
            if (wallet === stackView.wallet) paymentRequestModal.close()
        }
    }
    Binding {
        target: stackView.wallet
        property: "displayUnit"
        value: ApplicationModels.optionsModel.displayUnit
        when: stackView.wallet !== null
    }

    PaymentRequestModal { id: paymentRequestModal; wallet: stackView.wallet }

    initialItem: ActivityListPage {
        wallet: stackView.wallet
        onTransactionRequested: function(txid) { stackView.navigateToTransaction(txid) }
        onPaymentRequestRequested: function(requestId) {
            paymentRequestModal.openRequest(requestId)
        }
    }
}
