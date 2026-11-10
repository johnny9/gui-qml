// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0

import "../controls"

Popup {
    id: root
    objectName: "walletBackupOverwritePopup"
    required property BackupFileOperation operation
    readonly property string title: operation.replacing ? qsTr("Replace backup?") : qsTr("Save backup?")
    parent: Overlay.overlay
    width: parent ? Math.min(600, parent.width - 40) : 600
    implicitHeight: content.implicitHeight + topPadding + bottomPadding
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) : 0
    padding: 24
    modal: true
    dim: true
    focus: true
    closePolicy: Popup.NoAutoClose
    onOpened: content.forceActiveFocus()

    background: Rectangle {
        color: Theme.color.neutral1
        radius: 10
        border.color: Theme.color.neutral2
    }
    contentItem: ColumnLayout {
        id: content
        focus: true
        spacing: 16
        Keys.priority: Keys.BeforeItem
        Keys.onEscapePressed: function(event) {
            root.operation.refuse()
            event.accepted = true
        }
        CoreText {
            Layout.fillWidth: true
            textFormat: Text.PlainText
            text: root.title
            font: Theme.text.subtitle.font
        }
        CoreText {
            Layout.fillWidth: true
            textFormat: Text.PlainText
            text: root.operation.replacing
                ? qsTr("An existing file will be replaced at this final destination:")
                : qsTr("Save the backup at this final destination:")
            wrap: true
        }
        CoreText {
            objectName: "walletBackupFinalPathText"
            Layout.fillWidth: true
            textFormat: Text.PlainText
            text: root.operation.finalPath
            wrapMode: Text.WrapAnywhere
        }
        RowLayout {
            spacing: 12
            NeutralButton {
                objectName: "walletBackupOverwriteRejectButton"
                text: qsTr("Cancel")
                onClicked: root.operation.refuse()
            }
            NeutralButton {
                objectName: "walletBackupOverwriteApproveButton"
                text: root.operation.replacing ? qsTr("Replace backup") : qsTr("Save backup")
                onClicked: root.operation.approve()
            }
        }
    }
}
