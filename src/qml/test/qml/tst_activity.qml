// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtTest 1.2
import "../../qml/pages/wallet"

TestCase {
    name: "Activity"
    when: windowShown
    width: 900
    height: 700

    function init() {
        testPaymentRequest.clear()
        testWalletModel.lastLoadedPaymentRequestId = ""
        testWalletModel.lastLoadedPaymentRequestDetailId = ""
        walletController.openReceiveRequests = 0
    }

    Component {
        id: activityComponent

        Activity {
            width: 600
            height: 700
        }
    }

    Component {
        id: activityDetailsComponent

        ActivityDetails {
            width: 600
            height: 700
        }
    }

    function detailsProperties(txid, paymentRequests) {
        return {
            txid: txid,
            canBump: false,
            amount: "+0.01000000 BTC",
            date: "2026-01-01",
            depth: 3,
            status: 2,
            type: 1,
            address: "bcrt1qreceiveaddress",
            paymentRequests: paymentRequests || []
        }
    }

    function test_selectedWalletChanged_pops_to_root() {
        const page = createTemporaryObject(activityComponent, this)
        verify(page !== null)
        compare(page.depth, 1)

        page.push(activityDetailsComponent, detailsProperties("tx-1"))
        tryCompare(page, "depth", 2)

        page.push(activityDetailsComponent, detailsProperties("tx-2"))
        tryCompare(page, "depth", 3)

        walletController.setSelectedWallet("another-wallet")
        tryCompare(page, "depth", 1)
    }

    function test_editPaymentRequest_preserves_activity_context_until_wallet_switch() {
        const page = createTemporaryObject(activityComponent, this)
        verify(page !== null)
        compare(page.depth, 1)

        page.push(activityDetailsComponent, detailsProperties("tx-1", [
            {
                requestId: "req-1",
                label: "Alice",
                amountDisplay: "0.00100000 BTC",
                date: "Fri Jan 2 2026"
            }
        ]))
        tryCompare(page, "depth", 2)

        const detailsPage = page.currentItem
        verify(detailsPage !== null)

        tryVerify(function() {
            return findChild(detailsPage, "activityDetailsPaymentRequest_0") !== null
        })

        const requestRow = findChild(detailsPage, "activityDetailsPaymentRequest_0")
        verify(requestRow !== null)
        requestRow.clicked()

        tryCompare(page, "depth", 3)
        compare(page.currentItem.objectName, "paymentRequestDetailPage")
        compare(testWalletModel.lastLoadedPaymentRequestDetailId, "req-1")
        compare(testPaymentRequest.id, "req-1")
        compare(testPaymentRequest.isEditing, false)

        const editButton = findChild(page.currentItem, "paymentRequestDetailEdit")
        verify(editButton !== null)
        editButton.clicked()

        compare(testWalletModel.lastLoadedPaymentRequestId, "req-1")
        compare(walletController.openReceiveRequests, 1)
        compare(testPaymentRequest.isEditing, true)
        compare(page.depth, 3)
        compare(page.currentItem.objectName, "paymentRequestDetailPage")

        walletController.setSelectedWallet("third-wallet")
        tryCompare(page, "depth", 1)
    }
}
