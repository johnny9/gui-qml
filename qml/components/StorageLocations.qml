// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Dialogs

import org.bitcoincore.qt 1.0

import "../controls"

ColumnLayout {
    id: root
    objectName: "storageLocations"
    property bool hasError: optionsModel.dataDirError.length > 0

    function selectDefaultDataDir() {
        optionsModel.dataDir = optionsModel.getDefaultDataDirString
    }

    function selectCustomDataDir(path) {
        return optionsModel.setCustomDataDirArgs(path)
    }

    ButtonGroup {
        id: group
    }
    spacing: 15
    OptionButton {
        id: defaultDirOption
        objectName: "storageDefaultOption"
        Layout.fillWidth: true
        ButtonGroup.group: group
        text: qsTr("Default")
        description: qsTr("Your application directory.")
        customDir: optionsModel.getDefaultDataDirString
        checked: optionsModel.dataDir === optionsModel.getDefaultDataDirString
        onClicked: root.selectDefaultDataDir()
    }
    OptionButton {
        id: customDirOption
        objectName: "storageCustomOption"
        Layout.fillWidth: true
        ButtonGroup.group: group
        text: qsTr("Custom")
        description: qsTr("Choose the directory and storage device.")
        customDir: checked ? optionsModel.getCustomDataDirString() : ""
        checked: optionsModel.dataDir !== optionsModel.getDefaultDataDirString
        onClicked: folderDialog.open()
    }
    CoreText {
        objectName: "storageLocationErrorText"
        Layout.fillWidth: true
        visible: root.hasError
        text: optionsModel.dataDirError
        color: Theme.color.red
        font.pixelSize: 13
        wrapMode: Text.WordWrap
    }
    CoreText {
        objectName: "storageLocationAvailableText"
        Layout.fillWidth: true
        visible: !root.hasError && optionsModel.dataDirAvailable.length > 0
        text: optionsModel.dataDirAvailable
        color: Theme.color.neutral7
        font.pixelSize: 13
        wrapMode: Text.WordWrap
    }
    FolderDialog {
        id: folderDialog
        objectName: "storageFolderDialog"
        currentFolder: optionsModel.getDefaultDataDirectory
        onAccepted: {
            root.selectCustomDataDir(folderDialog.selectedFolder.toString())
        }
        onRejected: {
            console.log("Custom datadir selection canceled")
        }
    }
}
