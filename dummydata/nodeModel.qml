// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQml 2.15

QtObject {
    property bool blockSyncActive: true
    property int blockTipHeight: 910240
    property bool faulted: false
    property bool hasWarnings: false
    property bool headerPresync: false
    property bool headerSyncActive: false
    property int maxNumOutboundPeers: 10
    property bool mempoolInfoPollingActive: true
    property bool mempoolInformationAvailable: true
    property int mempoolMaxUsageMB: 300
    property int mempoolTransactionCount: 18452
    property int mempoolUsageMB: 48
    property var nodeInformationRows: [
        { label: qsTr("Network"), value: "main" },
        { label: qsTr("Block height"), value: "910,240" }
    ]
    property int numOutboundPeers: 8
    property int numPeers: 11
    property bool pause: false
    property int remainingSyncTime: 240
    property real verificationProgress: 0.82
    property bool runtimeDialogVisible: false
    property string runtimeDialogTitle: qsTr("Question")
    property string runtimeDialogMessage: qsTr("Designer preview")
    property int runtimeDialogButtons: 0
    property int runtimeDialogIcon: 0
    property string startupError: ""
    property var warningList: []

    function answerRuntimeDialog() {}
    function banPeer() {}
    function disconnectPeer() {}
    function requestShutdown() {}
    function startNodeInitializionThread() {}
}
// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
