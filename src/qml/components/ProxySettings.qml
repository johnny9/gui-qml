// Copyright (c) 2023 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../controls"

import org.bitcoincore.qt 1.0

ColumnLayout {
    property string ipAndPortHeader: qsTr("IP and Port")
    property string invalidIpError: qsTr("Invalid IP address or port format. Use '255.255.255.255:65535' or '[ffff::]:65535'")

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
        actionItem: OptionSwitch {
            objectName: "proxyEnableSwitch"
            checked: optionsModel.proxyEnabled
            onCheckedChanged: {
                // Default to the standard SOCKS5 proxy address (port 9050).
                // The Tor daemon also defaults to port 9050; the user can
                // edit the address after enabling.
                if (checked && optionsModel.proxyAddress === "") {
                    optionsModel.proxyAddress = nodeModel.defaultProxyAddress()
                }
                optionsModel.proxyEnabled = checked
                defaultProxy.state = checked ? "FILLED" : "DISABLED"
            }
        }
        onClicked: {
            loadedItem.toggle()
            loadedItem.toggled()
        }
    }
    Separator { Layout.fillWidth: true }
    Setting {
        id: defaultProxy
        Layout.fillWidth: true
        header: ipAndPortHeader
        errorText: invalidIpError
        state: !defaultProxyEnable.loadedItem.checked ? "DISABLED" : "FILLED"
        showErrorText: !defaultProxy.loadedItem.validInput && defaultProxyEnable.loadedItem.checked
        actionItem: IPAddressValueInput {
            objectName: "proxyAddressInput"
            parentState: defaultProxy.state
            description: optionsModel.proxyAddress.length > 0
                         ? optionsModel.proxyAddress
                         : nodeModel.defaultProxyAddress()
            activeFocusOnTab: true
            // IPAddressValueInput.text is initially bound to `description`.
            // If a stored address was loaded, text is non-empty here and we
            // mark the field as filled so it renders in the active style.
            Component.onCompleted: {
                if (text !== "") filled = true
            }
            onTextChanged: {
                validInput = nodeModel.validateProxyAddress(text)
                if (validInput) optionsModel.proxyAddress = text
            }
        }
        onClicked: {
            loadedItem.filled = true
            loadedItem.forceActiveFocus()
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
        actionItem: OptionSwitch {
            objectName: "torEnableSwitch"
            checked: optionsModel.torEnabled
            onCheckedChanged: {
                // Default to the standard SOCKS5 proxy address (port 9050).
                // The Tor daemon also defaults to port 9050; the user can
                // edit the address after enabling.
                if (checked && optionsModel.torAddress === "") {
                    optionsModel.torAddress = nodeModel.defaultProxyAddress()
                }
                optionsModel.torEnabled = checked
                torProxy.state = checked ? "FILLED" : "DISABLED"
            }
        }
        onClicked: {
            loadedItem.toggle()
            loadedItem.toggled()
        }
    }
    Separator { Layout.fillWidth: true }
    Setting {
        id: torProxy
        Layout.fillWidth: true
        header: ipAndPortHeader
        errorText: invalidIpError
        state: !torProxyEnable.loadedItem.checked ? "DISABLED" : "FILLED"
        showErrorText: !torProxy.loadedItem.validInput && torProxyEnable.loadedItem.checked
        actionItem: IPAddressValueInput {
            objectName: "torAddressInput"
            parentState: torProxy.state
            description: optionsModel.torAddress.length > 0
                         ? optionsModel.torAddress
                         : nodeModel.defaultProxyAddress()
            activeFocusOnTab: true
            // IPAddressValueInput.text is initially bound to `description`.
            // If a stored address was loaded, text is non-empty here and we
            // mark the field as filled so it renders in the active style.
            Component.onCompleted: {
                if (text !== "") filled = true
            }
            onTextChanged: {
                validInput = nodeModel.validateProxyAddress(text)
                if (validInput) optionsModel.torAddress = text
            }
        }
        onClicked: {
            loadedItem.filled = true
            loadedItem.forceActiveFocus()
        }
    }
    Separator { Layout.fillWidth: true }
}
