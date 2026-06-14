// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtTest 1.2
import "../../qml/pages/wallet"

TestCase {
    name: "CreateBackup"
    when: windowShown
    width: 520
    height: 720

    Component {
        id: createBackupComponent

        CreateBackup {
            width: 460
            height: 680
        }
    }

    function init() {
        walletController.reset()
    }

    function createBackupPage() {
        const page = createTemporaryObject(createBackupComponent, this)
        verify(page !== null)
        return page
    }

    function test_view_file_opens_wallet_location() {
        const page = createBackupPage()
        const viewFileButton = findChild(page, "createWalletBackupViewFileButton")
        verify(viewFileButton !== null)
        const errorText = findChild(page, "createWalletBackupErrorText")
        verify(errorText !== null)

        viewFileButton.clicked()

        compare(walletController.openSelectedWalletLocationCalls, 1)
        compare(errorText.text, "")
    }

    function test_view_file_shows_open_failure() {
        const page = createBackupPage()
        walletController.setOpenSelectedWalletLocationResult(false, "Could not open test wallet location.")
        const viewFileButton = findChild(page, "createWalletBackupViewFileButton")
        verify(viewFileButton !== null)
        const errorText = findChild(page, "createWalletBackupErrorText")
        verify(errorText !== null)

        viewFileButton.clicked()

        compare(walletController.openSelectedWalletLocationCalls, 1)
        compare(errorText.text, "Could not open test wallet location.")
    }

    function test_view_file_shows_missing_wallet_location() {
        const page = createBackupPage()
        walletController.setOpenSelectedWalletLocationResult(false, "No wallet file is available to view.")
        const viewFileButton = findChild(page, "createWalletBackupViewFileButton")
        verify(viewFileButton !== null)
        const errorText = findChild(page, "createWalletBackupErrorText")
        verify(errorText !== null)

        viewFileButton.clicked()

        compare(walletController.openSelectedWalletLocationCalls, 1)
        compare(errorText.text, "No wallet file is available to view.")
    }
}
