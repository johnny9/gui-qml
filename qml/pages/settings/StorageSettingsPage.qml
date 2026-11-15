// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import org.bitcoincore.qt 1.0
import QtQuick.Layouts 1.15

import "../../controls"
import "../../components"

SettingsPage {
    id: root
    objectName: "storageSettingsPage"
    title: qsTr("Storage")
    showBackButton: false

    property var settingsModel: ApplicationModels.optionsModel
    property bool onboarding: false
    readonly property string pruneTargetError: settingsModel.pruneTargetError
    property var coreSettingsModel: settingsModel.coreSettings
    readonly property var pruneSetting: coreSettingsModel.entry("prune")
    FormSection {
        Layout.fillWidth: true
        title: qsTr("Block storage")
        isOnSurface: root.onboarding
        showGradientBorder: false

        FormRow {
            Layout.fillWidth: true
            title: qsTr("Store recent blocks only")
            dividerColor: root.onboarding ? Theme.color.neutral3 : Theme.color.neutral2
            supportingText: root.pruneSetting.infoText
            enabled: root.pruneSetting.canEdit
            trailingItem: OptionSwitch {
                objectName: "pruneSwitch"
                checked: root.pruneSetting.enabled
                onToggled: root.pruneSetting.enabled = checked
            }
        }

        TextFieldRow {
            id: pruneTargetRow
            Layout.fillWidth: true
            title: qsTr("Block storage target (MiB; 1 is manual)")
            enabled: root.pruneSetting.enabled && root.pruneSetting.canEdit
            fieldObjectName: "pruneTargetInput"
            fieldWidth: 80
            text: root.settingsModel.pruneTargetText
            errorText: root.settingsModel.pruneTargetError
            supportingText: root.settingsModel.pruneTargetError.length === 0 ? root.pruneSetting.infoText : ""
            showDivider: false
            onTextEdited: function(text) { root.settingsModel.editPruneTarget(text) }
        }
    }

    ContinueButton {
        objectName: "restoreValidPruneInput"
        visible: root.settingsModel.pruneTargetError.length > 0
        text: qsTr("Restore the last valid target")
        onClicked: root.coreSettingsModel.discardPruneInput()
    }

    ContinueButton {
        objectName: "onboardingConfirmUnpruning"
        visible: root.onboarding && !root.settingsModel.prune
        text: qsTr("Allow rebuilding and downloading blocks if this profile was pruned")
        onClicked: root.settingsModel.confirmUnpruning(root.settingsModel.unpruneConsentToken)
    }

    FormSection {
        Layout.fillWidth: true
        title: qsTr("Data directory")
        isOnSurface: root.onboarding
        showGradientBorder: false
        description: qsTr("Selected before startup. The data directory cannot be changed while the node is running.")

        FormRow {
            Layout.fillWidth: true
            title: qsTr("Location")
            showDivider: false
            bodyItem: CoreText {
                objectName: "dataDirectoryValue"
                Layout.fillWidth: true
                text: root.settingsModel.dataDir
                color: Theme.color.neutral7
                font: Theme.text.caption.font
                lineHeight: Theme.text.caption.lineHeight
                lineHeightMode: Text.FixedHeight
                horizontalAlignment: Text.AlignLeft
                wrap: true
            }
        }
    }

    SettingsRestartNotice {
        objectName: "storageRestartNotice"
        visible: !root.onboarding && root.settingsModel.storageSettingsDirty
        Layout.fillWidth: true
        Layout.maximumWidth: root.contentLayout.width
    }
}
