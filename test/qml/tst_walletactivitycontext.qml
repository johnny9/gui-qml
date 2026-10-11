// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtTest 1.2
import org.bitcoincore.qt 1.0
import "../../qml/components"
import "../../qml/pages/wallet"

TestCase {
    id: testCase
    name: "WalletActivityContext"
    when: windowShown
    visible: true
    width: 1180
    height: 950

    Component { id: settingsComponent; SettingsView { width: 1180; height: 950 } }
    Component { id: activityComponent; ActivityListPage { width: 1180; height: 950 } }
    Component {
        id: coinListComponent
        CoinsList { width: 840; height: 650; wallet: walletController.selectedWallet }
    }
    Component {
        id: coinsComponent
        ListModel {
            property real totalSelectedSatoshi: 0
            property real totalSatoshi: 1000
            property real lockedSatoshi: 0
            property int coinCount: 0
            property real availableMinAmount: 0
            property real availableMaxAmount: 1000
            property real minAmount: -1
            property real maxAmount: -1
            property string filter: "all"
            property string searchText: ""
            property string sortBy: "date"
            property bool sortDescending: true
            property string groupBy: "date"
            property int lockCalls: 0
            property int toggleCalls: 0
            function setCoinsLocked(ids, locked) { ++lockCalls; return true }
            function toggleCoinSelectionById(coinId) { ++toggleCalls; return true }
        }
    }
    Component {
        id: walletComponent
        QtObject {
            property string name: "A"
            property string displayName: name
            property bool canManagePassphrase: true
            property bool isEncrypted: false
            property string keyScheme: "Single-key"
            property string privateKeysStatus: "Enabled"
            property string externalSignerStatus: "None"
            property string settingsError: ""
            property string lastBackupPath: ""
            property int backupCalls: 0
            property var transactionActivityModel: testTransactionActivityModel
            property var coinsListModel: null
            property bool feeEstimatePending: false
            property real estimatedFeeSatoshi: 0
            property real sendTotalSatoshi: 100
            property bool receiveRequestReconciliationPending: false
            property bool receiveOperationPending: false
            property string receiveOperationError: ""
            property string lastRemovedRequestId: ""
            property QtObject currentPaymentRequest: QtObject {
                property string id: ""
                function clear() { id = "" }
            }
            property QtObject detailPaymentRequest: QtObject {
                property string id: ""
                function clear() { id = "" }
            }
            signal receiveOperationFinished(string operation, bool success)
            function clearSettingsError() { settingsError = "" }
            function backupWallet(path) {
                lastBackupPath = path
                ++backupCalls
                return true
            }
            function removeReceiveRequest(requestId) {
                if (receiveOperationPending) return false
                receiveOperationPending = true
                lastRemovedRequestId = requestId
                return true
            }
            function completeRemove(success) {
                receiveOperationPending = false
                receiveOperationError = success ? "" : "Deletion failed"
                receiveOperationFinished("remove", success)
            }
        }
    }

    property var walletA
    property var walletB

    function init() {
        walletController.reset()
        walletController.initialized = true
        walletController.setWalletLoaded(true)
        walletA = createTemporaryObject(walletComponent, this, {
            name: "A", coinsListModel: createTemporaryObject(coinsComponent, this)
        })
        walletB = createTemporaryObject(walletComponent, this, {
            name: "B", coinsListModel: createTemporaryObject(coinsComponent, this)
        })
        walletController.setSelectedWalletObject(walletA)
        testTransactionActivityModel.loading = false
        testTransactionActivityModel.loadError = ""
        testTransactionActivityModel.setRows([])
    }

    function cleanup() {
        walletController.setSelectedWalletObject(testWalletModel)
    }

    function switchedWalletData() {
        return [
            {tag: "A_to_B", switchBack: false},
            {tag: "A_to_B_to_A", switchBack: true}
        ]
    }

    function switchWallet(switchBack) {
        walletController.setSelectedWalletObject(walletB)
        if (switchBack) walletController.setSelectedWalletObject(walletA)
    }

    function createSettings() {
        const view = createTemporaryObject(settingsComponent, this)
        verify(view !== null)
        view.openWalletSettings()
        return view
    }

    function createActivity() {
        const page = createTemporaryObject(activityComponent, this)
        verify(page !== null)
        return page
    }

    function openRequestMenu(page, requestId) {
        const menu = findChild(page, "activityRowContextMenu")
        menu.requestId = requestId
        menu.isRequest = true
        menu.open()
        tryCompare(menu, "opened", true)
        return menu
    }

    function startDelete(menu) {
        const button = findChild(menu.contentItem, "activityDeletePaymentRequest")
        verify(button.enabled)
        button.triggered()
    }

    function test_backup_accepts_only_its_opening() {
        const view = createSettings()
        const page = view.pageContainer.currentItem
        const dialog = findChild(page, "walletBackupFileDialog")
        page.startBackup()
        tryCompare(dialog, "visible", true)
        dialog.selectedFile = "file:///tmp/wallet-A.bak"
        dialog.accepted()
        compare(walletA.backupCalls, 1)
        compare(walletA.lastBackupPath, "file:///tmp/wallet-A.bak")
        dialog.accepted()
        compare(walletA.backupCalls, 1)
        compare(walletB.backupCalls, 0)
        dialog.close()
    }

    function test_backup_rejects_wallet_switch_data() { return switchedWalletData() }

    function test_backup_rejects_wallet_switch(data) {
        const view = createSettings()
        const page = view.pageContainer.currentItem
        const dialog = findChild(page, "walletBackupFileDialog")
        page.startBackup()
        tryCompare(dialog, "visible", true)
        switchWallet(data.switchBack)
        compare(view.pageContainer.currentItem, page)
        tryCompare(dialog, "visible", false)
        // A native dialog can deliver its result after the wallet changed.
        dialog.selectedFile = "file:///tmp/wallet-A.bak"
        dialog.accepted()
        compare(walletA.backupCalls, 0)
        compare(walletB.backupCalls, 0)
    }

    function test_backup_rejection_invalidates_acceptance() {
        const view = createSettings()
        const page = view.pageContainer.currentItem
        const dialog = findChild(page, "walletBackupFileDialog")
        page.startBackup()
        dialog.rejected()
        dialog.selectedFile = "file:///tmp/wallet-A.bak"
        dialog.accepted()
        compare(walletA.backupCalls, 0)
        dialog.close()
    }

    function test_export_dialog_rejects_wallet_switch_data() { return switchedWalletData() }

    function test_export_dialog_rejects_wallet_switch(data) {
        const page = createActivity()
        const dialog = findChild(page, "activityExportFileDialog")
        const proxy = findChild(page, "activityFilterProxyModel")
        proxy.autoCompleteExport = false
        page.openExportDialog()
        tryCompare(dialog, "visible", true)
        switchWallet(data.switchBack)
        tryCompare(dialog, "visible", false)
        dialog.selectedFile = "file:///tmp/wallet-A-activity.csv"
        dialog.accepted()
        verify(!proxy.exportPending)
    }

    function test_export_dialog_accepts_current_wallet() {
        const page = createActivity()
        const dialog = findChild(page, "activityExportFileDialog")
        const proxy = findChild(page, "activityFilterProxyModel")
        proxy.autoCompleteExport = false
        page.openExportDialog()
        tryCompare(dialog, "visible", true)
        dialog.selectedFile = "file:///tmp/wallet-A-activity.csv"
        dialog.accepted()
        verify(proxy.exportPending)
        dialog.close()
        proxy.completeExport()
        const result = findChild(page, "activityExportResultPopup")
        tryCompare(result, "opened", true)
        walletController.setSelectedWalletObject(walletB)
        tryCompare(result, "visible", false)
    }

    function test_export_completion_rejects_wallet_switch_data() {
        return [
            {tag: "success_A_to_B", success: true, switchBack: false},
            {tag: "failure_A_to_B", success: false, switchBack: false},
            {tag: "success_A_to_B_to_A", success: true, switchBack: true},
            {tag: "failure_A_to_B_to_A", success: false, switchBack: true}
        ]
    }

    function test_export_completion_rejects_wallet_switch(data) {
        const page = createActivity()
        const proxy = findChild(page, "activityFilterProxyModel")
        const result = findChild(page, "activityExportResultPopup")
        proxy.autoCompleteExport = false
        proxy.exportSucceeds = data.success
        page.exportActivity("/tmp/wallet-A-activity.csv")
        verify(proxy.exportPending)
        switchWallet(data.switchBack)
        proxy.completeExport()
        verify(!result.visible)
        // The next export still belongs to the current wallet.
        page.exportActivity("/tmp/current-wallet-activity.csv")
        verify(proxy.exportPending)
        proxy.completeExport()
        tryCompare(result, "opened", true)
        result.close()
    }

    function test_delete_completion_preserves_reopened_menu_data() {
        return [
            {tag: "success_different_request", success: true, sameRequest: false},
            {tag: "failure_different_request", success: false, sameRequest: false},
            {tag: "success_same_request", success: true, sameRequest: true},
            {tag: "failure_same_request", success: false, sameRequest: true}
        ]
    }

    function test_delete_menu_rejects_wallet_switch_data() { return switchedWalletData() }

    function test_delete_menu_rejects_wallet_switch(data) {
        const page = createActivity()
        const menu = openRequestMenu(page, "1")
        const button = findChild(menu.contentItem, "activityDeletePaymentRequest")
        switchWallet(data.switchBack)
        button.triggered()
        compare(walletA.lastRemovedRequestId, "")
        compare(walletB.lastRemovedRequestId, "")
    }

    function test_delete_completion_preserves_reopened_menu(data) {
        const page = createActivity()
        walletA.currentPaymentRequest.id = "1"
        const menu = openRequestMenu(page, "1")
        startDelete(menu)
        menu.close()
        tryCompare(menu, "visible", false)
        openRequestMenu(page, data.sameRequest ? "1" : "2")
        walletA.completeRemove(data.success)
        verify(menu.opened)
        compare(menu.deleteError, "")
        compare(walletA.currentPaymentRequest.id, data.success ? "" : "1")
        menu.close()
    }

    function test_delete_completion_keeps_original_wallet_data() { return switchedWalletData() }

    function test_delete_completion_keeps_original_wallet(data) {
        const page = createActivity()
        walletA.currentPaymentRequest.id = "1"
        walletB.currentPaymentRequest.id = "1"
        const menu = openRequestMenu(page, "1")
        startDelete(menu)
        switchWallet(data.switchBack)
        tryCompare(menu, "visible", false)
        openRequestMenu(page, "1")
        verify(!findChild(menu.contentItem, "activityDeletePaymentRequest").enabled)
        walletA.completeRemove(true)
        compare(walletA.currentPaymentRequest.id, "")
        compare(walletB.currentPaymentRequest.id, "1")
        verify(menu.opened)
        compare(menu.deleteError, "")
        verify(findChild(menu.contentItem, "activityDeletePaymentRequest").enabled)
        menu.close()
    }

    function test_delete_current_menu_result_data() {
        return [{tag: "success", success: true}, {tag: "failure", success: false}]
    }

    function test_delete_current_menu_result(data) {
        const page = createActivity()
        const menu = openRequestMenu(page, "1")
        startDelete(menu)
        walletA.completeRemove(data.success)
        if (data.success) tryCompare(menu, "visible", false)
        else {
            verify(menu.opened)
            compare(menu.deleteError, "Deletion failed")
            menu.close()
        }
    }

    function test_coin_overlays_reject_previous_context_data() {
        const cases = [
            {name: "coinBrowserSortMenu", action: "coinBrowserSortPicker", value: "amount"},
            {name: "coinBrowserGroupMenu", action: "coinBrowserGroupPicker", value: "address"},
            {name: "coinBrowserFilterMenu", action: "coinBrowserFilterPicker", value: "spendable"},
            {name: "coinBrowserAmountFilterPopup", action: "amount"},
            {name: "coinBrowserClearFiltersMenu", action: "clear"},
            {name: "walletCoinContextMenu", action: "lock"},
            {name: "lockedCoinSelectionAlert", action: "select"}
        ]
        const result = []
        for (const entry of cases) {
            for (const change of ["switch", "switchBack", "null", "hidden"]) {
                result.push({tag: entry.name + "_" + change, menu: entry.name,
                             action: entry.action, value: entry.value, change: change})
            }
        }
        return result
    }

    function test_coin_overlays_reject_previous_context(data) {
        const list = createTemporaryObject(coinListComponent, this)
        verify(list !== null)
        walletA.coinsListModel.filter = "locked"
        walletB.coinsListModel.filter = "locked"
        list.pendingLockedCoinId = "coin-A"
        list.contextCoinId = "coin-A"
        const menu = findChild(list, data.menu)
        verify(menu !== null)
        menu.open()
        tryCompare(menu, "opened", true)
        // AlertPopup removes button object names when its content becomes hidden.
        const confirmButton = data.action === "select"
            ? findChild(menu.contentItem, "lockedCoinSelectionConfirmButton") : null
        if (data.action === "select") verify(confirmButton !== null)
        if (data.change === "hidden") list.visible = false
        else if (data.change === "null") walletController.setSelectedWalletObject(null)
        else switchWallet(data.change === "switchBack")
        tryCompare(menu, "visible", false)
        if (data.action === "amount") {
            const slider = findChild(menu.contentItem, "coinBrowserAmountRangeSlider")
            slider.first.moved()
            slider.second.moved()
        } else if (data.action === "clear") {
            findChild(menu.contentItem, "coinBrowserClearFiltersAction").triggered()
        } else if (data.action === "lock") {
            findChild(menu.contentItem, "walletCoinLockToggle").triggered()
        } else if (data.action === "select") {
            confirmButton.clicked()
        } else {
            findChild(menu.contentItem, data.action).activated(data.value)
        }
        for (const wallet of [walletA, walletB]) {
            compare(wallet.coinsListModel.lockCalls, 0)
            compare(wallet.coinsListModel.toggleCalls, 0)
            compare(wallet.coinsListModel.filter, "locked")
            compare(wallet.coinsListModel.minAmount, -1)
            compare(wallet.coinsListModel.maxAmount, -1)
            compare(wallet.coinsListModel.sortBy, "date")
            compare(wallet.coinsListModel.groupBy, "date")
        }
    }

    function test_coin_filter_remains_per_wallet() {
        const list = createTemporaryObject(coinListComponent, this)
        const menu = findChild(list, "coinBrowserFilterMenu")
        menu.open()
        tryCompare(menu, "opened", true)
        findChild(menu.contentItem, "coinBrowserFilterPicker").activated("locked")
        compare(walletA.coinsListModel.filter, "locked")
        walletController.setSelectedWalletObject(walletB)
        compare(list.coins.filter, "all")
        walletController.setSelectedWalletObject(walletA)
        compare(list.coins.filter, "locked")
    }
}
