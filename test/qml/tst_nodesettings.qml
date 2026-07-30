// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.2
import org.bitcoincore.qt 1.0
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

    Component {
        id: subPageComponent
        Item {}
    }

    function init() {
        nodeModel.mempoolInformationAvailable = true
        AppMode.walletEnabled = true
        AppMode.isDesktop = true
        walletController.bundledHwiEnabled = false
        walletController.bundledHwiVerified = false
        testNetworkTrafficTower.active = false
    }

    function createNodeSettingsPage() {
        const page = createTemporaryObject(nodeSettingsComponent, testWindow.contentItem)
        verify(page !== null)
        wait(0)
        return page
    }

    function test_mempool_information_row_visible_when_available() {
        const page = createNodeSettingsPage()
        const row = findChild(page, "settings_mempool")
        verify(row !== null)
        compare(row.visible, true)
    }

    function test_mempool_information_row_hidden_when_unavailable() {
        nodeModel.mempoolInformationAvailable = false

        const page = createNodeSettingsPage()
        const row = findChild(page, "settings_mempool")
        verify(row !== null)
        compare(row.visible, false)
    }

    function test_sidebar_section_switching() {
        const page = createNodeSettingsPage()

        // currentSection is the sidebar row index in the grouped order:
        // Wallet(0), External Signer(1), Display(2), Window Behavior(3),
        // Storage(4), Connection(5), Network Traffic(6), Mempool(7),
        // Debug Log(8), About(9). With the wallet enabled the page lands on
        // the first visible row, Wallet.
        compare(page.currentSection, 0)

        const displayItem = findChild(page, "settings_display")
        verify(displayItem !== null)
        mouseClick(displayItem, displayItem.width / 2, displayItem.height / 2)
        compare(page.currentSection, 2)

        const connectionItem = findChild(page, "settings_connection")
        verify(connectionItem !== null)
        mouseClick(connectionItem, connectionItem.width / 2, connectionItem.height / 2)
        compare(page.currentSection, 5)
    }

    function test_network_traffic_only_publishes_while_selected() {
        const page = createNodeSettingsPage()

        compare(testNetworkTrafficTower.active, false)
        verify(findChild(page, "networkTrafficPage") === null)

        const networkTrafficItem = findChild(page, "settings_networktraffic")
        verify(networkTrafficItem !== null)
        mouseClick(networkTrafficItem, networkTrafficItem.width / 2, networkTrafficItem.height / 2)
        tryCompare(page, "currentSection", 6)
        tryCompare(testNetworkTrafficTower, "active", true)
        verify(findChild(page, "networkTrafficPage") !== null)

        // Leaving Settings unloads both graphs and suppresses worker snapshots,
        // while the C++ sampler continues retaining raw history off-thread.
        page.visible = false
        tryCompare(testNetworkTrafficTower, "active", false)
        tryVerify(function() { return findChild(page, "networkTrafficPage") === null })

        page.visible = true
        tryCompare(testNetworkTrafficTower, "active", true)
        verify(findChild(page, "networkTrafficPage") !== null)

        const displayItem = findChild(page, "settings_display")
        verify(displayItem !== null)
        mouseClick(displayItem, displayItem.width / 2, displayItem.height / 2)
        tryCompare(page, "currentSection", 2)
        tryCompare(testNetworkTrafficTower, "active", false)
        tryVerify(function() { return findChild(page, "networkTrafficPage") === null })
    }
    function test_wallet_section_hidden_when_disabled() {
        AppMode.walletEnabled = false

        const page = createNodeSettingsPage()
        const walletItem = findChild(page, "settings_wallet")
        verify(walletItem !== null)
        compare(walletItem.visible, false)

        const signerItem = findChild(page, "settings_externalsigner")
        verify(signerItem !== null)
        compare(signerItem.visible, false)
    }

    function test_bundled_hwi_status_locks_signer_path() {
        walletController.bundledHwiEnabled = true
        walletController.bundledHwiVerified = true

        const page = createNodeSettingsPage()
        const signerItem = findChild(page, "settings_externalsigner")
        verify(signerItem !== null)
        mouseClick(signerItem, signerItem.width / 2, signerItem.height / 2)

        const pathInput = findChild(page, "externalSignerPathInput")
        const statusText = findChild(page, "externalSignerStatusText")
        const checkButton = findChild(page, "externalSignerCheckDeviceButton")
        verify(pathInput !== null)
        verify(statusText !== null)
        verify(checkButton !== null)
        compare(pathInput.visible, false)
        compare(statusText.text, "Bundled HWI — signature verified. Connect a supported hardware wallet to continue.")
        compare(checkButton.enabled, true)
    }

    function test_window_behavior_hidden_on_non_desktop() {
        AppMode.isDesktop = false

        const page = createNodeSettingsPage()
        const windowItem = findChild(page, "settings_windowbehavior")
        verify(windowItem !== null)
        compare(windowItem.visible, false)
    }

    function test_wallet_settings_back_button_stays_hidden_when_subpage_open() {
        const page = createNodeSettingsPage()

        const walletSettingsPage = findChild(page, "walletSettingsPage")
        verify(walletSettingsPage !== null)
        const walletStack = findChild(page, "walletSettingsStack")
        verify(walletStack !== null)

        // The wallet settings page is reached from the sidebar and has no back
        // button of its own. Pushing a sub-page must not turn it on: binding it
        // to depth > 1 flashed the back button on this page during the push
        // transition.
        compare(walletSettingsPage.showBackButton, false)
        walletStack.push(subPageComponent)
        wait(0)
        verify(walletStack.depth > 1)
        compare(walletSettingsPage.showBackButton, false)
    }
}
