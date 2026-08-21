// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQml 2.15

QtObject {
    property BitcoinAmount amountAmount: BitcoinAmount { satoshi: 50000 }
    property BitcoinAmount feeAmount: BitcoinAmount { satoshi: 1000 }
    property BitcoinAmount totalAmount: BitcoinAmount { satoshi: 51000 }
    property string amount: "0.00050000 BTC"
    property string fee: "0.00001000 BTC"
    property string total: "0.00051000 BTC"
    property string label: qsTr("Example recipient")
}
// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
