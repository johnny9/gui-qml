// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.2
import "../../qml/controls"
import "../../qml/pages/wallet"
import "../../qml/pages/settings" as SettingsPages

TestCase {
    function init() {
        testSetup.failOnWarnings()
    }

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
            wallet: testWalletModel
            width: 460
            height: 680
        }
    }

    Component {
        id: updatePasswordComponent

        WalletPasswordSettings {
            updating: true
            wallet: testWalletModel
            width: 460
            height: 680
        }
    }

    Component {
        id: walletSettingsComponent
        SettingsPages.WalletSectionPage {
            wallet: testWalletModel
            width: 460
            height: 680
        }
    }

    function cleanup() {
        pageLoader.sourceComponent = null
        testWalletModel.isEncrypted = false
        testWalletModel.resetWalletSettingsTestState()
    }

    function test_set_password_explains_older_backup_coverage() {
        const page = createWalletPasswordSettingsPage(setPasswordComponent)
        const intro = findChild(page, "walletPasswordIntroTextDescription")
        verify(intro.text.indexOf("receiving and change keys") !== -1)
        verify(intro.text.indexOf("earlier backups") !== -1)
        verify(intro.text.indexOf("fresh backup") !== -1)
    }

    function test_encrypted_wallet_keeps_recovery_guidance_and_backup_action() {
        testWalletModel.isEncrypted = true
        pageLoader.sourceComponent = walletSettingsComponent
        const page = pageLoader.item
        verify(page !== null)
        const action = findChild(page, "walletBackupRow")
        verify(action !== null)
        compare(action.visible, true)
        verify(action.description.indexOf("earlier backups") !== -1)
        verify(action.description.indexOf("Keep your older backups") !== -1)
        const path = findChild(page, "walletSettingsBackupPathField")
        path.text = "/tmp/recovery-fresh.bak"
        action.clicked()
        compare(testWalletModel.backupWalletCalls, 1)
        compare(testWalletModel.lastBackupPath, "/tmp/recovery-fresh.bak")
        testWalletModel.isEncrypted = false
        compare(action.description, "")
        page.wallet = null
        compare(action.visible, false)
        compare(action.description, "")
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
        const intro = findChild(page, "walletPasswordIntroTextDescription")
        const newEntry = findChild(page, "walletPasswordNewEntry")
        const formSection = findChild(page, "walletPasswordFormSection")
        const formCard = findChild(page, "walletPasswordFormSectionCard")
        const saveButton = findChild(page, "walletPasswordSaveButton")
        verify(currentPassword !== null)
        verify(newPassword !== null)
        verify(intro !== null)
        verify(newEntry !== null)
        verify(formSection !== null)
        verify(formCard !== null)
        verify(saveButton !== null)

        compare(page.updating, false)
        compare(page.maximumContentWidth, 640)
        verify(intro.text.indexOf("password encrypts the wallet") !== -1)
        compare(formCard.radius, 16)
        compare(newPassword.background.color, Theme.color.neutral2)
        compare(currentPassword.focus, false)
        compare(newPassword.focus, true)
        compare(newPassword.echoMode, TextInput.Password)

        typeText("newsecret")
        compare(currentPassword.text, "")
        compare(newPassword.text, "newsecret")
    }

    function test_update_password_focuses_current_password_field() {
        const page = createWalletPasswordSettingsPage(updatePasswordComponent)
        const currentPassword = findChild(page, "walletPasswordCurrentField")
        const newPassword = findChild(page, "walletPasswordNewField")
        const currentEntry = findChild(page, "walletPasswordCurrentEntry")
        const currentLabel = findChild(page, "walletPasswordCurrentLabel")
        const currentToggle = findChild(page, "walletPasswordCurrentVisibilityToggle")
        verify(currentPassword !== null)
        verify(newPassword !== null)
        verify(currentEntry !== null)
        verify(currentLabel !== null)
        verify(currentToggle !== null)

        compare(page.updating, true)
        compare(currentLabel.text, "Current password")
        compare(currentPassword.focus, true)
        compare(newPassword.focus, false)
        compare(currentPassword.echoMode, TextInput.Password)

        currentToggle.clicked()
        compare(currentEntry.passwordVisible, true)
        compare(currentPassword.echoMode, TextInput.Normal)

        typeText("currentsecret")
        compare(currentPassword.text, "currentsecret")
        compare(newPassword.text, "")
    }

    function test_update_password_result_uses_toast_without_leaving_page() {
        const page = createWalletPasswordSettingsPage(updatePasswordComponent)
        const banner = findChild(page, "walletPasswordResultBanner")
        const resultText = findChild(page, "walletPasswordErrorText")
        verify(banner !== null)
        verify(resultText !== null)
        compare(banner.visible, false)

        let savedCount = 0
        page.saved.connect(function() { savedCount += 1 })

        page.errorText = "The current password is incorrect."
        tryCompare(banner, "visible", true)
        compare(resultText.text, "The current password is incorrect.")
        compare(banner.iconSource.toString(), "image://images/info-filled")
        compare(banner.textColor, Theme.color.red)

        page.clearResult()
        page.handleSaveResult(true)
        compare(savedCount, 0)
        compare(page.successText, "Password updated successfully.")
        tryCompare(banner, "visible", true)
        compare(resultText.text, "Password updated successfully.")
        compare(banner.iconSource.toString(), "image://images/check")
        compare(banner.textColor, Theme.color.green)
    }
}
