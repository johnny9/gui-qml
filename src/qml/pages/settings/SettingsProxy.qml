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

            CoreText {
                Layout.fillWidth: true
                Layout.topMargin: 20
                Layout.bottomMargin: 20
                Layout.leftMargin: 10
                Layout.rightMargin: 10
                text: qsTr("Proxy changes take effect after restarting the application.")
                color: Theme.color.neutral5
                wrapMode: Text.WordWrap
            }
        }
    }
}
