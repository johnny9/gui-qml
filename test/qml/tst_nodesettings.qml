// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.2
import "../../qml/pages/node"

TestCase {
    name: "NodeSettings"
    when: windowShown
    width: 520
    height: 720

    Window {
        id: testWindow
        width: 520
        height: 720
        visible: true
    }

    Component {
        id: nodeSettingsComponent

        NodeSettings {
            width: 460
            height: 680
        }
    }

    function init() {
        nodeModel.mempoolInformationAvailable = true
    }

    function createNodeSettingsPage() {
        const page = createTemporaryObject(nodeSettingsComponent, testWindow.contentItem)
        verify(page !== null)
        wait(0)
        return page
    }

    function test_mempool_information_row_visible_when_available() {
        const page = createNodeSettingsPage()
        const row = findChild(page, "settingsMempoolInformation")
        verify(row !== null)
        compare(row.visible, true)
    }

    function test_mempool_information_row_hidden_when_unavailable() {
        nodeModel.mempoolInformationAvailable = false

        const page = createNodeSettingsPage()
        const row = findChild(page, "settingsMempoolInformation")
        verify(row !== null)
        compare(row.visible, false)
    }
}
