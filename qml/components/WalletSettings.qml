// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import "../controls"

ColumnLayout {
    id: root

    spacing: 0
    readonly property bool bundledHwi: walletController.bundledHwiEnabled === true
    readonly property var signerStatus: (optionsModel.coreSettingStatuses || ({})).signer || ({})
    readonly property string signerPathError: root.bundledHwi
        ? ""
        : optionsModel.externalSignerPathValidationError(signerPathInput.text)

    Component.onCompleted: walletController.refreshExternalSignerStatus()

    function commitSignerPath() {
        if (signerPathError.length > 0) {
            return false
        }
        const normalizedPath = signerPathInput.text.trim()
        if (normalizedPath !== optionsModel.externalSignerPath) {
            optionsModel.externalSignerPath = normalizedPath
        }
        return true
    }

    CoreText {
        Layout.topMargin: 16
        Layout.fillWidth: true
        text: qsTr("Signer path")
        font.pixelSize: 15
        color: Theme.color.neutral9
        visible: !root.bundledHwi
    }

    CoreTextField {
        id: signerPathInput
        objectName: "externalSignerPathInput"
        Layout.topMargin: 8
        Layout.fillWidth: true
        placeholderText: qsTr("Enter external signer path")
        text: optionsModel.externalSignerPath
        enabled: root.signerStatus.canEdit !== false
        visible: !root.bundledHwi
        onEditingFinished: {
            if (root.commitSignerPath()) {
                walletController.refreshExternalSignerStatus()
            }
        }
    }

    CoreText {
        visible: !root.bundledHwi
            && (root.signerPathError.length > 0 || (root.signerStatus.infoText || "").length > 0)
        Layout.topMargin: 10
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        color: root.signerPathError.length > 0 ? Theme.color.red : Theme.color.neutral7
        text: root.signerPathError.length > 0 ? root.signerPathError : (root.signerStatus.infoText || "")
    }

    CoreText {
        Layout.topMargin: root.signerPathError.length > 0 ? 6 : 10
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        text: qsTr("The add wallet flow can offer external wallets when exactly one supported signer is connected.")
        font.pixelSize: 15
        color: Theme.color.neutral7
    }

    Rectangle {
        Layout.topMargin: 16
        Layout.fillWidth: true
        radius: 5
        color: Qt.rgba(Theme.color.neutral2.r, Theme.color.neutral2.g, Theme.color.neutral2.b, 0.5)
        implicitHeight: statusRow.implicitHeight + 20

        RowLayout {
            id: statusRow
            anchors.fill: parent
            anchors.margins: 10
            spacing: 8

            Icon {
                source: root.signerPathError.length > 0
                    || walletController.externalSignerError.length > 0
                    ? "image://images/error"
                    : walletController.canCreateExternalSignerWallet
                    ? "image://images/green-check"
                    : "image://images/info-filled"
                color: root.signerPathError.length > 0
                    || walletController.externalSignerError.length > 0
                    ? Theme.color.red
                    : walletController.canCreateExternalSignerWallet
                    ? Theme.color.green
                    : Theme.color.neutral9
                size: 16
                Layout.alignment: Qt.AlignVCenter
            }

            CoreText {
                objectName: "externalSignerStatusText"
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.color.neutral9
                text: {
                    if (root.signerPathError.length > 0) {
                        return root.signerPathError
                    }
                    if (root.bundledHwi) {
                        if (walletController.externalSignerError.length > 0) {
                            return walletController.externalSignerError
                        }
                        if (walletController.bundledHwiVerified && walletController.canCreateExternalSignerWallet) {
                            return qsTr("Bundled HWI — signature verified. Detected external signer: %1")
                                .arg(walletController.externalSignerName)
                        }
                        if (walletController.bundledHwiVerified) {
                            return qsTr("Bundled HWI — signature verified. Connect a supported hardware wallet to continue.")
                        }
                        if (walletController.canCreateExternalSignerWallet) {
                            return qsTr("Developer signer override: %1. Bundled signature checks are bypassed.")
                                .arg(walletController.externalSignerName)
                        }
                        return qsTr("Developer signer override active. No external signer is currently detected.")
                    }
                    if (walletController.canCreateExternalSignerWallet) {
                        return qsTr("Detected external signer: %1").arg(walletController.externalSignerName)
                    }
                    if (walletController.externalSignerError.length > 0) {
                        return walletController.externalSignerError
                    }
                    if (optionsModel.walletSettingsDirty) {
                        return qsTr("Path updated. Press Check device to rescan with the current signer command.")
                    }
                    if (optionsModel.externalSignerPath.length > 0) {
                        return qsTr("No external signer is currently detected.")
                    }
                    return qsTr("Set the command path for HWI or another external signer tool.")
                }
            }
        }
    }

    ContinueButton {
        objectName: "externalSignerCheckDeviceButton"
        Layout.topMargin: 20
        Layout.preferredWidth: Math.min(300, parent.width)
        Layout.alignment: Qt.AlignHCenter
        text: qsTr("Check device")
        enabled: root.bundledHwi
            || (root.signerPathError.length === 0 && root.signerStatus.canEdit !== false)
        onClicked: {
            if (root.bundledHwi || root.commitSignerPath()) {
                walletController.refreshExternalSignerStatus()
            }
        }
    }
}
