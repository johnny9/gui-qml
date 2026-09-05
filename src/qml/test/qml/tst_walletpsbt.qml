// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick
import QtTest
import "../../qml/pages/wallet"

TestCase {
    id: test
    name: "WalletPsbt"
    when: windowShown
    width: 900
    height: 1000
    QtObject { id: optionsModel; property int displayUnit: 0 }
    QtObject {
        id: reviewMock
        property bool hasReview: true
        property string feeText: "250 sat"
        property int displayUnit: 0
    }
    QtObject {
        id: psbtMock
        property var review: reviewMock
        property bool available: true
        property bool loaded: true
        property bool busy: false
        property bool complete: false
        property bool known: false
        property bool locked: false
        property int revision: 1
        readonly property bool canSubmit: complete && !busy
        readonly property bool canSign: available && !busy && !locked
        readonly property bool canUnlockForSigning: available && !busy && locked
        property var outputs: []
        property string error: ""
        property string status: "Incomplete transaction"
        property int signedCalls: 0
        property int importedCalls: 0
        property int submitCalls: 0
        property string passedPassword: ""
        function sign(password) { signedCalls++; passedPassword = password; busy = true }
        function importFile(path) { importedCalls++ }
        function submit() { submitCalls++; busy = true }
    }
    QtObject { id: overviewMock; property string displayName: "Original wallet" }
    QtObject { id: walletMock; property var psbt: psbtMock; property var overview: overviewMock }
    Component { id: pageComponent; Psbt { wallet: walletMock; sessionId: "1"; width: 900; height: 1000 } }
    function init() { psbtMock.locked = false; psbtMock.busy = false; psbtMock.complete = false; psbtMock.signedCalls = 0; psbtMock.importedCalls = 0; psbtMock.submitCalls = 0; psbtMock.passedPassword = "" }

    function test_review_does_not_trigger_actions() {
        const page = createTemporaryObject(pageComponent, test)
        verify(page !== null)
        compare(psbtMock.signedCalls, 0)
        compare(psbtMock.importedCalls, 0)
        verify(findChild(page, "psbtFee").text.indexOf("250 sat") >= 0)
    }
    function test_sign_clears_password_and_blocks_repeat_action() {
        psbtMock.locked = true
        const page = createTemporaryObject(pageComponent, test)
        verify(page !== null)
        const password = findChild(page, "psbtPassphrase")
        const sign = findChild(page, "psbtSign")
        password.text = "synthetic-only"
        verify(sign.enabled)
        verify(sign.text.indexOf("Unlock") >= 0)
        sign.clicked()
        compare(psbtMock.signedCalls, 1)
        compare(psbtMock.passedPassword, "synthetic-only")
        compare(password.text, "")
        tryCompare(sign, "enabled", false)
        tryCompare(findChild(page, "psbtExport"), "enabled", false)
    }
    function test_submit_requires_acknowledging_the_current_revision() {
        psbtMock.complete = true
        const page = createTemporaryObject(pageComponent, test)
        verify(page !== null)
        const submit = findChild(page, "psbtSubmit")
        const confirmation = findChild(page, "psbtReviewConfirmation")
        verify(!submit.enabled)
        confirmation.checked = true
        confirmation.toggled()
        tryCompare(submit, "enabled", true)
        psbtMock.revision++
        tryCompare(submit, "enabled", false)
        compare(psbtMock.submitCalls, 0)
        confirmation.toggled()
        tryCompare(submit, "enabled", true)
        submit.clicked()
        compare(psbtMock.submitCalls, 1)
        tryCompare(submit, "enabled", false)
    }
}
