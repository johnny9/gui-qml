// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import "../controls"

Popup {
    id: root

    property string popupObjectName: ""
    property string walletName: ""
    property string cancelButtonObjectName: ""
    property string confirmButtonObjectName: ""

    signal confirmed()

    objectName: popupObjectName
    modal: true
    padding: 0
    implicitWidth: 420
    implicitHeight: columnLayout.implicitHeight
    anchors.centerIn: parent

    background: Rectangle {
        color: Theme.color.background
        radius: 10
        border.color: Theme.color.neutral4
        border.width: 1
    }

    ColumnLayout {
        id: columnLayout
        anchors.fill: parent
        spacing: 0

        CoreText {
            Layout.fillWidth: true
            Layout.preferredHeight: 56
            text: qsTr("Close wallet")
            bold: true
            font.pixelSize: 24
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }

        Separator {
            Layout.fillWidth: true
        }

        Header {
            Layout.fillWidth: true
            Layout.margins: 20
            Layout.topMargin: 20
            header: qsTr("Do you want to close the wallet \"%1\"?").arg(root.walletName)
            headerBold: false
            headerSize: 16
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 20
            Layout.topMargin: 20
            spacing: 15

            OutlineButton {
                objectName: root.cancelButtonObjectName
                Layout.fillWidth: true
                Layout.minimumWidth: 120
                text: qsTr("Cancel")
                onClicked: root.close()
            }

            ContinueButton {
                objectName: root.confirmButtonObjectName
                Layout.fillWidth: true
                Layout.minimumWidth: 120
                text: qsTr("Close wallet")
                onClicked: {
                    root.confirmed()
                    root.close()
                }
            }
        }
    }
}
