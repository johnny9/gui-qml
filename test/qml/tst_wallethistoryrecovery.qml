// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtTest 1.2
import "../../qml/components"

TestCase {
    name: "WalletHistoryRecovery"
    when: windowShown
    visible: true
    width: 600
    height: 300

    QtObject {
        id: recoveryModel
        property bool enabled: true
        property bool active: true
        property bool incomplete: true
        property bool visible: true
        property real progress: 0.25
        property bool indeterminate: false
        property bool needsUnlock: false
        property string statusText: "Scanning wallet history…"
        property string errorText: ""
        property string state: "scanning"
        property int cancelCalls: 0
        property int retryCalls: 0
        property int unlockCalls: 0
        property string lastPassphrase: ""
        function cancel() { cancelCalls += 1 }
        function retry() { retryCalls += 1 }
        function unlockAndRetry(passphrase) { unlockCalls += 1; lastPassphrase = passphrase }
    }

    Component {
        id: bannerComponent
        WalletHistoryRecovery {
            width: 560
            recovery: recoveryModel
        }
    }

    function init() {
        recoveryModel.enabled = true
        recoveryModel.active = true
        recoveryModel.incomplete = true
        recoveryModel.visible = true
        recoveryModel.progress = 0.25
        recoveryModel.indeterminate = false
        recoveryModel.needsUnlock = false
        recoveryModel.statusText = "Scanning wallet history…"
        recoveryModel.errorText = ""
        recoveryModel.state = "scanning"
        recoveryModel.cancelCalls = 0
        recoveryModel.retryCalls = 0
        recoveryModel.unlockCalls = 0
        recoveryModel.lastPassphrase = ""
    }

    function createBanner() {
        const banner = createTemporaryObject(bannerComponent, this)
        verify(banner !== null)
        verify(waitForItemPolished(banner))
        return banner
    }

    function test_scan_progress_updates_and_cancel_is_available() {
        const banner = createBanner()
        const progress = findChild(banner, "recoveryProgressBar")
        const status = findChild(banner, "recoveryStatusText")
        const cancel = findChild(banner, "recoveryCancelButton")
        verify(progress.visible)
        verify(!progress.indeterminate)
        compare(progress.value, 0.25)
        compare(status.text, recoveryModel.statusText)
        compare(banner.recoveryState, "scanning")
        verify(banner.recoveryIncomplete)
        recoveryModel.progress = 0.75
        recoveryModel.statusText = "Restoring wallet transactions…"
        compare(progress.value, 0.75)
        compare(status.text, recoveryModel.statusText)
        mouseClick(cancel, cancel.width / 2, cancel.height / 2)
        compare(recoveryModel.cancelCalls, 1)
    }

    function test_waiting_uses_indeterminate_progress_and_can_cancel() {
        recoveryModel.state = "waiting-wallet"
        recoveryModel.active = false
        recoveryModel.indeterminate = true
        recoveryModel.statusText = "Waiting for the wallet…"
        const banner = createBanner()
        const progress = findChild(banner, "recoveryProgressBar")
        verify(progress.visible)
        verify(progress.indeterminate)
        verify(findChild(banner, "recoveryCancelButton").visible)
        verify(!findChild(banner, "recoveryRetryButton").visible)
    }

    function test_error_and_cancelled_keep_history_incomplete_data() {
        return [
            { tag: "error", state: "error", error: "No peer can provide the required block." },
            { tag: "cancelled", state: "cancelled", error: "" }
        ]
    }

    function test_error_and_cancelled_keep_history_incomplete(data) {
        recoveryModel.state = data.state
        recoveryModel.active = false
        recoveryModel.errorText = data.error
        const banner = createBanner()
        const retry = findChild(banner, "recoveryRetryButton")
        verify(banner.recoveryIncomplete)
        verify(findChild(banner, "recoveryIncompleteText").visible)
        verify(!findChild(banner, "recoveryProgressBar").visible)
        verify(!findChild(banner, "recoveryCancelButton").visible)
        verify(retry.visible)
        compare(findChild(banner, "recoveryErrorText").visible, data.error.length > 0)
        mouseClick(retry, retry.width / 2, retry.height / 2)
        compare(recoveryModel.retryCalls, 1)
    }

    function test_completed_history_has_no_incomplete_message_or_actions() {
        recoveryModel.state = "complete"
        recoveryModel.active = false
        recoveryModel.incomplete = false
        recoveryModel.progress = 1
        const banner = createBanner()
        compare(findChild(banner, "recoveryProgressBar").value, 1)
        verify(!findChild(banner, "recoveryIncompleteText").visible)
        verify(!findChild(banner, "recoveryCancelButton").visible)
        verify(!findChild(banner, "recoveryRetryButton").visible)
    }

    function test_retry_waits_until_cancelled_work_has_stopped() {
        recoveryModel.state = "cancelled"
        recoveryModel.active = true
        const banner = createBanner()
        const retry = findChild(banner, "recoveryRetryButton")
        verify(retry.visible)
        verify(!retry.enabled)
        recoveryModel.active = false
        verify(retry.enabled)
    }

    function test_unlock_submits_password_and_clears_field() {
        recoveryModel.state = "waiting-wallet"
        recoveryModel.active = false
        recoveryModel.needsUnlock = true
        const banner = createBanner()
        const unlock = findChild(banner, "recoveryUnlockButton")
        const popup = findChild(banner, "recoveryUnlockPopup")
        verify(unlock.visible)
        mouseClick(unlock, unlock.width / 2, unlock.height / 2)
        tryCompare(popup, "opened", true)
        const field = findChild(popup, "recoveryPassphraseInput")
        const confirm = findChild(popup, "recoveryUnlockConfirmButton")
        verify(field !== null)
        verify(confirm !== null)
        field.text = "recovery password"
        mouseClick(confirm, confirm.width / 2, confirm.height / 2)
        compare(recoveryModel.unlockCalls, 1)
        compare(recoveryModel.lastPassphrase, "recovery password")
        compare(field.text, "")
        tryCompare(popup, "opened", false)
    }

    function test_changing_wallet_closes_password_popup() {
        recoveryModel.state = "waiting-wallet"
        recoveryModel.active = false
        recoveryModel.needsUnlock = true
        const banner = createBanner()
        const popup = findChild(banner, "recoveryUnlockPopup")
        popup.open()
        tryCompare(popup, "opened", true)
        banner.recovery = null
        tryCompare(popup, "opened", false)
        compare(recoveryModel.unlockCalls, 0)
    }

    function test_disabled_or_missing_controller_hides_banner() {
        const banner = createBanner()
        recoveryModel.visible = false
        verify(!banner.visible)
        banner.recovery = null
        verify(!banner.visible)
        compare(banner.recoveryState, "disabled")
        verify(!banner.recoveryIncomplete)
    }

    function test_long_error_wraps_at_small_width() {
        recoveryModel.state = "error"
        recoveryModel.errorText = "No connected peer could provide the historical block. Connect to another peer and retry recovery."
        const banner = createBanner()
        banner.width = 320
        verify(waitForItemPolished(banner))
        const errorText = findChild(banner, "recoveryErrorText")
        const retry = findChild(banner, "recoveryRetryButton")
        verify(errorText.lineCount > 1)
        verify(errorText.width > 0)
        verify(retry.x + retry.width <= banner.width)
    }
}
