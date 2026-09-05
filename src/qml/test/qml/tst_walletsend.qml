// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick
import QtTest
import "../../qml/pages/wallet"

TestCase {
    id: test
    name: "WalletSend"
    when: windowShown
    width: 900
    height: 1200
    QtObject { id: optionsModel; property int displayUnit: 0 }
    ListModel { id: recipientsMock }
    ListModel {
        id: coinsMock
        property bool active: false
        property bool manual: false
        property bool busy: false
        property string error: ""
        property int displayUnit: 0
    }
    QtObject {
        id: feesMock
        property int target: 2
        property bool custom: false
        property string customRate: ""
        property bool pending: false
        property string estimatedFee: "Preview only"
        property int displayUnit: 0
    }
    QtObject {
        id: reviewMock
        property bool hasReview: false
        property string transactionId: "exact-transaction"
        property string feeText: "123 sat"
        property var outputs: []
        property int displayUnit: 0
    }
    QtObject {
        id: sendMock
        property var recipients: recipientsMock
        property var coins: coinsMock
        property var fees: feesMock
        property var review: reviewMock
        property bool available: true
        property bool busy: false
        readonly property bool canPrepare: available && !busy
        property bool needsPassphrase: true
        property string error: ""
        property int prepared: 0
        property string passedPassword: ""
        function prepare(password) { prepared++; passedPassword = password; busy = true; return true }
        function invalidateReview() { reviewMock.hasReview = false }
    }
    QtObject { id: overviewMock; property string displayName: "Test wallet" }
    QtObject { id: walletMock; property var send: sendMock; property var overview: overviewMock }
    Component { id: pageComponent; Send { wallet: walletMock; sessionId: "1"; width: 900; height: 1200 } }

    function init() { sendMock.busy = false; sendMock.prepared = 0; sendMock.passedPassword = ""; reviewMock.hasReview = false }

    function test_prepare_clears_password_and_disables_repeat_action() {
        const page = createTemporaryObject(pageComponent, test)
        verify(page !== null)
        const password = findChild(page, "sendPassphrase")
        const prepare = findChild(page, "sendPrepareButton")
        verify(password !== null)
        password.text = "synthetic-only"
        verify(prepare.enabled)
        prepare.clicked()
        compare(sendMock.prepared, 1)
        compare(sendMock.passedPassword, "synthetic-only")
        compare(password.text, "")
        tryCompare(prepare, "enabled", false)
    }

    function test_review_displays_actual_fee_not_preview() {
        const page = createTemporaryObject(pageComponent, test)
        verify(page !== null)
        reviewMock.hasReview = true
        const actual = findChild(page, "sendPreparedFee")
        verify(actual.text.indexOf("123 sat") >= 0)
        verify(actual.text.indexOf("Preview only") < 0)
    }
}
