// Copyright (c) 2023 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Layouts 1.15
import "../controls"

import org.bitcoincore.qt 1.0

ColumnLayout {
    id: root
    property var settingsModel: ApplicationModels.optionsModel
    property var coreSettingsModel: settingsModel.coreSettings
    property string proxyLocationHeader: qsTr("Proxy location")
    readonly property var proxySetting: coreSettingsModel.entry("proxy")
    readonly property var onionSetting: coreSettingsModel.entry("onion")
    property bool proxyEnabled: proxySetting.enabled
    property string proxyAddress: proxySetting.address.length > 0
                                  ? proxySetting.address
                                  : proxySetting.defaultAddress()
    property CoreProxyDraft proxyDraft: coreSettingsModel.createProxyDraft()
    property string proxyValidationError: proxyDraft.proxyError
    property bool torEnabled: onionSetting.enabled
    property string torAddress: onionSetting.address.length > 0
                                ? onionSetting.address
                                : onionSetting.defaultAddress()
    property string torValidationError: proxyDraft.torError

    signal proxyEnabledEdited(bool enabled)
    signal proxyAddressEdited(string address, string validationError)
    signal torEnabledEdited(bool enabled)
    signal torAddressEdited(string address, string validationError)

    spacing: 4
    Header {
        headerBold: true
        center: false
        header: qsTr("Default Proxy")
        headerSize: 24
        description: qsTr("Run peer connections through a proxy (SOCKS5) for improved privacy. The default proxy supports connections via IPv4, IPv6 and Tor.")
        descriptionSize: 15
        Layout.bottomMargin: 10
    }
    Separator { Layout.fillWidth: true }
    Setting {
        id: defaultProxyEnable
        Layout.fillWidth: true
        header: qsTr("Enable")
        state: root.proxySetting.canEdit && root.proxyDraft.ready ? "FILLED" : "DISABLED"
        infoText: root.proxySetting.infoText
        showInfoText: infoText.length > 0
        actionItem: OptionSwitch {
            objectName: "proxyEnableSwitch"
            checked: root.proxyEnabled
            onToggled: {
                root.proxyEnabledEdited(checked)
            }
        }
        onClicked: {
            const control = loadedItem as OptionSwitch
            control.toggle()
            control.toggled()
        }
    }
    Separator { Layout.fillWidth: true }
    Setting {
        id: defaultProxy
        objectName: "proxyAddressSetting"
        Layout.fillWidth: true
        header: root.proxyLocationHeader
        errorText: loadedItem ? (loadedItem as ProxyLocationInput).validationError : ""
        state: root.proxyEnabled && root.proxySetting.canEdit && root.proxyDraft.ready ? "FILLED" : "DISABLED"
        showErrorText: loadedItem && !(loadedItem as ProxyLocationInput).validInput && root.proxyEnabled
        infoText: root.proxySetting.infoText
        showInfoText: !showErrorText && infoText.length > 0
        actionItem: ProxyLocationInput {
            objectName: "proxyAddressInput"
            validationError: root.proxyValidationError
            validInput: !root.proxyDraft.pending && validationError.length === 0
            parentState: defaultProxy.visualState
            accessibleName: qsTr("Default proxy location")
            description: root.proxyAddress
            Component.onCompleted: {
                if (text !== "") filled = true
            }
            onDescriptionChanged: {
                filled = text !== ""
            }
            onTextChanged: {
                root.proxyAddressEdited(text, validationError)
            }
            onEditingFinished: {
                root.proxyAddressEdited(text, validationError)
                if (validInput) filled = true
            }
        }
        onClicked: {
            (loadedItem as ProxyLocationInput).beginEdit()
        }
    }
    Separator { Layout.fillWidth: true }
    Header {
        headerBold: true
        center: false
        header: qsTr("Tor Proxy")
        headerSize: 24
        description: qsTr("Run Tor connections through a dedicated proxy.")
        descriptionSize: 15
        Layout.topMargin: 35
        Layout.bottomMargin: 10
    }
    Separator { Layout.fillWidth: true }
    Setting {
        id: torProxyEnable
        Layout.fillWidth: true
        header: qsTr("Enable")
        state: root.onionSetting.canEdit && root.proxyDraft.ready ? "FILLED" : "DISABLED"
        infoText: root.onionSetting.infoText
        showInfoText: infoText.length > 0
        actionItem: OptionSwitch {
            objectName: "torEnableSwitch"
            checked: root.torEnabled
            onToggled: {
                root.torEnabledEdited(checked)
            }
        }
        onClicked: {
            const control = loadedItem as OptionSwitch
            control.toggle()
            control.toggled()
        }
    }
    Separator { Layout.fillWidth: true }
    Setting {
        id: torProxy
        objectName: "torAddressSetting"
        Layout.fillWidth: true
        header: root.proxyLocationHeader
        errorText: loadedItem ? (loadedItem as ProxyLocationInput).validationError : ""
        state: root.torEnabled && root.onionSetting.canEdit && root.proxyDraft.ready ? "FILLED" : "DISABLED"
        showErrorText: loadedItem && !(loadedItem as ProxyLocationInput).validInput && root.torEnabled
        infoText: root.onionSetting.infoText
        showInfoText: !showErrorText && infoText.length > 0
        actionItem: ProxyLocationInput {
            objectName: "torAddressInput"
            validationError: root.torValidationError
            validInput: !root.proxyDraft.pending && validationError.length === 0
            parentState: torProxy.visualState
            accessibleName: qsTr("Tor proxy location")
            description: root.torAddress
            Component.onCompleted: {
                if (text !== "") filled = true
            }
            onDescriptionChanged: {
                filled = text !== ""
            }
            onTextChanged: {
                root.torAddressEdited(text, validationError)
            }
            onEditingFinished: {
                root.torAddressEdited(text, validationError)
                if (validInput) filled = true
            }
        }
        onClicked: {
            (loadedItem as ProxyLocationInput).beginEdit()
        }
    }
    Separator { Layout.fillWidth: true }
}
