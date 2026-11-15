// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import org.bitcoincore.qt 1.0
import QtQuick.Layouts 1.15
import "../../controls"
import "../../components"

OnboardingView {
    id: root
    objectName: "importWalletSuccessPage"
    showNavigationBar: false
    usesSharedNavigation: true
    required property WalletOperationModel operation
    signal done()

    title: ""
    heading: qsTr("Import complete")
    subheading: qsTr("Your wallet was added successfully. We recommend running a health check to make sure that everything works as expected.")
    maximumContentWidth: 800
    showBackButton: false
    showCloseButton: false
    primaryButtonText: ""
    secondaryButtonText: root.operation.canOpenResult ? qsTr("Go to wallet") : qsTr("Done")
    secondaryButtonObjectName: "importWalletSuccessOverviewButton"
    onSecondaryClicked: root.done()

    imageView: WalletSuccessBadge {
        objectName: "importWalletSuccessBadge"
    }

    childView: FormSection {
        objectName: "importWalletInfoSection"
        isOnSurface: true
        showGradientBorder: false
        rowSpacing: 0

        KeyValueRow {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.topMargin: 16
            Layout.bottomMargin: 12
            key: CoreText {
                text: qsTr("Wallet name")
                color: Theme.color.neutral7
                font: Theme.text.body.font
            }
            value: CoreText {
                objectName: "importWalletSuccessWalletName"
                text: root.operation.resultWallet ? root.operation.resultWallet.displayName : root.operation.targetWalletId.length > 0 ? root.operation.targetWalletId : qsTr("Default wallet")
                color: Theme.color.neutral9
                font: Theme.text.subheading.font
                wrap: true
                horizontalAlignment: Text.AlignRight
            }
        }
        Separator {
            objectName: "importWalletInfoSeparator"
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            color: Theme.color.neutral3
        }
        KeyValueRow {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.topMargin: 12
            Layout.bottomMargin: 16
            key: CoreText {
                text: qsTr("Key scheme")
                color: Theme.color.neutral7
                font: Theme.text.body.font
            }
            value: CoreText {
                objectName: "importWalletSuccessKeyScheme"
                text: root.operation.keyScheme.length > 0 ? root.operation.keyScheme : qsTr("Unknown")
                color: Theme.color.neutral9
                font: Theme.text.subheading.font
                wrap: true
                horizontalAlignment: Text.AlignRight
            }
        }
        WalletOperationWarnings { Layout.fillWidth: true; operation: root.operation }
        CoreText { Layout.fillWidth: true; text: root.operation.result.recoveryPaths.join("\n"); textFormat: Text.PlainText; wrapMode: Text.WordWrap }
    }
}
