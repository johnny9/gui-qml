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
    Component.onCompleted: {
        wallet = walletManager.walletBySession(sessionId)
        if (send) send.coins.active = true
    }
    Component.onDestruction: { if (send) send.coins.active = false }
    signal back()
    Binding { target: root.send ? root.send.fees : null; property: "displayUnit"; value: optionsModel.displayUnit }
    Binding { target: root.send ? root.send.coins : null; property: "displayUnit"; value: optionsModel.displayUnit }
    background: Rectangle { color: Theme.color.background }
    header: ToolBar {
        RowLayout {
            anchors.fill: parent
            Button { text: qsTr("Back"); onClicked: root.back() }
            Label { text: root.wallet ? qsTr("Send from %1").arg(root.wallet.overview.displayName) : qsTr("Wallet unavailable"); Layout.fillWidth: true }
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
            Button { text: qsTr("Add recipient"); onClicked: root.send.recipients.add() }
            GroupBox {
                title: qsTr("Transaction fee")
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
            Label {
                objectName: "sendError"
                text: root.send.error
                visible: text.length > 0
                Layout.fillWidth: true
                wrapMode: Text.Wrap
            }
        }
    }
    }
}
