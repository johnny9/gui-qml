// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQml.Models 2.15

ListModel {
    enum Category { Receiving, Change }

    property int category: AddressListModel.Receiving
    readonly property var categoryOptions: [
        { text: qsTr("Receiving"), value: AddressListModel.Receiving },
        { text: qsTr("Change"), value: AddressListModel.Change }
    ]
    property bool showUsed: false

    function refresh() {}
    function setAddressLabel() { return true }
    function addressAt() { return "bc1qexampleaddressfordesignerpreview000000000" }
}
// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
