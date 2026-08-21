pragma Singleton
// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQml 2.15

QtObject {
    readonly property bool isDebug: true
    readonly property string fullClientVersion: "Bitcoin Core App (designer preview)"
}
