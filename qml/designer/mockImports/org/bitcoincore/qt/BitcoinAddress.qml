// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQml 2.15

QtObject {
    property string address: "bc1qexampleaddressfordesignerpreview000000000"
    readonly property string formattedAddress: address
    readonly property string ellipsesAddress: address.length > 18
        ? address.slice(0, 9) + "…" + address.slice(-8)
        : address

    function setAddress(value) {
        address = value
        return value.length
    }
}
// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
