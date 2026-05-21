// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtTest 1.2
import "../../qml/pages/wallet"

TestCase {
    name: "AddressComponents"
    when: windowShown
    width: 720
    height: 480

    function findObject(root, objectName) {
        if (!root) {
            return null
        }
        if (root.objectName === objectName) {
            return root
        }
        const objectChildren = root.children || []
        for (let i = 0; i < objectChildren.length; ++i) {
            const childMatch = findObject(objectChildren[i], objectName)
            if (childMatch) {
                return childMatch
            }
        }
        const visualChildren = root.childItems || []
        for (let j = 0; j < visualChildren.length; ++j) {
            const visualMatch = findObject(visualChildren[j], objectName)
            if (visualMatch) {
                return visualMatch
            }
        }
        return null
    }

    Component {
        id: rowComponent
        AddressRow {
            width: 520
            address: "bcrt1qexampleaddress"
            ellipsesAddress: "bcrt1qexa ... dress"
            label: ""
            category: "single-use"
            currentBalance: "0.00000000"
            displayAmount: "₿ 0.0"
            hasAmount: false
            scriptType: "P2WPKH"
            isUsed: false
            canEditLabel: true
            canCreatePaymentRequest: true
        }
    }

    Component {
        id: detailsComponent
        AddressDetails {
            width: 480
            address: "bcrt1qexampleaddress"
            label: "Pizza"
            amount: "₿ 1.25000000"
            hasAmount: true
            category: "single-use"
            scriptType: "P2WPKH"
            used: false
        }
    }

    function test_row_uses_display_amount_and_emits_actions() {
        const row = createTemporaryObject(rowComponent, this)
        verify(row !== null)

        const amountText = findObject(row, "addressRowAmountText")
        verify(amountText !== null)
        compare(amountText.text, "₿ 0.0")

        let editRequested = false
        row.editLabelRequested.connect(function(address, label) {
            editRequested = address === "bcrt1qexampleaddress" && label === ""
        })
        const noteButton = findObject(row, "addressRowNoteButton")
        verify(noteButton !== null)
        noteButton.clicked()
        verify(editRequested)

        let menuRequested = false
        row.menuRequested.connect(function(address, label, amount, hasAmount, category, scriptType, used, menuButton) {
            menuRequested = address === "bcrt1qexampleaddress"
                && amount === "₿ 0.0"
                && hasAmount === false
                && category === "single-use"
                && scriptType === "P2WPKH"
                && used === false
                && menuButton !== null
        })
        const menuButton = findObject(row, "addressRowMenuButton")
        verify(menuButton !== null)
        menuButton.clicked()
        verify(menuRequested)
    }

    function test_details_has_no_status_row_and_emits_actions() {
        const details = createTemporaryObject(detailsComponent, this)
        verify(details !== null)

        let sawAmount = false
        let sawStatus = false
        function scanText(root) {
            if (!root) {
                return
            }
            if (root.text !== undefined) {
                sawAmount = sawAmount || root.text === "₿ 1.25000000"
                sawStatus = sawStatus || root.text === "Status"
            }
            const objectChildren = root.children || []
            for (let i = 0; i < objectChildren.length; ++i) {
                scanText(objectChildren[i])
            }
            const visualChildren = root.childItems || []
            for (let j = 0; j < visualChildren.length; ++j) {
                scanText(visualChildren[j])
            }
        }
        scanText(details)
        verify(sawAmount)
        verify(!sawStatus)

        let closed = false
        details.closeRequested.connect(function() { closed = true })
        const closeButton = findObject(details, "addressDetailsCloseButton")
        if (closeButton !== null) {
            closeButton.clicked()
            verify(closed)
        }
    }
}
