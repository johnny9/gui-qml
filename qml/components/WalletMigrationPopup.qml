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
    property string titleText: qsTr("Wallet update required")
    property string descriptionText: ""
    property string confirmText: qsTr("Update wallet")
    property string busyConfirmText: qsTr("Updating...")
    property string errorText: ""
    property var errorDetailsModel: null
    property bool showErrorDetails: false
    onErrorTextChanged: showErrorDetails = false
    property string errorTextObjectName: ""
    property string cancelButtonObjectName: ""
    property string confirmButtonObjectName: ""
    property bool busy: false

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
        SurfaceGradientBorder {
            anchors.fill: parent
            surfaceColor: parent.color
            referenceColor: Theme.color.neutral1
            cornerRadius: parent.radius
        }
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

        Header {
            Layout.fillWidth: true
            Layout.margins: 20
            Layout.topMargin: 20
            header: root.descriptionText
            headerBold: false
            headerSize: 16
        }

        CoreText {
            objectName: root.errorTextObjectName
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            Layout.topMargin: 4
            visible: text.length > 0
            text: root.errorText
            color: Theme.color.red
            font.pixelSize: 15
            wrapMode: Text.WordWrap
        }

        TextButton {
            objectName: root.errorTextObjectName + "DetailsButton"
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            visible: root.errorText.length > 0 && root.errorDetailsModel !== null
            text: root.showErrorDetails ? qsTr("Hide details") : qsTr("Show details")
            onClicked: root.showErrorDetails = !root.showErrorDetails
        }

        ListView {
            id: errorDetails
            objectName: root.errorTextObjectName + "Details"
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            Layout.preferredHeight: visible ? 120 : 0
            visible: root.showErrorDetails && root.errorText.length > 0 && root.errorDetailsModel !== null
            model: root.errorDetailsModel
            clip: true
            reuseItems: true
            cacheBuffer: 0
            delegate: CoreText {
                required property string content
                width: errorDetails.width
                text: content
                textFormat: Text.PlainText
                wrapMode: Text.WordWrap
                font.pixelSize: 15
            }
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
                enabled: !root.busy
                text: qsTr("Cancel")
                onClicked: root.close()
            }

            ContinueButton {
                objectName: root.confirmButtonObjectName
                Layout.fillWidth: true
                Layout.minimumWidth: 120
                enabled: !root.busy
                text: root.busy ? root.busyConfirmText : root.confirmText
                onClicked: root.confirmed()
            }
        }
    }
}
