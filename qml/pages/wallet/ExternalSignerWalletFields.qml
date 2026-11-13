// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0
import "../../controls"

ColumnLayout {
    id: fields
    required property string defaultWalletName
    required property string nameError
    required property bool creatingWallet
    signal nameEdited()
    property alias walletName: nameEntry.text
    property bool showOperationErrorDetails: false
    spacing: 20

    CoreText {
        Layout.fillWidth: true
        text: qsTr("This wallet stores public descriptors on this computer and uses your external signer for addresses and signing.")
        color: Theme.color.neutral7
        font: Theme.text.body.font
        wrap: true
        horizontalAlignment: Text.AlignLeft
    }

    LabeledTextField {
        id: nameEntry
        objectName: "externalWalletNameEntry"
        Layout.fillWidth: true
        label: qsTr("Wallet name")
        fieldObjectName: "externalWalletNameInput"
        placeholderText: qsTr("Eg. Hardware wallet...")
        text: fields.defaultWalletName
        supportingText: qsTr("You cannot change this later.")
        fieldSurface: true
        fieldBackgroundColor: Theme.color.neutral2
        enabled: !fields.creatingWallet
        onTextEdited: {
            fields.nameEdited()
            ApplicationModels.walletController.clearWalletLoadStatus()
        }
    }

    CoreText {
        objectName: "externalWalletNameError"
        Layout.fillWidth: true
        visible: text.length > 0
        text: fields.nameError.length > 0 ? fields.nameError : ApplicationModels.walletController.walletLoadErrorDisplay
        color: Theme.color.red
        font: Theme.text.caption.font
        wrap: true
        horizontalAlignment: Text.AlignLeft
    }

    TextButton {
        objectName: "walletOperationErrorDetailsButton"
        visible: fields.nameError.length === 0 && operationErrorDetails.count > 0
        text: fields.showOperationErrorDetails ? qsTr("Hide error details") : qsTr("Show error details")
        onClicked: fields.showOperationErrorDetails = !fields.showOperationErrorDetails
    }

    ListView {
        id: operationErrorDetails
        objectName: "walletOperationErrorDetails"
        Layout.fillWidth: true
        Layout.preferredHeight: visible ? 120 : 0
        visible: fields.showOperationErrorDetails && fields.nameError.length === 0 && count > 0
        model: ApplicationModels.walletController.walletImportErrorHelpModel
        clip: true
        reuseItems: true
        cacheBuffer: 0
        delegate: CoreText {
            required property string content
            width: operationErrorDetails.width
            text: content
            textFormat: Text.PlainText
            font: Theme.text.caption.font
            wrapMode: Text.WordWrap
        }
    }
}
