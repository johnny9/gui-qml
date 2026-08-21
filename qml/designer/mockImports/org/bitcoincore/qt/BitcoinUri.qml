pragma Singleton
// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQml 2.15

QtObject {
    function parseBitcoinUri(value) {
        return {
            valid: value && value.length > 0,
            address: value ? value.replace(/^bitcoin:/, "") : "",
            amount: "",
            label: "",
            message: ""
        }
    }

    function parseBitcoinUriFromFile() { return ({ valid: false }) }
}
