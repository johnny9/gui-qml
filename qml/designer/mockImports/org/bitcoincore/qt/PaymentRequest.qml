// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQml 2.15

QtObject {
    property string address: "bc1qexampleaddressfordesignerpreview000000000"
    readonly property string addressFormatted: address
    property string label: qsTr("Savings")
    property string message: qsTr("Designer preview payment")
    property string addressType: "bech32"
    property string noteSelf: qsTr("Design-time example")
    property BitcoinAmount amount: BitcoinAmount { satoshi: 125000 }
    property string amountError: ""
    property string id: "preview-request"
    property bool needsUnlock: false
    property string unlockError: ""
    property string qrPayload: "bitcoin:" + address
    property string createdIso: "2026-08-21T12:00:00Z"
    property bool hasPaymentInfo: true
    property bool isEditing: true

    function clear() {}
    function edit() { isEditing = true }
}
// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
