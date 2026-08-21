pragma Singleton
// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQml 2.15

QtObject {
    enum Mode { DESKTOP, MOBILE }

    readonly property bool isDesktop: true
    readonly property bool isMobile: false
    readonly property bool walletEnabled: true
    readonly property string state: "DESKTOP"
}
