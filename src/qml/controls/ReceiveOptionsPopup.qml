// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import "../components"
import "../controls"

OptionPopup {
    id: root

    property alias addressFormatEnabled: addressFormatToggle.checked

    implicitWidth: 300
    implicitHeight: 58

    clip: true
    modal: true
    dim: false

    ColumnLayout {
        anchors.centerIn: parent
        anchors.margins: 10
        spacing: 0

        EllipsisMenuToggleItem {
            id: addressFormatToggle
            Layout.fillWidth: true
            text: qsTr("Choose address type")
        }
    }
}
