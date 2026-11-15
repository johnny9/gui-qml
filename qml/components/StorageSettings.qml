// Copyright (c) 2022-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
import QtQuick
import QtQuick.Layouts
import org.bitcoincore.qt 1.0
import "../controls"
ColumnLayout {
    id: root
    property var settingsModel: ApplicationModels.onboardingOptionsModel
    readonly property CoreSettingsModel coreSettingsModel: settingsModel.coreSettings
    property bool showRestartNotice: false
    readonly property bool customStorage: coreSettingsModel.showCustomStorage
    readonly property int customStorageAmount: coreSettingsModel.customStorageSizeGB
    readonly property string pruneTargetError: settingsModel.pruneTargetError
    readonly property var pruneSetting: coreSettingsModel.entry("prune")
    spacing: 8
    SettingsRestartNotice { visible: root.showRestartNotice; Layout.fillWidth: true }
    Setting {
        Layout.fillWidth: true
        header: qsTr("Store recent blocks only")
        state: root.pruneSetting.canEdit ? "FILLED" : "DISABLED"
        infoText: root.pruneSetting.infoText
        actionItem: OptionSwitch {
            checked: root.pruneSetting.enabled
            onToggled: root.pruneSetting.enabled = checked
        }
    }
    Setting {
        id: target
        objectName: "pruneTargetSetting"
        Layout.fillWidth: true
        header: qsTr("Block storage target (MiB; 1 is manual)")
        errorText: root.settingsModel.pruneTargetError
        showErrorText: errorText.length > 0
        state: root.pruneSetting.enabled && root.pruneSetting.canEdit ? "FILLED" : "DISABLED"
        actionItem: ValueInput {
            objectName: "pruneTargetInput"
            parentState: target.visualState
            description: root.settingsModel.pruneTargetText
            maximumLength: 65
            validator: null
            onTextEdited: root.settingsModel.editPruneTarget(text)
        }
    }
    CoreText { Layout.fillWidth: true; text: root.settingsModel.dataDir; wrap: true }
}
