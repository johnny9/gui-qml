// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import org.bitcoincore.qt 1.0
import "../../controls"

Item {
    id: root
    objectName: "createWalletWizard"
    clip: true
    property bool modalView: false
    property bool onboardingEntry: false
    property bool waitForWalletDiscovery: false
    property WalletOperationModel operation: null
    readonly property bool creatingWallet: operation !== null && operation.busy
    property bool importingWallet: false
    property string pendingImportResult: ""
    readonly property bool completedWatchOnly: operation !== null && operation.creationKind === WalletOperationModel.WatchOnly
    readonly property bool completedEncrypted: operation !== null && operation.encrypted
    property bool readyPending: false
    // The wallet just created or imported. The completion pages act on it,
    // even if another wallet is selected meanwhile.
    property WalletQmlModel importedWallet: null
    readonly property WalletQmlModel completedWallet: operation !== null ? operation.resultWallet : importedWallet
    readonly property alias currentItem: pages.currentItem
    readonly property OnboardingView currentOnboardingPage: pages.currentItem as OnboardingView
    readonly property alias depth: pages.depth
    readonly property alias busy: pages.busy
    readonly property alias pageStack: pages
    readonly property alias navigationBar: navigationBar
    readonly property bool walletDiscoveryReady: ApplicationModels.walletController.initialized
        && (!waitForWalletDiscovery || ApplicationModels.walletListModel.walletDirLoaded)
    signal finished(bool openActivity, var wallet)

    function showingCompletion() {
        const page = root.currentItem ? root.currentItem.objectName : ""
        return page === "walletCreationReadyPage" || page === "importWalletSuccessPage"
            || page === "externalWalletCreatedPage"
    }

    function completedWalletIsOpen() {
        return root.completedWallet !== null && root.completedWallet.available !== false
            && ApplicationModels.walletController.rowForWallet(root.completedWallet) >= 0
    }

    // A completion page for a wallet that has since closed has nothing
    // left to go to, so it closes too.
    function closeIfCompletedWalletClosed() {
        if (root.showingCompletion() && !root.completedWalletIsOpen()) root.finished(false, null)
    }

    function push(item, properties, operation) {
        if (operation !== undefined) return pages.push(item, properties, operation)
        if (properties !== undefined) return pages.push(item, properties)
        return pages.push(item)
    }
    function pop() { return pages.pop() }
    function goBack() { pages.goBack() }

    function showOperationResult() {
        if (root.busy) return
        if (!root.operation || !root.operation.terminal || root.showingCompletion()) return
        if (root.operation.phase === WalletOperationModel.Partial || root.operation.phase === WalletOperationModel.Uncertain) {
            if (!root.currentItem || root.currentItem.objectName !== "walletCreationOutcomePage") root.push(outcomePage, {}, StackView.Immediate)
            return
        }
        if (root.operation.phase !== WalletOperationModel.Succeeded) return
        if (root.operation.creationKind === WalletOperationModel.ExternalSigner) root.push(externalConfirm, {}, StackView.Immediate)
        else root.showReady()
    }

    Connections {
        target: root.operation
        function onChanged() { root.showOperationResult() }
    }

    function showReady() {
        if (root.busy) {
            root.readyPending = true
            return
        }
        root.readyPending = false
        root.push(readyPage, { "wallet": root.completedWallet,
                               "watchOnly": root.completedWatchOnly,
                               "encryptedWallet": root.completedEncrypted,
                               "modalView": root.modalView, "operation": root.operation }, StackView.Immediate)
    }

    function chooseImportFile() {
        if (root.importingWallet) return
        if (automationPathField.text.length > 0) {
            const path = automationPathField.text
            automationPathField.text = ""
            root.importFile(path)
            return
        }
        importFileDialog.open()
    }

    function importFile(path) {
        if (!path || root.importingWallet) return
        root.importingWallet = true
        ApplicationModels.walletController.importWallet(path)
    }

    function showImportResult() {
        if (root.busy || root.pendingImportResult.length === 0) return
        const result = root.pendingImportResult
        root.pendingImportResult = ""
        if (result === "success") root.push(importSuccess, {}, StackView.Immediate)
        else if (!root.currentItem || root.currentItem.objectName !== "walletImportErrorPage")
            root.push(importError, { "modalView": root.modalView }, StackView.Immediate)
    }

    Component.onCompleted: {
        if (!ApplicationModels.walletController.initialized) ApplicationModels.nodeModel.startNodeInitializionThread()
        ApplicationModels.walletController.refreshExternalSignerStatus()
        root.showOperationResult()
    }

    AppFileDialog {
        id: importFileDialog
        objectName: "walletImportFileDialog"
        nameFilters: [qsTr("Wallet backup files (*.bak *.dat)"), qsTr("All files (*)")]
        onAccepted: {
            if (selectedFile.toString().length > 0) root.importFile(selectedFile.toString())
        }
    }

    // The native file picker is not available to the QML functional test bridge.
    TextField {
        id: automationPathField
        objectName: "importWalletPathField"
        visible: false
    }

    Connections {
        target: root.completedWallet
        ignoreUnknownSignals: true
        function onAvailableChanged() {
            root.closeIfCompletedWalletClosed()
        }
    }

    Connections {
        target: ApplicationModels.walletController
        function onOpenWalletsChanged() {
            root.closeIfCompletedWalletClosed()
        }
        function onWalletLoadErrorChanged() {
            if (root.importingWallet && ApplicationModels.walletController.walletLoadHasError) {
                root.importingWallet = false
                root.pendingImportResult = "error"
                root.showImportResult()
            }
        }
        function onWalletImportSucceeded(wallet) {
            if (!root.importingWallet) return
            root.importingWallet = false
            root.importedWallet = wallet
            root.pendingImportResult = "success"
            root.showImportResult()
        }
    }

    OnboardingNavigationBar {
        id: navigationBar
        objectName: "walletCreationNavigationBar"
        width: parent.width
        isOnSurface: root.modalView
        buttonSidePadding: width >= 640 ? 24 : 16
        closeButtonSize: CloseButton.Large
        visible: root.currentOnboardingPage && root.currentOnboardingPage.usesSharedNavigation === true
        height: visible ? implicitHeight : 0
        title: visible ? root.currentOnboardingPage.title : ""
        showBackButton: visible && root.currentOnboardingPage.showBackButton
        showCloseButton: visible && root.currentOnboardingPage.showCloseButton
        onBackClicked: root.goBack()
        onCloseClicked: {
            if (root.currentOnboardingPage) root.currentOnboardingPage.closeClicked()
        }
    }

    PageStack {
        id: pages
        objectName: "walletCreationPageStack"
        anchors.top: navigationBar.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        clip: true
        initialItem: root.modalView ? typePage : startPage
        onBusyChanged: {
            if (!busy && root.readyPending) root.showReady()
            if (!busy) root.showOperationResult()
            if (!busy && root.pendingImportResult.length > 0) root.showImportResult()
        }
    }

    Component {
        id: startPage
        OnboardingView {
            objectName: "walletCreationStartPage"
            showNavigationBar: false
            usesSharedNavigation: true
            title: ""
            maximumContentWidth: 640
            heading: qsTr("Your wallet starts here")
            subheading: qsTr("Create a wallet to manage your keys in this app, or import an existing wallet file.")
            showCloseButton: !root.importingWallet
            primaryButtonText: qsTr("Create wallet")
            secondaryButtonText: qsTr("Import wallet")
            primaryButtonObjectName: "createWalletButton"
            secondaryButtonObjectName: "importWalletButton"
            primaryButtonEnabled: root.walletDiscoveryReady && !root.importingWallet
            secondaryButtonEnabled: root.walletDiscoveryReady && !root.importingWallet
            onCloseClicked: root.finished(false, null)
            onPrimaryClicked: root.push(typePage)
            onSecondaryClicked: root.chooseImportFile()

            childView: Item {
                implicitHeight: root.walletDiscoveryReady ? 0 : 48
                BusyIndicator {
                    objectName: "createWalletDiscoveryBusyIndicator"
                    anchors.centerIn: parent
                    visible: !root.walletDiscoveryReady
                    running: visible
                }
            }
        }
    }

    Component {
        id: typePage
        WalletCreationTypePage {
            modalView: root.modalView
            onboardingEntry: root.onboardingEntry
            importingWallet: root.importingWallet
            onCancel: root.finished(false, null)
            onRegularSelected: root.push(formPage, { "watchOnly": false,
                                                       "modalView": root.modalView })
            onWatchOnlySelected: root.push(formPage, { "watchOnly": true,
                                                         "modalView": root.modalView })
            onExternalSignerSelected: {
                ApplicationModels.walletController.clearWalletLoadStatus()
                ApplicationModels.walletController.refreshExternalSignerStatus()
                root.push(externalWallet, {
                    "defaultWalletName": ApplicationModels.walletController.suggestedExternalSignerWalletName
                })
            }
            onImportSelected: root.chooseImportFile()
        }
    }

    Component {
        id: formPage
        CreateWalletForm {
            creating: root.creatingWallet || root.readyPending
            onCancel: root.finished(false, null)
            onOperationSubmitted: function(accepted) {
                root.operation = accepted
                root.showOperationResult()
            }
        }
    }

    Component {
        id: outcomePage
        WalletCreationOutcomePage { operation: root.operation; onDone: root.finished(false, null) }
    }

    Component {
        id: readyPage
        WalletCreationReadyPage {
            onDone: root.finished(root.operation ? root.operation.canOpenResult : root.completedWalletIsOpen(), root.completedWallet)
        }
    }

    Component {
        id: importError
        WalletImportErrorPage {
            onCancel: root.finished(false, null)
            onRetryRequested: root.chooseImportFile()
        }
    }
    Component {
        id: importSuccess
        WalletImportCompletePage {
            onDone: root.finished(true, root.completedWallet)
        }
    }
    Component {
        id: externalWallet
        ExternalSignerWalletForm {
            modalView: root.modalView
            onCancel: root.finished(false, null)
            onOperationSubmitted: function(accepted) {
                root.operation = accepted
                root.showOperationResult()
            }
        }
    }
    Component {
        id: externalConfirm
        WalletCreationExternalReadyPage {
            operation: root.operation
            onDone: root.finished(root.operation ? root.operation.canOpenResult : root.completedWalletIsOpen(), root.completedWallet)
        }
    }
}
