// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15

QtObject {
    id: operation
    property int requestId: 0
    property var requestOwner: null
    readonly property bool pending: _pending
    readonly property var result: _published
    property bool _pending: true
    property bool _closed: false
    property var _candidate: ({})
    property var _published: ({})
    signal finished()

    function prepare(outcome, held) {
        _candidate = outcome
        if (!held) Qt.callLater(operation.retryPublication)
    }
    function retryPublication() {
        if (_closed || !_pending) return
        _published = _candidate
        _pending = false
        finished()
    }
    function cancel() { closeAdmission(); destroy() }
    function closeAdmission() {
        _closed = true
        _pending = false
        _candidate = ({})
        _published = ({})
    }
}
