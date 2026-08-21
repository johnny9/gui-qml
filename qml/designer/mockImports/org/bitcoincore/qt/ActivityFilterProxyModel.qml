// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQml.Models 2.15

ListModel {
    enum DateFilter { DateAll, Today, ThisWeek, ThisMonth, ThisYear }
    enum TypeFilter { TypeAll, Received, Sent, SentToSelf, Mined, PaymentRequest }

    property var sourceModel: null
    property string searchText: ""
    property int dateFilter: ActivityFilterProxyModel.DateAll
    property int typeFilter: ActivityFilterProxyModel.TypeAll
    property int displayUnit: BitcoinAmount.BTC

    function exportCsv() { return true }
}
// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
