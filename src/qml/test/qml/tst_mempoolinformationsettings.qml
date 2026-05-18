// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.2
import "../../qml/pages/node"

TestCase {
    name: "MempoolInformationSettings"
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
        id: mempoolInformationSettingsComponent

        MempoolInformationSettings {
            width: 460
            height: 680
        }
    }

    function init() {
        nodeModel.resetMempoolInfoPollingTestState()
    }

    function createMempoolInformationSettingsPage() {
        const page = createTemporaryObject(mempoolInformationSettingsComponent, testWindow.contentItem)
        verify(page !== null)
        page.visible = false
        wait(0)
        page.visible = true
        wait(0)
        compare(page.visible, true)
        return page
    }

    function test_polling_activity_tracks_page_visibility() {
        const page = createMempoolInformationSettingsPage()
        compare(nodeModel.mempoolInfoPollingActive, true)

        page.visible = false
        compare(nodeModel.mempoolInfoPollingActive, false)

        page.visible = true
        compare(nodeModel.mempoolInfoPollingActive, true)
    }

    function test_polling_activity_stops_on_page_destruction() {
        const page = createMempoolInformationSettingsPage()
        compare(nodeModel.mempoolInfoPollingActive, true)

        page.destroy()
        wait(0)
        compare(nodeModel.mempoolInfoPollingActive, false)
    }
}
