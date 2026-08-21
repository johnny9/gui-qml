// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQml 2.15
import QtQml.Models 2.15

QtObject {
    property var availableCommands: ["getblockchaininfo", "getnetworkinfo"]
    property bool executing: false
    property string errorColor: "#ec6363"
    property string keyColor: "#c075dc"
    property string replyColor: "#36b46b"
    property string requestColor: "#3ca3de"
    property ListModel outputModel: ListModel {
        ListElement { category: "reply"; message: "Welcome to the RPC console." }
    }

    function browseHistory() { return "" }
    function clear() { outputModel.clear() }
    function ensureWelcomeMessage() {}
    function resetHistoryNavigation() {}
    function submitCommand() {}
}
// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
