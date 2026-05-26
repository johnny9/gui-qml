// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtTest 1.2

import "../../qml/components"

TestCase {
    name: "WalletCloseConfirmationPopup"
    when: windowShown
    width: 480
    height: 320

    Component {
        id: popupComponent
        WalletCloseConfirmationPopup {
            popupObjectName: "tstCloseConfirmationPopup"
            cancelButtonObjectName: "tstCloseConfirmationCancel"
            confirmButtonObjectName: "tstCloseConfirmationConfirm"
        }
    }

    function findChild(parent, objectName) {
        if (!parent) return null
        if (parent.objectName === objectName) return parent
        if (parent.children) {
            for (var i = 0; i < parent.children.length; i++) {
                var result = findChild(parent.children[i], objectName)
                if (result) return result
            }
        }
        if (parent.contentItem) {
            var result = findChild(parent.contentItem, objectName)
            if (result) return result
        }
        return null
    }

    function test_defaults_are_sane() {
        const popup = createTemporaryObject(popupComponent, this)
        verify(popup !== null)
        verify(!popup.opened)
        compare(popup.walletName, "")
    }

    function test_wallet_name_property_round_trips() {
        const popup = createTemporaryObject(popupComponent, this)
        verify(popup !== null)
        popup.walletName = "savings"
        compare(popup.walletName, "savings")
    }

    function test_open_and_close_toggle_visibility() {
        const popup = createTemporaryObject(popupComponent, this)
        verify(popup !== null)
        popup.walletName = "savings"
        popup.open()
        tryCompare(popup, "opened", true)
        popup.close()
        tryCompare(popup, "opened", false)
    }

    function test_confirm_button_emits_confirmed_and_closes() {
        const popup = createTemporaryObject(popupComponent, this)
        verify(popup !== null)
        const spy = signalSpyComponent.createObject(this, {target: popup, signalName: "confirmed"})
        popup.walletName = "savings"
        popup.open()
        tryCompare(popup, "opened", true)

        const confirmBtn = findChild(popup, "tstCloseConfirmationConfirm")
        verify(confirmBtn !== null)
        confirmBtn.clicked()

        compare(spy.count, 1)
        tryCompare(popup, "opened", false)
    }

    function test_cancel_button_closes_without_confirmed() {
        const popup = createTemporaryObject(popupComponent, this)
        verify(popup !== null)
        const spy = signalSpyComponent.createObject(this, {target: popup, signalName: "confirmed"})
        popup.walletName = "savings"
        popup.open()
        tryCompare(popup, "opened", true)

        const cancelBtn = findChild(popup, "tstCloseConfirmationCancel")
        verify(cancelBtn !== null)
        cancelBtn.clicked()

        compare(spy.count, 0)
        tryCompare(popup, "opened", false)
    }

    function test_required_alias_properties_are_present() {
        const popup = createTemporaryObject(popupComponent, this)
        verify(popup !== null)
        compare(popup.popupObjectName, "tstCloseConfirmationPopup")
        compare(popup.cancelButtonObjectName, "tstCloseConfirmationCancel")
        compare(popup.confirmButtonObjectName, "tstCloseConfirmationConfirm")
    }

    Component {
        id: signalSpyComponent
        SignalSpy {}
    }
}
