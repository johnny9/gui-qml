// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0

import "../../controls"
import "../../components"
import "../settings"

Page {
    id: root
    background: null

    ScrollView {
        clip: true
        width: parent.width
        height: parent.height
        contentWidth: width

        CoreText {
            id: title
            anchors.left: contentRow.left
            anchors.top: parent.top
            anchors.topMargin: 20
            text: qsTr("Request a payment")
            font.pixelSize: 21
            bold: true
        }

        RowLayout {
            id: contentRow
            anchors.top: title.bottom
            anchors.topMargin: 40
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 30
            ColumnLayout {
                id: columnLayout
                Layout.minimumWidth: 450
                Layout.maximumWidth: 470

                spacing: 5

                Item {
                    BitcoinAmount {
                        id: bitcoinAmount
                    }

                    height: amountInput.height
                    Layout.fillWidth: true
                    CoreText {
                        id: amountLabel
                        width: 110
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        horizontalAlignment: Text.AlignLeft
                        color: Theme.color.neutral9
                        text: "Amount"
                        font.pixelSize: 18
                    }

                    TextField {
                        id: amountInput
                        anchors.left: amountLabel.right
                        anchors.verticalCenter: parent.verticalCenter
                        leftPadding: 0
                        font.family: "Inter"
                        font.styleName: "Regular"
                        font.pixelSize: 18
                        color: Theme.color.neutral9
                        placeholderTextColor: Theme.color.neutral7
                        background: Item {}
                        placeholderText: "0.00000000"
                        selectByMouse: true
                        onTextEdited: {
                            amountInput.text = bitcoinAmount.sanitize(amountInput.text)
                        }
                    }
                    Item {
                        width: unitLabel.width + flipIcon.width
                        height: Math.max(unitLabel.height, flipIcon.height)
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                if (bitcoinAmount.unit == BitcoinAmount.BTC) {
                                    amountInput.text = bitcoinAmount.convert(amountInput.text, BitcoinAmount.BTC)
                                    bitcoinAmount.unit = BitcoinAmount.SAT
                                } else {
                                    amountInput.text = bitcoinAmount.convert(amountInput.text, BitcoinAmount.SAT)
                                    bitcoinAmount.unit = BitcoinAmount.BTC
                                }
                            }
                        }
                        CoreText {
                            id: unitLabel
                            anchors.right: flipIcon.left
                            anchors.verticalCenter: parent.verticalCenter
                            text: bitcoinAmount.unitLabel
                            font.pixelSize: 18
                            color: Theme.color.neutral7
                        }
                        Icon {
                            id: flipIcon
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            source: "image://images/flip-vertical"
                            color: Theme.color.neutral8
                            size: 30
                        }
                    }
                }

                Separator {
                    Layout.fillWidth: true
                }

                LabeledTextInput {
                    id: label
                    Layout.fillWidth: true
                    labelText: qsTr("Label")
                    placeholderText: qsTr("Enter label...")
                }

                Separator {
                    Layout.fillWidth: true
                }

                LabeledTextInput {
                    id: message
                    Layout.fillWidth: true
                    labelText: qsTr("Message")
                    placeholderText: qsTr("Enter message...")
                }

                Separator {
                    Layout.fillWidth: true
                }

                Item {

                }

                ContinueButton {
                    id: continueButton
                    Layout.fillWidth: true
                    Layout.topMargin: 30
                    text: qsTr("Create bitcoin address")
                }
            }

            Rectangle {
                id: qrPlaceholder
                Layout.alignment: Qt.AlignTop
                Layout.minimumWidth: 150
                Layout.maximumWidth: 150
                color: Theme.color.neutral2
                width: 150
                height: 150
            }
        }
    }
}
