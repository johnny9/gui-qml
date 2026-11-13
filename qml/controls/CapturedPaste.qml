// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
import QtQuick 2.15
import QtQuick.Controls 2.15
import org.bitcoincore.qt 1.0

QtObject {
    id: root
    property TextField textInput: null
    property TextArea textEdit: null
    // Item is used only for focus/geometry/overlay handoffs; editing is typed.
    readonly property Item editorItem: textInput && !textEdit ? textInput
        : textEdit && !textInput ? textEdit : null
    readonly property bool editorReady: editorItem !== null
    readonly property int editorSelectionStart: textInput ? textInput.selectionStart : textEdit ? textEdit.selectionStart : 0
    readonly property int editorSelectionEnd: textInput ? textInput.selectionEnd : textEdit ? textEdit.selectionEnd : 0
    readonly property int editorCursorPosition: textInput ? textInput.cursorPosition : textEdit ? textEdit.cursorPosition : 0
    readonly property bool editorReadOnly: textInput ? textInput.readOnly : textEdit ? textEdit.readOnly : true
    readonly property bool editorHasSelection: editorReady && editorSelectionStart !== editorSelectionEnd
    property QtObject context: null
    property bool active: true
    property bool effectPending: false
    property string menuObjectName: ""
    readonly property bool pending: intent !== null
    readonly property bool literalActive: _literalActive
    readonly property bool mutating: _applying
    property var intent: null
    property int _sequence: 0
    property bool _applying: false
    property bool _literalActive: false
    signal requested(var payload)
    signal literalRequested(var payload)
    signal restoreRequested(var payload)
    signal cancelRequested(var payload)

    function owns(payload) {
        return active && editorReady && payload !== null && intent === payload
            && context === payload.model && editorItem === payload.editor
    }
    function capture(text, primary, edited, previous, current, cursor) {
        if (!active || !editorReady) return null
        cancel()
        const payload = {origin: root, model: context, editor: editorItem, sequence: ++_sequence,
            text: text, primary: !!primary,
            clipboardRevision: primary ? Clipboard.selectionRevision : Clipboard.revision,
            start: Math.min(editorSelectionStart, editorSelectionEnd),
            end: Math.max(editorSelectionStart, editorSelectionEnd),
            edited: !!edited, previous: previous || "", current: current || "",
            cursor: cursor === undefined ? editorCursorPosition : cursor, ticket: 0}
        if (payload.start === payload.end) payload.start = payload.end = editorCursorPosition
        intent = payload
        return payload
    }
    function request(primary) {
        const text = primary ? Clipboard.selectionText() : Clipboard.text()
        const payload = capture(text, primary, false, "", "", undefined)
        if (payload) requested(payload)
    }
    function nativeEdit(clipboard, previous, current, cursor) {
        const payload = capture(clipboard, false, true, previous, current, cursor)
        if (payload) requested(payload)
    }
    function apply(payload) {
        if (!owns(payload)) return false
        _literalActive = true
        _applying = true
        literalRequested(payload)
        _applying = false
        if (owns(payload) && !effectPending) complete(payload)
        return true
    }
    function restore(payload) {
        if (!owns(payload)) return
        _applying = true
        restoreRequested(payload)
        _applying = false
    }
    function complete(payload) {
        if (payload === null || intent !== payload) return
        const originalEditor = editorItem
        const originalContext = context
        const completedCursor = editorCursorPosition
        intent = null
        _literalActive = false
        // TextArea moves its cursor to End when pending releases readOnly.
        // Restore only this completed editor; reentrant replacement owns itself.
        if (active && intent === null && editorItem === originalEditor && context === originalContext) {
            if (textInput) textInput.cursorPosition = completedCursor
            else if (textEdit) textEdit.cursorPosition = completedCursor
        }
    }
    function cancel() {
        const payload = intent
        // Editor replacement may reenter while pending is notifying its null
        // transition. Do not invalidate that same binding a second time.
        if (payload !== null) intent = null
        if (_literalActive) _literalActive = false
        if (payload) cancelRequested(payload)
    }
    function invalidate() { if (!_applying && !_literalActive) cancel() }
    onEditorItemChanged: cancel()
    onContextChanged: cancel()
    onActiveChanged: if (!active) cancel()
    onEffectPendingChanged: if (!effectPending && _literalActive && intent) complete(intent)

    function deselectEditor() {
        if (!editorReady) return
        if (textInput) textInput.deselect()
        else if (textEdit) textEdit.deselect()
    }
    function cursorAt(x, y) {
        if (!editorReady) return -1
        if (textInput) return textInput.positionAt(x, y, TextInput.CursorBetweenCharacters)
        return textEdit.positionAt(x, y)
    }
    function positionCursorAt(x, y) {
        const position = cursorAt(x, y)
        if (position < 0) return
        if (textInput) textInput.cursorPosition = position
        else if (textEdit) textEdit.cursorPosition = position
    }
    function cutSelection() {
        if (!editorReady || editorReadOnly) return
        if (textInput) textInput.cut()
        else if (textEdit) textEdit.cut()
    }
    function copySelection() {
        if (!editorReady) return
        if (textInput) textInput.copy()
        else if (textEdit) textEdit.copy()
    }
    function selectAllEditor() {
        if (!editorReady) return
        if (textInput) textInput.selectAll()
        else if (textEdit) textEdit.selectAll()
    }
    function pointerPressed(mouse) {
        if (!editorReady || !active) { mouse.accepted = false; return }
        if (mouse.button === Qt.MiddleButton) {
            if (!Clipboard.primarySelectionSupported) { mouse.accepted = false; return }
            editorItem.forceActiveFocus()
            deselectEditor()
            positionCursorAt(mouse.x, mouse.y)
            request(true)
        } else {
            const point = editorItem.mapToItem(pasteMenu.parent, mouse.x, mouse.y)
            pasteMenu.x = point.x
            pasteMenu.y = point.y
            pasteMenu.open()
        }
        mouse.accepted = true
    }
    property Menu pasteMenu: Menu {
        id: pasteMenu
        parent: root.editorItem ? root.editorItem.Overlay.overlay : null
        objectName: root.menuObjectName
        MenuItem {
            text: qsTr("Cut")
            enabled: root.editorReady && !root.editorReadOnly && root.editorHasSelection
            onTriggered: root.cutSelection()
        }
        MenuItem {
            text: qsTr("Copy")
            enabled: root.editorHasSelection
            onTriggered: root.copySelection()
        }
        MenuItem {
            text: qsTr("Paste")
            objectName: root.menuObjectName + "Paste"
            enabled: root.active && root.editorReady
            onTriggered: root.request(false)
        }
        MenuItem {
            text: qsTr("Select all")
            enabled: root.editorReady
            onTriggered: root.selectAllEditor()
        }
    }
}
