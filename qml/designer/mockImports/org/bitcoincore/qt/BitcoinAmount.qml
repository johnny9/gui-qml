// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQml 2.15

QtObject {
    id: root
    enum Unit { BTC, mBTC, uBTC, SAT }

    property int unit: BitcoinAmount.BTC
    property string display: "0.00000000"
    readonly property string unitLabel: unit === BitcoinAmount.SAT ? "sat" : "BTC"
    readonly property string displayWithUnit: display + " " + unitLabel
    property double satoshi: 0

    function format() {}
    function flipUnit() {
        unit = unit === BitcoinAmount.SAT ? BitcoinAmount.BTC : BitcoinAmount.SAT
    }
    function clear() {
        satoshi = 0
        display = "0.00000000"
    }
}
// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
