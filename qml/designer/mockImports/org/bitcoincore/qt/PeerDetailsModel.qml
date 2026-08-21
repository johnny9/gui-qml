// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQml 2.15

QtObject {
    property int nodeId: 21
    property string rawAddress: "203.0.113.21:8333"
    property string address: rawAddress
    property string addressLocal: "127.0.0.1:8333"
    property string type: "outbound-full-relay"
    property string version: "70016"
    property string userAgent: "/Satoshi:29.0.0/"
    property string services: "NETWORK, WITNESS, NETWORK_LIMITED"
    property bool transactionRelay: true
    property bool addressRelay: true
    property string startingHeight: "910,000"
    property string syncedHeaders: "910,240"
    property string syncedBlocks: "910,240"
    property string direction: "Outbound"
    property string connectionDuration: "2 hours"
    property string lastSend: "4 seconds"
    property string lastReceived: "2 seconds"
    property string bytesSent: "1.4 MB"
    property string bytesReceived: "8.2 MB"
    property string pingTime: "42 ms"
    property string pingWait: "0 ms"
    property string pingMin: "38 ms"
    property string timeOffset: "0 s"
    property string mappedAS: "64500"
    property string permission: "none"
}
// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
