// Copyright (c) 2023 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import org.bitcoincore.qt 1.0
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../../controls"
import "../../components"

Page {
    signal back

    id: root
    objectName: "settingsProxy"

    property var settingsModel: ApplicationModels.optionsModel
    property var coreSettingsModel: settingsModel.coreSettings
    property bool onboarding: false
    readonly property bool proxySettingsDirty: settingsModel.proxySettingsDirty
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
    readonly property bool canSaveProxyDraft: proxyDraft.canApply
    property bool awaitingApply: false
    Connections {
        target: root.proxyDraft
        function onChanged() {
            if (!root.awaitingApply) return
            if (root.proxyDraft.operationState === CoreSettingsModel.Applied) {
                root.awaitingApply = false
                root.back()
            } else if (root.proxyDraft.operationState === CoreSettingsModel.Failed
                       || root.proxyDraft.operationState === CoreSettingsModel.Rejected) {
                root.awaitingApply = false
            }
        }
    }

    background: null

    Component.onCompleted: resetProxyDraft()

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
    function done() {
        if (!proxyDraftDirty && !proxyDraft.pending) { root.back(); return }
        commitProxyDraft()
    }

    function requestBack() {
        if (awaitingApply) return
        if (proxyDraftDirty) {
            discardProxyChangesPopup.open()
            return
        }
        root.back()
    }

    header: SettingsHeader {
        title: qsTr("Proxy settings")
        backButtonObjectName: "settingsProxyBack"
        onBack: root.requestBack()
        rightItem: NavButton {
            objectName: "settingsProxyDone"
            text: qsTr("Done")
            enabled: root.canSaveProxyDraft
            onClicked: root.done()
        }
    }

    ScrollView {
        anchors.fill: parent
        contentWidth: width
        clip: true

        ColumnLayout {
            width: Math.min(parent.width, 450)
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 0

            SettingsRestartNotice {
                Layout.fillWidth: true
                Layout.topMargin: 10
                Layout.bottomMargin: 20
                visible: !root.onboarding && root.settingsModel.proxySettingsDirty
            }

            CoreText {
                Layout.fillWidth: true
                text: root.proxyDraft.error
                visible: text.length > 0
                wrapMode: Text.WordWrap
            }

            ProxySettings {
                settingsModel: root.settingsModel
                proxyEnabled: root.draftProxyEnabled
                proxyAddress: root.draftProxyAddress
                proxyValidationError: root.draftProxyValidationError
                torEnabled: root.draftTorEnabled
                torAddress: root.draftTorAddress
                torValidationError: root.draftTorValidationError
                proxyDraft: root.proxyDraft
                onProxyEnabledEdited: (enabled) => root.proxyDraft.proxyEnabled = enabled
                onProxyAddressEdited: (address, validationError) => {
                    root.proxyDraft.proxyAddress = address
                }
                onTorEnabledEdited: (enabled) => root.proxyDraft.torEnabled = enabled
                onTorAddressEdited: (address, validationError) => {
                    root.proxyDraft.torAddress = address
                }
                Layout.fillWidth: true
            }
        }
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
                root.back()
            }
        }
    }
}
