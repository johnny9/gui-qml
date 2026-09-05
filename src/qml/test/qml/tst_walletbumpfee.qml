// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick
import QtTest
import "../../qml/pages/wallet"

TestCase {
    id: test
    name: "WalletBumpFee"
    when: windowShown
    width: 900
    height: 1000
    QtObject { id: optionsModel; property int displayUnit: 0 }
    QtObject {
        id: reviewMock
        property bool hasReview: true
        property string previousFeeText: "250 sat"
        property string feeText: "1250 sat"
        property var outputs: []
        property int displayUnit: 0
    }
    QtObject {
        id: bumpMock
        property var review: reviewMock
        property bool eligible: true
        property bool busy: false
        property bool accepted: false
        property int revision: 1
        readonly property bool canSubmit: eligible && !busy && review.hasReview
        property string error: ""
        property string feeIncrease: "1000 sat"
        property string replacementTransactionId: ""
        property int inspectCalls: 0
        property int prepareCalls: 0
        property int submitCalls: 0
        property string passedPassword: ""
        function inspect(txid) { inspectCalls++ }
        function prepare(rate) { prepareCalls++ }
        function submit(password) { submitCalls++; passedPassword = password; busy = true }
        function discardReview() { revision++; review.hasReview = false }
    }
    QtObject { id: walletMock; property var bump: bumpMock }
    Component { id: pageComponent; BumpFee { wallet: walletMock; sessionId: "1"; transactionId: "synthetic-id"; width: 900; height: 1000 } }
    function init() {
        reviewMock.hasReview = true
        bumpMock.busy = false
        bumpMock.inspectCalls = 0
        bumpMock.prepareCalls = 0
        bumpMock.submitCalls = 0
        bumpMock.passedPassword = ""
    }
    function test_open_does_not_prepare_or_submit() {
        const page = createTemporaryObject(pageComponent, test)
        verify(page !== null)
        compare(bumpMock.inspectCalls, 1)
        compare(bumpMock.prepareCalls, 0)
        compare(bumpMock.submitCalls, 0)
        verify(findChild(page, "bumpOldFee").text.indexOf("250 sat") >= 0)
        verify(findChild(page, "bumpNewFee").text.indexOf("1250 sat") >= 0)
        verify(findChild(page, "bumpFeeIncrease").text.indexOf("1000 sat") >= 0)
    }
    function test_submit_is_explicit_revision_bound_and_clears_password() {
        const page = createTemporaryObject(pageComponent, test)
        verify(page !== null)
        const submit = findChild(page, "bumpSubmit")
        const confirmation = findChild(page, "bumpConfirmation")
        const password = findChild(page, "bumpPassphrase")
        password.text = "synthetic-only"
        verify(!submit.enabled)
        confirmation.checked = true
        confirmation.toggled()
        tryCompare(submit, "enabled", true)
        bumpMock.revision++
        tryCompare(submit, "enabled", false)
        confirmation.toggled()
        tryCompare(submit, "enabled", true)
        submit.clicked()
        compare(bumpMock.submitCalls, 1)
        compare(bumpMock.passedPassword, "synthetic-only")
        compare(password.text, "")
        tryCompare(submit, "enabled", false)
        tryCompare(findChild(page, "bumpPrepare"), "enabled", false)
    }
}
