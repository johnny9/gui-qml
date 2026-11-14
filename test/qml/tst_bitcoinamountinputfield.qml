// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.2
import org.bitcoincore.qt 1.0
import "../../qml/components"

TestCase {
    id: testCase
    name: "BitcoinAmountInputField"
    when: windowShown
    width: 760
    height: 220
    property var editors: []

    Component {
        id: editorComponent
        BitcoinAmountInputField {
            id: editor
            property BitcoinAmount originalAmount: BitcoinAmount { unit: optionsModel.displayUnit }
            property bool hideAfterEdit: false
            property BitcoinAmount replacementAfterEdit: null
            property int textEditCount: 0
            property int inputChangeCount: 0
            property int pasteRequestCount: 0
            property int editingFinishCount: 0
            property string lastFinishedText: ""
            property var pasteReceipt: null
            amount: originalAmount
            interceptPaste: true
            inputObjectName: "standaloneAmountInput"
            unitToggleObjectName: "standaloneAmountUnit"
            unitLabelObjectName: "standaloneAmountUnitLabel"
            errorTextObjectName: "standaloneAmountError"
            width: Math.min(700, testCase.Window.window.contentItem.width)
            height: implicitHeight
            y: 20
            onInputTextChanged: inputChangeCount++
            onTextEdited: {
                textEditCount++
                if (hideAfterEdit) visible = false
                if (replacementAfterEdit) amount = replacementAfterEdit
            }
            onEditingFinished: (value) => {
                editingFinishCount++
                lastFinishedText = value
            }
            onPasteRequested: (payload) => {
                pasteRequestCount++
                pasteReceipt = payload
            }
        }
    }

    Component {
        id: amountComponent
        BitcoinAmount { unit: optionsModel.displayUnit }
    }

    function init() {
        testSetup.failOnWarnings()
        optionsModel.displayUnit = BitcoinAmount.BTC
        Clipboard.currentText = ""
    }

    function cleanup() {
        for (let i = 0; i < editors.length; ++i) {
            const receipt = testSetup.watchDestruction(editors[i])
            editors[i].destroy()
            tryCompare(receipt, "pending", 0)
            testSetup.releaseDestructionWatch(receipt)
        }
        editors = []
        optionsModel.displayUnit = BitcoinAmount.BTC
        Clipboard.currentText = ""
    }

    function createEditor(properties) {
        const editor = createTemporaryObject(editorComponent, testCase.Window.window.contentItem, properties || {})
        verify(editor !== null)
        editors.push(editor)
        editor.visible = true
        verify(waitForRendering(editor))
        return editor
    }

    function replacementModel(editor) {
        const amount = createTemporaryObject(amountComponent, editor)
        verify(amount !== null)
        amount.satoshi = 200000000
        return amount
    }

    function clickUnit(editor, requireEnabled) {
        const button = findChild(editor, "standaloneAmountUnit")
        verify(button !== null && button.visible)
        compare(button.enabled, requireEnabled)
        const window = button.Window.window
        const point = button.mapToItem(window.contentItem, button.width / 2, button.height / 2)
        verify(point.x >= 0 && point.x < window.contentItem.width)
        verify(point.y >= 0 && point.y < window.contentItem.height)
        window.update()
        verify(waitForRendering(button))
        mouseClick(button, button.width / 2, button.height / 2)
    }

    function captureAll(editor, text, throughIntent) {
        editor.field.forceActiveFocus()
        tryCompare(editor.field, "activeFocus", true)
        editor.field.selectAll()
        const amount = editor.amount
        const revision = amount.inputRevision
        const previous = amount.inputText
        let payload = null
        if (throughIntent) {
            Clipboard.currentText = text
            editor.paste()
            compare(editor.pasteRequestCount, 1)
            payload = editor.pasteReceipt
        } else payload = editor.capturePaste(text, false)
        verify(payload !== null)
        compare(payload.model, amount)
        compare(payload.editor, editor.field)
        compare(payload.start, 0)
        compare(payload.end, previous.length)
        compare(payload.inputRevision, revision)
        compare(payload.ticket, 0)
        compare(payload.text, text)
        compare(editor.pastePending, true)
        return payload
    }

    function snapshot(amount) {
        return {satoshi: amount.satoshi, revision: amount.inputRevision, input: amount.inputText,
            ticket: amount.inputOperationId, state: amount.inputOperationState}
    }

    function compareUnchanged(amount, before) {
        compare(amount.satoshi, before.satoshi)
        compare(amount.inputRevision, before.revision)
        compare(amount.inputText, before.input)
        compare(amount.inputOperationId, before.ticket)
        compare(amount.inputOperationState, before.state)
        compare(amount.inputPending, false)
    }

    function test_unit_placeholder_and_label_data() {
        return [
            {tag: "BTC", unit: BitcoinAmount.BTC, placeholder: "0.00000000", label: "BTC", present: true},
            {tag: "mBTC", unit: BitcoinAmount.MilliBTC, placeholder: "0.00000", label: "mBTC", present: true},
            {tag: "bits", unit: BitcoinAmount.MicroBTC, placeholder: "0.00", label: "bits", present: true},
            {tag: "sats", unit: BitcoinAmount.SAT, placeholder: "0", label: "sats", present: true},
            {tag: "absent-model", unit: BitcoinAmount.BTC, placeholder: "0.00000000", label: "BTC", present: false}
        ]
    }
    function test_unit_placeholder_and_label(data) {
        optionsModel.displayUnit = data.unit
        const editor = createEditor(data.present ? {} : {amount: null})
        compare(editor.placeholderText, data.placeholder)
        compare(editor.field.placeholderText, data.placeholder)
        compare(findChild(editor, "standaloneAmountUnitLabel").text, data.label)
        compare(findChild(editor, "standaloneAmountUnit").enabled, data.present)
        if (data.present) {
            compare(editor.amount.unit, data.unit)
            editor.amount.editInput("1")
            compare(editor.field.text, "1")
            compare(editor.amount.satoshi, data.unit === BitcoinAmount.BTC ? 100000000
                : data.unit === BitcoinAmount.MilliBTC ? 100000 : data.unit === BitcoinAmount.MicroBTC ? 100 : 1)
        } else {
            editor.field.selectAll()
            compare(editor.capturePaste("1", false), null)
            editor.syncFromAmount(true)
            compare(editor.field.text, "")
            compare(editor.originalAmount.inputOperationId, 0)
        }
        compare(Clipboard.currentText, "")
    }

    function test_sync_from_amount_respects_edit_focus_data() {
        return [{tag: "active-literal", focused: true, force: false, expected: "001.20"},
                {tag: "forced-active-reset", focused: true, force: true, expected: "1.20000000"},
                {tag: "inactive-reset", focused: false, force: false, expected: "1.20000000"}]
    }
    function test_sync_from_amount_respects_edit_focus(data) {
        const editor = createEditor()
        editor.field.forceActiveFocus()
        tryCompare(editor.field, "activeFocus", true)
        if (!data.focused) {
            editor.forceActiveFocus()
            tryCompare(editor.field, "activeFocus", false)
        }
        editor.amount.editInput("001.20")
        const revision = editor.amount.inputRevision
        compare(editor.field.text, "001.20")
        compare(editor.amount.satoshi, 120000000)
        compare(editor.amount.inputDirty, true)
        editor.syncFromAmount(data.force)
        compare(editor.field.text, data.expected)
        compare(editor.amount.satoshi, 120000000)
        compare(editor.amount.inputRevision, revision + (data.focused && !data.force ? 0 : 1))
        compare(editor.amount.inputDirty, data.focused && !data.force)
        compare(editor.amount.inputOperationId, 0)
    }

    function test_payment_uri_requires_short_exact_provenance_data() {
        const uri = "bitcoin:bcrt1qexample?amount=0.07"
        const mixed = "  BiTcOiN:bcrt1qexample?amount=0.07 \t"
        const longClipboard = "bitcoin:" + "x".repeat(505)
        return [
            {tag: "replace-entire-input", previous: "", current: uri, clip: uri, expected: uri},
            {tag: "preserved-prefix", previous: "12", current: "12" + uri, clip: uri, expected: uri},
            {tag: "preserved-suffix", previous: "34", current: uri + "34", clip: uri, expected: uri},
            {tag: "preserved-both", previous: "1234", current: "12" + uri + "34", clip: uri, expected: uri},
            {tag: "trimmed-case-preserved", previous: "", current: mixed, clip: mixed, expected: mixed.trim()},
            {tag: "unchanged-input", previous: uri, current: uri, clip: uri, expected: ""},
            {tag: "current-shorter-than-clipboard", previous: "", current: "bitcoin:", clip: uri, expected: ""},
            {tag: "altered-prefix", previous: "1234", current: "XX" + uri + "34", clip: uri, expected: ""},
            {tag: "altered-suffix", previous: "1234", current: "12" + uri + "XX", clip: uri, expected: ""},
            {tag: "previous-over-budget", previous: "x".repeat(513), current: uri, clip: uri, expected: ""},
            {tag: "current-over-budget", previous: "x".repeat(500), current: "x".repeat(500) + uri, clip: uri, expected: ""},
            {tag: "clipboard-over-budget", previous: "", current: longClipboard, clip: longClipboard, expected: ""},
            {tag: "empty-clipboard", previous: "", current: uri, clip: "", expected: ""},
            {tag: "ordinary-numeric-clipboard", previous: "", current: "1.20", clip: "1.20", expected: ""},
            {tag: "interception-disabled", previous: "", current: uri, clip: uri, expected: "", disabled: true}
        ]
    }
    function test_payment_uri_requires_short_exact_provenance(data) {
        const editor = createEditor({interceptPaste: !data.disabled})
        editor.amount.satoshi = 125000000
        const before = snapshot(editor.amount)
        Clipboard.currentText = data.clip
        const clipboardRevision = Clipboard.revision
        compare(editor.pastedPaymentUri(data.previous, data.current), data.expected)
        compareUnchanged(editor.amount, before)
        compare(editor.field.text, before.input)
        compare(editor.pastePending, false)
        compare(editor.textEditCount, 0)
        compare(editor.pasteRequestCount, 0)
        compare(Clipboard.currentText, data.clip)
        compare(Clipboard.revision, clipboardRevision)
    }

    function test_captured_paste_cancellation_keeps_original_ownership_data() {
        return [{tag: "explicit-cancel", action: "cancel"},
                {tag: "stale-then-successor", action: "successor"},
                {tag: "model-replaced", action: "replace"},
                {tag: "editor-hidden", action: "hide"},
                {tag: "editor-readonly", action: "readonly"}]
    }
    function test_captured_paste_cancellation_keeps_original_ownership(data) {
        const editor = createEditor()
        const original = editor.amount
        original.satoshi = 100000000
        const first = captureAll(editor, "0.07", data.action === "cancel")
        const before = snapshot(original)
        const replacement = replacementModel(editor)
        const replacementBefore = snapshot(replacement)
        if (data.action === "cancel") editor.cancelPaste(first)
        else if (data.action === "replace") editor.amount = replacement
        else if (data.action === "hide") editor.visible = false
        else if (data.action === "readonly") editor.readOnly = true
        else original.editInput("2.00000000")
        compare(editor.pastePending, false)
        compare(editor.applyCapturedPaste(first), false)
        compare(first.ticket, 0)
        compare(first.model, original)
        compare(original.inputOperationId, before.ticket)
        compareUnchanged(replacement, replacementBefore)
        if (data.action === "successor") {
            compare(original.satoshi, 200000000)
            compare(original.inputRevision, before.revision + 1)
            const second = captureAll(editor, "0.09")
            verify(second.sequence > first.sequence)
            editor.cancelPaste(first)
            compare(editor.pastePending, true)
            compare(editor.applyCapturedPaste(first), false)
            compare(editor.applyCapturedPaste(second), true)
            compare(second.model, original)
            compare(second.ticket, before.ticket + 1)
            compare(original.inputOperationId, second.ticket)
            compare(original.inputOperationState, BitcoinAmount.InputApplied)
            compare(original.satoshi, 9000000)
            compare(original.inputRevision, second.inputRevision + 1)
            compare(editor.textEditCount, 1)
            compare(editor.inputChangeCount, 1)
        } else {
            compareUnchanged(original, before)
            compare(editor.textEditCount, 0)
            compare(editor.inputChangeCount, 0)
        }
        compare(editor.pastePending, false)
        compare(editor.pasteRequestCount, data.action === "cancel" ? 1 : 0)
        compare(Clipboard.currentText, data.action === "cancel" ? "0.07" : "")
    }

    function test_reentrant_terminal_paste_keeps_applied_original_amount_data() {
        return [{tag: "hide-after-edit", replace: false}, {tag: "replace-after-edit", replace: true}]
    }
    function test_reentrant_terminal_paste_keeps_applied_original_amount(data) {
        const editor = createEditor()
        const original = editor.amount
        original.satoshi = 100000000
        const replacement = replacementModel(editor)
        const replacementBefore = snapshot(replacement)
        const payload = captureAll(editor, "0.07")
        const before = snapshot(original)
        editor.hideAfterEdit = !data.replace
        editor.replacementAfterEdit = data.replace ? replacement : null
        compare(editor.applyCapturedPaste(payload), true)
        compare(payload.model, original)
        compare(payload.inputRevision, before.revision)
        compare(payload.ticket, before.ticket + 1)
        compare(original.inputOperationId, payload.ticket)
        compare(original.inputOperationState, BitcoinAmount.InputApplied)
        compare(original.inputPending, false)
        compare(original.satoshi, 7000000)
        verify(original.inputRevision > before.revision)
        compare(editor.textEditCount, 1)
        compare(editor.inputChangeCount, 1)
        compare(editor.pastePending, false)
        compare(editor.amount, data.replace ? replacement : original)
        compare(editor.visible, data.replace)
        compareUnchanged(replacement, replacementBefore)
        editor.cancelPaste(payload)
        compare(original.cancelInput(payload.ticket), false)
        compare(original.inputOperationState, BitcoinAmount.InputApplied)
        compare(original.satoshi, 7000000)
        compare(original.inputOperationId, payload.ticket)
        compareUnchanged(replacement, replacementBefore)
    }

    function test_operation_refusal_precedence_and_revision_fallback() {
        const editor = createEditor()
        const amount = editor.amount
        amount.satoshi = 100000000
        const payload = captureAll(editor, "9".repeat(129))
        const revision = amount.inputRevision
        compare(editor.applyCapturedPaste(payload), true)
        compare(amount.inputPending, false)
        compare(amount.inputOperationState, BitcoinAmount.InputRejected)
        compare(amount.inputOperationId, payload.ticket)
        compare(amount.inputRevision, revision + 1)
        compare(amount.satoshi, 100000000)
        compare(editor.ownedPasteTicket, payload.ticket)
        compare(editor.ownedPasteRevision, amount.inputRevision)
        const refusal = "Amount input exceeds the 128-character limit."
        compare(amount.inputOperationError, refusal)
        compare(editor.effectiveErrorText, refusal)
        const error = findChild(editor, "standaloneAmountError")
        compare(error.text, refusal)
        compare(error.visible, true)
        editor.showErrorMessage = false
        compare(error.visible, false)
        compare(editor.effectiveErrorText, refusal)
        compare(amount.inputOperationState, BitcoinAmount.InputRejected)
        editor.showErrorMessage = true
        editor.errorText = "The send form rejected this amount."
        compare(error.text, "The send form rejected this amount.")
        editor.errorText = ""
        compare(error.text, refusal)
        amount.editInput("ordinary-invalid-input")
        compare(amount.inputRevision, revision + 2)
        compare(amount.inputOperationId, payload.ticket)
        compare(amount.inputOperationState, BitcoinAmount.InputRejected)
        compare(amount.inputOperationError, refusal)
        compare(editor.effectiveErrorText, amount.inputError)
        verify(editor.effectiveErrorText !== refusal)
        compare(error.text, amount.inputError)
        compare(error.visible, true)
        compare(amount.satoshi, 100000000)
    }

    function test_physical_commit_uses_real_model_or_local_editor_data() {
        return [{tag: "numeric-with-model", present: true, uri: false},
                {tag: "numeric-without-model", present: false, uri: false},
                {tag: "typed-uri-with-model", present: true, uri: true},
                {tag: "typed-uri-without-model", present: false, uri: true}]
    }
    function test_physical_commit_uses_real_model_or_local_editor(data) {
        const editor = createEditor(data.present ? {} : {amount: null})
        const original = editor.originalAmount
        const before = snapshot(original)
        Clipboard.currentText = "unrelated native clipboard"
        const clipboardRevision = Clipboard.revision
        const text = data.uri ? "bitcoin:bcrt1qexample?amount=0.07" : "1.25"
        verify(testSetup.replaceText(editor.field, text))
        compare(editor.field.text, data.uri ? "" : text)
        compare(original.satoshi, data.present && !data.uri ? 125000000 : 0)
        compare(original.inputRevision, before.revision + (data.present && !data.uri ? 1 : 0))
        compare(original.inputOperationId, before.ticket)
        compare(original.inputPending, false)
        compare(editor.textEditCount, data.uri ? 0 : 1)
        compare(editor.inputChangeCount, data.uri ? 0 : 1)
        compare(editor.pasteRequestCount, 0)
        compare(editor.pastePending, false)
        compare(Clipboard.currentText, "unrelated native clipboard")
        compare(Clipboard.revision, clipboardRevision)
    }

    function test_physical_unit_toggle_preserves_satoshi_data() {
        return [{tag: "BTC-to-SAT", initial: BitcoinAmount.BTC, target: BitcoinAmount.SAT, expected: "123456789", label: "sats", placeholder: "0"},
                {tag: "SAT-to-BTC", initial: BitcoinAmount.SAT, target: BitcoinAmount.BTC, expected: "1.23456789", label: "BTC", placeholder: "0.00000000"}]
    }
    function test_physical_unit_toggle_preserves_satoshi(data) {
        optionsModel.displayUnit = data.initial
        const editor = createEditor()
        editor.amount.satoshi = 123456789
        const ticket = editor.amount.inputOperationId
        Clipboard.currentText = "unit switch keeps clipboard"
        clickUnit(editor, true)
        compare(optionsModel.displayUnit, data.target)
        compare(editor.amount.unit, data.target)
        compare(editor.amount.satoshi, 123456789)
        compare(editor.amount.inputOperationId, ticket)
        compare(editor.field.text, data.expected)
        compare(editor.placeholderText, data.placeholder)
        compare(findChild(editor, "standaloneAmountUnitLabel").text, data.label)
        compare(editor.textEditCount, 0)
        compare(Clipboard.currentText, "unit switch keeps clipboard")
    }

    function test_unavailable_capture_and_disabled_unit_have_no_effect_data() {
        return [{tag: "absent-model", absent: true}, {tag: "disabled-editor", absent: false}]
    }
    function test_unavailable_capture_and_disabled_unit_have_no_effect(data) {
        const editor = createEditor(data.absent ? {amount: null} : {enabled: false})
        const original = editor.originalAmount
        original.satoshi = 100000000
        const before = snapshot(original)
        editor.field.selectAll()
        compare(editor.capturePaste("0.07", false), null)
        compare(editor.pastePending, false)
        compare(editor.pasteRequestCount, 0)
        clickUnit(editor, false)
        compare(optionsModel.displayUnit, BitcoinAmount.BTC)
        compareUnchanged(original, before)
        compare(editor.textEditCount, 0)
        compare(Clipboard.currentText, "")
    }

    function test_unintercepted_paste_applies_literal_once() {
        const editor = createEditor({interceptPaste: false})
        const amount = editor.amount
        amount.satoshi = 100000000
        editor.field.forceActiveFocus()
        tryCompare(editor.field, "activeFocus", true)
        editor.field.selectAll()
        const before = snapshot(amount)
        Clipboard.currentText = "0.07"
        const clipboardRevision = Clipboard.revision
        editor.paste()
        compare(amount.satoshi, 7000000)
        compare(amount.inputRevision, before.revision + 1)
        compare(amount.inputOperationId, before.ticket + 1)
        compare(amount.inputOperationState, BitcoinAmount.InputApplied)
        compare(amount.inputPending, false)
        compare(editor.ownedPasteTicket, amount.inputOperationId)
        compare(editor.ownedPasteRevision, amount.inputRevision)
        compare(editor.field.text, "0.07")
        compare(editor.field.readOnly, false)
        compare(editor.pastePending, false)
        compare(editor.pasteRequestCount, 0)
        compare(editor.pasteReceipt, null)
        compare(editor.textEditCount, 1)
        compare(editor.inputChangeCount, 1)
        compare(Clipboard.currentText, "0.07")
        compare(Clipboard.revision, clipboardRevision)
    }

    function test_provenance_rejects_unowned_surrounding_growth() {
        const editor = createEditor()
        editor.amount.satoshi = 125000000
        const before = snapshot(editor.amount)
        const uri = "bitcoin:bcrt1qexample?amount=0.07"
        Clipboard.currentText = uri
        const clipboardRevision = Clipboard.revision
        compare(editor.pastedPaymentUri("12", "12" + uri), uri)
        compare(editor.pastedPaymentUri("12", "12extra" + uri), "")
        compareUnchanged(editor.amount, before)
        compare(editor.field.text, before.input)
        compare(editor.pastePending, false)
        compare(editor.pasteRequestCount, 0)
        compare(editor.textEditCount, 0)
        compare(Clipboard.currentText, uri)
        compare(Clipboard.revision, clipboardRevision)
    }

    function test_pending_paste_defers_physical_edit_finish_until_cancel() {
        const editor = createEditor()
        editor.amount.satoshi = 100000000
        const payload = captureAll(editor, "0.07")
        const before = snapshot(editor.amount)
        const finishes = editor.editingFinishCount
        keyClick(Qt.Key_Return)
        compare(editor.editingFinishCount, finishes)
        compare(editor.pastePending, true)
        compare(editor.field.readOnly, true)
        compare(payload.ticket, 0)
        compareUnchanged(editor.amount, before)
        editor.cancelPaste(payload)
        compare(editor.pastePending, false)
        compare(editor.field.readOnly, false)
        keyClick(Qt.Key_Return)
        compare(editor.editingFinishCount, finishes + 1)
        compare(editor.lastFinishedText, "1.00000000")
        compare(editor.amount.satoshi, 100000000)
        compare(editor.amount.inputRevision, before.revision + 1)
        compare(editor.amount.inputOperationId, before.ticket)
        compare(editor.textEditCount, 0)
        compare(editor.inputChangeCount, 0)
        compare(Clipboard.currentText, "")
    }

    function test_model_absent_edit_finish_reports_local_literal() {
        const editor = createEditor({amount: null})
        const before = snapshot(editor.originalAmount)
        verify(testSetup.replaceText(editor.field, "1.25"))
        const finishes = editor.editingFinishCount
        keyClick(Qt.Key_Return)
        compare(editor.editingFinishCount, finishes + 1)
        compare(editor.lastFinishedText, "1.25")
        compare(editor.field.text, "1.25")
        compareUnchanged(editor.originalAmount, before)
        compare(editor.textEditCount, 1)
        compare(editor.inputChangeCount, 1)
        compare(editor.pasteRequestCount, 0)
        compare(Clipboard.currentText, "")
    }

    function test_keyboard_paste_keeps_original_capture_until_applied() {
        const editor = createEditor()
        const original = editor.amount
        original.satoshi = 100000000
        editor.field.forceActiveFocus()
        tryCompare(editor.field, "activeFocus", true)
        editor.field.selectAll()
        const before = snapshot(original)
        Clipboard.currentText = "0.07"
        const clipboardRevision = Clipboard.revision
        keyClick(Qt.Key_V, Qt.ControlModifier)
        compare(editor.pasteRequestCount, 1)
        const payload = editor.pasteReceipt
        verify(payload !== null)
        compare(payload.model, original)
        compare(payload.editor, editor.field)
        compare(payload.start, 0)
        compare(payload.end, before.input.length)
        compare(payload.inputRevision, before.revision)
        compare(payload.ticket, 0)
        compare(payload.text, "0.07")
        compare(editor.pastePending, true)
        compare(editor.field.text, before.input)
        compareUnchanged(original, before)
        compare(editor.textEditCount, 0)
        compare(editor.applyCapturedPaste(payload), true)
        compare(payload.ticket, before.ticket + 1)
        compare(original.inputOperationId, payload.ticket)
        compare(original.inputOperationState, BitcoinAmount.InputApplied)
        compare(original.inputRevision, before.revision + 1)
        compare(original.satoshi, 7000000)
        compare(editor.pastePending, false)
        compare(editor.textEditCount, 1)
        compare(editor.inputChangeCount, 1)
        editor.cancelPaste(payload)
        compare(original.inputOperationState, BitcoinAmount.InputApplied)
        compare(original.satoshi, 7000000)
        compare(original.inputOperationId, payload.ticket)
        compare(Clipboard.currentText, "0.07")
        compare(Clipboard.revision, clipboardRevision)
    }

    function test_numeric_key_with_interception_disabled_updates_real_amount() {
        const editor = createEditor({interceptPaste: false})
        editor.amount.clear()
        editor.field.forceActiveFocus()
        tryCompare(editor.field, "activeFocus", true)
        const before = snapshot(editor.amount)
        keyClick("2")
        compare(editor.field.text, "2")
        compare(editor.amount.satoshi, 200000000)
        compare(editor.amount.inputRevision, before.revision + 1)
        compare(editor.amount.inputOperationId, before.ticket)
        compare(editor.textEditCount, 1)
        compare(editor.inputChangeCount, 1)
        compare(editor.pasteRequestCount, 0)
        compare(editor.effectiveErrorText, "")
        const finishes = editor.editingFinishCount
        keyClick(Qt.Key_Return)
        compare(editor.field.text, "2.00000000")
        compare(editor.amount.satoshi, 200000000)
        compare(editor.amount.inputRevision, before.revision + 2)
        compare(editor.amount.inputOperationId, before.ticket)
        compare(editor.editingFinishCount, finishes + 1)
        compare(editor.lastFinishedText, "2.00000000")
        compare(editor.textEditCount, 1)
        compare(Clipboard.currentText, "")
    }
}
