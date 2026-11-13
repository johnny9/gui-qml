// Copyright (c) 2026 The Bitcoin Core developers
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
    objectName: "activityDetailsPage"
    property var wallet: null
    property string transactionId: ""
    readonly property ActivityDetailModel activityModel: wallet ? wallet.transactionActivityModel.createDetailView(root) : null
    Binding { target: root.activityModel; property: "transactionId"; value: root.transactionId; when: root.activityModel !== null }
    Binding { target: root.activityModel; property: "active"; value: root.visible && root.wallet !== null && root.wallet.available; when: root.activityModel !== null }
    property var transactionData: activityModel && activityModel.selectedTransactionDetails.txid === transactionId
        ? activityModel.selectedTransactionDetails : ({txid: transactionId, detailsLoading: true})
    readonly property OptionsTransactionLinks transactionLinks: ApplicationModels.optionsModel.createTransactionLinks()
    Binding { target: root.transactionLinks; property: "txid"; value: root.txid }
    property int outputIndex: -1
    property bool detailsExpanded: false
    onTxidChanged: { detailsExpanded = false; entryPopup.close() }
    readonly property string txid: transactionData.txid || ""
    readonly property bool detailsLoading: !!transactionData.detailsLoading
    readonly property string detailsError: transactionData.detailsError || ""
    readonly property string amount: transactionData.amount || ""
    readonly property int depth: transactionData.depth || 0
    readonly property bool statusKnown: !!transactionData.statusKnown
    readonly property bool inactive: !!transactionData.isInactive
    readonly property bool incoming: transactionData.netAmountSat > 0
    readonly property string replacedByTxid: inactive && depth <= 0 ? transactionData.replacedByTxid || "" : ""
    readonly property bool canBump: statusKnown && depth === 0 && !inactive && !!transactionData.canBump
    readonly property var flow: transactionData.flow || ({})
    readonly property string inactiveLabel: replacedByTxid ? qsTr("Replaced")
        : transactionData.status === Transaction.Abandoned ? qsTr("Cancelled")
        : transactionData.status === Transaction.Conflicted ? qsTr("Conflicted")
        : transactionData.status === Transaction.NotAccepted ? qsTr("Not accepted") : ""
    readonly property string actionLabel: {
        switch (transactionData.activityType) {
        case TransactionActivityModel.Send: return qsTr("Sent")
        case TransactionActivityModel.Receive: return qsTr("Received")
        case TransactionActivityModel.Multiple: return qsTr("Multiple actions")
        case TransactionActivityModel.Consolidation: return qsTr("Consolidation")
        case TransactionActivityModel.Split: return qsTr("Split")
        case TransactionActivityModel.InternalTransfer: return qsTr("Sent to yourself")
        case TransactionActivityModel.Mined: return qsTr("Mining reward")
        default: return qsTr("Transaction")
        }
    }
    readonly property real sidePadding: width < 640 ? 16 : 40
    readonly property real detailContentWidth: Math.max(0, Math.min(1100, width - sidePadding * 2))
    signal showTransaction(string txid)

    background: Rectangle { color: Theme.color.neutral0 }
    padding: 0
    title: qsTr("Transaction")

    header: NavigationBar2 {
        objectName: "transactionDetailNavigation"
        leftPadding: (root.width - root.detailContentWidth) / 2
        rightPadding: leftPadding
        topPadding: 24
        bottomPadding: 4
        leftItem: NavButton {
            objectName: "activityDetailsBackButton"
            text: root.width < 640 ? "" : qsTr("Activity")
            Accessible.name: qsTr("Back to Activity")
            iconSource: "image://images/caret-left"
            onClicked: if (root.StackView.view) root.StackView.view.pop()
        }
        centerItem: CoreText {
            objectName: "transactionDetailTitle"
            text: root.title
            font: Theme.text.heading.font
            wrap: false
        }
        rightItem: OverflowMenuButton {
            id: moreButton
            objectName: "transactionDetailMoreButton"
            onClicked: moreMenu.open()
            ContextMenu {
                id: moreMenu
                objectName: "transactionDetailMoreMenu"
                x: moreButton.width - width
                y: moreButton.height + 8
                ContextMenuButton {
                    objectName: "transactionCopyIdMenuButton"
                    text: qsTr("Copy transaction ID")
                    iconSource: "qrc:/icons/copy"
                    onTriggered: Clipboard.setText(root.txid)
                }
                Repeater {
                    model: root.transactionLinks ? root.transactionLinks.links : []
                    delegate: ContextMenuButton {
                        required property int index
                        required property string host
                        required property string url
                        enabled: root.transactionLinks && !root.transactionLinks.pending && root.transactionLinks.error.length === 0
                        objectName: "transactionThirdPartyLink_" + index
                        text: qsTr("Show in %1").arg(host)
                        iconSource: "image://images/export"
                        onTriggered: root.openExternalLink(url)
                    }
                }
            }
        }
    }

    function openPaymentRequestDetail(requestId) {
        if (requestId) paymentRequestModal.openRequest(requestId)
    }

    PaymentRequestModal { id: paymentRequestModal; objectName: "transactionPaymentRequestModal"; wallet: root.wallet }

    function openExternalLink(link) {
        externalLinkPopup.link = link
        externalLinkPopup.open()
    }

    ExternalPopup {
        id: externalLinkPopup
        objectName: "transactionExternalLinkPopup"
        parent: Overlay.overlay
    }

    ScrollView {
        id: scroll
        objectName: "transactionDetailScroll"
        anchors.fill: parent
        clip: true
        contentWidth: availableWidth
        contentHeight: content.height + 64
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

        Item {
            width: scroll.availableWidth
            height: scroll.contentHeight
            ColumnLayout {
                id: content
                x: (parent.width - width) / 2
                y: 24
                width: root.detailContentWidth
                spacing: 28

                TransactionSummary {
                    visible: root.amount.length > 0
                    Layout.fillWidth: true
                    Layout.bottomMargin: 8
                    amount: root.amount
                    amountColor: root.inactive ? Theme.color.neutral7 : root.incoming ? Theme.color.green : Theme.color.neutral9
                    subtitle: root.actionLabel + (root.transactionData.timestamp
                        ? " · " + Qt.formatDateTime(new Date(root.transactionData.timestamp * 1000), "MMM d, h:mm AP") : "")
                    iconComponent: ActivityIcon {
                        objectName: "transactionDetailIcon"
                        iconSize: 48
                        activityType: root.transactionData.activityType === undefined ? TransactionActivityModel.Other : root.transactionData.activityType
                        incoming: root.incoming
                        pending: root.statusKnown && root.depth === 0
                        inactive: root.inactive
                    }
                    statusComponent: RowLayout {
                        spacing: 8
                        ConfirmationPill {
                            objectName: "transactionConfirmationPill"
                            confirmations: root.depth
                            statusKnown: root.statusKnown
                            inactive: root.inactive
                        }
                        Rectangle {
                            visible: root.inactiveLabel.length > 0
                            implicitWidth: inactiveText.implicitWidth + 24
                            implicitHeight: 30
                            radius: 15
                            color: Theme.color.neutral2
                            CoreText {
                                id: inactiveText
                                anchors.centerIn: parent
                                text: root.inactiveLabel
                                color: Theme.color.neutral7
                                font: Theme.text.captionStrong.font
                            }
                        }
                    }
                }

                BusyIndicator {
                    objectName: "transactionDetailsLoading"
                    Layout.alignment: Qt.AlignHCenter
                    visible: root.detailsLoading
                    running: visible
                    Accessible.name: qsTr("Loading transaction details")
                }
                InfoBanner {
                    objectName: "transactionDetailsError"
                    Layout.fillWidth: true
                    visible: root.detailsError.length > 0
                    message: root.detailsError
                    primaryButtonText: qsTr("Try again")
                    onPrimaryClicked: if (root.activityModel) root.activityModel.reload()
                }
                InfoBanner {
                    objectName: "speedUpBanner"
                    visible: root.canBump
                    Layout.fillWidth: true
                    backgroundColor: Theme.color.neutral1
                    radius: 16
                    contentMargin: 20
                    bannerLayout: width < 640 ? InfoBanner.Layout.Vertical : InfoBanner.Layout.Horizontal
                    messageFontPixelSize: Theme.text.description.font.pixelSize
                    messageLineHeight: Theme.text.description.lineHeight
                    message: qsTr("This transaction is still unconfirmed. You can speed it up by increasing the fee")
                    primaryButtonText: qsTr("Speed up")
                    onPrimaryClicked: speedUpOverlay.open()
                }
                InfoBanner {
                    objectName: "replacedBanner"
                    visible: root.replacedByTxid.length > 0
                    Layout.fillWidth: true
                    backgroundColor: Theme.color.neutral1
                    radius: 16
                    contentMargin: 20
                    bannerLayout: width < 640 ? InfoBanner.Layout.Vertical : InfoBanner.Layout.Horizontal
                    title: qsTr("Replaced by a newer transaction")
                    message: root.replacedByTxid.substring(0, 12) + "…" + root.replacedByTxid.slice(-12)
                    primaryButtonText: qsTr("View replacement")
                    primaryButtonOutlined: true
                    onPrimaryClicked: root.showTransaction(root.replacedByTxid)
                }
                InfoBanner {
                    objectName: "transactionMaturityBanner"
                    visible: !root.inactive && root.transactionData.blocksToMaturity > 0
                    Layout.fillWidth: true
                    backgroundColor: Theme.color.neutral1
                    contentMargin: 20
                    message: qsTr("This mining reward will be spendable after %1 more blocks.").arg(root.transactionData.blocksToMaturity || 0)
                }

                TransactionFlow {
                    objectName: "transactionDetailFlow"
                    Layout.fillWidth: true
                    visible: !!root.flow.inputs && !!root.flow.outputs
                    flow: root.flow
                    transactionId: root.txid
                    displayUnit: ApplicationModels.optionsModel.displayUnit
                    walletName: root.wallet && root.wallet.name ? root.wallet.name : qsTr("Your wallet")
                    selectedEntryId: root.outputIndex >= 0 ? "output:" + root.outputIndex : ""
                    onPaymentRequestRequested: function(requestId) { root.openPaymentRequestDetail(requestId) }
                }
                DropdownButton {
                    objectName: "transactionDetailsToggle"
                    Layout.alignment: Qt.AlignHCenter
                    text: root.detailsExpanded ? qsTr("Hide details") : qsTr("Show details")
                    opened: root.detailsExpanded
                    onClicked: root.detailsExpanded = !root.detailsExpanded
                }
                TransactionEntriesList {
                    objectName: "transactionAllInputs"
                    visible: root.detailsExpanded
                    Layout.fillWidth: true
                    title: qsTr("All inputs")
                    input: true
                    entryModel: root.activityModel ? root.activityModel.detailInputs : null
                    walletName: root.wallet && root.wallet.name ? root.wallet.name : qsTr("Your wallet")
                    onEntryRequested: (row) => root.openEntry("inputs", row)
                    onPaymentRequestRequested: (requestId) => root.openPaymentRequestDetail(requestId)
                }
                TransactionEntriesList {
                    objectName: "transactionAllOutputs"
                    visible: root.detailsExpanded
                    Layout.fillWidth: true
                    title: qsTr("All outputs")
                    entryModel: root.activityModel ? root.activityModel.detailOutputs : null
                    walletName: root.wallet && root.wallet.name ? root.wallet.name : qsTr("Your wallet")
                    onEntryRequested: (row) => root.openEntry("outputs", row)
                    onPaymentRequestRequested: (requestId) => root.openPaymentRequestDetail(requestId)
                }
                TransactionEntriesList {
                    objectName: "transactionAllActions"
                    visible: root.detailsExpanded
                    Layout.fillWidth: true
                    title: qsTr("All actions")
                    entryModel: root.activityModel ? root.activityModel.detailActions : null
                    walletName: root.wallet && root.wallet.name ? root.wallet.name : qsTr("Your wallet")
                    onEntryRequested: (row) => root.openEntry("actions", row)
                    onPaymentRequestRequested: (requestId) => root.openPaymentRequestDetail(requestId)
                }
                TransactionDetailsSection {
                    visible: root.detailsExpanded
                    Layout.fillWidth: true
                    details: root.transactionData
                    activityModel: root.activityModel
                    enabled: !!root.wallet && root.wallet.available
                }
            }
        }
    }

    function openEntry(side, row) {
        if (!root.wallet || !root.wallet.available || !root.activityModel) return
        if (root.activityModel.requestEntryDetails(side, row)) entryPopup.open()
    }
    Popup {
        id: entryPopup
        objectName: "transactionEntryPopup"
        parent: Overlay.overlay
        anchors.centerIn: Overlay.overlay
        width: Math.min(720, Overlay.overlay ? Overlay.overlay.width - 32 : 720)
        height: Math.min(600, Overlay.overlay ? Overlay.overlay.height - 32 : 600)
        modal: true
        padding: 20
        contentItem: ColumnLayout {
            spacing: 12
            CoreText { text: qsTr("Complete entry"); font: Theme.text.subheading.font }
            BusyIndicator {
                visible: root.activityModel && root.activityModel.entryDetailsBusy
                running: visible
            }
            CoreText {
                Layout.fillWidth: true
                visible: !!root.activityModel && root.activityModel.entryDetailsError.length > 0
                text: root.activityModel ? root.activityModel.entryDetailsError : ""
                color: Theme.color.red
                wrap: true
            }
            ListView {
                id: entryFields
                objectName: "transactionEntryDocument"
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                cacheBuffer: 0
                reuseItems: true
                model: entryPopup.opened && root.activityModel ? root.activityModel.entryDocument : null
                spacing: 12
                ScrollBar.vertical: ScrollBar {}
                delegate: ColumnLayout {
                    id: documentRow
                    required property var entry
                    width: entryFields.width
                    CoreText { Layout.fillWidth: true; text: documentRow.entry.field; font: Theme.text.captionStrong.font; wrap: true }
                    TextArea {
                        Layout.fillWidth: true
                        text: documentRow.entry.content
                        readOnly: true
                        selectByMouse: true
                        textFormat: TextEdit.PlainText
                        wrapMode: TextEdit.WrapAnywhere
                        font: Theme.text.monoCaption.font
                        color: Theme.color.neutral9
                        padding: 0
                        background: null
                    }
                }
            }
            TextButton { text: qsTr("Close"); onClicked: entryPopup.close() }
        }
    }
    onVisibleChanged: if (!visible) entryPopup.close()
    onWalletChanged: entryPopup.close()

    SpeedUpOverlay {
        id: speedUpOverlay
        txid: root.txid
        bumpModel: root.wallet ? root.wallet.bumpModel : null
        onBumpSucceeded: {
            speedUpComplete.txid = speedUpOverlay.newTxid
            speedUpComplete.open()
        }
    }

    SpeedUpComplete {
        id: speedUpComplete
        onClosed: if (root.wallet) root.wallet.transactionActivityModel.reload()
        onDone: close()
        onViewNewTransaction: function(txid) {
            close()
            root.showTransaction(txid)
        }
    }
}
