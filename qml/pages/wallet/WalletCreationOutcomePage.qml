// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
import QtQuick 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0
import "../../controls"

OnboardingView {
    id: root
    objectName: "walletCreationOutcomePage"
    required property WalletOperationModel operation
    showNavigationBar: false
    usesSharedNavigation: true
    navigationBackEnabled: false
    heading: operation.phase === WalletOperationModel.Uncertain ? qsTr("Check the wallet creation outcome") : qsTr("Wallet creation needs attention")
    subheading: operation.error.message
    showBackButton: false
    showCloseButton: false
    primaryButtonText: operation.canReconcile ? qsTr("Reconcile created wallet") : ""
    primaryButtonEnabled: operation.canReconcile
    secondaryButtonText: qsTr("Done")
    secondaryButtonObjectName: "walletCreationOutcomeDoneButton"
    signal done()
    onPrimaryClicked: operation.reconcile()
    onSecondaryClicked: root.done()
    childView: ColumnLayout {
        CoreText {
            objectName: "walletCreationRecoveryText"
            Layout.fillWidth: true
            text: root.operation.error.recovery
            textFormat: Text.PlainText
            wrapMode: Text.WordWrap
        }
        CoreText {
            objectName: "walletCreationRecoveryPaths"
            Layout.fillWidth: true
            text: root.operation.result.recoveryPaths.join("\n")
            textFormat: Text.PlainText
            wrapMode: Text.WordWrap
        }
        WalletOperationWarnings { Layout.fillWidth: true; operation: root.operation }
    }
}
