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
    property string titleText: qsTr("Failed to open wallet")
    property string errorText: ""
    property string errorTextObjectName: ""
    property string dismissButtonObjectName: ""
    property string dismissText: qsTr("OK")

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
            text: root.titleText
            bold: true
            font.pixelSize: 24
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }

        Separator {
            Layout.fillWidth: true
        }

        CoreText {
            objectName: root.errorTextObjectName
            Layout.fillWidth: true
            Layout.margins: 20
            Layout.topMargin: 20
            text: root.errorText
            color: Theme.color.red
            font.pixelSize: 16
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }

        ContinueButton {
            objectName: root.dismissButtonObjectName
            Layout.fillWidth: true
            Layout.margins: 20
            Layout.topMargin: 0
            Layout.minimumWidth: 120
            text: root.dismissText
            onClicked: root.close()
        }
    }
}
