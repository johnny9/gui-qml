// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import org.bitcoincore.qt 1.0
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../controls"
import "../components"

ApplicationWindow {
    id: root
    property bool showCompleteDiagnostics: false
    visible: true
    title: "Bitcoin Core App"
    minimumWidth: 500
    minimumHeight: showCompleteDiagnostics ? 520 : 250
    color: Theme.color.background
    ColumnLayout {
        spacing: 30
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        width: Math.min(root.width - 56, 640)

        Header {
            Layout.topMargin: 30
            Layout.fillWidth: true
            headerBold: true
            header: qsTr("There was an issue starting up.")
            headerSize: 21
            description: ApplicationModels.startupError
            descriptionMargin: 3
        }
        MonospaceOutputView {
            objectName: "startupCompleteDiagnostics"
            Layout.fillWidth: true
            Layout.preferredHeight: 240
            visible: root.showCompleteDiagnostics
            listModel: ApplicationModels.startupErrorDetails
            autoScrollToBottom: false
        }
        OutlineButton {
            objectName: "startupDetailsButton"
            Layout.alignment: Qt.AlignCenter
            visible: ApplicationModels.startupErrorDetails !== null
            text: root.showCompleteDiagnostics ? qsTr("Show summary") : qsTr("Show complete diagnostics")
            onClicked: root.showCompleteDiagnostics = !root.showCompleteDiagnostics
        }
        OutlineButton {
            Layout.alignment: Qt.AlignCenter
            text: qsTr("Close")
            onClicked: Qt.quit()
        }
    }
}
