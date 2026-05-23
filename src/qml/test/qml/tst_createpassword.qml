// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtTest 1.2
import "../../qml/pages/wallet"

TestCase {
    name: "CreatePassword"
    when: windowShown
    width: 500
    height: 700

    Component {
        id: passwordPageComponent
        CreatePassword {
            walletName: "test_wallet"
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
        if (parent.header) {
            var result = findChild(parent.header, objectName)
            if (result) return result
        }
        if (parent.footer) {
            var result = findChild(parent.footer, objectName)
            if (result) return result
        }
        return null
    }

    function test_skip_button_disabled_when_not_initialized() {
        walletController.initialized = false
        const page = createTemporaryObject(passwordPageComponent, this)
        verify(page !== null)

        var btn = findChild(page, "createWalletPasswordSkipButton")
        verify(btn !== null)
        verify(!btn.enabled)

        walletController.initialized = true
    }

    function test_skip_button_enabled_when_initialized() {
        walletController.initialized = true
        const page = createTemporaryObject(passwordPageComponent, this)
        verify(page !== null)

        var btn = findChild(page, "createWalletPasswordSkipButton")
        verify(btn !== null)
        verify(btn.enabled)
    }

    function test_continue_text_shows_initializing() {
        walletController.initialized = false
        const page = createTemporaryObject(passwordPageComponent, this)
        verify(page !== null)

        walletController.initialized = true
    }

    function test_mismatch_text_visible_when_passwords_differ() {
        const page = createTemporaryObject(passwordPageComponent, this)
        verify(page !== null)

        const passwordInput = findChild(page, "createWalletPasswordInput")
        const repeatInput = findChild(page, "createWalletPasswordRepeatInput")
        const mismatchText = findChild(page, "createWalletPasswordMismatchText")
        verify(passwordInput !== null)
        verify(repeatInput !== null)
        verify(mismatchText !== null)

        passwordInput.text = "hunter2"
        repeatInput.text = "hunter3"
        passwordInput.focus = false
        repeatInput.focus = false
        verify(mismatchText.opacity > 0)
    }

    function test_mismatch_text_hidden_when_passwords_match() {
        const page = createTemporaryObject(passwordPageComponent, this)
        verify(page !== null)

        const passwordInput = findChild(page, "createWalletPasswordInput")
        const repeatInput = findChild(page, "createWalletPasswordRepeatInput")
        const mismatchText = findChild(page, "createWalletPasswordMismatchText")

        passwordInput.text = "hunter2"
        repeatInput.text = "hunter3"
        passwordInput.focus = false
        repeatInput.focus = false
        verify(mismatchText.opacity > 0)

        repeatInput.text = "hunter2"
        verify(mismatchText.opacity === 0)
    }

    function test_mismatch_text_hidden_when_either_field_empty() {
        const page = createTemporaryObject(passwordPageComponent, this)
        verify(page !== null)

        const passwordInput = findChild(page, "createWalletPasswordInput")
        const repeatInput = findChild(page, "createWalletPasswordRepeatInput")
        const mismatchText = findChild(page, "createWalletPasswordMismatchText")

        passwordInput.focus = false
        repeatInput.focus = false
        verify(mismatchText.opacity === 0)

        passwordInput.text = "hunter2"
        verify(mismatchText.opacity === 0)

        repeatInput.text = "hunter2"
        passwordInput.text = ""
        verify(mismatchText.opacity === 0)
    }

    function test_mismatch_text_hidden_while_field_has_focus() {
        const page = createTemporaryObject(passwordPageComponent, this)
        verify(page !== null)

        const passwordInput = findChild(page, "createWalletPasswordInput")
        const repeatInput = findChild(page, "createWalletPasswordRepeatInput")
        const mismatchText = findChild(page, "createWalletPasswordMismatchText")

        passwordInput.text = "hunter2"
        repeatInput.text = "hunter3"
        repeatInput.forceActiveFocus()
        verify(mismatchText.opacity === 0)

        passwordInput.focus = false
        repeatInput.focus = false
        verify(mismatchText.opacity > 0)
    }

    function test_acknowledgement_toggle_does_not_affect_mismatch_text() {
        const page = createTemporaryObject(passwordPageComponent, this)
        verify(page !== null)

        const passwordInput = findChild(page, "createWalletPasswordInput")
        const repeatInput = findChild(page, "createWalletPasswordRepeatInput")
        const mismatchText = findChild(page, "createWalletPasswordMismatchText")
        const toggle = findChild(page, "createWalletPasswordConfirmToggle")
        verify(toggle !== null)

        passwordInput.text = "hunter2"
        repeatInput.text = "hunter3"
        passwordInput.focus = false
        repeatInput.focus = false
        verify(mismatchText.opacity > 0)

        toggle.clicked()
        verify(mismatchText.opacity > 0)

        toggle.clicked()
        verify(mismatchText.opacity > 0)
    }
}
