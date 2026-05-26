// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtTest 1.2

import "../../qml/components"

TestCase {
    name: "WalletLoadErrorPopup"
    when: windowShown
    width: 480
    height: 320

    Component {
        id: popupComponent
        WalletLoadErrorPopup {
            popupObjectName: "tstLoadErrorPopup"
            errorTextObjectName: "tstLoadErrorText"
            dismissButtonObjectName: "tstLoadErrorDismiss"
        }
    }

    function test_defaults_are_sane() {
        const popup = createTemporaryObject(popupComponent, this)
        verify(popup !== null)
        verify(!popup.opened)
        compare(popup.titleText, qsTr("Failed to open wallet"))
        compare(popup.dismissText, qsTr("OK"))
        compare(popup.errorText, "")
    }

    function test_error_text_property_round_trips() {
        const popup = createTemporaryObject(popupComponent, this)
        verify(popup !== null)
        popup.errorText = "Data is not in recognized format."
        compare(popup.errorText, "Data is not in recognized format.")
    }

    function test_open_and_close_toggle_visibility() {
        const popup = createTemporaryObject(popupComponent, this)
        verify(popup !== null)
        popup.errorText = "test error"
        popup.open()
        tryCompare(popup, "opened", true)
        popup.close()
        tryCompare(popup, "opened", false)
    }

    // The titleText/dismissText/popupObjectName/errorTextObjectName/
    // dismissButtonObjectName properties exist as declared aliases — accessing
    // them on the instance is enough to catch a typo or removed property in
    // future refactors.
    function test_required_alias_properties_are_present() {
        const popup = createTemporaryObject(popupComponent, this)
        verify(popup !== null)
        compare(popup.popupObjectName, "tstLoadErrorPopup")
        compare(popup.errorTextObjectName, "tstLoadErrorText")
        compare(popup.dismissButtonObjectName, "tstLoadErrorDismiss")
    }
}
