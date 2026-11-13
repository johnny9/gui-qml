// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0

LabeledField {
    id: root

    property alias text: input.text
    property alias placeholderText: input.placeholderText
    property alias inputObjectName: input.objectName
    property bool readOnly: false
    readonly property alias field: input
    readonly property bool pastePending: pasteIntent.pending
    readonly property alias inputActiveFocus: input.activeFocus
    property var textStyle: Theme.text.description
    property bool interceptPaste: false
    property string paymentUriRestoreText: ""
    fieldFocused: input.activeFocus

    signal textEdited()
    signal editingFinished()
    signal pasteRequested(var payload)

    function paymentUri(text) {
        if (!root.interceptPaste || text.length > 512) return ""
        const trimmed = String(text).trim()
        return trimmed.toLowerCase().startsWith("bitcoin:") ? trimmed : ""
    }
    function pastedPaymentUri(previous, current) {
        const clipboard = Clipboard.text()
        if (previous.length > 512 || current.length > 512 || clipboard.length > 512) return ""
        const uri = paymentUri(clipboard)
        if (!uri.length || previous === current) return ""
        const offset = current.indexOf(clipboard)
        if (offset < 0) return ""
        return previous.startsWith(current.slice(0, offset)) && previous.endsWith(current.slice(offset + clipboard.length)) ? uri : ""
    }
    function capturePaste(text, primary) { return pasteIntent.capture(text, primary, false, "", "", undefined) }
    function applyCapturedPaste(payload) { return pasteIntent.apply(payload) }
    function cancelPaste(payload) { if (pasteIntent.intent === payload) pasteIntent.cancel() }
    function paste() { pasteIntent.request(false) }
    onPaymentUriRestoreTextChanged: {
        pasteIntent.invalidate()
        if (interceptPaste) input.syncFromModel()
    }
    CapturedPaste {
        id: pasteIntent
        textEdit: input
        context: root
        active: root.visible && root.enabled && !root.readOnly
        menuObjectName: root.inputObjectName + "ContextMenu"
        onRequested: (payload) => {
            if (root.interceptPaste) root.pasteRequested(payload)
            else apply(payload)
        }
        onRestoreRequested: (payload) => input.syncFromModel()
        onLiteralRequested: (payload) => {
            input.syncingFromModel = true
            if (payload.edited) {
                input.text = payload.current
                input.cursorPosition = payload.cursor
            } else {
                input.remove(payload.start, payload.end)
                input.insert(payload.start, payload.text)
                input.cursorPosition = payload.start + payload.text.length
            }
            input.syncingFromModel = false
            root.textEdited()
        }
    }

    TextArea {
        id: input
        property bool syncingFromModel: false

        function syncFromModel() {
            if (text.length <= 512 && root.paymentUriRestoreText.length <= 512 && text === root.paymentUriRestoreText) return
            syncingFromModel = true
            text = root.paymentUriRestoreText
            syncingFromModel = false
        }

        function handlePaymentUriInput() {
            if (!root.interceptPaste || pasteIntent.literalActive) return false
            const previous = root.paymentUriRestoreText
            const current = text
            const cursor = cursorPosition
            const clipboard = Clipboard.text()
            const longEdit = previous.length > 512 || current.length > 512 || clipboard.length > 512
            if (!longEdit) {
                const uri = root.paymentUri(clipboard)
                const offset = current.indexOf(clipboard)
                if (!uri.length || previous === current || offset < 0
                        || !previous.startsWith(current.slice(0, offset)) || !previous.endsWith(current.slice(offset + clipboard.length))) return false
                syncFromModel()
            }
            pasteIntent.nativeEdit(clipboard, previous, current, cursor)
            return true
        }
        Layout.fillWidth: true
        Layout.preferredHeight: Math.max(56, contentHeight + topPadding + bottomPadding)
        topPadding: 14
        bottomPadding: 14
        leftPadding: 0
        rightPadding: 0
        wrapMode: TextEdit.WrapAnywhere
        verticalAlignment: TextEdit.AlignVCenter
        textFormat: TextEdit.PlainText
        font: root.textStyle.font
        color: Theme.color.neutral9
        placeholderTextColor: enabled ? Theme.color.neutral7 : Theme.color.neutral4
        background: Item {}
        selectByMouse: true
        readOnly: root.readOnly || pasteIntent.pending
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.RightButton | Qt.MiddleButton
            onPressed: (mouse) => pasteIntent.pointerPressed(mouse)
        }
        Accessible.name: root.label
        Accessible.description: root.errorText.length ? root.errorText : root.supportingText

        Keys.onPressed: (event) => {
            if (root.interceptPaste && event.matches(StandardKey.Paste)) {
                pasteIntent.request(false)
                event.accepted = true
            }
        }

        Component.onCompleted: if (root.interceptPaste) syncFromModel()
        // TextEdit.textEdited was added in Qt 6.9. Treat changes made while
        // focused as edits, excluding updates restored from the model.
        onTextChanged: {
            if (syncingFromModel || !activeFocus) return
            pasteIntent.invalidate()
            if (!handlePaymentUriInput()) root.textEdited()
        }
        onEditingFinished: root.editingFinished()
    }
}
