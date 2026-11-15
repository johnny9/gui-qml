// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
import QtQuick 2.15
import org.bitcoincore.qt 1.0

Item {
    id: root
    property WalletQmlModel wallet: null
    property bool contextActive: true
    readonly property bool opened: loader.item ? loader.item.opened : false
    visible: loader.item ? loader.item.visible : false
    signal closed()
    function openRequest(id, origin) { return loader.item ? loader.item.openRequest(id, origin) : false }
    function close() {
        if (!loader.item) return
        if (root.wallet && root.wallet.receive) loader.item.cancelPending()
        loader.item.close()
    }
    Loader {
        id: loader
        sourceComponent: root.wallet && root.wallet.receive ? ownedDetails : compatibilityDetails
    }
    Component { id: ownedDetails; ReceiveRequestModal { wallet: root.wallet; contextActive: root.contextActive; onClosed: root.closed() } }
    Component { id: compatibilityDetails; PaymentRequestModal { wallet: root.wallet; onClosed: root.closed() } }
}
