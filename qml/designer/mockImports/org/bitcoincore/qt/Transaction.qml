// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQml 2.15

QtObject {
    enum Type { Other, Generated, SendToAddress, SendToOther, RecvWithAddress, RecvFromOther, SendToSelf }
    enum Status { Confirmed, Unconfirmed, Confirming, Conflicted, Abandoned, Immature, NotAccepted }

    property string address: "bc1qexampleaddressfordesignerpreview000000000"
    property string amount: "+0.00125000 BTC"
    property string label: qsTr("Example transaction")
    property int status: Transaction.Confirmed
    property int type: Transaction.RecvWithAddress
    property string timestamp: "2026-08-21 12:00"
}
// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
