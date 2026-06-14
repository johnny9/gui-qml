// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtTest 1.2
import "../../qml/pages/wallet"

TestCase {
    name: "DesktopWallets"
    when: windowShown
    width: 900
    height: 600

    Component {
        id: desktopWalletsComponent

        DesktopWallets {
            width: 900
            height: 600
        }
    }

    function init() {
        walletController.reset()
        walletController.initialized = true
        walletController.isWalletLoaded = true
        walletController.noWalletsFound = false
        walletListModel.reset()
    }

    function createDesktopWallets() {
        const page = createTemporaryObject(desktopWalletsComponent, this)
        verify(page !== null)
        const popup = findChild(page, "walletSelectPopup")
        verify(popup !== null)
        const badge = findChild(page, "walletBadge")
        verify(badge !== null)
        return page
    }

    function test_wallet_badge_refreshes_wallet_list_once_before_opening() {
        const page = createDesktopWallets()
        const popup = findChild(page, "walletSelectPopup")
        const badge = findChild(page, "walletBadge")

        compare(walletListModel.listWalletDirCalls, 0)

        badge.clicked()
        compare(walletListModel.listWalletDirCalls, 1)
        tryCompare(popup, "opened", true)

        badge.clicked()
        compare(walletListModel.listWalletDirCalls, 1)
        tryCompare(popup, "opened", false)

        badge.clicked()
        compare(walletListModel.listWalletDirCalls, 2)
        tryCompare(popup, "opened", true)
    }

    function test_explicit_open_wallet_selection_refreshes_wallet_list() {
        const page = createDesktopWallets()
        const popup = findChild(page, "walletSelectPopup")

        page.openWalletSelection()
        compare(walletListModel.listWalletDirCalls, 1)
        tryCompare(popup, "opened", true)
    }

    function test_receive_options_view_address_history_opens_settings_address_stack() {
        const page = createDesktopWallets()
        const receiveTab = findChild(page, "receiveTabButton")
        verify(receiveTab !== null)
        receiveTab.clicked()

        const optionsButton = findChild(page, "receiveOptionsButton")
        verify(optionsButton !== null)
        optionsButton.clicked()

        const popup = findChild(page, "receiveOptionsPopup")
        verify(popup !== null)
        tryCompare(popup, "opened", true)

        const viewHistoryButton = findChild(page, "receiveOptionsViewAddressHistoryButton")
        verify(viewHistoryButton !== null)
        viewHistoryButton.clicked()

        const settingsTab = findChild(page, "desktopWalletSettingsTabButton")
        verify(settingsTab !== null)
        compare(settingsTab.checked, true)

        const settingsStack = findChild(page, "nodeSettingsStack")
        verify(settingsStack !== null)
        tryCompare(settingsStack, "depth", 3)
        compare(settingsStack.currentItem.objectName, "addressListPage")
        verify(findChild(page, "walletSettingsPage") !== null)
    }
}
