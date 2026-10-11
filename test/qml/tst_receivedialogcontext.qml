// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtTest 1.2
import org.bitcoincore.qt 1.0
import "../../qml/components"
import "../../qml/pages/wallet"

TestCase {
    id: suite
    name: "ReceiveDialogContext"
    when: windowShown
    visible: true
    width: 900
    height: 900

    Component {
        id: walletComponent
        QtObject {
            property string name: ""
            property var receivingAddress: PaymentRequest {}
            property var currentPaymentRequest: PaymentRequest {}
            property var detailPaymentRequest: PaymentRequest {}
            property var receiveRequests: QtObject { property int count: 0 }
            property var receiveAddressTypes: [{ id: "bech32", label: "SegWit", description: "" }]
            property string requestedType: ""
            property bool receiveOperationPending: false
            property bool receiveRequestReconciliationPending: false
            property string receiveOperationError: ""
            property string submitted: ""
            property string removedRequestId: ""
            property string templateRequestId: ""
            signal receiveOperationFinished(string operation, bool success)
            function receiveAddressTypeLabel(type) { return type }
            function ensureReceivingAddress(next, type) {
                if (receiveOperationPending) return false
                receiveOperationPending = true
                requestedType = type || ""
                return true
            }
            function ensureReceivingAddressWithPassphrase(passphrase, next, type) {
                if (!ensureReceivingAddress(next, type)) return false
                submitted = passphrase
                return true
            }
            function finishAddress(success, needsUnlock) {
                receivingAddress.needsUnlock = needsUnlock
                receiveOperationError = success ? "" : "Unlock " + name
                receiveOperationPending = false
                receiveOperationFinished("address", success)
            }
            function loadPaymentRequestDetail(id) {
                detailPaymentRequest.clear()
                detailPaymentRequest.id = id
                detailPaymentRequest.label = name + " request " + id
                detailPaymentRequest.isEditing = false
                return true
            }
            function removeReceiveRequest(id) {
                if (receiveOperationPending) return false
                removedRequestId = id
                receiveOperationPending = true
                return true
            }
            function finishRemoval() {
                receiveOperationPending = false
                receiveOperationFinished("remove", true)
            }
            function commitReceivingPaymentRequest() {
                if (receiveOperationPending) return false
                receiveOperationPending = true
                return true
            }
            function finishSave(id, success) {
                if (success) currentPaymentRequest.id = id
                receiveOperationPending = false
                receiveOperationFinished("save", success)
            }
            function usePaymentRequestAsTemplate(id) { templateRequestId = id }
        }
    }
    Component { id: receivingComponent; ReceivingAddressCard { width: 480 } }
    Component { id: paymentComponent; PaymentRequestCard { width: 480; request: wallet ? wallet.currentPaymentRequest : null } }
    Component { id: modalComponent; PaymentRequestModal { reducedMotion: true } }
    Component { id: pageComponent; ReceivePage { width: 900; height: 900 } }

    function wallet(name) {
        const result = createTemporaryObject(walletComponent, suite, { name: name })
        verify(result)
        return result
    }
    function receivingCard(owner) {
        const result = createTemporaryObject(receivingComponent, suite, { wallet: owner })
        verify(result)
        return result
    }
    function requestModal(owner, requestId) {
        const result = createTemporaryObject(modalComponent, suite, { wallet: owner })
        verify(result)
        verify(result.openRequest(requestId))
        tryCompare(result, "opened", true)
        return result
    }
    function requestUnlock(card, owner) {
        verify(card.ensureAddress(false, ""))
        owner.finishAddress(false, true)
        const popup = findChild(card, "receivingAddressPassphrasePopup")
        tryCompare(popup, "opened", true)
        return popup
    }

    function test_password_form_closes_and_cannot_submit_to_another_wallet() {
        const first = wallet("A")
        const second = wallet("B")
        const card = receivingCard(first)
        const popup = requestUnlock(card, first)
        const field = findChild(card, "receivingAddressPassphraseField")
        field.text = "password for A"
        card.wallet = second
        tryCompare(popup, "visible", false)
        compare(field.text, "")
        popup.submitted("late password for A")
        compare(second.submitted, "")
        compare(first.submitted, "")
        compare(card.errorText, "")
        requestUnlock(card, second)
        compare(field.text, "")
        field.text = "password for B"
        popup.submitPassphrase()
        compare(second.submitted, "password for B")
        compare(first.submitted, "")
    }

    function test_old_address_completion_is_ignored_after_round_trip_or_reload() {
        const first = wallet("A")
        const second = wallet("B")
        const card = receivingCard(first)
        const popup = findChild(card, "receivingAddressPassphrasePopup")
        verify(card.ensureAddress(false, ""))
        card.wallet = second
        card.wallet = first
        first.finishAddress(false, true)
        compare(popup.visible, false)
        compare(card.errorText, "")
        verify(card.ensureAddress(false, ""))
        card.wallet = null
        const replacement = wallet("A")
        card.wallet = replacement
        first.finishAddress(false, true)
        compare(popup.visible, false)
        compare(card.errorText, "")
        requestUnlock(card, replacement)
        compare(card.errorText, "Unlock A")
    }

    function test_hidden_receiving_card_closes_prompt_and_ignores_completion() {
        const first = wallet("A")
        const card = receivingCard(first)
        const popup = requestUnlock(card, first)
        const field = findChild(card, "receivingAddressPassphraseField")
        field.text = "password for A"
        popup.submitPassphrase()
        card.visible = false
        tryCompare(popup, "visible", false)
        first.finishAddress(false, true)
        compare(popup.visible, false)
        compare(card.errorText, "")
    }

    function test_receive_qr_picker_closes_on_owner_change_and_hide() {
        const first = wallet("A")
        const second = wallet("B")
        const card = receivingCard(first)
        const dialog = findChild(card, "receivingAddressSaveQRDialog")
        dialog.open()
        tryCompare(dialog, "visible", true)
        card.wallet = second
        tryCompare(dialog, "visible", false)
        card.wallet = first
        dialog.accepted()
        compare(card.errorText, "")
        dialog.open()
        tryCompare(dialog, "visible", true)
        card.visible = false
        tryCompare(dialog, "visible", false)
    }

    function test_address_type_picker_rejects_stale_selection() {
        const first = wallet("A")
        const second = wallet("B")
        const card = receivingCard(first)
        const picker = findChild(card, "requestPaymentAddressTypeDropdown")
        picker.open()
        tryCompare(picker, "opened", true)
        card.wallet = second
        card.wallet = first
        tryCompare(picker, "opened", false)
        picker.activated("bech32")
        compare(first.receiveOperationPending, false)
        compare(second.receiveOperationPending, false)
        picker.open()
        tryCompare(picker, "opened", true)
        picker.activated("bech32")
        compare(first.requestedType, "bech32")
        first.finishAddress(true, false)
        picker.close()
        picker.open()
        card.visible = false
        tryCompare(picker, "opened", false)
        picker.activated("bech32m")
        compare(first.receiveOperationPending, false)
    }

    function test_receive_qr_menu_rejects_stale_action() {
        const first = wallet("A")
        const second = wallet("B")
        const card = receivingCard(first)
        const menu = findChild(card, "receivingAddressQRContextMenu")
        const copy = findChild(card, "receivingAddressQRContextCopy")
        menu.open()
        tryCompare(menu, "opened", true)
        card.wallet = second
        card.wallet = first
        copy.triggered()
        compare(card.errorText, "")
    }

    function test_payment_menus_reject_actions_from_previous_opening() {
        const first = wallet("A")
        const modal = requestModal(first, "1")
        const menu = findChild(modal.card, "paymentRequestMoreMenu")
        const remove = findChild(modal.card, "requestPaymentDeleteMenuButton")
        const qrMenu = findChild(modal.card, "requestPaymentQRContextMenu")
        const save = findChild(modal.card, "requestPaymentQRContextSave")
        const picker = findChild(modal.card, "requestPaymentSaveQRDialog")
        menu.open()
        tryCompare(menu, "opened", true)
        modal.close()
        tryCompare(modal, "visible", false)
        verify(modal.openRequest("2"))
        tryCompare(modal, "opened", true)
        remove.triggered()
        compare(first.removedRequestId, "")
        qrMenu.open()
        tryCompare(qrMenu, "opened", true)
        modal.close()
        tryCompare(modal, "visible", false)
        verify(modal.openRequest("1"))
        tryCompare(modal, "opened", true)
        save.triggered()
        compare(picker.visible, false)
        // A fresh menu still acts on the visible request.
        menu.open()
        tryCompare(menu, "opened", true)
        remove.triggered()
        compare(first.removedRequestId, "1")
    }

    function test_payment_qr_picker_closes_with_its_modal() {
        const first = wallet("A")
        const second = wallet("B")
        const modal = requestModal(first, "1")
        const dialog = findChild(modal.card, "requestPaymentSaveQRDialog")
        dialog.open()
        tryCompare(dialog, "visible", true)
        modal.wallet = second
        tryCompare(dialog, "visible", false)
        tryCompare(modal, "visible", false)
    }

    function test_delete_completion_does_not_close_reopened_request() {
        const first = wallet("A")
        const modal = requestModal(first, "1")
        verify(modal.card.deleteRequest())
        modal.close()
        tryCompare(modal, "visible", false)
        verify(modal.openRequest("2"))
        tryCompare(modal, "opened", true)
        first.finishRemoval()
        compare(modal.opened, true)
        compare(modal.card.request.id, "2")
    }

    function test_delete_exit_cleans_only_its_original_wallet() {
        const first = wallet("A")
        const second = wallet("B")
        first.currentPaymentRequest.id = "1"
        second.currentPaymentRequest.id = "1"
        second.currentPaymentRequest.label = "B edit"
        second.detailPaymentRequest.id = "1"
        const modal = requestModal(first, "1")
        verify(modal.card.deleteRequest())
        first.finishRemoval()
        verify(modal.visible)
        modal.wallet = second
        tryCompare(modal, "visible", false)
        compare(first.currentPaymentRequest.id, "")
        compare(first.detailPaymentRequest.id, "")
        compare(second.currentPaymentRequest.id, "1")
        compare(second.currentPaymentRequest.label, "B edit")
        compare(second.detailPaymentRequest.id, "1")
    }

    function test_delete_completion_after_wallet_round_trip_cleans_only_original_request() {
        const first = wallet("A")
        const second = wallet("B")
        first.currentPaymentRequest.id = "1"
        second.currentPaymentRequest.id = "1"
        second.detailPaymentRequest.id = "1"
        const modal = requestModal(first, "1")
        verify(modal.card.deleteRequest())
        modal.wallet = second
        tryCompare(modal, "visible", false)
        modal.wallet = first
        verify(modal.openRequest("2"))
        tryCompare(modal, "opened", true)
        first.finishRemoval()
        compare(first.currentPaymentRequest.id, "")
        compare(first.detailPaymentRequest.id, "2")
        compare(second.currentPaymentRequest.id, "1")
        compare(second.detailPaymentRequest.id, "1")
        compare(modal.opened, true)
    }

    function test_delete_completion_while_away_preserves_a_new_draft() {
        const first = wallet("A")
        const second = wallet("B")
        const modal = requestModal(first, "1")
        verify(modal.card.deleteRequest())
        modal.wallet = second
        tryCompare(modal, "visible", false)
        first.currentPaymentRequest.clear()
        first.currentPaymentRequest.label = "A new draft"
        first.finishRemoval()
        compare(first.currentPaymentRequest.label, "A new draft")
        compare(first.detailPaymentRequest.id, "")
        compare(modal.visible, false)
        modal.wallet = first
        compare(modal.visible, false)
    }

    function test_wallet_switch_during_open_closes_and_preserves_new_wallet() {
        const first = wallet("A")
        const second = wallet("B")
        second.currentPaymentRequest.label = "B draft"
        const modal = createTemporaryObject(modalComponent, suite, { wallet: first })
        verify(modal.openRequest("1"))
        modal.wallet = second
        tryCompare(modal, "visible", false)
        compare(second.currentPaymentRequest.label, "B draft")
    }

    function test_pending_save_watcher_is_released_when_its_wallet_unloads() {
        const first = wallet("A")
        const second = wallet("B")
        first.currentPaymentRequest.label = "A draft"
        const card = createTemporaryObject(paymentComponent, suite, { wallet: first })
        verify(card)
        card.createRequest()
        verify(findChild(card, "paymentRequestSaveCompletion"))
        card.wallet = second
        first.destroy()
        tryVerify(function() { return !findChild(card, "paymentRequestSaveCompletion") })
        compare(card.wallet, second)
        compare(card.errorText, "")
    }

    function test_background_saves_retire_only_successfully_consumed_drafts() {
        const first = wallet("A")
        const second = wallet("B")
        first.currentPaymentRequest.label = "A draft"
        second.currentPaymentRequest.label = "B draft"
        const card = createTemporaryObject(paymentComponent, suite, { wallet: first })
        verify(card)
        let created = 0
        card.created.connect(function() { ++created })
        card.createRequest()
        compare(first.receiveOperationPending, true)
        card.wallet = second
        card.createRequest()
        compare(second.receiveOperationPending, true)
        card.wallet = first
        first.finishSave("1", true)
        compare(first.currentPaymentRequest.id, "")
        compare(first.currentPaymentRequest.label, "")
        compare(card.saved, false)
        compare(created, 0)
        // A newer draft makes the backend report false instead of applying its saved entry.
        second.currentPaymentRequest.label = "B newer draft"
        second.finishSave("2", false)
        compare(second.currentPaymentRequest.label, "B newer draft")
        card.wallet = second
        compare(card.saved, false)
        compare(created, 0)
        // Saving in an unchanged opening still presents the newly created request.
        card.createRequest()
        compare(second.receiveOperationPending, true)
        second.finishSave("3", true)
        compare(created, 1)
        compare(second.currentPaymentRequest.id, "3")
    }

    function test_receive_drafts_return_with_their_wallet() {
        const first = wallet("A")
        const second = wallet("B")
        first.currentPaymentRequest.label = "A draft"
        second.currentPaymentRequest.label = "B draft"
        const page = createTemporaryObject(pageComponent, suite, { wallet: first })
        verify(page)
        const field = findChild(page.draftCard, "requestPaymentYourNameInput")
        compare(field.text, "A draft")
        field.text = "A unfinished edit"
        field.textEdited()
        page.wallet = second
        compare(second.currentPaymentRequest.label, "B draft")
        compare(field.text, "B draft")
        page.wallet = first
        compare(first.currentPaymentRequest.label, "A unfinished edit")
        compare(field.text, "A unfinished edit")
    }
}
