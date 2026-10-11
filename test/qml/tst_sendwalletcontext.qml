// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Window 2.15
import QtQuick.Dialogs
import QtTest 1.2
import org.bitcoincore.qt 1.0
import "../../qml/pages/wallet"
import "../../qml/components"

TestCase {
    id: testCase
    name: "SendWalletContext"
    when: windowShown
    width: 1000
    height: 800

    Component { id: sendFactory; SendCreate {} }
    Component { id: coinsFactory; SendCoinSelection {} }
    Component { id: feeFactory; SendFeeSelection { width: 800 } }

    function resetWallet(wallet, recipient, recipients, coins) {
        wallet.sendDraftSweepsWallet = false
        wallet.currentTransactionSweepsWallet = false
        wallet.clearMaximum()
        wallet.sendTransactionResult = true
        wallet.prepareTransactionResult = true
        wallet.feeEstimatePending = false
        wallet.customFeeEnabled = false
        wallet.customFeeRate = ""
        wallet.targetBlocks = 6
        wallet.sendAmountExhaustsBalance = false
        wallet.currentTransactionCanSend = true
        wallet.currentTransactionCanBroadcast = false
        wallet.currentTransactionIsImportedPsbt = false
        wallet.currentTransactionReviewMessage = ""
        wallet.lastSavedPsbtPath = ""
        wallet.hasExternalSigner = false
        recipient.address.setAddress("bcrt1qsendtoaddress")
        recipient.addressError = ""
        recipient.amount.display = "0.00100000"
        recipient.amountError = ""
        recipient.label = ""
        recipient.hasPaymentRequest = false
        recipient.paymentRequestLabel = ""
        recipient.message = ""
        recipient.isValid = true
        recipients.allValid = true
        recipients.validationError = ""
        recipients.clearToFront()
        recipients.totalAmountSatoshi = 100000
        coins.reset()
    }

    function init() {
        walletController.setSelectedWalletObject(testWalletModel)
        resetWallet(testWalletModel, testSendRecipient, testRecipientsModel, testCoinsListModel)
        resetWallet(otherWalletModel, otherSendRecipient, otherRecipientsModel, otherCoinsListModel)
        optionsModel.displayUnit = BitcoinAmount.BTC
        Clipboard.currentText = ""
    }

    function makeSend() {
        const page = createTemporaryObject(sendFactory, testCase.Window.window.contentItem,
                                           { width: width, height: height, visible: true })
        verify(page !== null)
        return page
    }

    function roundTrip() {
        walletController.setSelectedWalletObject(otherWalletModel)
        walletController.setSelectedWalletObject(testWalletModel)
    }

    function openReview(page, fullyOpened) {
        page.openTransactionReview()
        const popup = findChild(page, "transactionReviewPopup")
        verify(popup !== null)
        if (fullyOpened) tryCompare(popup, "opened", true)
        return popup
    }

    function test_wallet_drafts_survive_context_reset() {
        const page = makeSend()
        testSendRecipient.label = "Wallet A draft"
        otherSendRecipient.label = "Wallet B draft"
        testWalletModel.customFeeEnabled = true
        testWalletModel.customFeeRate = "4.25"
        otherWalletModel.targetBlocks = 10
        testRecipientsModel.add()
        testRecipientsModel.add()
        otherRecipientsModel.add()
        testCoinsListModel.toggleCoinSelection(0)
        otherCoinsListModel.toggleCoinSelection(1)
        page.manualCoinSelection = true
        page.prepareTransactionErrorText = "Wallet A error"
        walletController.setSelectedWalletObject(otherWalletModel)
        compare(page.wallet, otherWalletModel)
        compare(page.recipient, otherSendRecipient)
        compare(findChild(page, "walletSendPage").expandedRecipient, 1)
        compare(page.formErrorText, "")
        compare(page.manualCoinSelection, false)
        compare(otherSendRecipient.label, "Wallet B draft")
        compare(otherWalletModel.targetBlocks, 10)
        compare(otherCoinsListModel.selectedCoinsCount, 1)
        walletController.setSelectedWalletObject(testWalletModel)
        compare(page.recipient, testSendRecipient)
        compare(findChild(page, "walletSendPage").expandedRecipient, 2)
        compare(testSendRecipient.label, "Wallet A draft")
        compare(testWalletModel.customFeeRate, "4.25")
        compare(testCoinsListModel.selectedCoinsCount, 1)
    }

    function test_options_menu_cannot_clear_or_import_after_switch() {
        const page = makeSend()
        const menu = findChild(page, "sendOptionsPopup")
        testRecipientsModel.add()
        otherRecipientsModel.add()
        menu.open()
        verify(menu.visible)
        roundTrip()
        findChild(menu, "sendClearFormButton").clicked()
        findChild(menu, "sendImportPsbtFromFileButton").clicked()
        findChild(menu, "sendOptionsOpenPaymentRequestButton").clicked()
        compare(testRecipientsModel.count, 2)
        compare(otherRecipientsModel.count, 2)
        compare(findChild(page, "sendPsbtOpenDialog").visible, false)
        compare(findChild(page, "sendUriImportPopup").visible, false)
        walletController.setSelectedWalletObject(otherWalletModel)
        menu.open()
        findChild(menu, "sendClearFormButton").clicked()
        compare(otherRecipientsModel.count, 1)
        compare(testRecipientsModel.count, 2)
    }

    function test_uri_dialog_round_trip_rejects_old_input() {
        const page = makeSend()
        page.openPaymentRequestImport()
        const popup = findChild(page, "sendUriImportPopup")
        const input = findChild(page, "sendUriImportInput")
        tryCompare(popup, "opened", true)
        input.text = "bitcoin:bcrt1qoldwallet?label=Old%20request"
        testSendRecipient.label = "A"
        otherSendRecipient.label = "B"
        roundTrip()
        compare(popup.ownerWallet, null)
        compare(input.text, "")
        popup.applyRequest()
        compare(testSendRecipient.label, "A")
        compare(otherSendRecipient.label, "B")
        tryCompare(popup, "visible", false)
    }

    function test_deferred_import_open_rejects_wallet_round_trip() {
        const page = makeSend()
        page.openPaymentRequestImport()
        roundTrip()
        wait(0)
        compare(findChild(page, "sendUriImportPopup").visible, false)
        page.openPsbtFileImport()
        roundTrip()
        wait(0)
        compare(findChild(page, "sendPsbtOpenDialog").visible, false)
    }

    function test_review_password_cleared_on_unload_and_replacement() {
        const page = makeSend()
        const popup = findChild(page, "reviewPassphrasePopup")
        const field = findChild(page, "reviewPassphraseField")
        popup.ownerWallet = testWalletModel
        popup.ownerRevision = page.walletRevision
        popup.open()
        field.text = "Wallet A password"
        const calls = testWalletModel.prepareTransactionCalls
        const otherCalls = otherWalletModel.prepareTransactionCalls
        walletController.setSelectedWalletObject(null)
        compare(page.wallet, null)
        compare(popup.ownerWallet, null)
        compare(field.text, "")
        walletController.setSelectedWalletObject(otherWalletModel)
        popup.submitted("Wallet A password")
        compare(testWalletModel.prepareTransactionCalls, calls)
        compare(otherWalletModel.prepareTransactionCalls, otherCalls)
        compare(findChild(page, "transactionReviewPopup").visible, false)
        compare(popup.busy, false)
        tryCompare(popup, "visible", false)
    }

    function test_psbt_file_acceptance_after_switch_is_ignored() {
        const page = makeSend()
        const dialog = findChild(page, "sendPsbtOpenDialog")
        dialog.options = FileDialog.DontUseNativeDialog
        page.openPsbtFileImport()
        tryCompare(dialog, "visible", true)
        compare(dialog.ownerWallet, testWalletModel)
        roundTrip()
        compare(dialog.ownerWallet, null)
        tryCompare(dialog, "visible", false)
        // The mock intentionally has no importPsbtFromFile: a stale callback
        // must return before any backend import, including after A -> B -> A.
        dialog.accepted()
        compare(findChild(page, "transactionReviewPopup").visible, false)
    }

    function test_review_entering_switch_invalidates_actions() {
        const page = makeSend()
        const popup = openReview(page, false)
        verify(popup.visible)
        compare(popup.opened, false)
        const review = findChild(popup, "sendTransactionReviewPage")
        const sendCalls = testWalletModel.sendTransactionCalls
        const broadcastCalls = testWalletModel.broadcastCurrentTransactionCalls
        roundTrip()
        compare(popup.acceptingActions, false)
        compare(popup.reviewWallet, null)
        review.commitSend()
        review.commitBroadcast()
        compare(testWalletModel.sendTransactionCalls, sendCalls)
        compare(testWalletModel.broadcastCurrentTransactionCalls, broadcastCalls)
        tryCompare(popup, "visible", false)
    }

    function test_review_closing_invalidates_actions_immediately() {
        const page = makeSend()
        const popup = openReview(page, true)
        const review = findChild(popup, "sendTransactionReviewPage")
        const calls = testWalletModel.sendTransactionCalls
        popup.close()
        verify(popup.visible)
        compare(popup.acceptingActions, false)
        review.commitSend()
        compare(testWalletModel.sendTransactionCalls, calls)
        tryCompare(popup, "visible", false)
    }

    function test_complete_cannot_clear_another_context_and_reopen_starts_at_review() {
        const page = makeSend()
        const popup = openReview(page, true)
        const review = findChild(popup, "sendTransactionReviewPage")
        const stack = findChild(popup, "sendTransactionReviewStack")
        review.commitSend()
        tryCompare(stack, "depth", 2)
        const complete = findChild(stack, "sendCompletePage")
        verify(complete !== null)
        testSendRecipient.label = "A retained draft"
        otherSendRecipient.label = "B retained draft"
        testRecipientsModel.add()
        otherRecipientsModel.add()
        testCoinsListModel.toggleCoinSelection(0)
        otherCoinsListModel.toggleCoinSelection(1)
        roundTrip()
        complete.done()
        complete.viewNewTransaction("old-transaction")
        compare(testSendRecipient.label, "A retained draft")
        compare(otherSendRecipient.label, "B retained draft")
        compare(testRecipientsModel.count, 2)
        compare(otherRecipientsModel.count, 2)
        compare(testCoinsListModel.selectedCoinsCount, 1)
        compare(otherCoinsListModel.selectedCoinsCount, 1)
        // Reopening before the exit finishes cancels onClosed. The old
        // Complete page must still be removed before displaying this review.
        page.openTransactionReview()
        compare(stack.depth, 1)
        tryCompare(popup, "opened", true)
        compare(stack.currentItem.objectName, "sendTransactionReviewPage")
    }

    function test_nested_save_dialog_closes_and_rejects_stale_acceptance() {
        const page = makeSend()
        const popup = openReview(page, true)
        const review = findChild(popup, "sendTransactionReviewPage")
        const dialog = findChild(review, "sendTransactionReviewSaveDialog")
        dialog.options = FileDialog.DontUseNativeDialog
        review.startSavePsbt()
        tryCompare(dialog, "visible", true)
        compare(dialog.ownerWallet, testWalletModel)
        roundTrip()
        compare(dialog.ownerWallet, null)
        compare(dialog.ownerTransaction, null)
        tryCompare(dialog, "visible", false)
        dialog.accepted()
        compare(testWalletModel.lastSavedPsbtPath, "")
        compare(otherWalletModel.lastSavedPsbtPath, "")
    }

    function test_nested_password_closes_and_rejects_stale_submission() {
        const page = makeSend()
        const popup = openReview(page, true)
        const review = findChild(popup, "sendTransactionReviewPage")
        const password = findChild(review, "sendPassphrasePopup")
        const field = findChild(review, "sendPassphraseField")
        password.ownerWallet = testWalletModel
        password.ownerTransaction = testWalletTransaction
        password.open()
        field.text = "Wallet A password"
        roundTrip()
        compare(password.ownerWallet, null)
        compare(password.ownerTransaction, null)
        compare(field.text, "")
        password.submitted("Wallet A password")
        compare(password.busy, false)
        tryCompare(password, "visible", false)
    }

    function test_fee_picker_rejects_stale_choice_after_switch_and_close() {
        const control = createTemporaryObject(feeFactory, testCase.Window.window.contentItem,
                                              { walletModel: testWalletModel })
        const picker = findChild(control, "feeSelectionPicker")
        picker.open()
        compare(picker.opened, true)
        control.walletModel = otherWalletModel
        control.walletModel = testWalletModel
        picker.itemAtIndex(0).clicked()
        compare(testWalletModel.targetBlocks, 6)
        compare(otherWalletModel.targetBlocks, 6)
        picker.open()
        picker.itemAtIndex(0).clicked()
        compare(testWalletModel.targetBlocks, 2)
        compare(otherWalletModel.targetBlocks, 6)
        picker.open()
        picker.close()
        picker.itemAtIndex(2).clicked()
        compare(testWalletModel.targetBlocks, 2)
    }

    function test_coin_selection_entering_switch_cancels_original_snapshot() {
        const popup = createTemporaryObject(coinsFactory, testCase.Window.window.contentItem,
                                            { wallet: testWalletModel })
        testCoinsListModel.toggleCoinSelection(0)
        otherCoinsListModel.toggleCoinSelection(1)
        popup.open()
        verify(popup.visible)
        compare(popup.opened, false)
        compare(popup.editingWallet, testWalletModel)
        compare(popup.editingCoins, testCoinsListModel)
        testCoinsListModel.toggleCoinSelection(1)
        compare(testCoinsListModel.selectedCoinsCount, 2)
        popup.wallet = otherWalletModel
        popup.wallet = testWalletModel
        compare(popup.editingWallet, null)
        compare(popup.editingCoins, null)
        compare(popup.acceptingSelection, false)
        compare(testCoinsListModel.selectedCoinsCount, 1)
        compare(otherCoinsListModel.selectedCoinsCount, 1)
        popup.applySelection()
        compare(testCoinsListModel.selectedCoinsCount, 1)
        tryCompare(popup, "visible", false)
        // A fresh session can commit only the selected wallet's coins.
        popup.wallet = otherWalletModel
        popup.open()
        tryCompare(popup, "opened", true)
        otherCoinsListModel.toggleCoinSelection(0)
        popup.applySelection()
        compare(otherCoinsListModel.selectedCoinsCount, 2)
        compare(testCoinsListModel.selectedCoinsCount, 1)
        tryCompare(popup, "visible", false)
    }
}
