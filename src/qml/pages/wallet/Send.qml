// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import Qt.labs.settings 1.0
import org.bitcoincore.qt 1.0

import "../../controls"
import "../../components"

PageStack {
    id: root
    objectName: "sendPage"
    vertical: true

    property WalletQmlModel wallet: walletController.selectedWallet
    property SendRecipient recipient: wallet.recipients.current
    property string prepareTransactionErrorText: ""
    readonly property bool externalSignerWallet: wallet !== null && wallet.hasExternalSigner
    readonly property string recipientValidationError: wallet ? wallet.recipients.validationError : ""
    readonly property string formErrorText: recipientValidationError.length > 0 ? recipientValidationError : prepareTransactionErrorText

    signal transactionPrepared(bool multipleRecipientsEnabled)

    function clearPrepareTransactionError() {
        if (prepareTransactionErrorText.length > 0) {
            prepareTransactionErrorText = ""
        }
    }

    function scheduleFeeEstimates() {
        if (root.wallet) {
            root.wallet.scheduleFeeEstimates()
        }
    }

    Connections {
        target: walletController
        function onSelectedWalletChanged() {
            root.pop()
        }
    }

    Connections {
        target: root.wallet ? root.wallet.recipients : null
        function onListCleared() {
            root.clearPrepareTransactionError()
            settings.multipleRecipientsEnabled = false
            if (root.wallet) {
                root.wallet.scheduleFeeEstimates()
            }
        }
        function onCountChanged() {
            root.clearPrepareTransactionError()
            root.scheduleFeeEstimates()
        }
        function onCurrentRecipientChanged() {
            root.clearPrepareTransactionError()
            root.scheduleFeeEstimates()
        }
    }

    Connections {
        target: root.wallet ? root.wallet.coinsListModel : null
        function onSelectedCoinsCountChanged() {
            root.clearPrepareTransactionError()
            root.scheduleFeeEstimates()
        }
    }

    Connections {
        target: root.wallet
        function onCustomFeeEnabledChanged() {
            root.clearPrepareTransactionError()
        }
        function onCustomFeeRateChanged() {
            root.clearPrepareTransactionError()
        }
    }

    Binding {
        target: root.recipient ? root.recipient.amount : null
        property: "unit"
        value: optionsModel.displayUnit
        when: root.recipient !== null
    }

    initialItem: Page {
        background: null

        Settings {
            id: settings
            property alias coinControlEnabled: sendOptionsPopup.coinControlEnabled
            property alias multipleRecipientsEnabled: sendOptionsPopup.multipleRecipientsEnabled

            onMultipleRecipientsEnabledChanged: {
                if (!multipleRecipientsEnabled) {
                    root.wallet.recipients.clearToFront()
                } else {
                    root.wallet.recipients.add()
                }
            }

            onCoinControlEnabledChanged: {
                if (coinControlEnabled && root.wallet) {
                    root.wallet.coinsListModel.update()
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
                width: 520
                anchors.horizontalCenter: parent.horizontalCenter

                spacing: 10

                enabled: walletController.initialized

                Item {
                    id: titleRow
                    Layout.fillWidth: true
                    Layout.topMargin: 30
                    Layout.bottomMargin: root.externalSignerWallet ? 10 : 20

                    CoreText {
                        id: title
                        objectName: "walletSendTitle"
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Send bitcoin")
                        font.pixelSize: 21
                        color: Theme.color.neutral9
                        bold: true
                    }

                    IconButton {
                        id: menuButton
                        objectName: "sendOptionsButton"
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        checked: sendOptionsPopup.opened
                        iconSource: "image://images/ellipsis"
                        onClicked: {
                            if (sendOptionsPopup.opened) {
                                sendOptionsPopup.close()
                            } else {
                                sendOptionsPopup.open()
                            }
                        }
                    }

                    SendOptionsPopup {
                        id: sendOptionsPopup
                        x: menuButton.x - width + menuButton.width
                        y: menuButton.y + menuButton.height
                    }
                }

                CoreText {
                    visible: root.externalSignerWallet
                    Layout.fillWidth: true
                    Layout.bottomMargin: 10
                    horizontalAlignment: Text.AlignLeft
                    wrap: true
                    text: qsTr("Make sure you have your external signer at hand to approve this transaction.")
                    font.pixelSize: 18
                    color: Theme.color.neutral7
                }

                RowLayout {
                    id: selectAndAddRecipients
                    Layout.fillWidth: true
                    Layout.topMargin: 10
                    Layout.bottomMargin: 10
                    visible: settings.multipleRecipientsEnabled

                    CoreText {
                        id: selectAndAddRecipientsLabel
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignLeft
                        text: qsTr("Recipient %1 of %2").arg(wallet.recipients.currentIndex).arg(wallet.recipients.count)
                        horizontalAlignment: Text.AlignLeft
                        font.pixelSize: 18
                        color: Theme.color.neutral9
                    }

                    IconButton {
                        objectName: "sendRecipientPrevButton"
                        Layout.preferredWidth: 30
                        Layout.preferredHeight: 30
                        size: 30
                        iconSource: "image://images/caret-left"
                        enabled: wallet.recipients.currentIndex - 1 > 0
                        onClicked: {
                            wallet.recipients.prev()
                        }
                    }

                    IconButton {
                        objectName: "sendRecipientNextButton"
                        Layout.preferredWidth: 30
                        Layout.preferredHeight: 30
                        size: 30
                        iconSource: "image://images/caret-right"
                        enabled: wallet.recipients.currentIndex < wallet.recipients.count
                        onClicked: {
                            wallet.recipients.next()
                        }
                    }

                    IconButton {
                        objectName: "sendRecipientAddButton"
                        Layout.preferredWidth: 30
                        Layout.preferredHeight: 30
                        size: 30
                        iconSource: "image://images/plus-big-filled"
                        enabled: wallet.recipients.count < 25
                        onClicked: {
                            wallet.recipients.add()
                        }
                    }

                    IconButton {
                        objectName: "sendRecipientRemoveButton"
                        Layout.preferredWidth: 30
                        Layout.preferredHeight: 30
                        size: 30
                        iconSource: "image://images/minus"
                        enabled: wallet.recipients.count > 1
                        onClicked: {
                            wallet.recipients.remove()
                        }
                    }
                }

                Separator {
                    visible: settings.multipleRecipientsEnabled
                    Layout.fillWidth: true
                }

                BitcoinAddressInputField {
                    objectName: "sendAddressField"
                    Layout.fillWidth: true
                    inputObjectName: "sendAddressInput"
                    enabled: walletController.initialized
                    address: root.recipient.address
                    errorText: root.recipient.addressError
                    onTextChanged: {
                        root.clearPrepareTransactionError()
                        root.scheduleFeeEstimates()
                    }
                    onEditingFinished: root.scheduleFeeEstimates()
                }

                Separator {
                    Layout.fillWidth: true
                }

                BitcoinAmountInputField {
                    id: amountInput
                    Layout.fillWidth: true
                    inputObjectName: "sendAmountInput"
                    unitToggleObjectName: "sendAmountUnitToggle"
                    unitLabelObjectName: "sendAmountUnitLabel"
                    errorTextObjectName: "sendAmountErrorText"
                    amount: root.recipient ? root.recipient.amount : null
                    errorText: root.recipient ? root.recipient.amountError : ""
                    onInputTextChanged: {
                        root.clearPrepareTransactionError()
                        root.scheduleFeeEstimates()
                    }
                    onTextEdited: {
                        root.clearPrepareTransactionError()
                    }
                    onEditingFinished: root.scheduleFeeEstimates()
                }

                Separator {
                    Layout.fillWidth: true
                }

                LabeledTextInput {
                    id: label
                    objectName: "sendNoteField"
                    inputObjectName: "sendNoteInput"
                    Layout.fillWidth: true
                    labelText: qsTr("Note to self")
                    placeholderText: qsTr("Enter note…")
                    text: root.recipient.label
                    onTextEdited: root.recipient.label = label.text
                }

                Separator {
                    Layout.fillWidth: true
                }

                LabeledCoinControlButton {
                    objectName: "sendCoinControlButton"
                    valueObjectName: "sendCoinControlButtonText"
                    visible: settings.coinControlEnabled
                    Layout.fillWidth: true
                    coinsSelected: wallet.coinsListModel.selectedCoinsCount
                    coinCount: wallet.coinsListModel.coinCount
                    onOpenCoinControl: {
                        root.wallet.coinsListModel.update()
                        root.push(coinSelectionPage)
                    }
                }

                Separator {
                    visible: settings.coinControlEnabled
                    Layout.fillWidth: true
                }

                FeeSelection {
                    id: feeSelection
                    Layout.fillWidth: true
                    walletModel: root.wallet
                    includeFeeInAmount: root.recipient ? root.recipient.subtractFeeFromAmount : false
                    currentTarget: root.wallet ? root.wallet.targetBlocks : 2

                    onFeeChanged: function(target) {
                        root.clearPrepareTransactionError()
                        if (root.wallet) {
                            root.wallet.targetBlocks = target
                        }
                    }

                    onIncludeFeeInAmountToggled: function(checked) {
                        root.clearPrepareTransactionError()
                        if (root.recipient && root.recipient.subtractFeeFromAmount !== checked) {
                            root.recipient.subtractFeeFromAmount = checked
                            root.scheduleFeeEstimates()
                        }
                    }
                }

                RowLayout {
                    objectName: "sendFeeIncludedNote"
                    Layout.fillWidth: true
                    visible: root.recipient && root.recipient.subtractFeeFromAmount

                    Icon {
                        source: "image://images/check"
                        size: 18
                        color: Theme.color.green
                    }

                    CoreText {
                        objectName: "sendFeeIncludedNoteText"
                        Layout.fillWidth: true
                        text: qsTr("Fee is included in the amount")
                        font.pixelSize: 15
                        color: Theme.color.neutral7
                        horizontalAlignment: Text.AlignLeft
                    }
                }

                Separator {
                    Layout.fillWidth: true
                }

                RowLayout {
                    objectName: "sendPrepareTransactionError"
                    Layout.fillWidth: true
                    visible: root.prepareTransactionErrorText.length > 0
                        || root.recipientValidationError.length > 0

                    Icon {
                        source: "image://images/alert-filled"
                        size: 22
                        color: Theme.color.red
                    }

                    CoreText {
                        objectName: "sendPrepareTransactionErrorText"
                        text: root.formErrorText
                        font.pixelSize: 15
                        color: Theme.color.red
                        horizontalAlignment: Text.AlignLeft
                        Layout.fillWidth: true
                    }
                }

                ContinueButton {
                    id: continueButton
                    objectName: "sendReviewButton"
                    Layout.fillWidth: true
                    Layout.topMargin: 30
                    text: root.externalSignerWallet ? qsTr("Review transaction") : qsTr("Review")
                    enabled: root.wallet
                        && root.wallet.recipients.allValid
                        && (!root.wallet.customFeeEnabled || root.wallet.customFeeRateValid)
                    onClicked: {
                        root.clearPrepareTransactionError()
                        if (root.wallet.prepareTransaction()) {
                            root.transactionPrepared(settings.multipleRecipientsEnabled)
                        } else if (root.wallet.transactionNeedsUnlock) {
                            reviewPassphrasePopup.errorText = ""
                            reviewPassphrasePopup.open()
                        } else {
                            root.prepareTransactionErrorText = root.wallet.transactionError.length > 0
                                ? root.wallet.transactionError
                                : qsTr("Amount plus fee exceeds available balance")
                        }
                    }
                }
            }
        }
    }

    Component {
        id: coinSelectionPage
        CoinSelection {
            onDone: root.pop()
        }
    }

    WalletPassphrasePopup {
        id: reviewPassphrasePopup
        parent: Overlay.overlay
        width: Math.min(420, root.width - 40)
        popupObjectName: "reviewPassphrasePopup"
        passphraseFieldObjectName: "reviewPassphraseField"
        errorTextObjectName: "reviewPassphraseErrorText"
        cancelButtonObjectName: "reviewPassphraseCancelButton"
        confirmButtonObjectName: "reviewPassphraseConfirmButton"
        titleText: qsTr("Enter wallet password")
        descriptionText: qsTr("Enter your wallet password to prepare this transaction for review.")
        confirmText: qsTr("Unlock and continue")
        busyConfirmText: qsTr("Unlocking...")
        onSubmitted: (passphrase) => {
            reviewPassphrasePopup.busy = true
            if (root.wallet.prepareTransactionWithPassphrase(passphrase)) {
                reviewPassphrasePopup.busy = false
                reviewPassphrasePopup.close()
                root.transactionPrepared(settings.multipleRecipientsEnabled)
                return
            }
            reviewPassphrasePopup.busy = false
            reviewPassphrasePopup.errorText = root.wallet.transactionError
        }
    }
}
