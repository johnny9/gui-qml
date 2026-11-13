// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import org.bitcoincore.qt 1.0

import "../../controls"

OnboardingView {
    id: root
    readonly property CreateWalletFields fieldsView: root.loadedChildView as CreateWalletFields
    objectName: "createWalletFormPage"
    showNavigationBar: false
    usesSharedNavigation: true
    property bool watchOnly: false
    property bool modalView: false
    property bool creating: false
    readonly property WalletProvisioningCheck validation: ApplicationModels.walletController.createProvisioningCheck()
    readonly property string nameError: root.fieldsView && root.fieldsView.walletName.trim().length > 0 && validation && !validation.pending ? (validation.error.length > 0 ? validation.error : validation.nameError) : ""
    readonly property bool validXpub: validation !== null && validation.xpubValid
    Binding { target: root.validation; property: "name"; value: root.fieldsView ? root.fieldsView.walletName : "" }
    Binding { target: root.validation; property: "xpub"; value: root.fieldsView ? root.fieldsView.xpubText : "" }
    readonly property bool passwordsMatch: root.fieldsView
        && root.fieldsView.passwordText.length > 0
        && root.fieldsView.passwordText === root.fieldsView.confirmText
    readonly property bool canCreate: root.fieldsView && ApplicationModels.walletController.initialized && !creating
        && !ApplicationModels.walletController.walletLoadInProgress && validation && validation.ready && validation.nameError.length === 0
        && root.fieldsView.walletName.trim().length > 0
        && (watchOnly ? validXpub : root.fieldsView.encryptWallet
            ? passwordsMatch && root.fieldsView.passwordLossAcknowledged
            : root.fieldsView.unencryptedWalletAcknowledged)

    signal cancel()
    signal createRequested(string name, string xpub, string passphrase)

    function submit() {
        if (!canCreate) return
        createRequested(root.fieldsView.walletName.trim(), root.fieldsView.xpubText.trim(),
            watchOnly || !root.fieldsView.encryptWallet ? "" : root.fieldsView.passwordText)
    }

    function clearSecrets() {
        if (root.fieldsView) root.fieldsView.clearSecrets()
    }

    title: ""
    heading: watchOnly ? qsTr("View-only wallet") : qsTr("Single-key wallet")
    subheading: watchOnly
        ? qsTr("See balances and activity without keeping spending keys in this app. An external signer is needed to spend.")
        : qsTr("You can control it in this application.\nWallet data will be stored locally in your hard drive.")
    maximumContentWidth: 800
    showBackButton: !creating && navigationStack
        ? (navigationStack.canGoBack !== undefined
            ? navigationStack.canGoBack : navigationStack.depth > 1) : false
    showCloseButton: modalView && !creating
    primaryButtonText: creating ? qsTr("Creating…") : validation && validation.pending ? qsTr("Checking…") : qsTr("Create wallet")
    primaryButtonObjectName: "createWalletFormCreateButton"
    primaryButtonEnabled: canCreate
    onCloseClicked: root.cancel()
    onPrimaryClicked: {
        if (!root.canCreate) return
        if (root.watchOnly || root.fieldsView.encryptWallet) root.submit()
        else root.fieldsView.openUnencryptedWarning()
    }

    Component.onCompleted: {
        ApplicationModels.walletController.clearWalletCreateStatus()
        ApplicationModels.walletController.clearWalletLoadStatus()
    }
    Component.onDestruction: { clearSecrets(); if (validation) validation.closeAdmission() }

    childView: CreateWalletFields {
        watchOnly: root.watchOnly
        nameError: root.nameError
        validXpub: root.validXpub
        xpubChecked: root.validation !== null && root.validation.ready
        onSubmitRequested: root.submit()
    }

}
