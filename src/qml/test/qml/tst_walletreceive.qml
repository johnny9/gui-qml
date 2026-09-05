// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick
import QtTest
import "../../qml/pages/wallet"

TestCase {
    id: test
    name: "WalletReceive"
    when: windowShown
    width: 800
    height: 1000

    QtObject {
        id: draftMock
        property string id: ""
        property string address: ""
        property string label: ""
        property string message: ""
        property string noteSelf: ""
        property string amount: ""
        property string addressType: ""
        property string uri: ""
        property string qrImageSource: ""
    }
    ListModel { id: historyMock }
    QtObject {
        id: receiveMock
        property var draft: draftMock
        property var history: historyMock
        property bool available: true
        property bool busy: false
        property bool needsUnlock: false
        property string error: ""
        property var addressTypes: ["bech32", "legacy"]
        property string defaultAddressType: "bech32"
        property int saved: 0
        property string passedPassword: ""
        function save(password) { saved++; passedPassword = password; return true }
        function clear() {}
    }
    QtObject { id: overviewMock; property string displayName: "Test wallet" }
    QtObject { id: walletMock; property var receive: receiveMock; property var overview: overviewMock }
    Component { id: pageComponent; WalletReceive { width: 800; height: 1000; wallet: walletMock } }

    function init() {
        receiveMock.busy = false
        receiveMock.available = true
        receiveMock.needsUnlock = false
        receiveMock.saved = 0
        receiveMock.passedPassword = ""
    }

    function test_busy_and_capability_gate_actions() {
        const page = createTemporaryObject(pageComponent, test)
        verify(page !== null)
        const save = findChild(page, "saveReceiveRequestButton")
        verify(save.enabled)
        receiveMock.busy = true
        tryCompare(save, "enabled", false)
        receiveMock.busy = false
        receiveMock.available = false
        tryCompare(save, "enabled", false)
    }

    function test_password_is_action_only_and_cleared() {
        receiveMock.needsUnlock = true
        const page = createTemporaryObject(pageComponent, test)
        verify(page !== null)
        const password = findChild(page, "receivePasswordInput")
        const save = findChild(page, "saveReceiveRequestButton")
        password.text = "test-only"
        verify(save.enabled)
        save.clicked()
        compare(receiveMock.saved, 1)
        compare(receiveMock.passedPassword, "test-only")
        compare(password.text, "")
    }
}
