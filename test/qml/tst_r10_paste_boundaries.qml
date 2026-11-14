// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
import QtQuick 2.15
import QtTest 1.2
import org.bitcoincore.qt 1.0
import "../../qml/components" as Components
import "../../qml/controls" as Controls
TestCase {
    id: testCase
    name: "R10PasteBoundaries"
    when: windowShown
    width: 600
    height: 300
    Component { id: multiline; Controls.LabeledTextArea { width: 540; height: 180; interceptPaste: true } }
    Component { id: address; Components.BitcoinAddressInputField { width: 540; interceptPaste: true } }
    Component {
        id: nativeMultiline
        Controls.LabeledTextArea {
            width: 540
            height: 180
            interceptPaste: true
            property int pasteCalls: 0
            property string capturedText: ""
            property string capturedPrevious: ""
            property string capturedCurrent: ""
            onPasteRequested: (payload) => {
                pasteCalls += 1
                capturedText = payload.text
                capturedPrevious = payload.previous
                capturedCurrent = payload.current
                payload.origin.complete(payload)
            }
        }
    }
    function init() { testSetup.failOnWarnings(); Clipboard.currentText = "" }
    function test_compatibility_helper_preserves_the_captured_edit_data() {
        const uri = "bitcoin:bcrt1qtarget"
        const limit = "bitcoin:" + "a".repeat(504)
        const cases = [
            {tag: "insertion", clip: uri, previous: "left right", current: "left " + uri + "right", text: uri, address: uri},
            {tag: "trim-case", clip: "  BITCOIN:bcrt1qtarget  ", previous: "left right", current: "left   BITCOIN:bcrt1qtarget  right", text: "BITCOIN:bcrt1qtarget", address: "BITCOIN:bcrt1qtarget"},
            {tag: "ordinary", clip: "bcrt1qtarget", previous: "", current: "bcrt1qtarget", text: "", address: ""},
            {tag: "not-inserted", clip: uri, previous: "before", current: "after", text: "", address: ""},
            {tag: "unchanged", clip: uri, previous: uri, current: uri, text: "", address: ""},
            {tag: "prefix-changed", clip: uri, previous: "left right", current: "other " + uri + "right", text: "", address: uri},
            {tag: "suffix-changed", clip: uri, previous: "left right", current: "left " + uri + "other", text: "", address: uri},
            {tag: "previous-limit", clip: uri, previous: "x".repeat(513), current: uri, text: "", address: ""},
            {tag: "current-limit", clip: uri, previous: "", current: uri + "x".repeat(513), text: "", address: ""},
            {tag: "clipboard-limit", clip: limit + "a", previous: "", current: limit + "a", text: "", address: ""},
            {tag: "exact-limit", clip: limit, previous: "", current: limit, text: limit, address: limit},
            {tag: "exact-current-prefix", clip: limit.slice(0, 511), previous: "l", current: "l" + limit.slice(0, 511), text: limit.slice(0, 511), address: limit.slice(0, 511)},
            {tag: "exact-current-suffix", clip: limit.slice(0, 511), previous: "r", current: limit.slice(0, 511) + "r", text: limit.slice(0, 511), address: limit.slice(0, 511)},
            {tag: "exact-current-both", clip: limit.slice(0, 510), previous: "lr", current: "l" + limit.slice(0, 510) + "r", text: limit.slice(0, 510), address: limit.slice(0, 510)},
            {tag: "disabled", clip: uri, previous: "", current: uri, text: "", address: "", disabled: true}
        ]
        const rows = []
        for (let i = 0; i < cases.length; ++i) {
            rows.push(Object.assign({}, cases[i], {tag: "multiline-" + cases[i].tag, kind: "multiline"}))
            rows.push(Object.assign({}, cases[i], {tag: "address-" + cases[i].tag, kind: "address"}))
        }
        return rows
    }
    function test_compatibility_helper_preserves_the_captured_edit(data) {
        const editor = createTemporaryObject(data.kind === "multiline" ? multiline : address,
                                              testCase.Window.window.contentItem)
        verify(editor !== null)
        editor.interceptPaste = !data.disabled
        Clipboard.currentText = data.clip
        const before = data.kind === "multiline" ? editor.text : editor.inputText
        compare(editor.pastedPaymentUri(data.previous, data.current), data.kind === "multiline" ? data.text : data.address)
        compare(Clipboard.currentText, data.clip)
        compare(data.kind === "multiline" ? editor.text : editor.inputText, before)
        compare(editor.pastePending, false)
    }
    function test_native_multiline_preserves_clipboard_provenance_data() {
        const uri = "bitcoin:bcrt1qtarget"
        const limit = "bitcoin:" + "a".repeat(504)
        return [
            {tag: "empty-exact-limit", previous: "", current: limit, clip: limit, accepted: true},
            {tag: "prefix", previous: "left", current: "left" + uri, clip: uri, accepted: true},
            {tag: "suffix", previous: "right", current: uri + "right", clip: uri, accepted: true},
            {tag: "both", previous: "leftright", current: "left" + uri + "right", clip: uri, accepted: true},
            {tag: "different-prefix", previous: "leftright", current: "other" + uri + "right", clip: uri, accepted: false},
            {tag: "different-suffix", previous: "leftright", current: "left" + uri + "other", clip: uri, accepted: false},
            {tag: "unchanged", previous: uri, current: uri, clip: uri, accepted: false}
        ]
    }
    function test_native_multiline_preserves_clipboard_provenance(data) {
        const editor = createTemporaryObject(nativeMultiline, testCase.Window.window.contentItem,
                                             {paymentUriRestoreText: data.previous})
        verify(editor !== null)
        verify(waitForRendering(editor))
        Clipboard.currentText = data.clip
        verify(testSetup.replaceText(editor.field, data.current))
        compare(editor.pasteCalls, data.accepted ? 1 : 0)
        compare(editor.pastePending, false)
        compare(editor.text, data.accepted ? data.previous : data.current)
        compare(Clipboard.currentText, data.clip)
        if (data.accepted) {
            compare(editor.capturedText, data.clip)
            compare(editor.capturedPrevious, data.previous)
            compare(editor.capturedCurrent, data.current)
        }
    }

}
