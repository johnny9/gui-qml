// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQml 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import org.bitcoincore.qt 1.0

import "../../controls"
import "../../components"

SettingsPage {
    id: root
    objectName: "signVerifyMessagePage"

    property WalletQmlModel wallet: null
    property var signVerifyModel: wallet ? wallet.signVerifyMessageModel : null
    readonly property string verifyResultText: signVerifyModel ? signVerifyModel.verificationText : ""
    readonly property bool verifyResultSuccess: signVerifyModel ? signVerifyModel.verificationValid : false
    property int initialTab: SignVerifyMessage.SignTab
    property int selectedMode: root.initialTab

    enum Tabs { SignTab, VerifyTab }
    readonly property bool signingComplete: !!root.signVerifyModel
        && root.signVerifyModel.signingSucceeded

    BitcoinAddress {
        id: signAddressModel
        objectName: "signMessageAddressModel"
    }

    BitcoinAddress {
        id: verifyAddressModel
        objectName: "verifyMessageAddressModel"
    }

    title: qsTr("Sign or verify message")
    backButtonObjectName: "signVerifyMessageBackButton"
    maximumContentWidth: 706
    contentSpacing: 24

    function clearSignForm() {
        signAddress.text = ""
        signMessageText.text = ""
        if (root.signVerifyModel) root.signVerifyModel.clear()
    }

    function clearVerifyForm() {
        verifyAddress.text = ""
        verifyMessageText.text = ""
        verifySignature.text = ""
        if (root.signVerifyModel) root.signVerifyModel.clearVerification()
    }

    Binding { target: root.signVerifyModel; property: "signingAddress"; value: signAddressModel.address; when: root.signVerifyModel !== null }
    Binding { target: root.signVerifyModel; property: "signingMessage"; value: signMessageText.text; when: root.signVerifyModel !== null }
    Binding { target: root.signVerifyModel; property: "verificationAddress"; value: verifyAddressModel.address; when: root.signVerifyModel !== null }
    Binding { target: root.signVerifyModel; property: "verificationMessage"; value: verifyMessageText.text; when: root.signVerifyModel !== null }
    Binding { target: root.signVerifyModel; property: "verificationSignature"; value: verifySignature.text; when: root.signVerifyModel !== null }

    function submitSign(passphrase) {
        if (!root.signVerifyModel || root.signVerifyModel.signingBusy
                || signAddressModel.normalizationPending || signAddressModel.normalizationError.length > 0) return
        if (arguments.length === 0) root.signVerifyModel.requestSignMessage(signAddressModel.address, signMessageText.text)
        else root.signVerifyModel.requestSignMessageWithPassphrase(signAddressModel.address, signMessageText.text, passphrase)
    }

    function submitVerify() {
        if (root.signVerifyModel && !root.signVerifyModel.verificationBusy
                && !verifyAddressModel.normalizationPending && verifyAddressModel.normalizationError.length === 0)
            root.signVerifyModel.requestVerifyMessage(verifyAddressModel.address, verifyMessageText.text, verifySignature.text)
    }

    Connections {
        target: root.signVerifyModel
        function onSigningStateChanged() {
            if (!root.visible || !root.signVerifyModel || root.signVerifyModel.signingBusy) return
            if (root.signVerifyModel.signingSucceeded) signPassphrasePopup.close()
            else if (root.signVerifyModel.signingNeedsUnlock) signPassphrasePopup.open()
        }
    }

    Component.onCompleted: {
        if (root.signVerifyModel) root.signVerifyModel.clear()
    }

    SegmentedPicker {
        objectName: "signVerifyMessageModePicker"
        Layout.fillWidth: true
        Layout.maximumWidth: 400
        Layout.alignment: Qt.AlignHCenter
        model: [
            {
                text: qsTr("Sign message"),
                objectName: "signMessageTab"
            },
            {
                text: qsTr("Verify message"),
                objectName: "verifyMessageTab"
            }
        ]
        currentIndex: root.selectedMode
        onSelected: (index) => root.selectedMode = index
    }

    StackLayout {
        Layout.fillWidth: true
        currentIndex: root.selectedMode

        FormSection {
            objectName: "signMessageFormSection"
            Layout.fillWidth: true

            ColumnLayout {
                Layout.fillWidth: true
                Layout.margins: 20
                spacing: 20

                PageHeading {
                    objectName: "signMessageHeading"
                    Layout.fillWidth: true
                    description: qsTr("You can sign messages or agreements with your legacy P2PKH addresses to prove you can receive bitcoin sent to them. Be careful not to sign anything vague or random, as phishing attacks may try to trick you into signing your identity over to them. Only sign fully detailed statements you agree to.")
                }

                LabeledBitcoinAddressField {
                    id: signAddress
                    objectName: "signMessageAddressEntry"
                    Layout.fillWidth: true
                    label: qsTr("Bitcoin address")
                    fieldObjectName: "signMessageAddressField"
                    placeholderText: qsTr("Enter a legacy P2PKH address")
                    address: signAddressModel
                    fieldTextStyle: Theme.text.monoDescription
                    errorText: signAddressModel.address.length > 0 && root.signVerifyModel && !root.signVerifyModel.addressValidationPending && !root.signVerifyModel.signingAddressValid ? qsTr("Not a valid P2PKH address.") : ""
                    readOnly: root.signingComplete
                    showCopyButton: root.signingComplete
                    copyButtonObjectName: "signMessageCopyAddressButton"
                    onCopyRequested: Clipboard.setText(signAddressModel.address)
                }

                LabeledTextView {
                    id: signMessageText
                    objectName: "signMessageMessageEntry"
                    Layout.fillWidth: true
                    label: qsTr("Message")
                    fieldObjectName: "signMessageMessageField"
                    placeholderText: qsTr("Enter the message you want to sign")
                    fieldBackgroundColor: Theme.color.neutral2
                    readOnly: root.signingComplete
                    showCopyButton: root.signingComplete
                    copyButtonObjectName: "signMessageCopyMessageButton"
                    onCopyRequested: Clipboard.setText(text)
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12

                    ContinueButton {
                        objectName: "signMessageButton"
                        Layout.fillWidth: true
                        text: qsTr("Sign message")
                        enabled: !!root.signVerifyModel
                            && root.signVerifyModel.signingAddressValid && !root.signVerifyModel.signingBusy
                            && !signAddressModel.normalizationPending && signAddressModel.normalizationError.length === 0
                        onClicked: root.submitSign()
                    }

                    NeutralButton {
                        objectName: "signMessageClearButton"
                        buttonSize: NeutralButton.Large
                        Layout.preferredWidth: 140
                        text: qsTr("Clear all")
                        onClicked: root.clearSignForm()
                    }
                }

                CoreText {
                    objectName: "signMessageErrorText"
                    visible: !!root.signVerifyModel
                        && root.signVerifyModel.signingError.length > 0
                        && !root.signVerifyModel.signingNeedsUnlock
                    Layout.fillWidth: true
                    text: root.signVerifyModel ? root.signVerifyModel.signingError : ""
                    color: Theme.color.red
                    font: Theme.text.caption.font
                    lineHeight: Theme.text.caption.lineHeight
                    lineHeightMode: Text.FixedHeight
                    wrap: true
                }

                LabeledTextView {
                    objectName: "signMessageSignatureOutput"
                    visible: root.signingComplete
                    Layout.fillWidth: true
                    label: qsTr("Signature")
                    fieldObjectName: "signMessageSignatureText"
                    text: root.signVerifyModel ? root.signVerifyModel.signature : ""
                    readOnly: true
                    wrapMode: TextEdit.WrapAnywhere
                    fieldTextStyle: Theme.text.monoCaption
                    fieldBackgroundColor: Theme.color.neutral2
                    showCopyButton: true
                    copyButtonObjectName: "signMessageCopySignatureButton"
                    onCopyRequested: Clipboard.setText(root.signVerifyModel ? root.signVerifyModel.signature : "")
                }
            }
        }

        FormSection {
            objectName: "verifyMessageFormSection"
            Layout.fillWidth: true

            ColumnLayout {
                Layout.fillWidth: true
                Layout.margins: 20
                spacing: 20

                PageHeading {
                    objectName: "verifyMessageHeading"
                    Layout.fillWidth: true
                    description: qsTr("Enter the receiver's address, message (ensure you copy line breaks), spaces, tabs, etc. exactly) and signature below to verify the message. Be careful not to read more into the signature than what is in the signed message itself, to avoid being tricked by a man-in-the-middle attack. Note that this only proves the signing party receives with the address, it cannot prove sendership of any transaction.")
                }

                LabeledBitcoinAddressField {
                    id: verifyAddress
                    objectName: "verifyMessageAddressEntry"
                    Layout.fillWidth: true
                    label: qsTr("Bitcoin address")
                    fieldObjectName: "verifyMessageAddressField"
                    placeholderText: qsTr("Enter a legacy P2PKH address")
                    address: verifyAddressModel
                    fieldTextStyle: Theme.text.monoDescription
                    errorText: verifyAddressModel.address.length > 0 && root.signVerifyModel && !root.signVerifyModel.addressValidationPending && !root.signVerifyModel.verificationAddressValid ? qsTr("Not a valid P2PKH address.") : ""
                }

                LabeledTextView {
                    id: verifyMessageText
                    objectName: "verifyMessageMessageEntry"
                    Layout.fillWidth: true
                    label: qsTr("Message")
                    fieldObjectName: "verifyMessageMessageField"
                    placeholderText: qsTr("Enter the signed message")
                    fieldBackgroundColor: Theme.color.neutral2
                }

                LabeledTextView {
                    id: verifySignature
                    objectName: "verifyMessageSignatureEntry"
                    Layout.fillWidth: true
                    label: qsTr("Signature")
                    fieldObjectName: "verifyMessageSignatureField"
                    placeholderText: qsTr("Enter the message signature")
                    fieldHeight: 84
                    fieldBackgroundColor: Theme.color.neutral2
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12

                    ContinueButton {
                        objectName: "verifyMessageButton"
                        Layout.fillWidth: true
                        text: qsTr("Verify message")
                        enabled: !!root.signVerifyModel
                            && root.signVerifyModel.verificationAddressValid && !root.signVerifyModel.verificationBusy
                            && !verifyAddressModel.normalizationPending && verifyAddressModel.normalizationError.length === 0
                            && verifySignature.text.length > 0
                        onClicked: root.submitVerify()
                    }

                    NeutralButton {
                        objectName: "verifyMessageClearButton"
                        buttonSize: NeutralButton.Large
                        Layout.preferredWidth: 140
                        text: qsTr("Clear all")
                        onClicked: root.clearVerifyForm()
                    }
                }

                ToastBanner {
                    objectName: "verifyMessageResultBanner"
                    visible: root.verifyResultText.length > 0
                    Layout.fillWidth: true
                    tintColor: root.verifyResultSuccess ? Theme.color.green : Theme.color.red
                    iconSource: root.verifyResultSuccess
                        ? "image://images/check"
                        : "image://images/info-filled"
                    text: root.verifyResultText
                    textObjectName: "verifyMessageResultText"
                }
            }
        }
    }

    WalletPassphrasePopup {
        id: signPassphrasePopup
        parent: Overlay.overlay
        width: Math.min(420, root.width - 40)
        popupObjectName: "signMessagePassphrasePopup"
        passphraseFieldObjectName: "signMessagePassphraseField"
        errorTextObjectName: "signMessagePassphraseErrorText"
        cancelButtonObjectName: "signMessagePassphraseCancelButton"
        confirmButtonObjectName: "signMessagePassphraseConfirmButton"
        titleText: qsTr("Enter wallet password")
        descriptionText: qsTr("Enter the wallet password to sign this message.")
        confirmText: qsTr("Unlock and sign")
        busyConfirmText: qsTr("Signing...")
        property var context: null
        readonly property bool contextValid: context !== null && root.visible
            && root.signVerifyModel === context.model
            && root.signVerifyModel.signingAddress === context.address
            && root.signVerifyModel.signingMessage === context.message
        onAboutToShow: context = { model: root.signVerifyModel, address: signAddressModel.address, message: signMessageText.text }
        onContextValidChanged: if (!contextValid) close()
        onClosed: context = null
        busy: contextValid && context.model.signingBusy
        errorText: contextValid && context.model.signingNeedsUnlock ? context.model.signingError : ""
        onSubmitted: (passphrase) => {
            if (contextValid && !context.model.signingBusy)
                context.model.requestSignMessageWithPassphrase(context.address, context.message, passphrase)
        }
    }

}
