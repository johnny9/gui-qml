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

    readonly property bool proxySettingsDirty: optionsModel.proxySettingsDirty

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

            Rectangle {
                Layout.fillWidth: true
                Layout.topMargin: 10
                Layout.bottomMargin: 20
                Layout.leftMargin: 10
                Layout.rightMargin: 10
                // Note: this advisory is only shown in the post-onboarding settings
                // context. No restart is needed when configuring proxy during onboarding
                // since the node has not started yet.
                implicitHeight: advisoryRow.implicitHeight + 20
                radius: 5
                color: optionsModel.proxySettingsDirty
                    ? Qt.rgba(Theme.color.blue.r, Theme.color.blue.g, Theme.color.blue.b, 0.25)
                    : Qt.rgba(Theme.color.neutral2.r, Theme.color.neutral2.g, Theme.color.neutral2.b, 0.5)
                RowLayout {
                    id: advisoryRow
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 10
                    spacing: 8
                    Icon {
                        source: "image://images/info-filled"
                        color: optionsModel.proxySettingsDirty ? Theme.color.blue : Theme.color.neutral9
                        size: 16
                        Layout.alignment: Qt.AlignVCenter
                    }
                    CoreText {
                        Layout.fillWidth: true
                        text: qsTr("Restart the application for these changes to take effect.")
                        color: optionsModel.proxySettingsDirty ? Theme.color.blue : Theme.color.neutral9
                        wrapMode: Text.WordWrap
                    }
                }
            }

            ProxySettings {
                Layout.fillWidth: true
            }
        }
    }
}
