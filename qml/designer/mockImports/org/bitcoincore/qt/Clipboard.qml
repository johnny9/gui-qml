pragma Singleton
// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQml 2.15

QtObject {
    property string contents: "bc1qexampleaddressfordesignerpreview000000000"

    function setText(value) { contents = value }
    function text() { return contents }
}
