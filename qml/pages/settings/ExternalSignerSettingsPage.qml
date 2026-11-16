// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import org.bitcoincore.qt 1.0
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import "../../controls"

SettingsPage {
    id: root
    objectName: "externalSignerSettingsPage"
    title: qsTr("External signer")
    showBackButton: false

    property SettingsEditorModel editor: null
    readonly property SettingsEntryModel signerEntry: editor ? editor.entry("signer") : null
    readonly property bool savingConfiguration: editor ? editor.phase === SettingsEditorModel.Checking || editor.phase === SettingsEditorModel.Saving : discovery && discovery.saving
    readonly property bool canApplyConfiguration: editor ? editor.canSave : discovery && discovery.canApply
    readonly property SignerDiscoveryModel discovery: ApplicationModels.signers
    readonly property bool signerConnected: discovery && !savingConfiguration && discovery.fresh && discovery.uniqueSignerId.length > 0
    readonly property string signerStatusText: !discovery ? qsTr("Signer discovery is unavailable.")
        : savingConfiguration ? qsTr("Saving signer configuration…")
        : discovery.pending ? qsTr("Checking external signer…")
        : discovery.error.message.length > 0 ? discovery.error.message
        : signerConnected ? qsTr("Detected external signer: %1").arg(discovery.uniqueSignerName)
        : qsTr("No external signer is currently detected.")

    PageHeading {
        objectName: "externalSignerIntroduction"
        Layout.fillWidth: true
        description: qsTr("Connect a hardware wallet or another external signing tool.")
    }

    FormSection {
        objectName: "externalSignerPathSection"
        Layout.fillWidth: true
        title: qsTr("Signer path")
        footerText: qsTr("The add wallet flow can offer external wallets when exactly one supported signer is connected.")

        FormRow {
            objectName: "externalSignerPathRow"
            Layout.fillWidth: true
            enabled: root.discovery !== null
            showDivider: false
            bodySpacing: 0
            topPadding: 16
            bottomPadding: 16
            bodyItem: RowLayout {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                spacing: 16

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    spacing: 12

                    TextField {
                        id: signerPathInput
                        objectName: "externalSignerPathInput"
                        Layout.fillWidth: true
                        implicitHeight: 37
                        text: root.signerEntry ? root.signerEntry.text : root.discovery ? root.discovery.editedPath : ""
                        maximumLength: 32769
                        readOnly: root.editor ? !root.signerEntry || !root.signerEntry.editable : !root.discovery || !root.discovery.configurationKnown || !root.discovery.editable || root.discovery.saving
                        placeholderText: qsTr("Enter external signer path")
                        placeholderTextColor: Theme.color.neutral7
                        color: Theme.color.neutral9
                        font: Theme.text.description.font
                        selectByMouse: true
                        leftPadding: 15
                        rightPadding: 10
                        topPadding: 0
                        bottomPadding: 0
                        verticalAlignment: TextInput.AlignVCenter
                        background: Rectangle {
                            color: Theme.color.neutral2
                            radius: 5

                            FocusBorder {
                                objectName: "externalSignerPathFocusBorder"
                                visible: signerPathInput.activeFocus
                                border.color: Theme.color.orange
                                borderRadius: 7
                                topMargin: -2
                                bottomMargin: -2
                                leftMargin: -2
                                rightMargin: -2
                            }
                        }
                        onTextEdited: if (root.signerEntry) root.signerEntry.editText(text); else if (root.discovery) root.discovery.editPath(text)
                        onAccepted: focus = false
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Rectangle {
                            objectName: "externalSignerStatusIndicator"
                            Layout.preferredWidth: 10
                            Layout.preferredHeight: 10
                            Layout.alignment: Qt.AlignTop
                            Layout.topMargin: 4
                            radius: width / 2
                            color: !root.discovery || root.savingConfiguration || !root.discovery.fresh ? Theme.color.neutral7
                                : root.signerConnected ? Theme.color.green : Theme.color.red

                            Behavior on color {
                                enabled: Theme.animationsEnabled
                                ColorAnimation { duration: 150 }
                            }
                        }

                        CoreText {
                            objectName: "externalSignerStatusText"
                            Layout.fillWidth: true
                            text: root.signerStatusText
                            color: Theme.color.neutral7
                            font: Theme.text.description.font
                            lineHeight: Theme.text.description.lineHeight
                            lineHeightMode: Text.FixedHeight
                            horizontalAlignment: Text.AlignLeft
                            wrap: true
                        }
                    }
                }

                ContinueButton {
                    objectName: "externalSignerCheckDeviceButton"
                    Layout.preferredWidth: 140
                    Layout.preferredHeight: 40
                    Layout.alignment: Qt.AlignVCenter
                    text: root.editor && root.editor.inputPending ? qsTr("Checking edited path…") : root.canApplyConfiguration ? qsTr("Save and check") : qsTr("Check device")
                    textStyle: Theme.text.subheading
                    enabled: root.discovery && (!root.editor || !root.editor.inputPending) && !root.savingConfiguration && (root.canApplyConfiguration || root.discovery.canRefresh)
                    onClicked: {
                        if (!enabled) return
                        if (root.canApplyConfiguration) { if (root.editor) root.editor.save(); else root.discovery.apply(); }
                        else root.discovery.refresh()
                    }
                }
            }
        }
    }

    CoreText {
        objectName: "externalSignerEditErrorText"
        Layout.fillWidth: true
        text: root.editor ? root.signerEntry.error || root.editor.error : root.discovery ? root.discovery.editError.message : ""
        visible: text.length > 0
        color: Theme.color.red
        wrap: true
    }
    CoreText {
        objectName: "externalSignerEffectiveConfigurationText"
        Layout.fillWidth: true
        text: root.discovery && root.discovery.configurationSource === "CommandLine" ? qsTr("Set by command line. Remove the override to edit it here.")
            : root.discovery && (root.signerEntry ? root.signerEntry.text : root.discovery.editedPath) !== root.discovery.effectivePath ? qsTr("The edited path has not been applied. Device status refers to the effective configuration.") : ""
        visible: text.length > 0
        wrap: true
    }
    onVisibleChanged: if (visible && root.discovery) root.discovery.refresh()
    Component.onCompleted: ApplicationModels.walletController.refreshExternalSignerStatus()
}
