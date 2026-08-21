import QtQml.Models 2.15
// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


ListModel {
    property bool walletDirLoaded: true

    ListElement {
        name: "designer-wallet"
        displayName: "Designer Wallet"
        balance: "0.01250000 BTC"
        loadState: 1
        errorMessage: ""
        keySchemeKind: 0
    }

    function listWalletDir() {}
}
