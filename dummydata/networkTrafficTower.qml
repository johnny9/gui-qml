// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQml 2.15

QtObject {
    property bool active: true
    property real maxReceivedRateBps: 1250000
    property real maxSentRateBps: 320000
    property var receivedRateList: [120000, 280000, 510000, 340000, 750000]
    property var sentRateList: [40000, 80000, 60000, 150000, 110000]
    property string totalBytesReceived: "8.4 GB"
    property string totalBytesSent: "1.2 GB"

    function updateFilterWindowSize() {}
}
// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
