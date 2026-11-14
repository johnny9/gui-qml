// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
import QtQuick 2.15
import QtTest 1.2
import org.bitcoincore.qt 1.0
import "../../qml/pages/settings" as SettingsPages
import "../../qml/pages/node" as NodePages
TestCase {
    id: testCase
    name: "R10LegacySurfaces"
    when: windowShown
    width: 850
    height: 760
    Component { id: proxyComponent; SettingsPages.SettingsProxy { width: 780; height: 700; settingsModel: optionsModel } }
    Component { id: trafficComponent; NodePages.NetworkTraffic { width: 780; height: 700 } }
    Component { id: backSpy; SignalSpy { signalName: "back" } }
    function init() {
        testSetup.failOnWarnings()
        const core = optionsModel.coreSettings
        core.holdProxyEffectsForTest = false
        core.refuseProxyAdmissionForTest = false
        core.proxyApplyStateForTest = CoreSettingsModel.Applied
        optionsModel.clearCoreSettingStatusesForTest()
        const proxy = core.entry("proxy"), onion = core.entry("onion")
        proxy.enabled = false; proxy.address = proxy.defaultAddress()
        onion.enabled = false; onion.address = onion.defaultAddress()
        testNetworkTrafficTower.active = false
    }
    function cleanup() {
        const core = optionsModel.coreSettings
        for (let turn = 0; turn < 16; ++turn) if (!core.deliverProxyEffectForTest()) break
        core.holdProxyEffectsForTest = false
        core.refuseProxyAdmissionForTest = false
        core.proxyApplyStateForTest = CoreSettingsModel.Applied
    }
    function proxyPage() {
        const view = createTemporaryObject(proxyComponent, testCase.Window.window.contentItem)
        verify(view !== null)
        tryCompare(view.proxyDraft, "ready", true)
        tryCompare(view.proxyDraft, "pending", false)
        return view
    }
    function dirty(view) {
        verify(waitForRendering(view))
        const toggle = findChild(view, "proxyEnableSwitch")
        verify(toggle !== null)
        mouseClick(toggle)
        tryCompare(view.proxyDraft, "pending", false)
        compare(view.draftProxyEnabled, true)
        compare(view.proxyDraftDirty, true)
    }
    function test_proxy_apply_waits_for_its_original_outcome_data() {
        return [{tag: "applied", state: CoreSettingsModel.Applied}, {tag: "failed", state: CoreSettingsModel.Failed}, {tag: "rejected", state: CoreSettingsModel.Rejected}]
    }
    function test_proxy_apply_waits_for_its_original_outcome(data) {
        const view = proxyPage()
        const spy = createTemporaryObject(backSpy, this, {target: view})
        dirty(view)
        const core = optionsModel.coreSettings
        core.holdProxyEffectsForTest = true
        core.proxyApplyStateForTest = data.state
        const before = core.proxyApplyCallsForTest
        const done = findChild(view, "settingsProxyDone")
        verify(done !== null)
        mouseClick(done)
        compare(view.awaitingApply, true)
        compare(spy.count, 0)
        compare(core.entry("proxy").enabled, false)
        compare(core.proxyApplyCallsForTest, before + 1)
        view.done()
        compare(core.proxyApplyCallsForTest, before + 1)
        verify(core.deliverProxyEffectForTest())
        compare(view.awaitingApply, false)
        compare(spy.count, data.state === CoreSettingsModel.Applied ? 1 : 0)
        compare(core.entry("proxy").enabled, data.state === CoreSettingsModel.Applied)
        if (data.state !== CoreSettingsModel.Applied) verify(view.proxyDraft.error.length > 0)
        view.proxyDraft.changed()
        compare(spy.count, data.state === CoreSettingsModel.Applied ? 1 : 0)
        compare(core.deliverProxyEffectForTest(), false)
    }
    function test_proxy_admission_refusal_is_not_completion() {
        const view = proxyPage()
        const spy = createTemporaryObject(backSpy, this, {target: view})
        dirty(view)
        const core = optionsModel.coreSettings
        core.refuseProxyAdmissionForTest = true
        const before = core.proxyApplyCallsForTest
        mouseClick(findChild(view, "settingsProxyDone"))
        compare(view.awaitingApply, false)
        compare(core.proxyApplyCallsForTest, before)
        compare(core.entry("proxy").enabled, false)
        compare(spy.count, 0)
    }
    function test_proxy_back_cancel_and_discard_preserve_storage() {
        const view = proxyPage()
        const spy = createTemporaryObject(backSpy, this, {target: view})
        dirty(view)
        const back = findChild(view, "settingsProxyBack")
        verify(back !== null)
        mouseClick(back)
        const popup = findChild(testCase.Window.window.contentItem, "discardProxyChangesPopup")
        verify(popup !== null)
        tryCompare(popup, "opened", true)
        mouseClick(findChild(popup.contentItem, "discardProxyChangesCancelButton"))
        tryCompare(popup, "opened", false)
        // opened clears when the exit transition starts; the modal overlay
        // still owns input until the popup is actually hidden.
        tryCompare(popup, "visible", false)
        compare(spy.count, 0)
        compare(view.draftProxyEnabled, true)
        mouseClick(back)
        tryCompare(popup, "opened", true)
        mouseClick(findChild(popup.contentItem, "discardProxyChangesConfirmButton"))
        compare(spy.count, 1)
        tryCompare(view.proxyDraft, "pending", false)
        compare(view.draftProxyEnabled, false)
        compare(optionsModel.coreSettings.entry("proxy").enabled, false)
    }
    function test_proxy_field_validation_does_not_persist_data() {
        return [{tag: "proxy-empty", key: "proxy", text: "", valid: false}, {tag: "proxy-valid", key: "proxy", text: "10.0.0.3:9050", valid: true},
                {tag: "onion-empty", key: "onion", text: "", valid: false}, {tag: "onion-valid", key: "onion", text: "127.0.0.1:9051", valid: true}]
    }
    function test_proxy_field_validation_does_not_persist(data) {
        const view = proxyPage()
        verify(waitForRendering(view))
        const toggle = findChild(view, data.key === "proxy" ? "proxyEnableSwitch" : "torEnableSwitch")
        verify(toggle !== null)
        mouseClick(toggle)
        tryCompare(view.proxyDraft, "pending", false)
        const input = findChild(view, data.key === "proxy" ? "proxyAddressInput" : "torAddressInput")
        verify(input !== null)
        input.beginEdit()
        testSetup.replaceText(input, data.text)
        input.editingFinished()
        tryCompare(view.proxyDraft, "pending", false)
        compare(input.validationError, data.valid ? "" : "Proxy location is required.")
        compare(view.canSaveProxyDraft, data.valid)
        compare(optionsModel.coreSettings.entry(data.key).enabled, false)
    }
    function test_proxy_clean_done_and_back_have_no_effect() {
        const view = proxyPage()
        const spy = createTemporaryObject(backSpy, this, {target: view})
        const before = optionsModel.coreSettings.proxyApplyCallsForTest
        view.done()
        compare(spy.count, 1)
        view.requestBack()
        compare(spy.count, 2)
        compare(optionsModel.coreSettings.proxyApplyCallsForTest, before)
    }
    function test_old_proxy_effect_does_not_resolve_new_view() {
        const first = proxyPage()
        dirty(first)
        first.updateProxyAddress("10.0.0.1:9050")
        tryCompare(first.proxyDraft, "pending", false)
        const core = optionsModel.coreSettings
        core.holdProxyEffectsForTest = true
        verify(first.commitProxyDraft() > 0)
        first.destroy()
        testSetup.processDeferredDeletes()
        const replacement = proxyPage()
        const spy = createTemporaryObject(backSpy, this, {target: replacement})
        replacement.updateProxyAddress("10.0.0.2:9050")
        tryCompare(replacement.proxyDraft, "pending", false)
        verify(core.deliverProxyEffectForTest())
        compare(core.entry("proxy").address, "10.0.0.1:9050")
        compare(core.entry("proxy").enabled, true)
        compare(replacement.draftProxyAddress, "10.0.0.2:9050")
        compare(spy.count, 0)
    }
    function buttonWithText(root, text) {
        if (!root) return null
        if (root.text === text && typeof root.clicked === "function") return root
        const children = root.children || []
        for (let i = 0; i < children.length; ++i) { const found = buttonWithText(children[i], text); if (found) return found }
        return null
    }
    function test_traffic_units_have_independent_boundary_oracles_data() {
        return [{tag: "zero", bytes: 0, text: "0 Bytes"}, {tag: "bytes", bytes: 999, text: "999 Bytes"},
                {tag: "KB", bytes: 1000, text: "1 KB"}, {tag: "MB", bytes: 1000000, text: "1 MB"},
                {tag: "GB", bytes: 1000000000, text: "1 GB"}, {tag: "TB", bytes: 1000000000000, text: "1 TB"},
                {tag: "PB", bytes: 1000000000000000, text: "1 PB"}, {tag: "largest-suffix", bytes: 1000000000000000000, text: "1000 PB"}]
    }
    function test_traffic_units_have_independent_boundary_oracles(data) {
        const view = createTemporaryObject(trafficComponent, testCase.Window.window.contentItem)
        verify(view !== null)
        compare(view.formatBytes(data.bytes), data.text)
        compare(testNetworkTrafficTower.active, true)
        view.destroy()
        testSetup.processDeferredDeletes()
        compare(testNetworkTrafficTower.active, false)
    }
    function test_traffic_range_buttons_update_original_tower_data() {
        return [{tag: "five-minutes", text: "5 min", scale: 300, window: 30}, {tag: "one-hour", text: "1 hour", scale: 3600, window: 360},
                {tag: "twelve-hours", text: "12 hours", scale: 43200, window: 4320}, {tag: "one-day", text: "1 day", scale: 86400, window: 8640}]
    }
    function test_traffic_range_buttons_update_original_tower(data) {
        const view = createTemporaryObject(trafficComponent, testCase.Window.window.contentItem)
        verify(view !== null)
        verify(waitForRendering(view))
        const button = buttonWithText(view, data.text)
        verify(button !== null)
        mouseClick(button)
        tryCompare(view, "trafficGraphScale", data.scale)
        compare(testNetworkTrafficTower.lastFilterWindowSize, data.window)
        compare(testNetworkTrafficTower.active, true)
        view.destroy()
        testSetup.processDeferredDeletes()
        compare(testNetworkTrafficTower.active, false)
    }
}
