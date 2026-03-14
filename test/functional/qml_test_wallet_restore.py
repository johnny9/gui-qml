#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""End-to-end GUI test for restoring a wallet backup through the import flow."""

import os
import sys

from qml_driver import QmlDriverError
from qml_test_harness import dump_qml_tree
from qml_wallet_test_lib import WalletFlowHarness, rpc_call


def run_test():
    harness = WalletFlowHarness("qml_wallet_restore", port_offset=40)
    gui = None
    try:
        harness.start_source_node()
        wallet_name = "restore_source"
        backup_path = os.path.join(harness.tmpdir, f"{wallet_name}.bak")
        rpc_call(harness.source_rpc_port, "createwallet", {"wallet_name": wallet_name})
        rpc_call(harness.source_rpc_port, "backupwallet", [backup_path], wallet=wallet_name)
        harness.stop_source_node()

        harness.start_gui()
        gui = harness.driver
        gui.wait_for_property("walletBadge", "loading", False, timeout_ms=20000)
        assert gui.get_property("walletBadge", "noWalletLoaded") is True, "Expected no wallet to be loaded at startup"

        gui.click("walletBadge")
        try:
            gui.wait_for_property("walletSelectPopup", "opened", True, timeout_ms=2000)
            gui.click("walletSelectAddWalletButton")
        except QmlDriverError:
            pass
        gui.wait_for_property("importWalletButton", "visible", True, timeout_ms=10000)
        gui.click("importWalletButton")
        gui.wait_for_page("importWalletOptions", timeout_ms=10000)

        gui.set_text("importWalletPathField", backup_path)
        gui.click("importWalletNextButton")

        gui.wait_for_property("walletBadge", "text", wallet_name, timeout_ms=20000)
        restored_wallet_path = os.path.join(harness.gui_wallets_path, wallet_name)
        assert os.path.isdir(restored_wallet_path), f"Expected restored wallet directory at {restored_wallet_path}"

        print("Restore flow passed.")
        return 0
    except Exception as err:  # noqa: BLE001 - preserve failure context for functional test output
        print(f"\nFAILED: {err}", file=sys.stderr)
        import traceback
        traceback.print_exc()
        gui_output = harness.process_output(harness.gui_process)
        if gui_output:
            print("\n--- GUI process output ---", file=sys.stderr)
            print(gui_output, file=sys.stderr)
        if gui is not None:
            dump_qml_tree(gui)
        return 1
    finally:
        harness.stop()


if __name__ == "__main__":
    sys.exit(run_test())
