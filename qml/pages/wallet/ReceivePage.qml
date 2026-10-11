// Copyright (c) 2024-2026 The Bitcoin Core developers
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
    objectName: "requestPaymentPage"
    property var wallet: walletController.selectedWallet
    property var request: wallet ? wallet.currentPaymentRequest : null
    property var clipboard: Clipboard
    property var presentedRequest: null
    property string presentedRequestId: ""
    property int contextRevision: 0
    readonly property real pageSidePadding: width < 640 ? 16 : 40
    readonly property real headerContentWidth: Math.max(0, Math.min(1100, width - pageSidePadding * 2))
    readonly property alias requestModal: requestModal
    readonly property alias draftCard: card
    signal addressHistoryRequested()
    signal paymentRequestCreated()
    background: Rectangle { color: Theme.color.neutral0 }
    padding: 0

    function prepareReceivingAddress() {
        const type = root.request && !root.request.id && root.wallet && root.wallet.receivingAddress.address === ""
            ? root.request.addressType.toLowerCase() : ""
        receivingCard.ensureAddress(false, type)
    }

    // The Addresses page may bring an existing request into Receive.
    // Saved requests always use the same modal, including this entry point.
    function prepareCurrentWallet(revision) {
        if (!visible || contextRevision !== revision) return
        prepareReceivingAddress()
        if (request && request.id && !requestModal.visible) {
            presentedRequest = request
            presentedRequestId = request.id
            requestModal.openRequest(request.id)
        }
    }
    onWalletChanged: {
        ++contextRevision
        presentedRequest = null
        presentedRequestId = ""
        requestModal.close()
        receiveMoreMenu.close()
        Qt.callLater(root.prepareCurrentWallet, contextRevision)
    }
    onVisibleChanged: {
        ++contextRevision
        if (visible) Qt.callLater(root.prepareCurrentWallet, contextRevision)
        else receiveMoreMenu.close()
    }
    Component.onCompleted: Qt.callLater(root.prepareCurrentWallet, contextRevision)

    Item {
        objectName: "requestHistoryCount"
        visible: false
        property int count: root.wallet ? root.wallet.receiveRequests.count : 0
    }

    ScrollView {
        id: scroll
        anchors.fill: parent
        clip: true
        contentWidth: availableWidth
        contentHeight: content.implicitHeight + 48
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ColumnLayout {
            id: content
            width: scroll.availableWidth
            spacing: 24
            RowLayout {
                objectName: "receivePageHeader"
                Layout.fillWidth: true
                Layout.preferredWidth: root.headerContentWidth
                Layout.maximumWidth: root.headerContentWidth
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: 28
                CoreText {
                    Layout.fillWidth: true
                    text: qsTr("Receive bitcoin")
                    font: Theme.text.headline.font
                    lineHeight: Theme.text.headline.lineHeight
                    lineHeightMode: Text.FixedHeight
                    horizontalAlignment: Text.AlignLeft
                }
                OverflowMenuButton {
                    id: receiveMoreButton
                    objectName: "receiveMoreButton"
                    checked: receiveMoreMenu.opened
                    onClicked: receiveMoreMenu.opened ? receiveMoreMenu.close() : receiveMoreMenu.open()
                    ContextMenu {
                        id: receiveMoreMenu
                        objectName: "receiveMoreMenu"
                        property int ownerRevision: -1
                        onAboutToShow: ownerRevision = root.contextRevision
                        onAboutToHide: ownerRevision = -1
                        function acceptAction() {
                            const current = root.visible && ownerRevision === root.contextRevision
                            close()
                            return current
                        }
                        x: receiveMoreButton.width - width
                        y: receiveMoreButton.height + 8
                        ContextMenuButton {
                            objectName: "nextReceivingAddressButton"
                            text: qsTr("Generate new address")
                            autoClose: false
                            onTriggered: if (receiveMoreMenu.acceptAction()) receivingCard.ensureAddress(true, "")
                        }
                        ContextMenuDivider {}
                        ContextMenuButton {
                            objectName: "requestPaymentHistoryButton"
                            text: qsTr("View addresses")
                            autoClose: false
                            onTriggered: if (receiveMoreMenu.acceptAction()) root.addressHistoryRequested()
                        }
                    }
                }
            }
            GridLayout {
                objectName: "receiveWorkspace"
                Layout.preferredWidth: root.headerContentWidth
                Layout.maximumWidth: root.headerContentWidth
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignHCenter
                columns: width >= 780 ? 2 : 1
                columnSpacing: 40
                rowSpacing: 24
                ReceivingAddressCard {
                    id: receivingCard
                    objectName: "receivingAddressCard"
                    wallet: root.wallet
                    Layout.fillWidth: true
                    Layout.preferredWidth: 360
                    Layout.maximumWidth: parent.columns === 2 ? 400 : Infinity
                    Layout.minimumWidth: 300
                    Layout.alignment: Qt.AlignTop
                }
                PaymentRequestCard {
                    id: card
                    Layout.preferredWidth: 532
                    Layout.minimumWidth: 300
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    wallet: root.wallet
                    request: root.request
                    clipboard: root.clipboard
                    enabled: walletController.initialized
                    onCreated: function(requestId) {
                        const requestWallet = root.wallet
                        const createdRequest = root.request
                        root.paymentRequestCreated()
                        if (root.wallet !== requestWallet || root.request !== createdRequest) return
                        root.presentedRequest = createdRequest
                        root.presentedRequestId = requestId
                        requestModal.openRequest(requestId)
                    }
                }
            }
        }
    }
    PaymentRequestModal {
        id: requestModal
        wallet: root.wallet
        onClosed: {
            if (root.presentedRequest && root.request === root.presentedRequest
                    && root.request.id === root.presentedRequestId) root.request.clear()
            root.presentedRequest = null
            root.presentedRequestId = ""
            card.resetFields()
            if (root.visible) Qt.callLater(root.prepareCurrentWallet, root.contextRevision)
        }
    }
    Connections {
        target: walletController
        function onOpenReceiveRequested() {
            const revision = root.contextRevision
            Qt.callLater(function() {
                if (revision !== root.contextRevision) return
                card.resetFields()
                root.prepareCurrentWallet(revision)
            })
        }
    }
}
