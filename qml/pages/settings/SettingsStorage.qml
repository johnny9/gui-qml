// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import org.bitcoincore.qt 1.0
import "../../controls"
import "../../components"

InformationPage {
    id: root
    objectName: "settingsStoragePage"
    property var settingsModel: ApplicationModels.optionsModel
    readonly property bool customStorage: storageSettings.customStorage
    readonly property int customStorageAmount: storageSettings.customStorageAmount
    property bool onboarding: false
    property bool showBackButton: true
    bannerActive: false
    bold: true
    showHeader: root.onboarding
    headerText: qsTr("Storage settings")
    headerMargin: 0
    detailActive: true
    detailItem: StorageSettings {
        id: storageSettings
        settingsModel: root.settingsModel
        showRestartNotice: !root.onboarding && root.settingsModel.storageSettingsDirty

    }
    showNavBar: false
    header: SettingsHeader {
        title: root.onboarding ? "" : qsTr("Storage settings")
        showBackButton: !root.onboarding && root.showBackButton
        backButtonObjectName: "settingsStorageBack"
        onBack: root.back()
        rightItem: NavButton {
            visible: root.onboarding
            text: qsTr("Done")
            onClicked: root.back()
        }
    }
}
