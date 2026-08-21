// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import "../controls"
import "../pages/settings"

ApplicationWindow {
    id: root
    width: 800
    height: 665
    visible: true
    title: qsTr("Bitcoin Core App — Designer Preview")
    color: Theme.color.background

    SettingsDesignSystem {
        anchors.fill: parent
    }
}
