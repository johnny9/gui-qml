// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0
import "../../controls"
import "../../components"

Page {
    signal backClicked()

    property PeerDetailsModel details

    Connections {
        target: details
        function onDisconnected() {
            root.backClicked()
        }
    }

    id: root
    background: null

    header: NavigationBar2 {
        leftItem: NavButton {
            iconSource: "image://images/caret-left"
            text: qsTr("Back")
            onClicked: root.backClicked()
        }
        centerItem: Header {
            headerBold: true
            headerSize: 18
            header: qsTr("Peer " + details.nodeId)
        }
    }

    component PeerKeyValueRow: Row {
        property string key: ""
        property string value: ""
        CoreText {
            color: value.length > 0 ? Theme.color.white : Theme.color.neutral6;
            text: key;
            width: 125;
            horizontalAlignment: Qt.AlignLeft;
        }
        CoreText {
            text: value.length > 0 ? value : "--";
            horizontalAlignment: Qt.AlignLeft;
        }
    }

    ScrollView {
        id: scrollView
        width: parent.width
        height: parent.height
        clip: true
        contentWidth: width

        Column {
            width: Math.min(parent.width, 450)
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 20

            CoreText { text: "Information"; bold: true; font.pixelSize: 18; horizontalAlignment: Qt.AlignLeft; }
            Column {
                PeerKeyValueRow { key: qsTr("IP"); value: details.address }
                PeerKeyValueRow { key: qsTr("VIA"); value: details.addressLocal }
                PeerKeyValueRow { key: qsTr("Type"); value: details.type }
                PeerKeyValueRow { key: qsTr("Permission"); value: details.permission }
                PeerKeyValueRow { key: qsTr("Version"); value: details.version }
                PeerKeyValueRow { key: qsTr("User agent"); value: details.userAgent }
                PeerKeyValueRow { key: qsTr("Services"); value: details.services }
                PeerKeyValueRow { key: qsTr("Transport relay"); value: details.transactionRelay }
                PeerKeyValueRow { key: qsTr("Address relay"); value: details.addressRelay }
                PeerKeyValueRow { key: qsTr("Mapped AS"); value: details.mappedAS }
            }

            CoreText { text: "Block data"; bold: true; font.pixelSize: 18; horizontalAlignment: Qt.AlignLeft; }
            Column {
                PeerKeyValueRow { key: qsTr("Starting block"); value: details.startingHeight }
                PeerKeyValueRow { key: qsTr("Synced headers"); value: details.syncedHeaders }
                PeerKeyValueRow { key: qsTr("Synced blocks"); value: details.syncedBlocks }
            }

            CoreText { text: "Network traffic"; bold: true; font.pixelSize: 18; horizontalAlignment: Qt.AlignLeft; }
            Column {
                PeerKeyValueRow { key: qsTr("Direction"); value: details.direction }
                PeerKeyValueRow { key: qsTr("Last send"); value: details.lastSend }
                PeerKeyValueRow { key: qsTr("Last receive"); value: details.lastReceived }
                PeerKeyValueRow { key: qsTr("Sent"); value: details.bytesSent }
                PeerKeyValueRow { key: qsTr("Received"); value: details.bytesReceived }
                PeerKeyValueRow { key: qsTr("Ping time"); value: details.pingTime }
                PeerKeyValueRow { key: qsTr("Ping wait"); value: details.pingWait }
                PeerKeyValueRow { key: qsTr("Min ping"); value: details.pingMin }
                PeerKeyValueRow { key: qsTr("Time offset"); value: details.timeOffset }
            }

            SettingsButton {
                text: qsTr("Disconnect")
                width: 150
                height: 30
            }

            CoreText { text: "Ban this peer"; bold: true; font.pixelSize: 18; horizontalAlignment: Qt.AlignLeft; }

            Row {
                PeerRadioButton {
                    text: qsTr("1 hour")
                }
                PeerRadioButton {
                    text: qsTr("1 day")
                }
                PeerRadioButton {
                    text: qsTr("1 week")
                }
                PeerRadioButton {
                    text: qsTr("1 year")
                }
            }

            SettingsButton {
                text: qsTr("Ban")
                width: 150
                height: 30
            }
        }
    }
}