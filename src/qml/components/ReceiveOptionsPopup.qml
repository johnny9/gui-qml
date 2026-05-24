// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import "../controls"

OptionPopup {
    id: root
    objectName: "receiveOptionsPopup"

    property alias showName: nameToggle.checked
    property alias showMessage: messageToggle.checked
    property alias showNoteSelf: noteSelfToggle.checked
    property alias showAddressType: addressTypeToggle.checked
    property bool showRequestActions: false

    signal useAsTemplate()
    signal deleteFromHistory()
    signal viewAddressHistory()

    implicitWidth: 300
    implicitHeight: columnLayout.implicitHeight + 20

    clip: true
    modal: true
    dim: false

    ColumnLayout {
        id: columnLayout
        anchors.centerIn: parent
        anchors.margins: 10
        spacing: 5

        EllipsisMenuToggleItem {
            id: nameToggle
            objectName: "receiveOptionsNameToggle"
            Layout.fillWidth: true
            text: qsTr("Name")
            checked: true
        }

        EllipsisMenuToggleItem {
            id: messageToggle
            objectName: "receiveOptionsMessageToggle"
            Layout.fillWidth: true
            text: qsTr("Message")
            checked: true
        }

        EllipsisMenuToggleItem {
            id: noteSelfToggle
            objectName: "receiveOptionsNoteSelfToggle"
            Layout.fillWidth: true
            text: qsTr("Note to self")
            checked: true
        }

        EllipsisMenuToggleItem {
            id: addressTypeToggle
            objectName: "receiveOptionsAddressTypeToggle"
            Layout.fillWidth: true
            text: qsTr("Address type")
        }

        EllipsisMenuButtonItem {
            objectName: "receiveOptionsViewAddressHistoryButton"
            Layout.fillWidth: true
            text: qsTr("View address history")
            onClicked: {
                root.close()
                root.viewAddressHistory()
            }
        }

        EllipsisMenuButtonItem {
            objectName: "receiveOptionsUseAsTemplateButton"
            Layout.fillWidth: true
            visible: root.showRequestActions
            text: qsTr("Use as template")
            onClicked: {
                root.close()
                root.useAsTemplate()
            }
        }

        EllipsisMenuButtonItem {
            objectName: "receiveOptionsDeleteFromHistoryButton"
            Layout.fillWidth: true
            visible: root.showRequestActions
            text: qsTr("Delete from history")
            onClicked: {
                root.close()
                root.deleteFromHistory()
            }
        }
    }
}
