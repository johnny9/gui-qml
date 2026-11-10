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
    readonly property string effectiveErrorText: errorText || (amount ? amount.inputError : "")
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
    signal pasteRequested()

    function syncFromAmount(force) {
        if (root.amount && (force || !amountInput.activeFocus)) root.amount.resetInput()
    }

    function paymentUri(text) {
        const trimmedText = String(text).trim()
        return root.interceptPaste && trimmedText.toLowerCase().startsWith("bitcoin:")
            ? trimmedText
            : ""
    }

    function pastedPaymentUri(previousText, currentText) {
        const clipboardText = Clipboard.text()
        const uri = paymentUri(clipboardText)
        if (uri.length === 0 || previousText === currentText) return ""

        let offset = String(currentText).indexOf(clipboardText)
        while (offset !== -1) {
            const prefix = String(currentText).slice(0, offset)
            const suffix = String(currentText).slice(offset + clipboardText.length)
            if ((prefix.length === 0 || String(previousText).startsWith(prefix))
                    && (suffix.length === 0 || String(previousText).endsWith(suffix))
                    && prefix.length + suffix.length <= String(previousText).length) {
                return uri
            }
            offset = String(currentText).indexOf(clipboardText, offset + 1)
        }
        return ""
    }

    function paste() {
        amountInput.paste()
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
            function finishAmountEditing() {
                if (root.amount) root.amount.format()
                root.editingFinished(text)
            }

            function handlePaymentUriInput() {
                const previousText = root.amount ? root.amount.inputText : ""
                const uri = root.pastedPaymentUri(previousText, text)
                const looksLikeUri = root.interceptPaste && String(text).toLowerCase().indexOf("bitcoin:") !== -1
                if (uri.length === 0 && !looksLikeUri) return false
                if (root.amount) root.amount.restoreInput()
                if (uri.length > 0) root.pasteRequested()
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
                    root.pasteRequested()
                    event.accepted = true
                }
            }

            text: root.amount ? root.amount.inputText : ""
            onTextEdited: {
                if (handlePaymentUriInput()) return
                if (root.amount) root.amount.editInput(text)
                root.inputTextChanged()
                root.textEdited()
            }
            onEditingFinished: finishAmountEditing()
            onActiveFocusChanged: if (!activeFocus) finishAmountEditing()
            maximumLength: root.interceptPaste ? 2177 : 129

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
