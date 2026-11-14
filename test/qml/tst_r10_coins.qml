// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
import QtQuick 2.15
import QtTest 1.2
import org.bitcoincore.qt 1.0
import "../../qml/components"
import "../../qml/pages/wallet"
TestCase {
    id: testCase
    name: "R10Coins"
    when: windowShown
    width: 900
    height: 760
    Component { id: browserComponent; CoinsList { width: 820; height: 700; wallet: testWalletModel } }
    Component { id: walletPageComponent; WalletCoins { width: 820; height: 700; wallet: testWalletModel } }
    function init() {
        testSetup.failOnWarnings()
        testWalletModel.available = true
        testWalletModel.feeEstimatePending = false
        testCoinsListModel.reset()
        testCoinsListModel.controlCoinActions(true, true)
        testCoinsListModel.minAmount = -1
        testCoinsListModel.maxAmount = -1
        testCoinsListModel.filter = "all"
        testCoinsListModel.groupBy = "month"
    }
    function browser() {
        const item = createTemporaryObject(browserComponent, testCase.Window.window.contentItem)
        verify(item !== null)
        return item
    }
    function test_amount_popup_uses_public_range_data() {
        return [{tag: "available", wallet: true, saved: false}, {tag: "saved", wallet: true, saved: true}, {tag: "closed-wallet", wallet: false, saved: false}]
    }
    function test_amount_popup_uses_public_range(data) {
        const item = browser()
        if (!data.wallet) item.wallet = null
        if (data.saved) { testCoinsListModel.minAmount = 150000; testCoinsListModel.maxAmount = 250000 }
        verify(waitForRendering(item))
        const button = findChild(item, "coinBrowserAmountFilterButton")
        verify(button !== null)
        mouseClick(button)
        const popup = findChild(testCase.Window.window.contentItem, "coinBrowserAmountFilterPopup")
        verify(popup !== null)
        tryCompare(popup, "opened", true)
        const slider = findChild(popup.contentItem, "coinBrowserAmountRangeSlider")
        verify(slider !== null)
        compare(slider.minValue, data.wallet ? 100000 : 0)
        compare(slider.maxValue, data.wallet ? 300000 : 0)
        compare(slider.lowerValue, !data.wallet ? 0 : data.saved ? 150000 : 100000)
        compare(slider.upperValue, !data.wallet ? 0 : data.saved ? 250000 : 300000)
        if (data.wallet && !data.saved) {
            verify(waitForRendering(popup.contentItem))
            const upper = slider.upperValue
            const handle = slider.first.handle
            mouseDrag(handle, handle.width / 2, handle.height / 2, 35, 0)
            verify(testCoinsListModel.minAmount > 100000)
            verify(testCoinsListModel.minAmount < upper)
            compare(testCoinsListModel.maxAmount, -1)
        }
        popup.close()
        tryCompare(popup, "opened", false)
    }
    function test_wallet_marks_refusal_has_no_lock_effect_data() { return [{tag: "admitted", admitted: true}, {tag: "refused", admitted: false}] }
    function test_wallet_marks_refusal_has_no_lock_effect(data) {
        const item = browser()
        item.selectionMode = false
        verify(waitForRendering(item))
        testCoinsListModel.controlCoinActions(data.admitted, true)
        mouseClick(findChild(item, "walletCoinsSelectionButton"))
        const row = findChild(item, "coinSelectionItem_0")
        verify(row !== null)
        const box = findChild(row, "coinSelectionCheckbox")
        verify(box !== null)
        tryCompare(box, "opacity", 1)
        tryCompare(box, "x", 0)
        mouseClick(box)
        compare(testCoinsListModel.markCalls, 1)
        compare(testCoinsListModel.markedCount, data.admitted ? 1 : 0)
        compare(testCoinsListModel.lockCalls, 0)
        compare(item.errorText, data.admitted ? "" : "This coin could not be marked. Try again.")
    }
    function test_wallet_lock_command_keeps_original_ids_data() { return [{tag: "applied", admitted: true}, {tag: "refused", admitted: false}] }
    function test_wallet_lock_command_keeps_original_ids(data) {
        const item = browser()
        item.selectionMode = false
        verify(waitForRendering(item))
        testCoinsListModel.controlCoinActions(true, data.admitted)
        mouseClick(findChild(item, "walletCoinsSelectionButton"))
        const row = findChild(item, "coinSelectionItem_0")
        verify(row !== null)
        const box = findChild(row, "coinSelectionCheckbox")
        verify(box !== null)
        tryCompare(box, "opacity", 1)
        tryCompare(box, "x", 0)
        mouseClick(box)
        compare(testCoinsListModel.markedCount, 1)
        const lock = findChild(item, "coinsLockButton")
        verify(lock !== null)
        mouseClick(lock)
        compare(testCoinsListModel.lockCalls, 1)
        compare(testCoinsListModel.lastLockIds.length, 1)
        compare(testCoinsListModel.lastLockIds[0], "coin-1")
        compare(row.locked, data.admitted)
        compare(testCoinsListModel.lockedSatoshi, data.admitted ? 400000 : 300000)
        compare(testCoinsListModel.totalSatoshi, 600000)
        compare(testCoinsListModel.markedCount, data.admitted ? 0 : 1)
        compare(item.errorText, data.admitted ? "" : "Some coins could not be updated. Try again.")
    }
    function test_clear_filters_keeps_other_choices() {
        const item = browser()
        testCoinsListModel.filter = "locked"
        testCoinsListModel.minAmount = 150000
        testCoinsListModel.maxAmount = 250000
        testCoinsListModel.groupBy = "address"
        item.clearFilters()
        compare(testCoinsListModel.filter, "all")
        compare(testCoinsListModel.minAmount, -1)
        compare(testCoinsListModel.maxAmount, -1)
        compare(testCoinsListModel.groupBy, "address")
        compare(testCoinsListModel.lockCalls, 0)
        item.wallet = null
        item.clearFilters()
        compare(testCoinsListModel.groupBy, "address")
    }
    function test_wallet_page_uses_the_public_coin_browser() {
        const view = createTemporaryObject(walletPageComponent, testCase.Window.window.contentItem)
        verify(view !== null)
        verify(waitForRendering(view))
        verify(findChild(view, "walletCoinsSelectionButton") !== null)
        verify(findChild(view, "coinSelectionItem_0") !== null)
        compare(testCoinsListModel.lockCalls, 0)
    }
}
