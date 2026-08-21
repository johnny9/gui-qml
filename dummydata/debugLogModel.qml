// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQml.Models 2.15

ListModel {
    property bool active: true
    property string filter: ""
    property bool hasMoreLines: false

    ListElement {
        command: ""
        dateLabel: "12:00:00"
        message: "Bitcoin Core App designer preview"
        severity: 0
    }

    function loadMore() {}
    function openLogFile() {}
    function refresh() {}
    function updateRelativeTimes() {}
}
// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
