// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Window 2.15
import QtTest 1.2
import org.bitcoincore.qt 1.0
import "../../qml/components"

TestCase {
    id: testCase
    name: "TypedReceive"
    when: windowShown
    width: 840
    height: 820
    property var views: []
    function init() { testSetup.failOnWarnings(); optionsModel.displayUnit = BitcoinAmount.BTC; Clipboard.currentText = "unchanged external clipboard" }
    function cleanup() {
        for (let index = 0; index < views.length; ++index) {
            const receipt = testSetup.watchDestruction(views[index])
            views[index].destroy()
            tryCompare(receipt, "pending", 0)
            testSetup.releaseDestructionWatch(receipt)
        }
        views = []
        optionsModel.displayUnit = BitcoinAmount.BTC
        Clipboard.currentText = ""
    }
    Component {
        id: viewComponent
        Item {
            id: view
            width: Math.min(740, testCase.Window.window.contentItem.width)
            height: Math.min(780, testCase.Window.window.contentItem.height)
            property alias control: control
            property alias card: card
            property alias flickable: scroll.contentItem
            property bool saved: false
            ControlledReceiveFixture { id: control }
            ScrollView {
                id: scroll
                anchors.fill: parent
                clip: true
                contentWidth: availableWidth
                contentHeight: card.implicitHeight
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                ReceiveRequestEditorCard {
                    id: card
                    width: scroll.availableWidth
                    receive: control.receive
                    editor: view.saved ? control.savedEditor : control.receive.editor
                }
            }
        }
    }
    function makeView(saved) {
        const view = createTemporaryObject(viewComponent, testCase.Window.window.contentItem, {saved: !!saved})
        verify(view !== null)
        views.push(view)
        view.control.settle()
        if (saved) verify(view.control.openSaved())
        else verify(view.control.receive.ensureAddress())
        view.control.settle()
        verify(waitForRendering(view))
        verify(view.card.editor !== null)
        return view
    }
    function field(view, name) { const item = findChild(view, name); verify(item !== null, name); return item }
    function clickVisible(view, name) {
        const item = field(view, name)
        const flickable = view.flickable
        if (flickable && flickable.contentItem) {
            const point = item.mapToItem(flickable.contentItem, item.width / 2, item.height / 2)
            flickable.contentY = Math.max(0, Math.min(flickable.contentHeight - flickable.height, point.y - flickable.height / 2))
        }
        tryVerify(function() {
            if (!item.visible || !item.enabled || item.width <= 0 || item.height <= 0) return false
            const window = item.Window.window
            const center = item.mapToItem(window.contentItem, item.width / 2, item.height / 2)
            if (center.x < 0 || center.y < 0 || center.x >= window.width || center.y >= window.height) return false
            for (let parent = item.parent; parent; parent = parent.parent) {
                if (parent.clip) {
                    const point = item.mapToItem(parent, item.width / 2, item.height / 2)
                    if (point.x < 0 || point.y < 0 || point.x >= parent.width || point.y >= parent.height) return false
                }
            }
            return true
        }, name + " center must be visible through every clip")
        mouseClick(item, item.width / 2, item.height / 2)
    }
    function edit(view, name, text) { verify(testSetup.replaceText(field(view, name), text)); view.control.settle() }
    function test_physical_input_is_the_captured_saved_request() {
        const view = makeView(false)
        edit(view, "receiveMessageInput", "a public message")
        edit(view, "receivePrivateNoteInput", "a private note")
        edit(view, "receiveAmountInput", "1.25")
        clickVisible(view, "receiveSaveRequestButton")
        compare(view.control.writes, 0)
        view.control.settle()
        compare(view.control.writes, 1)
        compare(view.control.persistedAmount, 125000000)
        compare(view.control.persistedMessage, "a public message")
        compare(view.control.persistedNote, "a private note")
        verify(view.card.editor.saveResult.requestSaved)
    }
    function test_unit_change_before_save_preserves_canonical_amount_data() { return [{tag: "own-unit-button", physical: true}, {tag: "other-view-unit", physical: false}] }
    function test_unit_change_before_save_preserves_canonical_amount(data) {
        const view = makeView(false)
        edit(view, "receiveAmountInput", "1.25")
        if (data.physical) clickVisible(view, "receiveAmountUnitButton")
        else optionsModel.displayUnit = BitcoinAmount.SAT
        compare(view.card.editor.amount.satoshi, 125000000)
        clickVisible(view, "receiveSaveRequestButton")
        view.control.settle()
        compare(view.control.writes, 1)
        compare(view.control.persistedAmount, 125000000)
    }
    function test_invalid_amount_never_submits_the_previous_value() {
        const view = makeView(false)
        edit(view, "receiveAmountInput", "1.25")
        edit(view, "receiveAmountInput", "1..2")
        compare(field(view, "receiveSaveRequestButton").enabled, false)
        compare(view.control.writes, 0)
        verify(!view.card.editor.valid)
        compare(view.card.editor.amount.inputText, "1..2")
    }
    function test_rejected_type_cannot_submit_the_old_displayed_address() {
        const view = makeView(false)
        const oldAddress = view.card.editor.address
        view.control.setNextGenerationError(ReceiveError.Unsupported)
        verify(view.control.receive.chooseAddressType("legacy"))
        compare(field(view, "receiveSaveRequestButton").enabled, false)
        view.control.settle()
        compare(view.control.receive.reservation.address, oldAddress)
        verify(!view.control.receive.reservation.available)
        compare(view.control.writes, 0)
    }
    function test_used_reservation_keeps_composition_until_explicit_next() {
        const view = makeView(false)
        edit(view, "receiveMessageInput", "do not silently rebind")
        const oldAddress = view.card.editor.address
        view.control.markDraftUsed()
        view.control.settle()
        compare(view.control.generations, 1)
        compare(view.card.editor.address, oldAddress)
        compare(view.card.editor.message.text, "do not silently rebind")
        verify(!view.card.editor.canSave)
        verify(view.control.receive.nextAddress())
        view.control.settle()
        compare(view.control.generations, 2)
        compare(view.card.editor.message.text, "do not silently rebind")
    }
    function test_close_question_keeps_or_discards_exact_original_editor_data() { return [{tag: "keep", button: "receiveKeepEditingButton", replace: false}, {tag: "discard", button: "receiveDiscardCloseButton", replace: true}] }
    function test_close_question_keeps_or_discards_exact_original_editor(data) {
        const view = makeView(false)
        edit(view, "receiveMessageInput", "original draft")
        const editorId = view.card.editor.editorId
        verify(view.control.receive.useAddress("bcrt1q-selected"))
        view.control.settle()
        clickVisible(view, data.button)
        view.control.settle()
        compare(view.card.editor.editorId !== editorId, data.replace)
        compare(view.control.receive.replacementPending, false)
        compare(view.control.writes, 0)
        if (!data.replace) compare(view.card.editor.message.text, "original draft")
    }
    function test_new_editor_does_not_inherit_the_previous_native_undo_context() {
        const view = makeView(false)
        edit(view, "receiveAmountInput", "1.25")
        const oldEditorId = view.card.editor.editorId
        const oldField = field(view, "receiveAmountInput")
        verify(view.control.receive.newRequest())
        view.control.settle()
        clickVisible(view, "receiveDiscardCloseButton")
        view.control.settle()
        verify(view.card.editor.editorId !== oldEditorId)
        const current = field(view, "receiveAmountInput")
        verify(current !== oldField)
        current.forceActiveFocus()
        keyClick(Qt.Key_Z, Qt.ControlModifier)
        view.control.settle()
        compare(view.card.editor.amount.satoshi, 0)
        compare(current.text, "")
        compare(view.control.writes, 0)
    }
    function test_copy_uses_saved_owned_payload_without_scene_capture() {
        const view = makeView(true)
        view.control.settle()
        clickVisible(view, "savedRequestCopyUriButton")
        view.control.backend()
        compare(view.control.clipboardWrites, 0)
        view.control.publish()
        compare(view.control.clipboardWrites, 1)
        compare(view.control.copiedText, "bitcoin:bcrt1q-original?amount=0.000001&message=original%20message")
        compare(Clipboard.text(), "unchanged external clipboard")
    }
    function test_hide_revokes_a_queued_copy_before_native_handoff() {
        const view = makeView(true)
        view.control.settle()
        clickVisible(view, "savedRequestCopyUriButton")
        view.control.backend()
        view.visible = false
        view.control.publish()
        compare(view.control.clipboardWrites, 0)
        verify(!view.card.sharing.lastOperation.result.performed)
    }
    function test_paid_saved_public_fields_freeze_but_private_note_saves() {
        const view = makeView(true)
        view.control.markSavedUsed()
        view.control.settle()
        compare(field(view, "receiveMessageInput").readOnly, true)
        compare(field(view, "receivePrivateNoteInput").readOnly, false)
        edit(view, "receivePrivateNoteInput", "private after payment")
        clickVisible(view, "receiveSaveRequestButton")
        view.control.settle()
        compare(view.control.persistedMessage, "original message")
        compare(view.control.persistedNote, "private after payment")
        compare(view.control.writes, 1)
        compare(field(view, "savedRequestCopyUriButton").enabled, false)
    }
    function test_persistence_error_keeps_the_original_field_intent_visible() {
        const view = makeView(false)
        edit(view, "receiveMessageInput", "keep after failure")
        view.control.setNextWriteError(ReceiveError.Persistence)
        clickVisible(view, "receiveSaveRequestButton")
        view.control.settle()
        verify(field(view, "receiveEditorError").text.length > 0)
        compare(view.card.editor.message.text, "keep after failure")
        verify(view.card.editor.dirty)
        verify(!view.card.editor.saveResult.requestSaved)
        compare(view.control.writes, 1)
    }
}
