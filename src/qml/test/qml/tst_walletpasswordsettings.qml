// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.2
import "../../qml/pages/wallet"

TestCase {
    name: "WalletPasswordSettings"
    when: windowShown
    width: 520
    height: 720

    Window {
        id: testWindow
        visible: true
        width: 520
        height: 720

        Item {
            id: testRoot
            anchors.fill: parent
            focus: true

            Loader {
                id: pageLoader
                anchors.fill: parent
            }
        }
    }

    Component {
        id: setPasswordComponent

        WalletPasswordSettings {
            updating: false
            width: 460
            height: 680
        }
    }

    Component {
        id: updatePasswordComponent

        WalletPasswordSettings {
            updating: true
            width: 460
            height: 680
        }
    }

    function createWalletPasswordSettingsPage(component) {
        pageLoader.sourceComponent = component
        const page = pageLoader.item
        verify(page !== null)
        testWindow.requestActivate()
        tryCompare(testWindow, "active", true)
        testRoot.forceActiveFocus()
        page.focusInitialPasswordField()
        wait(0)
        return page
    }

    function typeText(text) {
        for (let i = 0; i < text.length; ++i) {
            keyClick(text.charAt(i))
        }
    }

    function test_set_password_focuses_new_password_field() {
        const page = createWalletPasswordSettingsPage(setPasswordComponent)
        const currentPassword = findChild(page, "walletPasswordCurrentField")
        const newPassword = findChild(page, "walletPasswordNewField")
        verify(currentPassword !== null)
        verify(newPassword !== null)

        compare(page.updating, false)
        compare(currentPassword.focus, false)
        compare(newPassword.focus, true)

        typeText("newsecret")
        compare(currentPassword.text, "")
        compare(newPassword.text, "newsecret")
    }

    function test_update_password_focuses_current_password_field() {
        const page = createWalletPasswordSettingsPage(updatePasswordComponent)
        const currentPassword = findChild(page, "walletPasswordCurrentField")
        const newPassword = findChild(page, "walletPasswordNewField")
        verify(currentPassword !== null)
        verify(newPassword !== null)

        compare(page.updating, true)
        compare(currentPassword.focus, true)
        compare(newPassword.focus, false)

        typeText("currentsecret")
        compare(currentPassword.text, "currentsecret")
        compare(newPassword.text, "")
    }
}
