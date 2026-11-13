// Copyright (c) 2025-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0

import "../controls"

ColumnLayout {
    id: root

    property BitcoinAmount amount
    property string errorText: ""
    property bool showErrorMessage: true
    readonly property string effectiveErrorText: errorText || (amount && ownedPasteTicket === amount.inputOperationId
        && ownedPasteRevision === amount.inputRevision && amount.inputOperationState === BitcoinAmount.InputRejected
        ? amount.inputOperationError : amount ? amount.inputError : "")
    property double ownedPasteTicket: 0
    property double ownedPasteRevision: -1
    property BitcoinAmount pasteModel: null
    property bool readOnly: false
    readonly property bool pastePending: pasteIntent.pending
    property string labelText: qsTr("Amount")
    property string accessibleName: labelText
    property alias inputObjectName: amountInput.objectName
    property alias unitToggleObjectName: unitToggle.objectName
    property alias unitLabelObjectName: unitToggle.labelObjectName
    property alias errorTextObjectName: errorTextLabel.objectName
    readonly property alias field: amountInput
    property bool enabled: true
    property string placeholderText: root.amount ? root.amountInputPlaceholder(root.amount.unit) : "0.00000000"
    property bool interceptPaste: false
    property bool showLabel: true

    signal inputTextChanged
    signal textEdited
    signal editingFinished(string value)
    signal pasteRequested(var payload)

    function syncFromAmount(force) {
        if (root.amount && (force || !amountInput.activeFocus)) root.amount.resetInput()
    }

    function paymentUri(text) {
        if (!root.interceptPaste || text.length > 512) return ""
        const trimmed = String(text).trim()
        return trimmed.toLowerCase().startsWith("bitcoin:") ? trimmed : ""
    }
    function shortProvenance(previous, current, clipboard) {
        if (previous.length > 512 || current.length > 512 || clipboard.length > 512 || !clipboard.length
                || previous === current || current.length < clipboard.length) return false
        const unchanged = current.length - clipboard.length
        if (unchanged > previous.length) return false
        let prefix = 0
        let suffix = 0
        while (prefix < Math.min(previous.length, current.length) && previous[prefix] === current[prefix]) prefix++
        while (suffix < Math.min(previous.length, current.length)
                && previous[previous.length - suffix - 1] === current[current.length - suffix - 1]) suffix++
        const low = Math.max(0, unchanged - suffix)
        const high = Math.min(prefix, unchanged)
        const found = current.indexOf(clipboard, low)
        return low <= high && found >= 0 && found <= high
    }
    function pastedPaymentUri(previous, current) {
        const clipboard = Clipboard.text()
        const uri = paymentUri(clipboard)
        return uri.length && shortProvenance(previous, current, clipboard) ? uri : ""
    }
    function capturePaste(text, primary) {
        pasteModel = root.amount
        const payload = pasteIntent.capture(text, primary, false, "", "", undefined)
        if (payload && root.amount) payload.inputRevision = root.amount.inputRevision
        return payload
    }
    function applyCapturedPaste(payload) { return pasteIntent.apply(payload) }
    function cancelPaste(payload) { if (pasteIntent.intent === payload) pasteIntent.cancel() }
    function paste() { pasteIntent.request(false) }
    onAmountChanged: { pasteIntent.cancel(); ownedPasteTicket = 0; ownedPasteRevision = -1 }

    CapturedPaste {
        id: pasteIntent
        textInput: amountInput
        context: root.amount
        active: root.visible && root.enabled && !root.readOnly && !!root.amount
        effectPending: root.amount ? root.amount.inputPending : false
        menuObjectName: root.inputObjectName + "ContextMenu"
        onRequested: (payload) => {
            root.pasteModel = root.amount
            if (root.amount) payload.inputRevision = root.amount.inputRevision
            if (root.interceptPaste) root.pasteRequested(payload)
            else apply(payload)
        }
        onRestoreRequested: (payload) => amountInput.restoreFromModel()
        onLiteralRequested: (payload) => {
            if (!root.amount) return
            payload.ticket = payload.edited
                ? root.amount.requestEditInput(payload.current, payload.inputRevision, payload.cursor)
                : root.amount.requestPaste(payload.text, payload.start, payload.end, payload.inputRevision)
            root.ownedPasteTicket = payload.ticket
            root.ownedPasteRevision = root.amount.inputRevision
            amountInput.restoreFromModel()
            amountInput.cursorPosition = root.amount.inputCursorPosition
            root.inputTextChanged()
            root.textEdited()
        }
        onCancelRequested: (payload) => {
            const model = root.pasteModel
            root.pasteModel = null
            if (model && payload.model === model && payload.ticket) model.cancelInput(payload.ticket)
        }
    }

    function amountInputPlaceholder(unit) {
        if (unit === BitcoinAmount.SAT) return "0"
        if (unit === BitcoinAmount.MicroBTC) return "0.00"
        if (unit === BitcoinAmount.MilliBTC) return "0.00000"
        return "0.00000000"
    }

    function flippedDisplayUnit(unit) {
        return unit === BitcoinAmount.SAT ? BitcoinAmount.BTC : BitcoinAmount.SAT
    }

    Layout.fillWidth: true
    spacing: 4

    Item {
        id: inputRow
        Layout.fillWidth: true
        Layout.preferredHeight: 56

        CoreText {
            id: lbl
            visible: root.showLabel
            width: visible ? 128 : 0
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            horizontalAlignment: Text.AlignLeft
            text: root.labelText
            font: Theme.text.body.font
            lineHeight: Theme.text.body.lineHeight
            lineHeightMode: Text.FixedHeight
        }

        TextField {
            id: amountInput
            function restoreFromModel() {
                text = Qt.binding(function() { return root.amount ? root.amount.inputText : "" })
            }
            function finishAmountEditing() {
                if (pasteIntent.pending) return
                if (root.amount) root.amount.format()
                root.editingFinished(text)
            }

            function handlePaymentUriInput() {
                if (!root.interceptPaste || pasteIntent.literalActive) return false
                const previous = root.amount ? root.amount.inputText : ""
                const current = text
                const cursor = cursorPosition
                const clipboard = Clipboard.text()
                const longEdit = previous.length > 512 || current.length > 512 || clipboard.length > 512
                const uri = longEdit ? "" : root.paymentUri(clipboard)
                const fromClipboard = !longEdit && uri.length > 0 && root.shortProvenance(previous, current, clipboard)
                const typedUri = !longEdit && current.toLowerCase().indexOf("bitcoin:") >= 0
                if (!longEdit && !fromClipboard && !typedUri) return false
                restoreFromModel()
                if (longEdit || fromClipboard) pasteIntent.nativeEdit(clipboard, previous, current, cursor)
                return true
            }
            Accessible.name: root.accessibleName
            anchors.left: lbl.right
            anchors.right: unitToggle.left
            anchors.verticalCenter: parent.verticalCenter
            leftPadding: 0
            enabled: root.enabled
            font: Theme.text.body.font
            color: Theme.color.neutral9
            placeholderTextColor: enabled ? Theme.color.neutral7 : Theme.color.neutral4
            verticalAlignment: TextInput.AlignVCenter
            background: Item {}
            placeholderText: root.placeholderText
            selectByMouse: true

            Keys.onPressed: (event) => {
                if (root.interceptPaste && event.matches(StandardKey.Paste)) {
                    pasteIntent.request(false)
                    event.accepted = true
                }
            }

            text: root.amount ? root.amount.inputText : ""
            onTextEdited: {
                pasteIntent.invalidate()
                if (handlePaymentUriInput()) return
                if (root.amount) root.amount.editInput(text)
                root.inputTextChanged()
                root.textEdited()
            }
            onEditingFinished: finishAmountEditing()
            onActiveFocusChanged: if (!activeFocus) finishAmountEditing()
            // Intercept long native commits before business validation; the model
            // refuses the exact original input instead of parsing a cut prefix.
            maximumLength: root.interceptPaste ? 2147483647 : 129
            readOnly: root.readOnly || pasteIntent.pending || (root.amount ? root.amount.inputPending : false)
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.RightButton | Qt.MiddleButton
                onPressed: (mouse) => pasteIntent.pointerPressed(mouse)
            }
            Connections {
                target: root.amount
                function onInputChanged() { pasteIntent.invalidate() }
            }

        }

        AmountUnitButton {
            id: unitToggle
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            opacity: root.enabled ? 1.0 : 0.5
            enabled: root.enabled && !!root.amount && root.amount.inputAcceptable
            unit: root.amount ? root.amount.unit : BitcoinAmount.BTC
            onClicked: {
                root.amount.format()
                ApplicationModels.optionsModel.displayUnit = root.flippedDisplayUnit(root.amount.unit)
            }
        }
    }

    RowLayout {
        id: errorRow
        Layout.fillWidth: true
        visible: root.showErrorMessage && root.effectiveErrorText.length > 0

        Icon {
            source: "image://images/alert-filled"
            size: 22
            color: Theme.color.red
        }

        CoreText {
            id: errorTextLabel
            text: root.effectiveErrorText
            font: Theme.text.description.font
            lineHeight: Theme.text.description.lineHeight
            lineHeightMode: Text.FixedHeight
            color: Theme.color.red
            horizontalAlignment: Text.AlignLeft
            Layout.fillWidth: true
        }
    }
}
