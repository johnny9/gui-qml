// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtTest 1.2
import "../../qml/components"

TestCase {
    name: "StorageLocations"
    when: windowShown
    width: 600
    height: 400

    Component {
        id: storageLocationsComponent
        StorageLocations {}
    }

    function init() {
        optionsModel.dataDir = optionsModel.getDefaultDataDirString
        optionsModel.dataDirError = ""
        optionsModel.setCustomDataDirString("/tmp/bitcoin-custom")
    }

    function test_default_option_checked_initially() {
        const item = createTemporaryObject(storageLocationsComponent, this)
        verify(item !== null)

        const defaultOption = findChild(item, "storageDefaultOption")
        const customOption = findChild(item, "storageCustomOption")
        verify(defaultOption !== null)
        verify(customOption !== null)
        compare(defaultOption.checked, true)
        compare(customOption.checked, false)
    }

    function test_select_custom_dir_updates_option_state() {
        const item = createTemporaryObject(storageLocationsComponent, this)
        verify(item !== null)

        verify(item.selectCustomDataDir("file:///tmp/bitcoin-selected"))
        const defaultOption = findChild(item, "storageDefaultOption")
        const customOption = findChild(item, "storageCustomOption")
        compare(optionsModel.dataDir, "/tmp/bitcoin-selected")
        compare(defaultOption.checked, false)
        compare(customOption.checked, true)
        compare(customOption.customDir, "/tmp/bitcoin-selected")
    }

    function test_empty_custom_dir_surfaces_error() {
        const item = createTemporaryObject(storageLocationsComponent, this)
        verify(item !== null)

        verify(!item.selectCustomDataDir(""))
        const errorText = findChild(item, "storageLocationErrorText")
        verify(errorText !== null)
        compare(item.hasError, true)
        compare(errorText.text, "Select a data directory.")
    }

    function test_cancel_keeps_existing_selection() {
        const item = createTemporaryObject(storageLocationsComponent, this)
        verify(item !== null)

        const dialog = findChild(item, "storageFolderDialog")
        const defaultOption = findChild(item, "storageDefaultOption")
        const customOption = findChild(item, "storageCustomOption")
        verify(dialog !== null)

        dialog.rejected()
        compare(defaultOption.checked, true)
        compare(customOption.checked, false)

        verify(item.selectCustomDataDir("file:///tmp/bitcoin-selected"))
        dialog.rejected()
        compare(defaultOption.checked, false)
        compare(customOption.checked, true)
        compare(optionsModel.dataDir, "/tmp/bitcoin-selected")
    }

    function test_default_selection_clears_custom_state() {
        const item = createTemporaryObject(storageLocationsComponent, this)
        verify(item !== null)

        verify(item.selectCustomDataDir("file:///tmp/bitcoin-selected"))
        item.selectDefaultDataDir()

        const defaultOption = findChild(item, "storageDefaultOption")
        const customOption = findChild(item, "storageCustomOption")
        compare(optionsModel.dataDir, optionsModel.getDefaultDataDirString)
        compare(defaultOption.checked, true)
        compare(customOption.checked, false)
    }
}
