// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQml 2.15

QtObject {
    enum State { Idle, Preparing, NeedsConfirmation, Committing, Succeeded, Failed }
    enum ActionType { SpeedUp }

    property int state: BumpTransactionModel.Idle
    property int actionType: BumpTransactionModel.SpeedUp
    property string oldFee: "0.00001000 BTC"
    property string newFee: "0.00002000 BTC"
    property string feeIncrease: "0.00001000 BTC"
    property string oldTxid: "00…00"
    property string newTxid: "11…11"
    property string errorText: ""
    property bool needsUnlock: false

    function prepareFeeBump() { state = BumpTransactionModel.NeedsConfirmation }
    function confirmFeeBump() { state = BumpTransactionModel.Succeeded; return true }
    function confirmFeeBumpWithPassphrase() { return confirmFeeBump() }
    function reset() { state = BumpTransactionModel.Idle }
}
// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
