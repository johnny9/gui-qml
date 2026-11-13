// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtTest 1.2
import org.bitcoincore.qt 1.0
import "../../qml/controls/utils.js" as Utils
import "../../qml/controls"
import "../../qml/components"
import "../../qml/pages/wallet"

TestCase {
    name: "RequestPayment"
    when: windowShown
    visible: true
    width: 900
    height: 700

    Component {
        id: requestPaymentComponent

        ReceivePage { wallet: testWalletModel }
    }

    Component {
        id: paymentFieldComponent

        PaymentRequestField {
            width: 480
            label: "Requested amount"
            fieldObjectName: "resizingPaymentInput"
            unitControl: AmountUnitButton {
                unit: BitcoinAmount.BTC
            }
        }
    }

    function test_paymentFieldImplicitWidthDoesNotDependOnAssignedWidth() {
        const field = createTemporaryObject(paymentFieldComponent, this)
        verify(field !== null)
        const input = findChild(field, "resizingPaymentInput")
        verify(input !== null)
        wait(0)
        const preferredWidth = field.implicitWidth

        for (const width of [280, 480, 900]) {
            field.width = width
            tryVerify(function() {
                return input.mapToItem(field, 0, 0).x >= field.leftPadding
                    && input.mapToItem(field, input.width, 0).x <= field.width - field.rightPadding + 1
            })
            compare(field.implicitWidth, preferredWidth)
            verify(input.width >= 80 && input.width <= 260)
        }
    }

    function init() {
        testSetup.failOnWarnings()
        testWalletModel.available = true
        testWalletModel.capabilitiesKnown = true
        optionsModel.displayUnit = BitcoinAmount.BTC
        testPaymentRequest.clear()
        testWalletModel.lastCommitAddressType = ""
        testWalletModel.lastTemplateRequestId = ""
        testWalletModel.lastRemovedRequestId = ""
        testWalletModel.removeReceiveRequestResult = true
        testWalletModel.ensureReceivingAddressResult = true
        testWalletModel.ensureReceivingAddressNeedsUnlock = false
        testWalletModel.receiveRequestReconciliationPending = false
        testWalletModel.receiveRequestReconciliationError = ""
        walletController.closePaymentRequestDetailRequests = 0
    }

    function test_receive_waits_for_actual_capability_publication() {
        testWalletModel.capabilitiesKnown = false
        const before = testWalletModel.ensureReceivingAddressCalls
        const page = createTemporaryObject(requestPaymentComponent, this)
        verify(page !== null)
        wait(0)
        compare(page.addressCapabilitiesReady, false)
        compare(page.addressErrorText, "")
        compare(testWalletModel.ensureReceivingAddressCalls, before)
        testWalletModel.capabilitiesKnown = true
        tryVerify(function() { return testWalletModel.ensureReceivingAddressCalls > before })
        compare(page.addressErrorText, "")
        verify(page.addressCapabilitiesReady)
    }

    function test_receiving_password_closes_when_opening_context_ends_data() {
        return [{tag: "hidden", action: 0}, {tag: "changed-opening", action: 1}, {tag: "unloaded", action: 2}]
    }

    function test_receiving_password_closes_when_opening_context_ends(data) {
        testWalletModel.ensureReceivingAddressNeedsUnlock = true
        const page = createTemporaryObject(requestPaymentComponent, this)
        verify(page !== null)
        const prompt = findChild(page, "receivePassphrasePopup")
        const field = findChild(prompt, "receivePassphraseField")
        verify(prompt !== null && field !== null)
        tryCompare(prompt, "opened", true)
        field.text = "original-opening-password"
        try {
            if (data.action === 0) page.visible = false
            else if (data.action === 1) page.wallet = testSecondWalletModel
            else testWalletModel.available = false
            tryCompare(prompt, "visible", false)
            compare(field.text, "")
            wait(0) // Settle the new view's own address-initialization intent.
            const originalCalls = testWalletModel.ensureReceivingAddressCalls
            const otherCalls = testSecondWalletModel.ensureReceivingAddressCalls
            prompt.submitted("delayed-password")
            compare(testWalletModel.ensureReceivingAddressCalls, originalCalls)
            compare(testSecondWalletModel.ensureReceivingAddressCalls, otherCalls)
        } finally {
            testWalletModel.available = true
            prompt.close()
        }
    }

    function test_formatRelativeTime_empty() {
        compare(Utils.formatRelativeTime(""), "")
        compare(Utils.formatRelativeTime(null), "")
        compare(Utils.formatRelativeTime(undefined), "")
    }

    function test_formatRelativeTime_just_now() {
        var now = new Date()
        compare(Utils.formatRelativeTime(now.toISOString()), "just now")
    }

    function test_formatRelativeTime_minutes() {
        var d = new Date()
        d.setMinutes(d.getMinutes() - 5)
        compare(Utils.formatRelativeTime(d.toISOString()), "5 minutes ago")

        var d1 = new Date()
        d1.setMinutes(d1.getMinutes() - 1)
        compare(Utils.formatRelativeTime(d1.toISOString()), "1 minute ago")
    }

    function test_formatRelativeTime_hours() {
        var d = new Date()
        d.setHours(d.getHours() - 3)
        compare(Utils.formatRelativeTime(d.toISOString()), "3 hours ago")

        var d1 = new Date()
        d1.setHours(d1.getHours() - 1)
        compare(Utils.formatRelativeTime(d1.toISOString()), "1 hour ago")
    }

    function test_formatRelativeTime_days() {
        var d = new Date()
        d.setDate(d.getDate() - 2)
        compare(Utils.formatRelativeTime(d.toISOString()), "2 days ago")

        var d1 = new Date()
        d1.setDate(d1.getDate() - 1)
        compare(Utils.formatRelativeTime(d1.toISOString()), "1 day ago")
    }

    Component { id: btcValidatorComponent; BitcoinAmount { unit: BitcoinAmount.BTC } }
    Component { id: satValidatorComponent; BitcoinAmount { unit: BitcoinAmount.SAT } }

    function test_btcValidator_accepts_valid_amounts() {
        var field = createTemporaryObject(btcValidatorComponent, this)
        verify(field !== null)

        field.editInput("0")
        compare(field.inputAcceptable, true)

        field.editInput("1.00000000")
        compare(field.inputAcceptable, true)

        field.editInput("21000000.00000000")
        compare(field.inputAcceptable, true)

        field.editInput("0.00000001")
        compare(field.inputAcceptable, true)

        field.editInput("01.00000000")
        compare(field.inputAcceptable, true)

        field.editInput("00000001.00000000")
        compare(field.inputAcceptable, true)
    }

    function test_btcValidator_rejects_invalid_amounts() {
        var field = createTemporaryObject(btcValidatorComponent, this)
        verify(field !== null)

        field.editInput("-1")
        compare(field.inputAcceptable, false)

        field.editInput("1.2.3")
        compare(field.inputAcceptable, false)

        field.editInput("0.000000001")
        compare(field.inputAcceptable, false)

        field.editInput("100000000.00000000")
        compare(field.inputAcceptable, false)

        field.editInput("abc")
        compare(field.inputAcceptable, false)
    }

    function test_satValidator_accepts_valid_amounts() {
        var field = createTemporaryObject(satValidatorComponent, this)
        verify(field !== null)

        field.editInput("0")
        compare(field.inputAcceptable, true)

        field.editInput("100000000")
        compare(field.inputAcceptable, true)

        field.editInput("2100000000000000")
        compare(field.inputAcceptable, true)

        field.editInput("00")
        compare(field.inputAcceptable, true)

        field.editInput("0002100000000000000")
        compare(field.inputAcceptable, true)
    }

    function test_satValidator_rejects_invalid_amounts() {
        var field = createTemporaryObject(satValidatorComponent, this)
        verify(field !== null)

        field.editInput("1.5")
        compare(field.inputAcceptable, false)

        field.editInput("-100")
        compare(field.inputAcceptable, false)

        field.editInput("10000000000000000")
        compare(field.inputAcceptable, false)
    }


    function createPage() {
        const page = createTemporaryObject(requestPaymentComponent, this, { width: 900, height: 900, wallet: testWalletModel })
        verify(page !== null)
        tryVerify(function() { return testWalletModel.receivingAddress.address !== "" })
        return page
    }
    function createRequest(page) {
        const card = page.draftCard
        if (!card.hasFields) editField(card, "requestPaymentYourNameInput", "Request")
        card.createRequest()
        tryCompare(page.requestModal, "opened", true)
        return page.requestModal.card
    }

    function test_customize_fields_matches_visible_rows() {
        const page = createPage()
        const card = page.draftCard
        const amount = findChild(card, "requestPaymentAmountField")
        const recipient = findChild(card, "requestPaymentLabelRow")
        const message = findChild(card, "requestPaymentMessageRow")
        verify(amount.visible)
        verify(message.visible)
        verify(!recipient.visible)
        compare(message.label, "Message")
        const footer = findChild(card, "requestPaymentDetailsFooter")
        verify(footer.visible)

        waitForRendering(page)
        mouseClick(findChild(card, "requestPaymentCustomizeButton"))
        const menu = findChild(card, "requestPaymentFieldsMenu")
        tryCompare(menu, "opened", true)
        const picker = findChild(card, "requestPaymentFieldsPicker")
        tryVerify(function() { return picker.itemAtIndex(2) !== null })
        const amountOption = picker.itemAtIndex(0)
        const messageOption = picker.itemAtIndex(1)
        const recipientOption = picker.itemAtIndex(2)
        verify(amountOption.selected)
        verify(messageOption.selected)
        verify(!recipientOption.selected)
        compare(recipientOption.text, "Recipient Name")

        mouseClick(recipientOption)
        verify(recipient.visible)
        verify(recipientOption.selected)
        verify(menu.opened)
        mouseClick(messageOption)
        verify(!message.visible)
        verify(!messageOption.selected)
        verify(!recipient.showDivider)
        verify(amount.showDivider)
        mouseClick(amountOption)
        verify(!amount.visible)
        verify(!amountOption.selected)
        mouseClick(recipientOption)
        verify(!recipient.visible)
        verify(!footer.visible)
        verify(findChild(card, "requestPaymentGenerateButton").enabled)

        keyClick(Qt.Key_Escape)
        tryCompare(menu, "visible", false)
        mouseClick(findChild(card, "requestPaymentCustomizeButton"))
        tryCompare(menu, "opened", true)
        verify(!amountOption.selected)
        verify(!messageOption.selected)
        verify(!recipientOption.selected)
        menu.close()
        tryCompare(menu, "visible", false)
        card.createRequest()
        tryCompare(page.requestModal, "opened", true)
        verify(findChild(page.requestModal.card, "requestPaymentAmountField").visible)
        verify(findChild(page.requestModal.card, "requestPaymentLabelRow").visible)
        verify(findChild(page.requestModal.card, "requestPaymentMessageRow").visible)
        compare(findChild(page.requestModal.card, "requestPaymentCustomizeButton"), null)
    }

    function test_customize_clears_fields_and_omits_them_from_request() {
        const page = createPage()
        const card = page.draftCard
        editField(card, "requestPaymentAmountInput", "0.001").editingFinished()
        editField(card, "requestPaymentMessageInput", "Lunch split").editingFinished()
        editField(card, "requestPaymentYourNameInput", "Hal").editingFinished()
        waitForRendering(page)
        mouseClick(findChild(card, "requestPaymentCustomizeButton"))
        const menu = findChild(card, "requestPaymentFieldsMenu")
        tryCompare(menu, "opened", true)
        const picker = findChild(card, "requestPaymentFieldsPicker")
        tryVerify(function() { return picker.itemAtIndex(2) !== null })
        for (let i = 0; i < 3; ++i) mouseClick(picker.itemAtIndex(i))
        menu.close()
        tryCompare(menu, "visible", false)
        compare(card.request.amount.satoshi, 0)
        compare(card.request.message, "")
        compare(card.request.label, "")
        compare(findChild(card, "requestPaymentAmountInput").text, "")
        compare(findChild(card, "requestPaymentMessageInput").text, "")
        compare(findChild(card, "requestPaymentYourNameInput").text, "")
        verify(findChild(card, "requestPaymentEmptyRow").visible)
        verify(!findChild(card, "requestPaymentDetailsFooter").visible)
        card.createRequest()
        tryCompare(page.requestModal, "opened", true)
        const saved = page.requestModal.card.request
        compare(saved.amount.satoshi, 0)
        compare(saved.label, "")
        compare(saved.message, "")
        compare(saved.qrPayload, "bitcoin:" + saved.address)
    }

    function test_empty_payment_details_restore_default_fields() {
        const page = createPage()
        const card = page.draftCard
        card.showAmount = false
        card.showMessage = false
        const add = findChild(card, "requestPaymentAddDetailsButton")
        verify(add.visible)
        compare(add.text, "Add details to your payment request")
        waitForRendering(page)
        mouseClick(add)
        verify(findChild(card, "requestPaymentAmountField").visible)
        verify(findChild(card, "requestPaymentMessageRow").visible)
        verify(!findChild(card, "requestPaymentLabelRow").visible)
        verify(!findChild(card, "requestPaymentEmptyRow").visible)
        verify(findChild(card, "requestPaymentDetailsFooter").visible)
        compare(card.request.amount.satoshi, 0)
        compare(card.request.message, "")
    }

    function test_unchecking_invalid_amount_clears_validation() {
        const page = createPage()
        const card = page.draftCard
        editField(card, "requestPaymentAmountInput", "0.001").editingFinished()
        editField(card, "requestPaymentAmountInput", ".").editingFinished()
        verify(card.errorText.length > 0)
        compare(card.request.amount.satoshi, 100000)
        waitForRendering(page)
        mouseClick(findChild(card, "requestPaymentCustomizeButton"))
        const menu = findChild(card, "requestPaymentFieldsMenu")
        tryCompare(menu, "opened", true)
        const picker = findChild(card, "requestPaymentFieldsPicker")
        mouseClick(picker.itemAtIndex(0))
        menu.close()
        tryCompare(menu, "visible", false)
        compare(card.request.amount.satoshi, 0)
        compare(findChild(card, "requestPaymentAmountInput").text, "")
        compare(card.errorText, "")
        verify(!card.modified)
        card.createRequest()
        tryCompare(page.requestModal, "opened", true)
        compare(page.requestModal.card.request.amount.satoshi, 0)
    }

    function test_empty_request_can_be_created_without_showing_address() {
        const page = createPage()
        const address = testWalletModel.receivingAddress.address
        compare(testPaymentRequest.id, "")
        compare(findChild(page, "receivingAddressCard"), null)
        compare(findChild(page, "receivingAddressQRImage"), null)
        compare(findChild(page.draftCard, "requestPaymentAddressTypeSection"), null)
        const button = findChild(page.draftCard, "requestPaymentGenerateButton")
        verify(button.enabled)
        page.draftCard.createRequest()
        tryCompare(page.requestModal, "opened", true)
        const card = page.requestModal.card
        compare(card.saved, true)
        compare(card.request.address, address)
        compare(card.request.amount.satoshi, 0)
        compare(card.request.label, "")
        compare(card.request.message, "")
        compare(card.request.noteSelf, "")
        compare(findChild(card, "paymentRequestStatus").text, "Awaiting payment")
        compare(findChild(card, "requestPaymentAddressTypeSection"), null)
        page.requestModal.close()
        tryCompare(page.requestModal, "visible", false)
        verify(testWalletModel.receivingAddress.address !== "")
        verify(testWalletModel.receivingAddress.address !== address)
        compare(testPaymentRequest.id, "")
    }

    function test_next_address_and_type_selection_rotate_without_request() {
        const page = createPage()
        const first = testWalletModel.receivingAddress.address
        page.ensureReceivingAddress(true, "")
        verify(testWalletModel.receivingAddress.address !== first)
        const settingsButton = findChild(page, "receiveAddressSettingsButton")
        const moreButton = findChild(page, "receiveMoreButton")
        compare(settingsButton.width, moreButton.width)
        compare(settingsButton.height, moreButton.height)
        verify(settingsButton.x + settingsButton.width <= moreButton.x)
        compare(settingsButton.iconSource.toString(), "qrc:/icons/address-settings.svg")
        compare(findChild(findChild(page, "receiveMoreMenu"), "receiveAddressTypePicker"), null)
        mouseClick(settingsButton)
        const menu = findChild(page, "receiveAddressTypeMenu")
        tryCompare(menu, "opened", true)
        const picker = findChild(page, "receiveAddressTypePicker")
        compare(picker.title, "Address Type")
        compare(picker.model.length, 4)
        tryVerify(function() { return picker.itemAtIndex(3) !== null })
        for (let i = 0; i < 4; ++i) {
            compare(picker.itemAtIndex(i).selected, picker.itemAtIndex(i).rowValue === picker.currentValue)
            compare(picker.itemAtIndex(i).subtitle, picker.model[i].description)
            verify(picker.itemAtIndex(i).subtitle.length > 0)
        }
        mouseClick(picker.itemAtIndex(2))
        tryCompare(menu, "visible", false)
        compare(testWalletModel.receivingAddress.addressType, "p2sh-segwit")
        compare(picker.currentValue, "p2sh-segwit")
        verify(picker.itemAtIndex(2).selected)
        compare(testPaymentRequest.id, "")
        const card = createRequest(page)
        compare(testWalletModel.lastCommitAddressType, "p2sh-segwit")
    }

    function test_clear_form_removes_hidden_and_invalid_drafts_without_changing_address() {
        const page = createPage()
        const card = page.draftCard
        const address = testWalletModel.receivingAddress.address
        const type = testWalletModel.receivingAddress.addressType
        editField(card, "requestPaymentAmountInput", "0.0001").editingFinished()
        editField(card, "requestPaymentYourNameInput", "Alice").editingFinished()
        editField(card, "requestPaymentMessageInput", "Lunch split").editingFinished()
        editField(card, "requestPaymentNoteSelfInput", "Lunch with Hal").editingFinished()
        compare(card.request.amount.satoshi, 10000)
        compare(card.request.label, "Alice")
        editField(card, "requestPaymentAmountInput", ".").editingFinished()
        verify(card.errorText.length > 0)
        card.showRecipientName = false
        card.showAmount = false
        card.showMessage = false

        mouseClick(findChild(page, "receiveMoreButton"))
        const menu = findChild(page, "receiveMoreMenu")
        tryCompare(menu, "opened", true)
        const clear = findChild(page, "receiveClearFormButton")
        compare(clear.text, "Clear form")
        mouseClick(clear)
        tryCompare(menu, "visible", false)
        compare(card.request.amount.satoshi, 0)
        compare(card.request.label, "")
        compare(card.request.message, "")
        compare(card.request.noteSelf, "")
        compare(card.errorText, "")
        verify(!card.hasFields)
        verify(!card.modified)
        verify(!card.showRecipientName)
        verify(!card.showAmount)
        verify(!card.showMessage)
        compare(testWalletModel.receivingAddress.address, address)
        compare(testWalletModel.receivingAddress.addressType, type)
        card.createRequest()
        tryCompare(page.requestModal, "opened", true)
        compare(page.requestModal.card.request.address, address)
    }

    function test_qr_context_menus_close_when_address_is_no_longer_shareable() {
        const page = createPage()
        const card = createRequest(page)
        const requestMenu = findChild(card, "requestPaymentQRContextMenu")
        mouseClick(findChild(card, "requestPaymentQRContextArea"), 20, 20, Qt.RightButton)
        tryCompare(requestMenu, "opened", true)
        compare(findChild(card, "requestPaymentQRContextCopy").text, "Copy QR code")
        compare(findChild(card, "requestPaymentQRContextSave").text, "Save QR code")
        card.request.paymentReceived = true
        tryCompare(requestMenu, "visible", false)
        verify(!findChild(card, "requestPaymentQRContextArea").enabled)
    }

    // A QR save dialog exports only the request it was opened for.
    function test_qr_save_dialog_belongs_to_the_request_it_was_opened_for() {
        const page = createPage()
        const card = createRequest(page)
        const dialog = findChild(card, "requestPaymentSaveQRDialog")
        const address = card.request.address
        try {
            card.openSaveQRDialog()
            compare(dialog.visible, true)
            card.request.address = "bcrt1qreplacedaddress"
            compare(dialog.visible, false)

            card.openSaveQRDialog()
            compare(dialog.visible, true)
            card.request.paymentReceived = true
            compare(dialog.visible, false)
            card.request.paymentReceived = false

            // A late accept for a request that changed reports it instead of
            // silently saving nothing.
            card.openSaveQRDialog()
            dialog.target = "a request that is no longer shown"
            dialog.selectAndAccept("file:///tmp/request-qr.png")
            compare(card.actionErrorText, "The payment request changed before the QR code was saved.")
        } finally {
            card.request.address = address
        }
    }

    function test_old_QR_save_dialog_cannot_start_a_new_capture_data() {
        return [{tag: "wallet"}, {tag: "generation"}, {tag: "hidden-return"}, {tag: "close-before-notify"}, {tag: "checking-before-notify"}]
    }

    function test_old_QR_save_dialog_cannot_start_a_new_capture(data) {
        const card = createRequest(createPage())
        tryCompare(findChild(card, "requestPaymentQRImage"), "status", Image.Ready)
        const grabber = createTemporaryObject(controlledQRGrabber, this)
        card.qrGrabber = grabber
        const dialog = findChild(card, "requestPaymentSaveQRDialog")
        card.openSaveQRDialog()
        compare(dialog.visible, true)
        if (data.tag === "wallet") card.wallet = testSecondWalletModel
        if (data.tag === "generation") card.request.generation++
        if (data.tag === "hidden-return") { card.visible = false; card.visible = true }
        if (data.tag === "close-before-notify") testWalletModel.closeBeforeAvailabilityNotificationForTest()
        else if (data.tag === "checking-before-notify") testWalletModel.checkBeforeReconciliationNotificationForTest()
        else compare(dialog.visible, false)
        // The platform may still deliver the old chooser's accept signal.
        dialog.selectAndAccept("file:///tmp/request-qr-old-interest.png")
        compare(grabber.captures, 0)
        compare(card.actionErrorText, "The payment request changed before the QR code was saved.")
    }

    function test_primary_action_returns_to_copy_after_update() {
        const page = createPage()
        const card = createRequest(page)
        const copy = findChild(card, "requestPaymentCopyButton")
        const update = findChild(card, "requestPaymentUpdateButton")
        verify(copy.visible)
        verify(!update.visible)
        editField(card, "requestPaymentYourNameInput", "Hal")
        verify(!copy.visible)
        verify(update.visible)
        update.clicked()
        compare(card.request.label, "Hal")
        verify(page.requestModal.opened)
        verify(copy.visible)
        verify(!update.visible)
        card.request.paymentReceived = true
        verify(!copy.visible)
        editField(card, "requestPaymentNoteSelfInput", "Received")
        verify(update.visible)
        update.clicked()
        verify(!copy.visible)
        verify(!update.visible)
    }

    function test_request_amount_survives_note_only_update_data() {
        return [
            {tag: "BTC", unit: 0, amount: "0.00000001", label: "BTC"},
            {tag: "mBTC", unit: 1, amount: "0.00001", label: "mBTC"},
            {tag: "bits", unit: 2, amount: "0.01", label: "bits"},
            {tag: "sats", unit: 3, amount: "1", label: "sats"},
        ]
    }
    function test_request_amount_survives_note_only_update(data) {
        optionsModel.displayUnit = data.unit
        const page = createPage()
        editField(page.draftCard, "requestPaymentAmountInput", data.amount).editingFinished()
        compare(page.draftCard.request.amount.satoshi, 1)
        compare(findChild(page.draftCard, "requestPaymentAmountUnitToggle").text, data.label)
        page.draftCard.createRequest()
        tryCompare(page.requestModal, "opened", true)
        const card = page.requestModal.card
        compare(card.request.amount.satoshi, 1)
        compare(findChild(card, "requestPaymentAmountInput").text, data.amount)
        compare(findChild(card, "requestPaymentAmountUnitToggle").text, data.label)
        editField(card, "requestPaymentNoteSelfInput", "Private reminder")
        findChild(card, "requestPaymentUpdateButton").clicked()
        compare(card.request.noteSelf, "Private reminder")
        compare(card.request.amount.satoshi, 1)
        compare(findChild(card, "requestPaymentAmountInput").text, data.amount)
        verify(!card.modified)
    }

    function test_saved_request_note_only_update_preserves_amount_data() {
        return test_request_amount_survives_note_only_update_data()
    }
    function test_saved_request_note_only_update_preserves_amount(data) {
        const page = createPage()
        editField(page.draftCard, "requestPaymentAmountInput", "0.00000001").editingFinished()
        const card = createRequest(page)
        optionsModel.displayUnit = data.unit
        compare(findChild(card, "requestPaymentAmountInput").text, data.amount)
        verify(!card.modified)
        editField(card, "requestPaymentNoteSelfInput", "Private reminder")
        findChild(card, "requestPaymentUpdateButton").clicked()
        compare(card.request.noteSelf, "Private reminder")
        compare(card.request.amount.satoshi, 1)
        verify(!card.modified)
    }

    function test_saved_request_unit_toggle_does_not_enable_update() {
        const page = createPage()
        editField(page.draftCard, "requestPaymentAmountInput", "0.001")
        const card = createRequest(page)
        const amount = card.request.amount.satoshi
        const button = findChild(card, "requestPaymentUpdateButton")
        const icon = findChild(card, "requestPaymentAmountUnitIcon")
        tryCompare(icon, "width", 12)
        tryCompare(icon, "height", 12)
        verify(!button.enabled)
        card.toggleAmountUnit()
        compare(optionsModel.displayUnit, BitcoinAmount.SAT)
        compare(card.request.amount.satoshi, amount)
        verify(!button.enabled)
        tryCompare(icon, "width", 12)
        tryCompare(icon, "height", 12)
        card.toggleAmountUnit()
        compare(optionsModel.displayUnit, BitcoinAmount.BTC)
        compare(card.request.amount.satoshi, amount)
        verify(!button.enabled)
    }

    function test_update_can_remove_all_optional_details() {
        const card = createRequest(createPage())
        editField(card, "requestPaymentAmountInput", "")
        editField(card, "requestPaymentMessageInput", "")
        editField(card, "requestPaymentNoteSelfInput", "")
        editField(card, "requestPaymentYourNameInput", "")
        verify(findChild(card, "requestPaymentUpdateButton").enabled)
        verify(card.saveFields())
        compare(card.request.label, "")
        compare(card.request.noteSelf, "")
        compare(card.request.qrPayload, "bitcoin:" + card.request.address)
    }

    function test_empty_request_can_remove_saved_private_note_data() {
        return [{tag: "unpaid", paid: false}, {tag: "paid", paid: true}]
    }
    function test_empty_request_can_remove_saved_private_note(data) {
        const page = createPage()
        page.draftCard.createRequest()
        tryCompare(page.requestModal, "opened", true)
        const card = page.requestModal.card
        card.request.paymentReceived = data.paid
        editField(card, "requestPaymentNoteSelfInput", "Private note")
        verify(card.saveFields())
        compare(card.request.noteSelf, "Private note")
        editField(card, "requestPaymentNoteSelfInput", "")
        verify(findChild(card, "requestPaymentUpdateButton").enabled)
        verify(card.saveFields())
        compare(card.request.noteSelf, "")
        verify(!findChild(card, "requestPaymentError").visible)
    }

    function test_request_again_prefills_without_saving_data() {
        return [{tag: "unpaid", paid: false}, {tag: "paid", paid: true}]
    }
    function test_request_again_prefills_without_saving(data) {
        const page = createPage()
        editField(page.draftCard, "requestPaymentNoteSelfInput", "Lunch with Hal")
        const card = createRequest(page)
        const id = card.request.id
        card.request.paymentReceived = data.paid
        const opened = walletController.openReceiveRequests
        findChild(card, "paymentRequestMoreButton").clicked()
        tryCompare(findChild(card, "paymentRequestMoreMenu"), "opened", true)
        findChild(card, "requestPaymentAgainMenuButton").clicked()
        tryCompare(page.requestModal, "visible", false)
        tryCompare(walletController, "openReceiveRequests", opened + 1)
        compare(testWalletModel.lastTemplateRequestId, id)
        compare(testPaymentRequest.id, "")
        compare(testPaymentRequest.noteSelf, "Lunch with Hal")
        tryCompare(findChild(page.draftCard, "requestPaymentNoteSelfInput"), "text", "Lunch with Hal")
        tryCompare(findChild(page.draftCard, "requestPaymentGenerateButton"), "enabled", true)
    }

    function test_request_again_reveals_populated_hidden_fields() {
        const page = createPage()
        editField(page.draftCard, "requestPaymentAmountInput", "0.001").editingFinished()
        editField(page.draftCard, "requestPaymentMessageInput", "Lunch split").editingFinished()
        editField(page.draftCard, "requestPaymentYourNameInput", "Hal").editingFinished()
        page.draftCard.createRequest()
        tryCompare(page.requestModal, "opened", true)
        const requestId = page.requestModal.card.request.id
        page.requestModal.close()
        tryCompare(page.requestModal, "visible", false)

        page.draftCard.showAmount = false
        page.draftCard.showMessage = false
        page.draftCard.showRecipientName = false
        verify(page.requestModal.openRequest(requestId))
        tryCompare(page.requestModal, "opened", true)
        const card = page.requestModal.card
        findChild(card, "paymentRequestMoreButton").clicked()
        tryCompare(findChild(card, "paymentRequestMoreMenu"), "opened", true)
        findChild(card, "requestPaymentAgainMenuButton").clicked()
        tryCompare(page.requestModal, "visible", false)
        tryCompare(testWalletModel, "lastTemplateRequestId", requestId)
        tryCompare(findChild(page.draftCard, "requestPaymentGenerateButton"), "enabled", true)

        compare(testPaymentRequest.id, "")
        compare(findChild(page.draftCard, "requestPaymentAmountInput").text, "0.00100000")
        compare(findChild(page.draftCard, "requestPaymentMessageInput").text, "Lunch split")
        compare(findChild(page.draftCard, "requestPaymentYourNameInput").text, "Hal")
        verify(findChild(page.draftCard, "requestPaymentAmountField").visible)
        verify(findChild(page.draftCard, "requestPaymentMessageRow").visible)
        verify(findChild(page.draftCard, "requestPaymentLabelRow").visible)
    }

    function test_empty_request_reverted_edit_restores_copy_data() {
        return [
            {tag: "private-note", field: "requestPaymentNoteSelfInput"},
            {tag: "message", field: "requestPaymentMessageInput"},
            {tag: "recipient", field: "requestPaymentYourNameInput"},
        ]
    }
    function test_empty_request_reverted_edit_restores_copy(data) {
        const page = createPage()
        page.draftCard.createRequest()
        tryCompare(page.requestModal, "opened", true)
        const card = page.requestModal.card
        const copy = findChild(card, "requestPaymentCopyButton")
        const update = findChild(card, "requestPaymentUpdateButton")
        verify(copy.visible)

        editField(card, data.field, "Temporary detail")
        verify(card.modified)
        verify(update.enabled)
        verify(!copy.visible)
        editField(card, data.field, "")
        verify(!card.modified)
        verify(!update.visible)
        verify(copy.visible)
        verify(copy.enabled)
        verify(!findChild(card, "requestPaymentError").visible)
        compare(card.request.noteSelf, "")
        compare(card.request.message, "")
        compare(card.request.label, "")
    }

    function editField(card, objectName, text) {
        if (!card.modalView && objectName === "requestPaymentYourNameInput") card.showRecipientName = true
        const input = findChild(card, objectName)
        input.forceActiveFocus()
        verify(testSetup.replaceText(input, text))
        return input
    }

    function test_fields_save_explicitly_without_changing_address() {
        const card = createRequest(createPage())
        const address = card.request.address
        const input = editField(card, "requestPaymentYourNameInput", "Friday coffee")
        findChild(card, "requestPaymentMessageInput").forceActiveFocus()
        compare(card.request.label, "Request")
        verify(card.saveFields())
        compare(card.request.label, "Friday coffee")
        compare(card.request.address, address)
        verify(card.sharing)
        verify(findChild(card, "requestPaymentQRImage").visible)
        editField(card, "requestPaymentYourNameInput", "")
        findChild(card, "requestPaymentMessageInput").forceActiveFocus()
        verify(card.saveFields())
        compare(card.request.label, "")
        compare(card.request.address, address)
    }

    function test_closing_modal_discards_unsaved_field() {
        const page = createPage()
        const card = createRequest(page)
        editField(card, "requestPaymentMessageInput", "Saved on close")
        const request = card.request
        page.requestModal.close()
        tryCompare(page.requestModal, "visible", false)
        compare(request.message, "")
    }

    function test_pending_save_preserves_fields_until_its_controlled_outcome() {
        const card = createRequest(createPage())
        const input = editField(card, "requestPaymentMessageInput", "Unsaved controlled draft")
        const operation = testWalletModel.pendingReceiveOperation(card.request.id)
        verify(card.startOperation(operation, "edit"))
        verify(card.pending)
        verify(card.modified)
        compare(input.text, "Unsaved controlled draft")
        verify(!findChild(card, "requestPaymentUpdateButton").enabled)
        operation.complete(false, false, "Persistence failed")
        compare(card.pending, false)
        compare(card.actionErrorText, "Persistence failed")
        compare(input.text, "Unsaved controlled draft")
        verify(card.modified)
    }

    function test_old_delete_outcome_cannot_close_a_reused_detail() {
        const page = createPage()
        const card = createRequest(page)
        const oldId = card.request.id
        const operation = testWalletModel.pendingReceiveOperation(oldId)
        verify(card.startOperation(operation, "delete"))
        // The detail QObject is reused, but a new draft generation is a new
        // view interest. Completion still belongs to the original operation.
        card.request.clear()
        card.request.id = "another-request"
        operation.complete(false, true, "")
        compare(card.request.id, "another-request")
        verify(page.requestModal.visible)
        compare(testWalletModel.lastRemovedRequestId, "")
    }

    function test_received_payment_freezes_fields_and_hides_sharing() {
        const card = createRequest(createPage())
        const message = editField(card, "requestPaymentMessageInput", "Uncommitted message")
        card.request.receivedAmountSatoshi = 141
        card.request.paymentReceived = true
        compare(card.request.message, "")
        compare(message.text, "")
        verify(!card.sharing)
        verify(!findChild(card, "requestPaymentQRPlaceholder").visible)
        const summary = findChild(card, "requestPaymentReceivedSummary")
        verify(summary.visible)
        compare(summary.amountValue, "0.00000141")
        verify(!findChild(card, "requestPaymentReceivedIcon").dashed)
        findChild(card, "paymentRequestMoreButton").clicked()
        const menu = findChild(card, "paymentRequestMoreMenu")
        tryCompare(menu, "opened", true)
        verify(!findChild(card, "requestPaymentCopyQRMenuButton").visible)
        verify(!findChild(card, "requestPaymentSaveQRMenuButton").visible)
        verify(findChild(card, "requestPaymentDeleteMenuButton").visible)
        menu.close()
        verify(!findChild(card, "requestPaymentAddressText").interactive)
        verify(!findChild(card, "requestPaymentAmountInput").enabled)
        verify(!findChild(card, "requestPaymentYourNameInput").enabled)
        verify(!message.enabled)
        const note = editField(card, "requestPaymentNoteSelfInput", "Received, thank you")
        note.editingFinished()
        verify(card.saveFields())
        compare(card.request.noteSelf, "Received, thank you")
    }

    function test_payment_check_failure_keeps_draft_and_retries_data() {
        return [{tag: "saved", saved: true}, {tag: "new-draft", saved: false}]
    }

    function test_payment_check_failure_keeps_draft_and_retries(data) {
        const page = createPage()
        const card = data.saved ? createRequest(page) : page.draftCard
        const note = editField(card, "requestPaymentNoteSelfInput", "Keep this draft")
        testWalletModel.receiveRequestReconciliationPending = true
        testWalletModel.receiveRequestReconciliationError = "Payments could not be checked. Try again."
        const retry = findChild(card, "requestPaymentRetryCheckButton")
        verify(retry.visible)
        verify(findChild(card, "requestPaymentError").text.includes("could not be checked"))
        verify(!card.sharing)
        verify(!findChild(card, data.saved ? "requestPaymentUpdateButton" : "requestPaymentGenerateButton").enabled)
        const before = testWalletModel.receiveRequestReloadCalls
        retry.clicked()
        compare(testWalletModel.receiveRequestReloadCalls, before + 1)
        compare(note.text, "Keep this draft")
        testWalletModel.receiveRequestReconciliationError = ""
        testWalletModel.receiveRequestReconciliationPending = false
        verify(!retry.visible)
        compare(note.text, "Keep this draft")
    }

    Component {
        id: controlledQRGrabber
        QtObject {
            id: capture
            property var completion: null
            property int captures: 0
            property int synchronousSaves: 0
            function grabToImage(callback, size) {
                completion = callback
                captures++
                return true
            }
            function release() {
                const callback = completion
                completion = null
                callback({image: null, saveToFile: function(path) { capture.synchronousSaves++; return true }})
            }
        }
    }

    function test_native_unavailability_before_notification_blocks_copy_and_capture_data() {
        return [{tag: "closed"}, {tag: "checking"}]
    }

    function test_native_unavailability_before_notification_blocks_copy_and_capture(data) {
        const card = createRequest(createPage())
        tryCompare(findChild(card, "requestPaymentQRImage"), "status", Image.Ready)
        const grabber = createTemporaryObject(controlledQRGrabber, this)
        card.qrGrabber = grabber
        let copies = 0
        card.clipboard = {setText: function(text) { copies++ }}
        verify(card.sharing)
        if (data.tag === "closed") testWalletModel.closeBeforeAvailabilityNotificationForTest()
        else testWalletModel.checkBeforeReconciliationNotificationForTest()
        // No notify signal: the presentation binding has not caught up.
        verify(card.sharing)
        card.copyRequest()
        const started = card.captureQR(function(result) { fail("Closed wallet captured") })
        compare(copies, 0)
        compare(started, false)
        compare(grabber.captures, 0)
    }

    function test_controlled_QR_capture_keeps_original_interest_data() {
        return [{tag: "current"}, {tag: "wallet"}, {tag: "generation"},
                {tag: "close-before-notify"}, {tag: "checking-before-notify"}, {tag: "paid"}, {tag: "hidden"},
                {tag: "wallet-return"}, {tag: "hidden-return"}, {tag: "request"}, {tag: "revision"}]
    }

    function test_controlled_QR_capture_keeps_original_interest(data) {
        const card = createRequest(createPage())
        tryCompare(findChild(card, "requestPaymentQRImage"), "status", Image.Ready)
        const grabber = createTemporaryObject(controlledQRGrabber, this)
        card.qrGrabber = grabber
        let deliveries = 0
        verify(card.captureQR(function(result) { deliveries++ }))
        compare(grabber.captures, 1)
        compare(deliveries, 0)
        if (data.tag === "wallet" || data.tag === "wallet-return") {
            const original = card.wallet
            card.wallet = testSecondWalletModel
            if (data.tag === "wallet-return") card.wallet = original
        }
        if (data.tag === "request") {
            testPaymentRequest.id = card.request.id
            testPaymentRequest.address = card.request.address
            card.request = testPaymentRequest
        }
        if (data.tag === "revision") card.request.addressType = "bech32m"
        if (data.tag === "generation") {
            const request = card.request
            const id = request.id
            const address = request.address
            const amount = request.amount.satoshi
            const label = request.label
            const message = request.message
            request.clear()
            request.id = id
            request.address = address
            request.amount.satoshi = amount
            request.label = label
            request.message = message
        }
        if (data.tag === "close-before-notify") testWalletModel.closeBeforeAvailabilityNotificationForTest()
        if (data.tag === "checking-before-notify") testWalletModel.checkBeforeReconciliationNotificationForTest()
        if (data.tag === "paid") card.request.paymentReceived = true
        if (data.tag === "hidden" || data.tag === "hidden-return") {
            card.visible = false
            if (data.tag === "hidden-return") card.visible = true
        }
        grabber.release()
        compare(deliveries, data.tag === "current" ? 1 : 0)
    }

    function test_QR_export_waits_for_its_owned_background_outcome() {
        const card = createRequest(createPage())
        tryCompare(findChild(card, "requestPaymentQRImage"), "status", Image.Ready)
        const grabber = createTemporaryObject(controlledQRGrabber, this)
        card.qrGrabber = grabber
        verify(card.saveQRToFile("file:///tmp/controlled-qr.png"))
        compare(card.imageExport, null)
        grabber.release()
        compare(grabber.synchronousSaves, 0)
        verify(card.imageExport !== null)
        verify(card.pending)
        compare(card.imageExport.path, "file:///tmp/controlled-qr.png")
        card.imageExport.complete(false, "The file could not be written")
        compare(card.pending, false)
        compare(card.actionErrorText, "The file could not be written")
    }

    function test_QR_export_late_outcome_does_not_retarget_a_reused_card() {
        const card = createRequest(createPage())
        tryCompare(findChild(card, "requestPaymentQRImage"), "status", Image.Ready)
        const grabber = createTemporaryObject(controlledQRGrabber, this)
        card.qrGrabber = grabber
        verify(card.saveQRToFile("file:///tmp/original-qr.png"))
        grabber.release()
        const original = card.imageExport
        card.wallet = testSecondWalletModel
        verify(original.retireCalls > 0)
        card.actionErrorText = "New view"
        original.complete(false, "Old write error")
        compare(card.actionErrorText, "New view")
        compare(original.path, "file:///tmp/original-qr.png")
    }

    function test_closed_wallet_cannot_share_a_retained_request() {
        const page = createPage()
        const card = createRequest(page)
        verify(card.sharing)
        testWalletModel.available = false
        compare(card.sharing, false)
        verify(!findChild(card, "requestPaymentCopyButton").enabled)
        verify(!findChild(card, "requestPaymentQRImage").visible)
    }

    function test_saved_request_cannot_be_shared_while_payment_scan_is_pending() {
        const page = createPage()
        const saved = createRequest(page)
        const requestId = saved.request.id
        page.requestModal.close()
        tryCompare(page.requestModal, "visible", false)
        testWalletModel.receiveRequestReconciliationPending = true
        verify(page.requestModal.openRequest(requestId))
        tryCompare(page.requestModal, "opened", true)
        const card = page.requestModal.card
        verify(!card.sharing)
        verify(!findChild(card, "requestPaymentQRImage").visible)
        verify(!findChild(card, "requestPaymentCopyButton").enabled)
        verify(!findChild(card, "requestPaymentAddressText").interactive)
        card.copyRequest()

        // The saved unpaid flag can be stale until the scan marks a payment.
        card.request.paymentReceived = true
        testWalletModel.receiveRequestReconciliationPending = false
        verify(!card.sharing)
        verify(!findChild(card, "requestPaymentQRImage").visible)
        verify(!findChild(card, "requestPaymentCopyButton").visible)
    }

    function test_payment_arrival_preserves_private_note_draft() {
        const card = createRequest(createPage())
        const note = editField(card, "requestPaymentNoteSelfInput", "Keep this draft")
        card.request.paymentReceived = true
        compare(note.text, "Keep this draft")
        verify(note.enabled)
        note.editingFinished()
        verify(card.saveFields())
        compare(card.request.noteSelf, "Keep this draft")
    }

    function test_amount_input_rejects_invalid_characters_and_preserves_precision() {
        const card = createRequest(createPage())
        const original = card.request.amount.satoshi
        optionsModel.displayUnit = BitcoinAmount.BTC
        const input = editField(card, "requestPaymentAmountInput", "0.12345678")
        input.cursorPosition = input.text.length
        keyClick("9")
        compare(input.text, "0.123456789")
        keyClick(".")
        keyClick("x")
        compare(input.text, "0.123456789.x")
        input.editingFinished()
        verify(!card.saveFields())
        compare(card.request.amount.satoshi, original)
        verify(card.errorText.length > 0)
        editField(card, "requestPaymentAmountInput", "0.12345678")
        verify(card.saveFields())
        compare(card.request.amount.satoshi, 12345678)
        compare(input.text, "0.12345678")
    }

    function test_unit_toggle_preserves_pending_canonical_amount() {
        const page = createPage()
        const card = createRequest(page)
        optionsModel.displayUnit = BitcoinAmount.BTC
        const input = editField(card, "requestPaymentAmountInput", "12")
        findChild(card, "requestPaymentAmountUnitToggle").clicked()
        compare(card.amountUnit, BitcoinAmount.SAT)
        compare(optionsModel.displayUnit, BitcoinAmount.SAT)
        compare(page.draftCard.amountUnit, BitcoinAmount.SAT)
        compare(input.text, "1200000000")
        verify(card.saveFields())
        compare(card.request.amount.satoshi, 1200000000)
        findChild(card, "requestPaymentAmountUnitToggle").clicked()
        compare(input.text, "12.00000000")
        verify(!findChild(card, "requestPaymentUpdateButton").enabled)
        compare(card.request.amount.satoshi, 1200000000)
        compare(optionsModel.displayUnit, BitcoinAmount.BTC)
        compare(page.draftCard.amountUnit, BitcoinAmount.BTC)
        optionsModel.displayUnit = BitcoinAmount.SAT
        compare(card.amountUnit, BitcoinAmount.SAT)
        optionsModel.displayUnit = BitcoinAmount.BTC
    }

    function test_reverting_invalid_amount_clears_error_data() {
        return [{tag: "draft", saved: false}, {tag: "saved", saved: true}]
    }
    function test_reverting_invalid_amount_clears_error(data) {
        optionsModel.displayUnit = BitcoinAmount.SAT
        const page = createPage()
        editField(page.draftCard, "requestPaymentAmountInput", "1").editingFinished()
        const card = data.saved ? createRequest(page) : page.draftCard
        compare(card.request.amount.satoshi, 1)
        editField(card, "requestPaymentAmountInput", "1.5").editingFinished()
        if (data.saved) findChild(card, "requestPaymentUpdateButton").clicked()
        verify(card.errorText.length > 0)

        editField(card, "requestPaymentAmountInput", "1").editingFinished()
        compare(card.errorText, "")
        verify(!findChild(card, "requestPaymentError").visible)
        verify(!card.modified)
        compare(card.request.amount.satoshi, 1)
        if (data.saved) {
            verify(!findChild(card, "requestPaymentUpdateButton").visible)
            verify(findChild(card, "requestPaymentCopyButton").visible)
        }
    }

    function test_returning_to_receive_preserves_invalid_amount_error() {
        optionsModel.displayUnit = BitcoinAmount.SAT
        const page = createPage()
        const card = page.draftCard
        editField(card, "requestPaymentAmountInput", "1.5").editingFinished()
        const error = card.errorText
        verify(error.length > 0)
        page.visible = false
        page.visible = true
        waitForRendering(page)
        compare(findChild(card, "requestPaymentAmountInput").text, "1.5")
        compare(card.errorText, error)
        verify(findChild(card, "requestPaymentError").visible)

        editField(card, "requestPaymentNoteSelfInput", "Private note").editingFinished()
        compare(card.errorText, error)
        card.createRequest()
        compare(card.request.id, "")
        verify(!page.requestModal.visible)
    }

    function test_address_errors_are_independent_of_amount_validation() {
        optionsModel.displayUnit = BitcoinAmount.SAT
        const page = createPage()
        const card = page.draftCard
        testWalletModel.ensureReceivingAddressResult = false
        page.prepareReceivingAddress()
        const addressError = card.errorText
        verify(addressError.length > 0)
        editField(card, "requestPaymentNoteSelfInput", "Private note").editingFinished()
        compare(card.errorText, addressError)
        card.clearForm()
        compare(card.errorText, addressError)

        editField(card, "requestPaymentAmountInput", "1.5").editingFinished()
        const amountError = card.errorText
        verify(amountError.length > 0 && amountError !== addressError)
        testWalletModel.ensureReceivingAddressResult = true
        page.prepareReceivingAddress()
        compare(page.addressErrorText, "")
        compare(card.errorText, amountError)
        editField(card, "requestPaymentAmountInput", "1").editingFinished()
        compare(card.errorText, "")
    }

    function test_reverting_amount_preserves_unrelated_action_error() {
        optionsModel.displayUnit = BitcoinAmount.SAT
        const page = createPage()
        editField(page.draftCard, "requestPaymentAmountInput", "1").editingFinished()
        const card = createRequest(page)
        testWalletModel.removeReceiveRequestResult = false
        verify(!card.deleteRequest())
        const error = card.errorText
        verify(error.length > 0)
        editField(card, "requestPaymentAmountInput", "1.5").editingFinished()
        verify(!card.saveFields())
        editField(card, "requestPaymentAmountInput", "1").editingFinished()
        compare(card.errorText, error)
        verify(!card.modified)
    }

    function test_fractional_sats_and_out_of_range_amounts_never_save() {
        const page = createPage()
        const card = page.draftCard
        optionsModel.displayUnit = BitcoinAmount.SAT
        editField(card, "requestPaymentAmountInput", "1.5")
        card.createRequest()
        compare(card.request.id, "")
        verify(card.errorText.length > 0)
        optionsModel.displayUnit = BitcoinAmount.BTC
        editField(card, "requestPaymentAmountInput", "21000000.00000001")
        card.createRequest()
        compare(card.request.id, "")
        editField(card, "requestPaymentAmountInput", "21000000.00000000")
        verify(card.saveFields())
        compare(card.request.amount.satoshi, 2100000000000000)
    }

    function test_delete_menu_closes_modal_and_clears_matching_request() {
        const page = createPage()
        const card = createRequest(page)
        const requestId = card.request.id
        findChild(card, "paymentRequestMoreButton").clicked()
        tryCompare(findChild(card, "paymentRequestMoreMenu"), "opened", true)
        verify(findChild(card, "requestPaymentCopyQRMenuButton").visible)
        verify(findChild(card, "requestPaymentSaveQRMenuButton").visible)
        findChild(card, "requestPaymentDeleteMenuButton").clicked()
        tryCompare(page.requestModal, "visible", false)
        compare(testWalletModel.lastRemovedRequestId, requestId)
        compare(card.request.id, "")
        compare(testPaymentRequest.id, "")
        verify(page.draftCard.visible)
    }

    function test_delete_failure_preserves_request_and_modal() {
        const page = createPage()
        const card = createRequest(page)
        const requestId = card.request.id
        testWalletModel.removeReceiveRequestResult = false
        findChild(card, "paymentRequestMoreButton").clicked()
        tryCompare(findChild(card, "paymentRequestMoreMenu"), "opened", true)
        findChild(card, "requestPaymentDeleteMenuButton").clicked()
        verify(page.requestModal.opened)
        compare(card.request.id, requestId)
        compare(testWalletModel.lastRemovedRequestId, "")
        verify(card.errorText.length > 0)
    }

    function test_history_button_requests_navigation() {
        const page = createPage()
        let requests = 0
        page.addressHistoryRequested.connect(function() { ++requests })
        findChild(page, "receiveMoreButton").clicked()
        tryCompare(findChild(page, "receiveMoreMenu"), "opened", true)
        findChild(page, "requestPaymentHistoryButton").clicked()
        tryCompare(findChild(page, "receiveMoreMenu"), "opened", false)
        compare(requests, 1)
    }
}
