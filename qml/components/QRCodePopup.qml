// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
import QtQuick 2.15
import QtQuick.Controls 2.15
import org.bitcoincore.qt 1.0

Popup {
    id: root
    property RequestSharingModel sharing: null
    parent: Overlay.overlay
    anchors.centerIn: Overlay.overlay
    width: 360
    modal: true
    dim: true
    contentItem: Loader {
        active: root.sharing !== null
        sourceComponent: SavedRequestSharing { sharing: root.sharing; visible: root.visible }
    }
}
