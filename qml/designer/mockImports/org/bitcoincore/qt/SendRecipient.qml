// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQml 2.15

QtObject {
    property BitcoinAddress address: BitcoinAddress {}
    property string label: qsTr("Example recipient")
    property string message: ""
    property BitcoinAmount amount: BitcoinAmount { satoshi: 50000 }
    property bool subtractFeeFromAmount: false
    property string addressError: ""
    property string amountError: ""
    property bool isValid: true

    function clear() {}
}
// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
