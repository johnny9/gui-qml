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

    property BitcoinAddress address: null
    property string errorText: ""
    property bool showErrorMessage: true
    property string labelText: qsTr("Send to")
    property bool enabled: true
    property bool embedded: false
    property bool isOnSurface: false
    property color surfaceColor: Theme.color.neutral1
    property bool showLabel: true
    property var textStyle: Theme.text.monoBody
    property alias inputObjectName: addressInput.objectName
    property bool interceptPaste: false
    property alias inputText: addressInput.text
    property alias placeholderText: addressInput.placeholderText
    property bool readOnly: false
    readonly property alias field: addressInput
    readonly property alias contextMenu: pasteIntent.pasteMenu

    signal textChanged()
    signal editingFinished()
    signal pasteRequested(var payload)
    readonly property bool pastePending: pasteIntent.pending
    property BitcoinAddress pasteModel: null

    // Short editor routing is bounded; large native edits are probed on Domain.
    function paymentUri(text) {
        if (!root.interceptPaste || text.length > 512) return ""
        const trimmed = String(text).trim()
        return trimmed.toLowerCase().startsWith("bitcoin:") ? trimmed : ""
    }
    function pastedPaymentUri(previous, current) {
        const clipboard = Clipboard.text()
        if (previous.length > 512 || current.length > 512 || clipboard.length > 512) return ""
        const uri = paymentUri(clipboard)
        return uri.length && previous !== current && String(current).indexOf(clipboard) >= 0 ? uri : ""
    }
    function capturePaste(text, primary) { pasteModel = root.address; return pasteIntent.capture(text, primary, false, "", "", undefined) }
    function applyCapturedPaste(payload) { return pasteIntent.apply(payload) }
    function cancelPaste(payload) { if (pasteIntent.intent === payload) pasteIntent.cancel() }
    function paste() { pasteIntent.request(false) }

    CapturedPaste {
        id: pasteIntent
        textEdit: addressInput
        context: root.address
        active: root.visible && root.enabled && !root.readOnly && !!root.address
        effectPending: root.address ? root.address.normalizationPending : false
        menuObjectName: root.inputObjectName + "ContextMenu"
        onRequested: (payload) => {
            root.pasteModel = root.address
            if (root.interceptPaste) root.pasteRequested(payload)
            else apply(payload)
        }
        onRestoreRequested: (payload) => addressInput.syncFromAddress()
        onLiteralRequested: (payload) => {
            if (!root.address) return
            const cursor = payload.edited
                ? root.address.setAddress(payload.current, payload.cursor)
                : root.address.pasteAddress(payload.text, payload.start, payload.end)
            payload.ticket = root.address.normalizationId
            if (cursor >= 0) { addressInput.syncFromAddress(); addressInput.cursorPosition = cursor }
        }
        onCancelRequested: (payload) => {
            const model = root.pasteModel
            root.pasteModel = null
            if (model && payload.model === model && payload.ticket) model.cancelNormalization(payload.ticket)
        }
    }

    function syncFromAddress() {
        addressInput.syncFromAddress()
    }

    onAddressChanged: { pasteIntent.cancel(); syncFromAddress() }

    Layout.fillWidth: true
    spacing: 4

    Item {
        id: inputRow
        Layout.fillWidth: true
        implicitHeight: root.embedded ? Math.max(52, addressInput.height) : Math.max(56, addressInput.height + 16)

        CoreText {
            id: label
            visible: root.showLabel
            width: visible ? 128 : 0
            anchors.left: parent.left
            anchors.verticalCenter: addressInput.verticalCenter
            horizontalAlignment: Text.AlignLeft
            text: root.labelText
            font: Theme.text.body.font
            lineHeight: Theme.text.body.lineHeight
            lineHeightMode: Text.FixedHeight
        }

        TextArea {
            id: addressInput
            property bool syncingFromAddress: false
            readOnly: root.readOnly || pasteIntent.pending || (root.address ? root.address.normalizationPending : false)

            function syncFromAddress() {
                const formattedAddress = root.address ? root.address.formattedAddress : ""
                if (text === formattedAddress) return
                syncingFromAddress = true
                text = formattedAddress
                syncingFromAddress = false
            }

            function handlePaymentUriInput() {
                if (!root.interceptPaste || pasteIntent.literalActive) return false
                const previous = root.address ? root.address.formattedAddress : ""
                const current = text
                const cursor = cursorPosition
                const clipboard = Clipboard.text()
                const longEdit = previous.length > 512 || current.length > 512 || clipboard.length > 512
                if (!longEdit) {
                    const uri = root.paymentUri(clipboard)
                    if (!uri.length || previous === current || current.indexOf(clipboard) < 0) return false
                }
                syncFromAddress()
                pasteIntent.nativeEdit(clipboard, previous, current, cursor)
                return true
            }
            anchors.left: label.right
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            enabled: root.enabled
            placeholderText: qsTr("Enter address...")
            text: ""
            Component.onCompleted: syncFromAddress()
            wrapMode: Text.WrapAnywhere
            verticalAlignment: TextEdit.AlignVCenter
            leftPadding: root.embedded ? 16 : 0
            topPadding: root.embedded ? 14 : 0
            rightPadding: root.embedded ? 16 : 0
            bottomPadding: root.embedded ? 14 : 0
            height: root.embedded ? Math.max(52, contentHeight + topPadding + bottomPadding) : Math.max(contentHeight, 32)
            font: root.textStyle.font
            color: Theme.color.neutral9
            placeholderTextColor: enabled ? Theme.color.neutral7 : Theme.color.neutral4
            background: Rectangle {
                color: root.isOnSurface ? root.surfaceColor
                    : root.embedded ? Theme.color.neutral2 : "transparent"
                radius: root.embedded ? 10 : 0
                border.width: root.embedded && addressInput.activeFocus ? 2 : 0
                border.color: Theme.color.orange
            }
            selectByMouse: true

            Keys.onPressed: (event) => {
                if (root.interceptPaste && event.matches(StandardKey.Paste)) {
                    pasteIntent.request(false)
                    event.accepted = true
                }
            }

            onTextChanged: {
                if (syncingFromAddress) {
                    root.textChanged()
                    return
                }
                pasteIntent.invalidate()
                if (handlePaymentUriInput()) return
                if (root.address) {
                    const cursor = root.address.setAddress(text, cursorPosition)
                    if (cursor >= 0) cursorPosition = cursor
                }
                root.textChanged()
            }

            onEditingFinished: {
                if (root.address && !pasteIntent.pending && !root.address.normalizationPending) {
                    root.address.setAddress(text, cursorPosition)
                    root.editingFinished()
                }
            }

            onActiveFocusChanged: {
                if (!activeFocus && root.address && !pasteIntent.pending && !root.address.normalizationPending) {
                    root.address.setAddress(text)
                }
            }

            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.RightButton | Qt.MiddleButton
                onPressed: (mouse) => pasteIntent.pointerPressed(mouse)
            }

            Connections {
                target: root.address ? root.address : null
                function onAddressChanged() { pasteIntent.invalidate() }
                function onFormattedAddressChanged() {
                    addressInput.syncFromAddress()
                    if (root.address && !root.address.normalizationPending)
                        addressInput.cursorPosition = root.address.normalizedCursor
                }
            }
        }
    }


    RowLayout {
        id: addressIssue
        Layout.fillWidth: true
        visible: root.showErrorMessage && root.errorText.length > 0
        spacing: 8
        Layout.topMargin: 4

        Icon {
            source: "image://images/alert-filled"
            size: 22
            color: Theme.color.red
        }

        CoreText {
            text: root.errorText
            font: Theme.text.description.font
            lineHeight: Theme.text.description.lineHeight
            lineHeightMode: Text.FixedHeight
            color: Theme.color.red
            horizontalAlignment: Text.AlignLeft
            Layout.fillWidth: true
        }
    }
}
