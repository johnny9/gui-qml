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
    objectName: "sendReviewPage"
    background: null

    property WalletQmlModel wallet: walletController.selectedWallet
    property SendRecipient recipient: wallet.recipients.current
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

            BitcoinAddressDisplayField {
                objectName: "sendReviewAddressField"
                text: root.recipient ? root.recipient.address.formattedAddress : ""
            }

            Separator {
                Layout.fillWidth: true
            }

            LabeledValueField {
                id: noteField
                objectName: "sendReviewNoteField"
                visible: text.length > 0
                labelText: qsTr("Note")
                text: root.recipient ? root.recipient.label : ""
            }

            Separator {
                Layout.fillWidth: true
                visible: noteField.visible
            }

            BitcoinAmountDisplayField {
                objectName: "sendReviewAmountField"
                labelText: qsTr("Amount")
                amountText: root.recipient ? root.recipient.amount.display : ""
                unitText: root.recipient ? root.recipient.amount.unitLabel : ""
            }

            Separator {
                Layout.fillWidth: true
            }

            BitcoinAmountDisplayField {
                objectName: "sendReviewFeeField"
                labelText: qsTr("Fee")
                amountText: root.transaction ? root.transaction.feeAmount.display : ""
                unitText: root.transaction ? root.transaction.feeAmount.unitLabel : ""
            }

            Separator {
                Layout.fillWidth: true
            }

            BitcoinAmountDisplayField {
                objectName: "sendReviewTotalField"
                labelText: qsTr("Total")
                amountText: root.transaction ? root.transaction.totalAmount.display : ""
                unitText: root.transaction ? root.transaction.totalAmount.unitLabel : ""
            }

            ContinueButton {
                id: confimationButton
                objectName: "sendReviewSendButton"
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
