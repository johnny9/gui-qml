// Copyright (c) 2023 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../../controls"
import "../../components"

Page {
    signal back

    id: root
    objectName: "settingsProxy"

    // Snapshot of proxy state at page-open time; used to detect pending changes.
    property bool initialProxyEnabled: false
    property string initialProxyAddress: ""
    property bool initialTorEnabled: false
    property string initialTorAddress: ""

    readonly property bool settingsModified:
        optionsModel.proxyEnabled !== initialProxyEnabled ||
        optionsModel.proxyAddress !== initialProxyAddress ||
        optionsModel.torEnabled !== initialTorEnabled ||
        optionsModel.torAddress !== initialTorAddress

    Component.onCompleted: {
        initialProxyEnabled = optionsModel.proxyEnabled
        initialProxyAddress = optionsModel.proxyAddress
        initialTorEnabled = optionsModel.torEnabled
        initialTorAddress = optionsModel.torAddress
    }

    background: null

    header: NavigationBar2 {
        leftItem: NavButton {
            objectName: "settingsProxyBack"
            iconSource: "image://images/caret-left"
            text: qsTr("Back")
            onClicked: root.back()
        }
        centerItem: Header {
            headerBold: true
            headerSize: 18
            header: qsTr("Proxy Settings")
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

            ProxySettings {
                Layout.fillWidth: true
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.topMargin: 20
                Layout.bottomMargin: 20
                Layout.leftMargin: 10
                Layout.rightMargin: 10
                // Note: this advisory is only shown in the post-onboarding settings
                // context. No restart is needed when configuring proxy during onboarding
                // since the node has not started yet.
                implicitHeight: advisoryText.implicitHeight + 20
                radius: 5
                color: root.settingsModified ? Theme.color.blue : Theme.color.neutral2
                CoreText {
                    id: advisoryText
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 10
                    text: qsTr("Proxy changes take effect after restarting the application.")
                    color: root.settingsModified ? Theme.color.white : Theme.color.neutral5
                    wrapMode: Text.WordWrap
                }
            }
        }
    }
}
