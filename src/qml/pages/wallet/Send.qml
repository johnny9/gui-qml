// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../../controls"

Page {
    id: root
    objectName: "walletSendPage"
    required property string sessionId
    property var wallet: null
    readonly property var send: wallet ? wallet.send : null
    readonly property bool reviewing: !!send && send.review.hasReview
    readonly property bool hasResult: !!send && send.submissionStatus.length > 0
    Component.onCompleted: {
        if (!wallet) wallet = walletManager.walletBySession(sessionId)
        if (send) send.coins.active = true
    }
    Component.onDestruction: { if (send) { send.coins.active = false; send.invalidateReview() } }
    signal back()
    signal navigateRequested(string route, var parameters)
    Binding { target: root.send ? root.send.fees : null; property: "displayUnit"; value: optionsModel.displayUnit }
    Binding { target: root.send ? root.send.coins : null; property: "displayUnit"; value: optionsModel.displayUnit }
    Binding { target: root.send ? root.send.review : null; property: "displayUnit"; value: optionsModel.displayUnit }
    background: Rectangle { color: Theme.color.background }
    header: ToolBar {
        RowLayout {
            anchors.fill: parent
            Button { text: qsTr("Back"); onClicked: { if (root.reviewing || root.hasResult) root.send.editDraft(); else root.back() } }
            Label { text: root.wallet ? qsTr("Send from %1").arg(root.wallet.overview.displayName) : qsTr("Wallet unavailable"); Layout.fillWidth: true }
        }
    }
    footer: ToolBar {
        visible: !!root.send
        contentItem: RowLayout {
            TextField {
                id: passphrase
                objectName: "sendPassphrase"
                Layout.fillWidth: true
                visible: !!root.send && root.send.needsPassphrase && !root.reviewing && !root.hasResult
                enabled: !!root.send && !root.send.busy
                placeholderText: qsTr("Wallet passphrase")
                echoMode: TextInput.Password
                inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText
            }
            Button {
                objectName: "sendPrepareButton"
                visible: !root.reviewing && !root.hasResult
                text: root.send && root.send.busy ? qsTr("Preparing…") : qsTr("Prepare transaction")
                enabled: !!root.send && root.send.canPrepare
                onClicked: {
                    const secret = passphrase.text
                    passphrase.clear()
                    root.send.prepare(secret)
                }
            }
            Button {
                objectName: "sendSubmitButton"
                visible: root.reviewing
                text: qsTr("Submit transaction")
                enabled: !!root.send && root.send.canSubmit
                onClicked: root.send.submit()
            }
            Button {
                objectName: "sendEditDraftButton"
                visible: root.hasResult && !!root.send && !root.send.busy
                text: root.send && root.send.submittedTransactionId.length ? qsTr("New payment") : qsTr("Edit draft")
                onClicked: root.send.editDraft()
            }
        }
    }
    Loader {
      anchors.fill: parent
      active: !!root.send
      sourceComponent: ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        ColumnLayout {
            width: parent.width
            spacing: 12
            enabled: root.send.available
            ColumnLayout {
              Layout.fillWidth: true
              visible: !root.reviewing && !root.hasResult
              Repeater {
                model: root.send.recipients
                delegate: GroupBox {
                    required property int index
                    required property string recipientAddress
                    required property string recipientAmount
                    required property string recipientLabel
                    required property bool subtractFee
                    required property bool maximum
                    Layout.fillWidth: true
                    title: qsTr("Recipient %1").arg(index + 1)
                    enabled: !root.send.busy
                    ColumnLayout {
                        anchors.fill: parent
                        TextField {
                            objectName: "sendAddress-" + index
                            Layout.fillWidth: true
                            placeholderText: qsTr("Bitcoin address")
                            text: recipientAddress
                            onTextEdited: root.send.recipients.setAddress(index, text)
                        }
                        TextField {
                            objectName: "sendAmount-" + index
                            Layout.fillWidth: true
                            placeholderText: qsTr("Amount (BTC)")
                            text: recipientAmount
                            enabled: !maximum
                            onTextEdited: root.send.recipients.setAmount(index, text)
                        }
                        TextField {
                            Layout.fillWidth: true
                            placeholderText: qsTr("Label (optional)")
                            text: recipientLabel
                            onTextEdited: root.send.recipients.setLabel(index, text)
                        }
                        CheckBox {
                            text: qsTr("Subtract fee from this amount")
                            checked: subtractFee
                            enabled: !maximum
                            onToggled: root.send.recipients.setSubtractFee(index, checked)
                        }
                        CheckBox {
                            text: qsTr("Use remaining balance")
                            checked: maximum
                            onToggled: root.send.recipients.setMaximum(index, checked)
                        }
                        Button { text: qsTr("Remove recipient"); onClicked: root.send.recipients.remove(index) }
                    }
                }
            }
            Button { text: qsTr("Add recipient"); enabled: !root.send.busy && root.send.recipients.canAdd; onClicked: root.send.recipients.add() }
            GroupBox {
                title: qsTr("Transaction fee")
                enabled: !root.send.busy
                Layout.fillWidth: true
                ColumnLayout {
                    anchors.fill: parent
                    ComboBox {
                        objectName: "sendFeeTarget"
                        model: [qsTr("Fast (1 block)"), qsTr("Standard (2 blocks)"), qsTr("Economical (6 blocks)")]
                        currentIndex: [1, 2, 6].indexOf(root.send.fees.target)
                        enabled: !root.send.fees.custom
                        onActivated: root.send.fees.target = [1, 2, 6][currentIndex]
                    }
                    CheckBox {
                        objectName: "sendCustomFee"
                        text: qsTr("Custom fee rate")
                        checked: root.send.fees.custom
                        onToggled: root.send.fees.custom = checked
                    }
                    TextField {
                        objectName: "sendCustomFeeRate"
                        visible: root.send.fees.custom
                        placeholderText: qsTr("sat/vB")
                        text: root.send.fees.customRate
                        onTextEdited: root.send.fees.customRate = text
                    }
                    Label {
                        objectName: "sendFeeEstimate"
                        text: root.send.fees.pending ? qsTr("Estimating fee…") : root.send.fees.estimatedFee
                    }
                }
            }
            GroupBox {
                title: qsTr("Coin selection")
                enabled: !root.send.busy
                Layout.fillWidth: true
                ColumnLayout {
                    anchors.fill: parent
                    CheckBox {
                        objectName: "sendManualCoins"
                        text: qsTr("Spend selected coins only")
                        checked: root.send.coins.manual
                        onToggled: root.send.coins.manual = checked
                    }
                    Label { text: root.send.coins.error; visible: text.length > 0 }
                    ListView {
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.min(300, contentHeight)
                        clip: true
                        model: root.send.coins
                        delegate: RowLayout {
                            width: ListView.view.width
                            required property string coinKey
                            required property string coinAddress
                            required property string coinAmount
                            required property int confirmations
                            required property bool coinSelected
                            required property bool coinLocked
                            CheckBox {
                                checked: coinSelected
                                enabled: !coinLocked && !root.send.coins.busy
                                onToggled: root.send.coins.select(coinKey, checked)
                            }
                            Label { text: coinAmount + " · " + coinAddress + " · " + qsTr("%1 confirmations").arg(confirmations); Layout.fillWidth: true; elide: Text.ElideMiddle }
                            Button {
                                text: coinLocked ? qsTr("Unlock coin") : qsTr("Lock coin")
                                enabled: !root.send.coins.busy
                                onClicked: root.send.coins.setLocked(coinKey, !coinLocked)
                            }
                        }
                    }
                }
            }
            }
            Label {
                objectName: "sendError"
                text: root.send.error
                visible: text.length > 0
                Layout.fillWidth: true
                wrapMode: Text.Wrap
            }
            GroupBox {
                objectName: "sendTransactionReview"
                visible: root.send.review.hasReview
                title: qsTr("Review prepared transaction")
                Layout.fillWidth: true
                ColumnLayout {
                    anchors.fill: parent
                    Label { text: qsTr("Transaction: %1").arg(root.send.review.transactionId); Layout.fillWidth: true; wrapMode: Text.WrapAnywhere }
                    Repeater {
                        model: root.send.review.outputs
                        delegate: Label {
                            required property var modelData
                            text: modelData.amount + " → " + modelData.address + (modelData.change ? qsTr(" (change)") : "")
                            Layout.fillWidth: true
                            wrapMode: Text.WrapAnywhere
                        }
                    }
                    Label { objectName: "sendPreparedFee"; text: qsTr("Actual fee: %1").arg(root.send.review.feeText) }
                }
            }
            Label {
                objectName: "sendSubmissionStatus"
                text: root.send.submissionStatus
                visible: text.length > 0
                Layout.fillWidth: true
                wrapMode: Text.Wrap
            }
            Label {
                objectName: "sendSubmittedTransactionId"
                text: root.send.submittedTransactionId
                visible: text.length > 0
                Layout.fillWidth: true
                wrapMode: Text.WrapAnywhere
            }
            Button {
                objectName: "sendViewTransactionButton"
                text: qsTr("View transaction")
                // count's notification tracks refreshed rows; the lookup itself
                // never manufactures an optimistic activity entry.
                readonly property string rowKey: root.wallet.history.count >= 0 ? root.wallet.history.keyForTransaction(root.send.submittedTransactionId) : ""
                visible: root.send.submittedTransactionId.length > 0
                enabled: rowKey.length > 0
                onClicked: root.navigateRequested("wallet-transaction", {"sessionId": root.sessionId, "rowKey": rowKey})
            }
        }
    }
    }
}
