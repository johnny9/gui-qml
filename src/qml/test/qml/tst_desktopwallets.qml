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
}
