// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import "../controls"

Popup {
    id: root

    property string title: qsTr("Action failed")
    property string message: ""

    modal: true
    padding: 0
    anchors.centerIn: parent
    width: parent ? Math.min(parent.width - 40, 360) : 360
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

        CoreText {
            Layout.fillWidth: true
            Layout.preferredHeight: 56
            text: root.title
            bold: true
            font.pixelSize: 20
            color: Theme.color.neutral9
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }

        Separator {
            Layout.fillWidth: true
        }

        CoreText {
            objectName: "actionErrorMessage"
            Layout.fillWidth: true
            Layout.margins: 20
            text: root.message
            color: Theme.color.neutral8
            font.pixelSize: 15
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
        }

        ContinueButton {
            objectName: "actionErrorCloseButton"
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            Layout.bottomMargin: 20
            text: qsTr("OK")
            onClicked: root.close()
        }
    }
}
