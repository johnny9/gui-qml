// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import org.bitcoincore.qt 1.0
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import "../../controls"
import "../../components"

SettingsPage {
    id: root
    objectName: "proxySettingsPage"
    title: qsTr("Proxy settings")
    backButtonObjectName: "proxySettingsBackButton"

    property var settingsModel: ApplicationModels.optionsModel
    property bool onboardingModal: false
    property var coreSettingsModel: settingsModel.coreSettings
    readonly property var proxySetting: coreSettingsModel.entry("proxy")
    readonly property var onionSetting: coreSettingsModel.entry("onion")
    readonly property CoreProxyDraft proxyDraft: coreSettingsModel.createProxyDraft()
    readonly property bool draftProxyEnabled: proxyDraft.proxyEnabled
    readonly property string draftProxyAddress: proxyDraft.proxyAddress
    readonly property string draftProxyValidationError: proxyDraft.proxyError
    readonly property bool draftTorEnabled: proxyDraft.torEnabled
    readonly property string draftTorAddress: proxyDraft.torAddress
    readonly property string draftTorValidationError: proxyDraft.torError
    readonly property bool proxyDraftDirty: proxyDraft.dirty
    readonly property bool canSaveProxyDraft: proxyDraft.canApply && proxyDraft.dirty
    property bool awaitingApply: false
    Connections {
        target: root.proxyDraft
        function onChanged() {
            if (!root.awaitingApply) return
            if (root.proxyDraft.operationState === CoreSettingsModel.Applied) {
                root.awaitingApply = false
                root.closeRequested()
            } else if (root.proxyDraft.operationState === CoreSettingsModel.Failed
                       || root.proxyDraft.operationState === CoreSettingsModel.Rejected) {
                root.awaitingApply = false
            }
        }
    }

    signal closeRequested()

    CoreText {
        Layout.fillWidth: true
        text: root.proxyDraft.error
        visible: text.length > 0
        wrapMode: Text.WordWrap
    }

    function resetProxyDraft() { proxyDraft.reset() }
    function updateProxyAddress(address) { proxyDraft.proxyAddress = address }
    function updateTorAddress(address) { proxyDraft.torAddress = address }
    function commitProxyDraft() {
        if (!canSaveProxyDraft || awaitingApply) return 0
        awaitingApply = true
        const ticket = proxyDraft.requestApply()
        if (!ticket) awaitingApply = false
        return ticket
    }
    function save() { commitProxyDraft() }

    function requestBack() {
        if (awaitingApply) return
        if (root.proxyDraftDirty) {
            discardProxyChangesPopup.open()
            return
        }
        root.closeRequested()
    }

    onBack: root.requestBack()
    Component.onCompleted: root.resetProxyDraft()

    SettingsRestartNotice {
        objectName: "proxyRestartNotice"
        visible: !root.onboardingModal && root.settingsModel.proxySettingsDirty
        Layout.fillWidth: true
    }

    FormSection {
        objectName: "defaultProxySection"
        Layout.fillWidth: true
        isOnSurface: root.onboardingModal
        showGradientBorder: false
        title: qsTr("Default proxy")
        description: qsTr("Route peer connections through a SOCKS5 proxy. IPv4, IPv6, and Tor connections are supported.")

        FormRow {
            Layout.fillWidth: true
            dividerColor: root.onboardingModal ? Theme.color.neutral3 : Theme.color.neutral2
            title: qsTr("Enable")
            supportingText: root.proxySetting.infoText
            enabled: root.proxySetting.canEdit && root.proxyDraft.ready && !root.awaitingApply
            trailingItem: OptionSwitch {
                objectName: "proxyEnableSwitch"
                checked: root.draftProxyEnabled
                onToggled: root.proxyDraft.proxyEnabled = checked
            }
        }

        TextFieldRow {
            id: proxyAddressRow
            objectName: "proxyAddressRow"
            Layout.fillWidth: true
            title: qsTr("Proxy location")
            enabled: root.draftProxyEnabled && root.proxySetting.canEdit && root.proxyDraft.ready && !root.awaitingApply
            fieldObjectName: "proxyAddressInput"
            fieldWidth: 220
            text: root.draftProxyAddress
            placeholderText: root.proxySetting.defaultAddress()
            errorText: root.draftProxyEnabled ? root.draftProxyValidationError : ""
            showDivider: false
            onTextEdited: function(text) { root.updateProxyAddress(text) }
            onEditingFinished: {
                root.updateProxyAddress(proxyAddressRow.text)
            }
        }
    }

    FormSection {
        objectName: "torProxySection"
        Layout.fillWidth: true
        isOnSurface: root.onboardingModal
        showGradientBorder: false
        title: qsTr("Tor proxy")
        description: qsTr("Route Tor connections through a dedicated SOCKS5 proxy.")

        FormRow {
            Layout.fillWidth: true
            dividerColor: root.onboardingModal ? Theme.color.neutral3 : Theme.color.neutral2
            title: qsTr("Enable")
            supportingText: root.onionSetting.infoText
            enabled: root.onionSetting.canEdit && root.proxyDraft.ready && !root.awaitingApply
            trailingItem: OptionSwitch {
                objectName: "torEnableSwitch"
                checked: root.draftTorEnabled
                onToggled: root.proxyDraft.torEnabled = checked
            }
        }

        TextFieldRow {
            id: torAddressRow
            objectName: "torAddressRow"
            Layout.fillWidth: true
            title: qsTr("Proxy location")
            enabled: root.draftTorEnabled && root.onionSetting.canEdit && root.proxyDraft.ready && !root.awaitingApply
            fieldObjectName: "torAddressInput"
            fieldWidth: 220
            text: root.draftTorAddress
            placeholderText: root.onionSetting.defaultAddress()
            errorText: root.draftTorEnabled ? root.draftTorValidationError : ""
            showDivider: false
            onTextEdited: function(text) { root.updateTorAddress(text) }
            onEditingFinished: {
                root.updateTorAddress(torAddressRow.text)
            }
        }
    }

    ContinueButton {
        objectName: "proxySettingsSaveButton"
        Layout.alignment: Qt.AlignRight
        Layout.preferredWidth: 140
        text: qsTr("Save")
        enabled: root.canSaveProxyDraft
        onClicked: root.save()
    }

    AlertPopup {
        id: discardProxyChangesPopup
        objectName: "discardProxyChangesPopup"
        parent: Overlay.overlay
        title: qsTr("Discard changes?")
        message: qsTr("This will discard your proxy settings changes.")
        messageObjectName: "discardProxyChangesMessage"

        AlertAction {
            text: qsTr("Cancel")
            role: AlertAction.Cancel
            buttonObjectName: "discardProxyChangesCancelButton"
        }

        AlertAction {
            text: qsTr("Discard")
            role: AlertAction.Destructive
            buttonObjectName: "discardProxyChangesConfirmButton"
            onTriggered: {
                root.resetProxyDraft()
                root.closeRequested()
            }
        }
    }
}
