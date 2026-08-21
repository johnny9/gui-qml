// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQml 2.15
import org.bitcoincore.qt 1.0

QtObject {
    property QtObject coreSettings: QtObject {
        function entry(name) {
            return {
                name: name,
                canEdit: true,
                enabled: name === "listen" || name === "natpmp",
                value: name === "prune" ? 20 : true,
                address: name === "onion" ? "127.0.0.1:9050" : "127.0.0.1:9050",
                error: ""
            }
        }
    }
    property var coreSettingStatuses: ({
        maxmempool: { canEdit: true, source: "settings" },
        signer: { canEdit: true, source: "settings" },
        lang: { canEdit: true, source: "settings" }
    })
    property int assumedBlockchainSize: 700
    property int assumedChainstateSize: 12
    property int dbcacheSizeMiB: 450
    property int minDbcacheSizeMiB: 4
    property int maxDbcacheSizeMiB: 16384
    property int maxMempoolSizeMB: 300
    property int minMaxMempoolSizeMB: 5
    property int maxMaxMempoolSizeMB: 10000
    property int scriptThreads: 0
    property int minScriptThreads: -15
    property int maxScriptThreads: 15
    property int displayUnit: BitcoinAmount.BTC
    property string language: "en"
    property var availableLanguages: ["en", "es", "de", "fr"]
    property string moneyFont: "BitcoinCoreSans"
    property string moneyFontChoice: "bitcoin-core-sans"
    property string externalSignerPath: ""
    property string dataDir: "/home/example/.bitcoin"
    property bool useDefaultDataDir: true
    property bool existingProfile: false
    property bool canFinish: true
    property bool connectionSettingsDirty: false
    property bool proxySettingsDirty: false
    property bool storageSettingsDirty: false
    property bool developerSettingsDirty: false
    property bool mempoolSettingsDirty: false
    property bool walletSettingsDirty: false
    property int fullStorageRequiredGB: 712
    property int storageMinimumRequiredGB: 12
    property int storageAvailableGB: 1024
    property string storageAvailableText: qsTr("1 TB available")
    property string storageWarningText: ""
    property string storageErrorText: ""
    property bool storageCheckPending: false
    property bool storageEnoughForFull: true
    property bool storageEnoughForSelected: true
    property string previewError: ""
    property var thirdPartyTransactionUrls: []

    function displayUnitLabelForAmount() { return "BTC" }
    function languageLabel(tag) {
        const labels = { en: "English", es: "Español", de: "Deutsch", fr: "Français" }
        return labels[tag] || tag
    }
    function externalSignerPathValidationError() { return "" }
    function thirdPartyTransactionLinks() { return [] }
    function getDefaultDataDirString() { return "/home/example/.bitcoin" }
    function selectCustomDataDir(path) { dataDir = path }
    function validateCustomDataDir() { return true }
}
// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
