// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtTest 1.2
import "../../qml/pages/wallet"

TestCase {
    name: "WalletSettings"
    when: windowShown
    width: 520
    height: 720

    Component {
        id: walletSettingsComponent

        WalletSettings {
            width: 460
            height: 680
        }
    }

    function init() {
        testWalletModel.resetWalletSettingsTestState()
    }

    function createWalletSettingsPage() {
        const page = createTemporaryObject(walletSettingsComponent, this)
        verify(page !== null)
        return page
    }

    function test_wallet_settings_password_and_backup_actions_remain_available() {
        const page = createWalletSettingsPage()
        let passwordRequests = 0
        let addressRequests = 0
        page.passwordRequested.connect(function() {
            ++passwordRequests
        })
        page.addressesRequested.connect(function() {
            ++addressRequests
        })

        const addressesRow = findChild(page, "settingsAddresses")
        verify(addressesRow !== null)
        addressesRow.clicked()
        compare(addressRequests, 1)

        const passwordRow = findChild(page, "walletSettingsPasswordRow")
        verify(passwordRow !== null)
        passwordRow.clicked()
        compare(passwordRequests, 1)

        const backupPathField = findChild(page, "walletSettingsBackupPathField")
        verify(backupPathField !== null)
        backupPathField.text = "/tmp/qml-wallet-settings-test.bak"

        const backupRow = findChild(page, "walletSettingsBackupRow")
        verify(backupRow !== null)
        backupRow.clicked()
        compare(testWalletModel.backupWalletCalls, 1)
        compare(testWalletModel.lastBackupPath, "/tmp/qml-wallet-settings-test.bak")
    }

    function test_wallet_settings_hides_password_action_for_external_signer_wallet() {
        testWalletModel.setExternalSignerWalletSettingsTestState()
        const page = createWalletSettingsPage()

        const passwordRow = findChild(page, "walletSettingsPasswordRow")
        verify(passwordRow !== null)
        compare(passwordRow.visible, false)

        const backupRow = findChild(page, "walletSettingsBackupRow")
        verify(backupRow !== null)
    }
}
