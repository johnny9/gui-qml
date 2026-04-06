// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0

import "../../controls"
import "../../components"

Page {
    id: root
    objectName: "multipleSendReviewPage"
    background: null

    property WalletQmlModel wallet: walletController.selectedWallet
    property WalletQmlModelTransaction transaction: walletController.selectedWallet.currentTransaction

    signal finished()
    signal back()
    signal transactionSent()

    header: NavigationBar2 {
        id: navbar
        leftItem: NavButton {
            iconSource: "image://images/caret-left"
            text: qsTr("Back")
            onClicked: {
                root.back()
            }
        }
    }

    ScrollView {
        clip: true
        width: parent.width
        height: parent.height
        contentWidth: width

        ColumnLayout {
            id: columnLayout
            width: 450
            anchors.horizontalCenter: parent.horizontalCenter

            spacing: 10

            CoreText {
                id: title
                Layout.topMargin: 30
                Layout.bottomMargin: 20
                text: qsTr("Transaction details")
                font.pixelSize: 21
                bold: true
            }

            ListView {
                id: inputsList
                objectName: "multipleSendReviewRecipientsList"
                Layout.fillWidth: true
                Layout.preferredHeight: contentHeight
                model: root.wallet.recipients
                clip: true

                delegate: Item {
                    id: delegate
                    implicitHeight: delegateColumn.implicitHeight
                    height: implicitHeight
                    width: ListView.view.width

                    required property string address;
                    required property string label;
                    required property string amount;
                    required property string formattedAddress;
                    required property string amountUnitLabel;

                    ColumnLayout {
                        id: delegateColumn
                        width: parent.width
                        spacing: 0

                        RowLayout {
                            Layout.topMargin: 10
                            Layout.fillWidth: true
                            spacing: 10

                            CoreText {
                                objectName: "multipleSendReviewRecipient" + index + "Label"
                                Layout.fillWidth: true
                                Layout.preferredWidth: 0
                                horizontalAlignment: Text.AlignLeft
                                text: label
                                font.pixelSize: 18
                                color: Theme.color.neutral9
                                visible: label.length > 0
                            }

                            Item {
                                Layout.fillWidth: true
                                visible: label.length === 0
                            }

                            Item {
                                id: amountDisplay
                                objectName: "multipleSendReviewRecipient" + index + "Amount"
                                property string text: amountUnitLabel.length > 0 ? amount + " " + amountUnitLabel : amount
                                implicitWidth: amountRow.implicitWidth
                                implicitHeight: amountRow.implicitHeight
                                Layout.alignment: Qt.AlignRight

                                RowLayout {
                                    id: amountRow
                                    anchors.fill: parent
                                    spacing: 6

                                    CoreText {
                                        text: amount
                                        font.pixelSize: 18
                                        wrap: false
                                        color: Theme.color.neutral9
                                    }

                                    CoreText {
                                        visible: amountUnitLabel.length > 0
                                        text: amountUnitLabel
                                        font.pixelSize: 18
                                        color: Theme.color.neutral7
                                    }
                                }
                            }
                        }

                        CoreText {
                            objectName: "multipleSendReviewRecipient" + index + "Address"
                            Layout.fillWidth: true
                            Layout.leftMargin: 110
                            Layout.topMargin: label.length > 0 ? 4 : 0
                            horizontalAlignment: Text.AlignLeft
                            text: formattedAddress
                            font.pixelSize: 18
                            color: Theme.color.neutral9
                        }

                        Separator {
                            Layout.topMargin: 10
                            Layout.fillWidth: true
                        }
                    }
                }
            }

            BitcoinAmountDisplayField {
                objectName: "multipleSendReviewTotalField"
                Layout.topMargin: 10
                labelText: qsTr("Total")
                amountText: root.transaction ? root.transaction.totalAmount.display : ""
                unitText: root.transaction ? root.transaction.totalAmount.unitLabel : ""
            }

            Separator {
                Layout.fillWidth: true
            }

            BitcoinAmountDisplayField {
                objectName: "multipleSendReviewFeeField"
                labelText: qsTr("Fee")
                amountText: root.transaction ? root.transaction.feeAmount.display : ""
                unitText: root.transaction ? root.transaction.feeAmount.unitLabel : ""
            }

            ContinueButton {
                id: confirmationButton
                objectName: "multipleSendReviewSendButton"
                Layout.fillWidth: true
                Layout.topMargin: 30
                text: qsTr("Send")
                onClicked: {
                    root.wallet.sendTransaction()
                    root.transactionSent()
                }
            }
        }
    }
}
