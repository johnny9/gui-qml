// Copyright (c) 2024-2025 The Bitcoin Core developers
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

    signal showTransaction(string txid)

    property string txid: ""
    property bool canBump: false
    property string replacedByTxid: ""
    property string message: ""
    property string amount: ""
    property string label: ""
    property string address: ""
    property string direction: ""
    property string date: ""
    property int depth: 0
    property int type: 0
    property int status: 0
    property var paymentRequests: []
    readonly property int paymentRequestCount: root.paymentRequests ? root.paymentRequests.length : 0

    property color iconColor: {
        if (root.status == Transaction.Confirmed) {
            if (root.type == Transaction.RecvWithAddress ||
                root.type == Transaction.RecvFromOther ||
                root.type == Transaction.Generated) {
                Theme.color.green
            } else {
                Theme.color.orange
            }
        } else {
            Theme.color.blue
        }
    }
    property color amountColor: {
        if (root.type == Transaction.RecvWithAddress
            || root.type == Transaction.RecvFromOther
            || root.type == Transaction.Generated) {
            Theme.color.green
        } else {
            Theme.color.neutral9
        }
    }

    background: null

    function paymentRequestTitle(request) {
        if (request && request.label && request.label.length > 0) {
            return request.label
        }
        return qsTr("Payment request")
    }

    function paymentRequestSubtitle(request) {
        var amount = request && request.amountDisplay && request.amountDisplay.length > 0
            ? request.amountDisplay
            : qsTr("No amount")
        if (request && request.date && request.date.length > 0) {
            return amount + " - " + request.date
        }
        return amount
    }

    function openPaymentRequestDetail(requestId) {
        if (!walletController.selectedWallet || !root.StackView.view || requestId.length === 0) {
            return
        }
        if (walletController.selectedWallet.loadPaymentRequestDetail(requestId)) {
            root.StackView.view.push(paymentRequestDetailPage)
        }
    }

    header: NavigationBar2 {
        id: navbar
        leftItem: NavButton {
            iconSource: "image://images/caret-left"
            text: qsTr("Back")
            onClicked: {
                root.StackView.view.pop()
            }
        }
        centerItem: Item {
            id: header
            Layout.fillWidth: true

            CoreText {
                anchors.left: parent.left
                text: qsTr("Transaction")
                font.pixelSize: 18
                bold: true
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
            anchors.horizontalCenter: parent.horizontalCenter
            width: Math.min(parent.width, 450)
            opacity: root.replacedByTxid !== "" ? 0.4 : 1.0
            spacing: 0

            Rectangle {
                Layout.topMargin: 25
                Layout.bottomMargin: 25
                width: 60
                height: 60
                Layout.alignment: Qt.AlignHCenter
                radius: 30
                color: root.iconColor

                Icon {
                    anchors.centerIn: parent
                    source: {
                        if (root.type == Transaction.RecvWithAddress
                            || root.type == Transaction.RecvFromOther) {
                            "qrc:/icons/triangle-down"
                        } else if (root.type == Transaction.Generated) {
                            "qrc:/icons/coinbase"
                        } else {
                            "qrc:/icons/triangle-up"
                        }
                    }
                    color: Theme.color.white
                    size: 30
                }
            }

            CoreText {
                Layout.alignment: Qt.AlignHCenter
                Layout.bottomMargin: 5
                text: root.amount
                color: amountColor
                font.pixelSize: 28
            }

            CoreText {
                Layout.alignment: Qt.AlignHCenter
                text: root.date
                color: Theme.color.neutral7
                font.pixelSize: 18
            }

            CoreText {
                Layout.alignment: Qt.AlignHCenter
                Layout.bottomMargin: 10
                text: qsTr("%1 confirmations").arg(root.depth)
                color: Theme.color.neutral7
                font.pixelSize: 18
            }

            LabeledTextInput {
                id: labelTextInput
                Layout.fillWidth: true
                Layout.bottomMargin: 20
                labelText: qsTr("Note to self")
                visible: root.label != ""
                enabled: false
                text: root.label
            }

            LabeledTextInput {
                id: messageTextInput
                Layout.fillWidth: true
                Layout.bottomMargin: 20
                labelText: qsTr("Message")
                visible: root.message != ""
                enabled: false
                text: root.message
            }

            Item {
                height: addressLabel.height + addressText.height
                Layout.fillWidth: true
                Layout.bottomMargin: 20
                CoreText {
                    id: addressLabel
                    anchors.left: parent.left
                    anchors.top: parent.top
                    color: Theme.color.neutral7
                    text: qsTr("Address")
                    font.pixelSize: 15
                }

                CoreText {
                    id: addressText
                    anchors.left: parent.left
                    anchors.right: copyIcon.left
                    anchors.top: addressLabel.bottom
                    leftPadding: 0
                    font.family: "Roboto Mono"
                    font.styleName: "Regular"
                    font.pixelSize: 18
                    horizontalAlignment: Text.AlignLeft
                    color: Theme.color.neutral9
                    text: root.address
                    wrapMode: Text.WrapAnywhere
                }

                Icon {
                    id: copyIcon
                    anchors.right: parent.right
                    anchors.verticalCenter: addressText.verticalCenter
                    source: "image://images/copy"
                    color: Theme.color.neutral8
                    size: 30
                    enabled: true
                    onClicked: {
                        Clipboard.setText(addressText.text.replace(/\s+/g, ""))
                    }
                }
            }

            ColumnLayout {
                id: paymentRequestsSection
                objectName: "activityDetailsPaymentRequestsSection"
                visible: root.paymentRequestCount > 0
                Layout.fillWidth: true
                Layout.topMargin: 10
                Layout.bottomMargin: 20
                spacing: 0

                Repeater {
                    model: root.paymentRequests

                    delegate: ItemDelegate {
                        id: paymentRequestDelegate
                        required property int index
                        required property var modelData

                        objectName: "activityDetailsPaymentRequest_" + paymentRequestDelegate.index
                        Layout.fillWidth: true
                        leftPadding: 0
                        rightPadding: 0
                        hoverEnabled: AppMode.isDesktop
                        background: Item {
                            Rectangle {
                                anchors.fill: parent
                                color: paymentRequestDelegate.hovered ? Theme.color.neutral1 : "transparent"
                                radius: 4
                            }
                        }
                        onClicked: root.openPaymentRequestDetail(paymentRequestDelegate.modelData.requestId
                            ? String(paymentRequestDelegate.modelData.requestId)
                            : "")

                        contentItem: RowLayout {
                            spacing: 10

                            Icon {
                                Layout.alignment: Qt.AlignVCenter
                                source: "qrc:/icons/triangle-down"
                                color: Theme.color.purple
                                size: 14
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2

                                CoreText {
                                    objectName: "activityDetailsPaymentRequestTitle_" + paymentRequestDelegate.index
                                    Layout.fillWidth: true
                                    text: root.paymentRequestTitle(paymentRequestDelegate.modelData)
                                    color: paymentRequestDelegate.hovered ? Theme.color.orange : Theme.color.neutral9
                                    font.pixelSize: 16
                                    elide: Text.ElideRight
                                }

                                CoreText {
                                    objectName: "activityDetailsPaymentRequestSubtitle_" + paymentRequestDelegate.index
                                    Layout.fillWidth: true
                                    text: root.paymentRequestSubtitle(paymentRequestDelegate.modelData)
                                    color: Theme.color.neutral7
                                    font.pixelSize: 14
                                    elide: Text.ElideRight
                                }
                            }

                            CaretRightIcon {
                                Layout.alignment: Qt.AlignVCenter
                                color: paymentRequestDelegate.hovered ? Theme.color.orange : Theme.color.neutral7
                            }
                        }
                    }
                }
            }

            InfoBanner {
                objectName: "speedUpBanner"
                visible: root.canBump
                Layout.alignment: Qt.AlignHCenter
                Layout.fillWidth: true
                Layout.maximumWidth: 600
                Layout.topMargin: 20
                bannerLayout: InfoBanner.Layout.Vertical
                message: qsTr("This transaction is still unconfirmed. You can speed it up by increasing the fee.")
                primaryButtonText: qsTr("Speed up")
                onPrimaryClicked: speedUpOverlay.open()
            }

        }
    }

    Component {
        id: paymentRequestDetailPage
        PaymentRequestDetail {}
    }

    InfoBanner {
        objectName: "replacedBanner"
        visible: root.replacedByTxid !== ""
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 20
        anchors.bottomMargin: 30
        title: qsTr("This transaction was updated with a faster one")
        message: qsTr("You increased the fee while this transaction was still unconfirmed. Only the new one will confirm on-chain.")
        primaryButtonText: qsTr("View updated transaction")
        onPrimaryClicked: root.showTransaction(root.replacedByTxid)
    }

    SpeedUpOverlay {
        id: speedUpOverlay
        txid: root.txid
        anchors.centerIn: parent
        onBumpSucceeded: {
            var page = root.StackView.view.push("SendResult.qml", {
                resultType: SendResult.ResultType.SpeedUp
            })
            page.done.connect(function() {
                if (walletController.selectedWallet) {
                    walletController.selectedWallet.activityListModel.reload()
                }
                root.StackView.view.pop(null)
            })
            page.viewNewTransaction.connect(function() {
                if (walletController.selectedWallet) {
                    walletController.selectedWallet.activityListModel.reload()
                }
                root.StackView.view.pop(null)
                root.showTransaction(speedUpOverlay.newTxid)
            })
        }
    }
}
