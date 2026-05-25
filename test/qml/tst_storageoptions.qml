// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.2
import "../../qml/components"

TestCase {
    name: "StorageOptions"
    when: windowShown
    width: 520
    height: 360

    Window {
        id: testWindow
        width: 520
        height: 360
        visible: true
    }

    Component {
        id: storageOptionsComponent

        StorageOptions {
            width: 420
        }
    }

    function init() {
        optionsModel.prune = false
        optionsModel.pruneSizeGB = 9
    }

    function test_does_not_change_prune_when_loaded() {
        const options = createTemporaryObject(storageOptionsComponent, testWindow.contentItem)
        verify(options !== null)
        wait(0)

        compare(optionsModel.prune, false)
        compare(optionsModel.pruneSizeGB, 9)
    }
}
