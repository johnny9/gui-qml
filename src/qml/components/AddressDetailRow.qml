// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0

import "../controls"

Item {
    id: root

    property string label: qsTr("Address")
    property string address: ""

    signal copied()

    Layout.fillWidth: true
    implicitHeight: addressCol.implicitHeight + 20

    function formatAddressRichText(addr) {
        if (!addr) return ""
        var primaryColor = Theme.color.neutral9
        var secondaryColor = Theme.color.neutral7
        var html = ""
        for (var i = 0; i < addr.length; i += 4) {
            var chunk = addr.substring(i, Math.min(i + 4, addr.length))
            var color = (Math.floor(i / 4) % 2 === 0) ? primaryColor : secondaryColor
            if (i > 0) html += " "
            html += "<nobr><font color=\"" + color + "\">" + chunk + "</font></nobr>"
        }
        return html
    }

    ColumnLayout {
        id: addressCol
        anchors.left: parent.left
        anchors.right: copyIcon.left
        anchors.rightMargin: 10
        anchors.top: parent.top
        anchors.topMargin: 10
        spacing: 4

        CoreText {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignLeft
            color: Theme.color.neutral7
            text: root.label
            font: Theme.text.description.font
            lineHeight: Theme.text.description.lineHeight
            lineHeightMode: Text.FixedHeight
        }

        CoreText {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignLeft
            color: Theme.color.neutral9
            font: Theme.text.monoBody.font
            lineHeight: Theme.text.monoBody.lineHeight
            lineHeightMode: Text.FixedHeight
            textFormat: Text.RichText
            text: root.formatAddressRichText(root.address)
            wrapMode: Text.WordWrap
        }
    }

    Icon {
        id: copyIcon
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        source: "qrc:/icons/copy"
        color: Theme.color.neutral9
        size: 24
    }

    MouseArea {
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        width: 44
        height: 44
        cursorShape: Qt.PointingHandCursor
        onClicked: {
            Clipboard.setText(root.address)
            root.copied()
        }
    }
}
