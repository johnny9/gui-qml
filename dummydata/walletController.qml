// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQml 2.15
import org.bitcoincore.qt 1.0

QtObject {
    property bool canCreateExternalSignerWallet: true
    property bool initialized: true
    property bool isWalletLoaded: true
    property bool isWalletOpen: true
    property bool noWalletsFound: false
    property WalletQmlModel selectedWallet: WalletQmlModel {}
    property string externalSignerError: ""
    property string externalSignerName: ""
    property string homePath: "/home/example"
    property int lastImportedWalletKeyScheme: WalletQmlModel.SingleKey
    property string lastImportedWalletName: ""
    property string suggestedExternalSignerWalletName: qsTr("Hardware wallet")
    property string walletCreateError: ""
    property string walletImportErrorDescription: ""
    property string walletImportErrorHelpText: ""
    property string walletImportErrorTitle: ""
    property string walletLoadError: ""
    property bool walletLoadInProgress: false
    property var walletLoadWarnings: []
    property string walletLocationOpenError: ""
    property string walletMigrationError: ""
    property bool walletMigrationInProgress: false
    property string walletNameAvailabilityError: ""

    function clearWalletCreateStatus() {}
    function clearWalletLoadStatus() {}
    function clearWalletLocationOpenError() {}
    function closeWallet() {}
    function createExternalSignerWallet() {}
    function createSingleSigWallet() {}
    function createWatchOnlyWallet() {}
    function importWallet() {}
    function migrateWallet() {}
    function normalizeWalletPath(path) { return path }
    function openSelectedWalletLocation() {}
    function refreshExternalSignerStatus() {}
    function requestClosePaymentRequestDetail() {}
    function requestOpenReceive() {}
    function requestOpenWalletSettings() {}
    function setSelectedWallet() {}
    function setWalletDisplayName(value) { selectedWallet.displayName = value }
    function validateXpub() { return true }
    function walletPathExists() { return false }
}
// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
