// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import "../controls"

Popup {
    id: root
    objectName: "nodeRuntimeDialog"
    readonly property int contentMargin: 28

    function syncOpenState() {
        if (nodeModel.runtimeDialogVisible && !opened) {
            open()
        } else if (!nodeModel.runtimeDialogVisible && opened) {
            close()
        }
    }

    Connections {
        target: nodeModel
        function onRuntimeDialogChanged() {
            root.syncOpenState()
        }
    }

    Component.onCompleted: syncOpenState()

    modal: true
    closePolicy: Popup.NoAutoClose
    padding: 0
    anchors.centerIn: parent
    width: parent ? Math.min(parent.width - (2 * contentMargin), 640) : 640
    height: Math.min(implicitHeight, parent ? parent.height - 80 : 520)
    implicitHeight: columnLayout.implicitHeight

    background: Rectangle {
        color: Theme.color.background
        radius: 8
        border.color: Theme.color.neutral4
        border.width: 1
    }

    ColumnLayout {
        id: columnLayout
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 64
            Layout.leftMargin: root.contentMargin
            Layout.rightMargin: root.contentMargin
            spacing: 12

            Icon {
                Layout.preferredWidth: 24
                Layout.preferredHeight: 24
                source: nodeModel.runtimeDialogIcon
                color: Theme.color.orange
                size: 24
            }

            CoreText {
                objectName: "nodeRuntimeDialogTitle"
                Layout.fillWidth: true
                text: nodeModel.runtimeDialogTitle
                bold: true
                font.pixelSize: 20
                horizontalAlignment: Text.AlignLeft
            }
        }

        Separator { Layout.fillWidth: true }

        ScrollView {
            id: runtimeMessageScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: root.contentMargin
            contentWidth: availableWidth
            clip: true

            CoreText {
                objectName: "nodeRuntimeDialogMessage"
                width: runtimeMessageScroll.availableWidth
                text: nodeModel.runtimeDialogMessage
                color: Theme.color.neutral8
                font.pixelSize: 15
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignLeft
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: root.contentMargin
            Layout.topMargin: 0
            spacing: 12

            OutlineButton {
                objectName: "nodeRuntimeDialogSecondaryButton"
                Layout.fillWidth: true
                visible: nodeModel.runtimeDialogSecondaryText.length > 0
                text: nodeModel.runtimeDialogSecondaryText
                onClicked: nodeModel.answerRuntimeDialog(false)
            }

            ContinueButton {
                objectName: "nodeRuntimeDialogPrimaryButton"
                Layout.fillWidth: true
                text: nodeModel.runtimeDialogPrimaryText
                onClicked: nodeModel.answerRuntimeDialog(true)
            }
        }
    }
}
