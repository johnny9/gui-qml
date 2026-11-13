// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Window 2.15
import QtTest 1.2
import org.bitcoincore.qt 1.0
import "../../qml/controls"

TestCase {
    id: testCase
    name: "CapturedPaste"
    when: windowShown
    width: 500
    height: 400

    function init() {
        testSetup.failOnWarnings()
        Clipboard.currentText = ""
        Clipboard.currentSelectionText = ""
    }

    Component {
        id: harnessComponent
        Item {
            id: harness
            width: 420
            height: 220
            readonly property alias single: single
            readonly property alias multiline: multiline
            readonly property alias capture: capture
            readonly property alias firstContext: firstContext
            readonly property alias secondContext: secondContext
            property int requestedCount: 0
            property int literalCount: 0
            property int cancelledCount: 0
            property var lastPayload: null
            property var cancelledPayload: null
            property string lastLiteral: ""
            property bool replaceEditorOnCompletion: false
            QtObject { id: firstContext }
            QtObject { id: secondContext }
            TextField { id: single; width: 400; height: 44; text: "abc def" }
            TextArea { id: multiline; y: 60; width: 400; height: 140; text: "abc\ndef" }
            CapturedPaste {
                id: capture
                textInput: single
                context: firstContext
                onRequested: (payload) => { harness.lastPayload = payload; harness.requestedCount += 1 }
                onLiteralRequested: (payload) => { harness.lastLiteral = payload.text; harness.literalCount += 1 }
                onCancelRequested: (payload) => { harness.cancelledPayload = payload; harness.cancelledCount += 1 }
                onPendingChanged: {
                    if (!pending && harness.replaceEditorOnCompletion) {
                        textInput = null
                        textEdit = harness.multiline
                        harness.multiline.cursorPosition = 2
                    }
                }
            }
        }
    }
    Component {
        id: labeledComponent
        LabeledTextArea { width: 420; height: 200; text: "prefix old suffix" }
    }

    function fixture(multiline) {
        const harness = createTemporaryObject(harnessComponent, testCase.Window.window.contentItem)
        verify(harness !== null)
        if (multiline) {
            harness.capture.textInput = null
            harness.capture.textEdit = harness.multiline
        }
        verify(waitForRendering(harness))
        return harness
    }
    function editorRows() { return [{tag: "TextField", multiline: false}, {tag: "TextArea", multiline: true}] }

    function test_typed_selection_copy_cut_and_readonly_data() { return editorRows() }
    function test_typed_selection_copy_cut_and_readonly(data) {
        const harness = fixture(data.multiline)
        const editor = data.multiline ? harness.multiline : harness.single
        const original = editor.text
        editor.select(0, 3)
        compare(harness.capture.editorSelectionStart, 0)
        compare(harness.capture.editorSelectionEnd, 3)
        verify(harness.capture.editorHasSelection)
        harness.capture.copySelection()
        compare(testSetup.nativeClipboardText(), "abc")
        editor.readOnly = true
        verify(harness.capture.editorReadOnly)
        harness.capture.cutSelection()
        compare(editor.text, original)
        editor.readOnly = false
        editor.select(0, 3)
        harness.capture.cutSelection()
        compare(editor.text, original.slice(3))
        harness.capture.selectAllEditor()
        compare(harness.capture.editorSelectionEnd - harness.capture.editorSelectionStart, editor.text.length)
        harness.capture.deselectEditor()
        verify(!harness.capture.editorHasSelection)
    }

    function test_middleclick_uses_the_correct_typed_position_signature_data() { return editorRows() }
    function test_middleclick_uses_the_correct_typed_position_signature(data) {
        const harness = fixture(data.multiline)
        const editor = data.multiline ? harness.multiline : harness.single
        const rectangle = editor.positionToRectangle(4)
        // TextInput's rectangle is in layout coordinates; its hit-test uses
        // editor coordinates including Controls padding. TextEdit includes it.
        const x = rectangle.x + (data.multiline ? 0 : editor.leftPadding) + 0.1
        const y = data.multiline ? rectangle.y + rectangle.height / 2 : 10000
        if (!data.multiline)
            compare(editor.positionAt(x, y, TextInput.CursorBetweenCharacters), 4)
        compare(harness.capture.cursorAt(x, y), 4)
        Clipboard.currentSelectionText = "  primary é\t  "
        const revision = Clipboard.selectionRevision
        const pointer = {button: Qt.MiddleButton, x: x, y: y, accepted: false}
        harness.capture.pointerPressed(pointer)
        verify(pointer.accepted)
        compare(harness.requestedCount, 1)
        compare(harness.literalCount, 0)
        compare(editor.cursorPosition, 4)
        compare(harness.lastPayload.start, 4)
        compare(harness.lastPayload.end, 4)
        compare(harness.lastPayload.text, "  primary é\t  ")
        compare(harness.lastPayload.primary, true)
        compare(harness.lastPayload.clipboardRevision, revision)
    }

    function test_literal_and_selection_are_captured_before_later_clipboard_changes_data() { return editorRows() }
    function test_literal_and_selection_are_captured_before_later_clipboard_changes(data) {
        const harness = fixture(data.multiline)
        const editor = data.multiline ? harness.multiline : harness.single
        editor.select(1, 3)
        Clipboard.currentText = "  original é\t  "
        const revision = Clipboard.revision
        harness.capture.request(false)
        const payload = harness.lastPayload
        verify(payload !== null)
        payload.ticket = 73
        Clipboard.currentText = "replacement"
        editor.select(0, 1)
        harness.capture.effectPending = true
        verify(harness.capture.apply(payload))
        compare(harness.lastLiteral, "  original é\t  ")
        compare(payload.start, 1)
        compare(payload.end, 3)
        compare(payload.clipboardRevision, revision)
        compare(payload.ticket, 73)
        verify(payload.editor === editor)
        verify(payload.model === harness.firstContext)
        harness.capture.invalidate()
        verify(harness.capture.pending)
        verify(harness.capture.literalActive)
        harness.capture.effectPending = false
        verify(!harness.capture.pending)
        verify(!harness.capture.literalActive)
        verify(!harness.capture.apply(payload))
        compare(harness.literalCount, 1)
    }

    function test_original_intent_is_cancelled_on_context_editor_or_admission_change_data() {
        return [{tag: "context", action: 0}, {tag: "editor", action: 1}, {tag: "inactive", action: 2}]
    }
    function test_original_intent_is_cancelled_on_context_editor_or_admission_change(data) {
        const harness = fixture(false)
        const payload = harness.capture.capture(" exact original ", false, false, "", "", undefined)
        payload.ticket = 91
        if (data.action === 0) harness.capture.context = harness.secondContext
        else if (data.action === 1) {
            harness.capture.textInput = null
            harness.capture.textEdit = harness.multiline
        } else harness.capture.active = false
        compare(harness.cancelledCount, 1)
        verify(harness.cancelledPayload === payload)
        compare(harness.cancelledPayload.ticket, 91)
        verify(harness.cancelledPayload.model === harness.firstContext)
        verify(harness.cancelledPayload.editor === harness.single)
        verify(!harness.capture.pending)
        verify(!harness.capture.apply(payload))
        compare(harness.literalCount, 0)
    }

    function test_native_edit_keeps_its_original_history_and_cursor_data() { return editorRows() }
    function test_native_edit_keeps_its_original_history_and_cursor(data) {
        const harness = fixture(data.multiline)
        harness.capture.nativeEdit(" literal ", "original before", "original after", 5)
        const payload = harness.lastPayload
        compare(harness.requestedCount, 1)
        verify(payload.edited)
        compare(payload.text, " literal ")
        compare(payload.previous, "original before")
        compare(payload.current, "original after")
        compare(payload.cursor, 5)
        verify(harness.capture.apply(payload))
        compare(harness.literalCount, 1)
        verify(!harness.capture.pending)
    }

    function test_ambiguous_editor_ports_cannot_capture_or_apply() {
        const harness = fixture(false)
        const original = harness.capture.capture("original", false, false, "", "", undefined)
        harness.capture.textEdit = harness.multiline
        verify(!harness.capture.editorReady)
        compare(harness.cancelledCount, 1)
        verify(!harness.capture.apply(original))
        verify(harness.capture.capture("new", false, false, "", "", undefined) === null)
        harness.capture.request(false)
        compare(harness.requestedCount, 0)
        compare(harness.literalCount, 0)
    }

    function test_completion_does_not_move_a_reentrant_replacement_editor() {
        const harness = fixture(false)
        harness.single.cursorPosition = 5
        const payload = harness.capture.capture("original", false, false, "", "", undefined)
        verify(payload !== null)
        harness.replaceEditorOnCompletion = true
        harness.capture.complete(payload)
        compare(harness.capture.textInput, null)
        compare(harness.capture.textEdit, harness.multiline)
        compare(harness.multiline.cursorPosition, 2)
        compare(harness.literalCount, 0)
        verify(!harness.capture.pending)
    }

    function test_labeled_textarea_applies_the_exact_captured_literal_once() {
        const labeled = createTemporaryObject(labeledComponent, testCase.Window.window.contentItem)
        verify(labeled !== null)
        verify(waitForRendering(labeled))
        labeled.field.select(7, 10)
        const literal = "  é\n\t original  "
        const payload = labeled.capturePaste(literal, false)
        verify(payload !== null)
        Clipboard.currentText = "different clipboard"
        verify(labeled.applyCapturedPaste(payload))
        compare(labeled.text, "prefix " + literal + " suffix")
        compare(labeled.field.cursorPosition, 7 + literal.length)
        verify(!labeled.pastePending)
        verify(!labeled.applyCapturedPaste(payload))
        compare(labeled.text, "prefix " + literal + " suffix")
    }
}
