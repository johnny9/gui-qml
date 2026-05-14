#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""End-to-end GUI test for selector-driven legacy wallet migration."""

import argparse
import os
import re
import shutil
import sys
from datetime import datetime

from qml_test_harness import dump_qml_tree
from qml_wallet_test_lib import WalletFlowHarness, find_legacy_bitcoind, rpc_call


def parse_args():
    parser = argparse.ArgumentParser(
        description="Wallet migration GUI functional test",
        add_help=True,
    )
    parser.add_argument(
        "--save-screenshots",
        action="store_true",
        help="Save a PNG at each GUI checkpoint under test/artifacts/",
    )
    return parser.parse_args()


def make_screenshot_root():
    repo_root = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
    artifacts_root = os.path.join(repo_root, "test", "artifacts")
    timestamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    screenshot_root = os.path.join(artifacts_root, f"qml_test_wallet_migration-{timestamp}")
    os.makedirs(screenshot_root, exist_ok=True)
    return screenshot_root


class CheckpointRecorder:
    def __init__(self, save_screenshots, screenshot_root):
        self.save_screenshots = save_screenshots
        self.screenshot_root = screenshot_root
        self.index = 0

    def _sanitize_label(self, label):
        return re.sub(r"[^a-z0-9]+", "-", label.lower()).strip("-") or "checkpoint"

    def checkpoint(self, label, gui=None):
        if gui is not None:
            gui.settle()

        self.index += 1
        prefix = f"[qml_test_wallet_migration] checkpoint {self.index:02d}"
        print(f"{prefix}: {label}")
        if gui is None:
            return

        if not self.save_screenshots:
            return

        filename = f"{self.index:02d}-{self._sanitize_label(label)}.png"
        screenshot_path = os.path.join(self.screenshot_root, filename)
        screenshot = gui.save_screenshot(screenshot_path)
        print(
            f"{prefix}: screenshot saved to {screenshot['path']} "
            f"({screenshot['width']}x{screenshot['height']})"
        )


def run_test(*, save_screenshots=False, screenshot_root=None):
    harness = WalletFlowHarness("qml_wallet_migration", port_offset=50)
    checkpoints = CheckpointRecorder(save_screenshots, screenshot_root)
    gui = None
    try:
        legacy_binary = find_legacy_bitcoind()
        if not legacy_binary:
            print("SKIPPED: legacy bitcoind not found.")
            print("Set BITCOIND_LEGACY or provide releases/v28.0/bin/bitcoind to exercise this flow.")
            return 77

        checkpoints.checkpoint(f"legacy bitcoind located: {legacy_binary}")
        harness.start_source_node(
            binary=legacy_binary,
            extra_args=["-deprecatedrpc=create_bdb"],
        )
        checkpoints.checkpoint("legacy source node started")
        wallet_name = "legacy_flow"
        try:
            rpc_call(
                harness.source_rpc_port,
                "createwallet",
                {
                    "wallet_name": wallet_name,
                    "descriptors": False,
                },
            )
        except RuntimeError as err:
            print(f"SKIPPED: could not create a legacy wallet fixture: {err}")
            return 77

        checkpoints.checkpoint("legacy wallet fixture created")
        rpc_call(harness.source_rpc_port, "unloadwallet", [wallet_name])
        source_wallet_path = os.path.join(harness.source_wallets_path, wallet_name)
        target_wallet_path = os.path.join(harness.gui_wallets_path, wallet_name)
        os.makedirs(os.path.dirname(target_wallet_path), exist_ok=True)
        shutil.copytree(source_wallet_path, target_wallet_path)
        harness.stop_source_node()
        checkpoints.checkpoint(f"legacy wallet fixture copied to GUI datadir: {target_wallet_path}")

        harness.start_gui()
        gui = harness.driver
        gui.wait_for_property("walletBadge", "loading", False, timeout_ms=20000)
        gui.wait_for_property("walletBadge", "visible", True, timeout_ms=10000)
        checkpoints.checkpoint("GUI launched", gui)

        wallet_dir = rpc_call(harness.gui_rpc_port, "listwalletdir")["wallets"]
        legacy_entry = next((entry for entry in wallet_dir if entry["name"] == wallet_name), None)
        assert legacy_entry is not None, f"Expected {wallet_name} to be present in listwalletdir"
        assert legacy_entry["warnings"] == [
            "This wallet is a legacy wallet and will need to be migrated with migratewallet before it can be loaded"
        ], f"Expected migration warning for {wallet_name}"
        print(f"[qml_test_wallet_migration] legacy wallet dir entry: {legacy_entry}")
        checkpoints.checkpoint("legacy wallet warning verified", gui)

        gui.click("walletBadge")
        gui.wait_for_property("walletSelectPopup", "opened", True, timeout_ms=5000)
        gui.wait_for_property("walletSelectList", "count", 1, timeout_ms=5000)
        gui.wait_for_object(f"walletSelectItem_{wallet_name}", timeout_ms=5000)
        checkpoints.checkpoint("legacy wallet visible in selector", gui)
        gui.click(f"walletSelectItem_{wallet_name}")

        gui.wait_for_page("importWalletMigration", timeout_ms=10000)
        gui.wait_for_property("walletMigrationActionButton", "text", "Update wallet", timeout_ms=5000)
        checkpoints.checkpoint("migration confirmation page displayed", gui)
        gui.click("walletMigrationActionButton")
        gui.wait_for_property("walletMigrationActionButton", "text", "Done", timeout_ms=30000)
        checkpoints.checkpoint("wallet migration completed", gui)
        gui.click("walletMigrationActionButton")

        gui.wait_for_property("walletBadge", "text", wallet_name, timeout_ms=20000)
        checkpoints.checkpoint("migrated wallet loaded", gui)
        wallet_dat_path = os.path.join(target_wallet_path, "wallet.dat")
        with open(wallet_dat_path, "rb") as wallet_file:
            assert wallet_file.read(16) == b"SQLite format 3\x00", "Migrated wallet should be SQLite"

        wallet_info = rpc_call(harness.gui_rpc_port, "getwalletinfo", wallet=wallet_name)
        assert wallet_info["descriptors"] is True, "Migrated wallet should be descriptor based"
        assert wallet_info["format"] == "sqlite", "Migrated wallet should be stored as sqlite"
        print(f"[qml_test_wallet_migration] migrated wallet info: {wallet_info}")
        checkpoints.checkpoint("migration RPC verification passed", gui)

        print("Migration flow passed.")
        return 0
    except Exception as err:  # noqa: BLE001 - preserve failure context for functional test output
        print(f"\nFAILED: {err}", file=sys.stderr)
        import traceback
        traceback.print_exc()
        if gui is not None:
            try:
                checkpoints.checkpoint("failure state", gui)
            except Exception as screenshot_err:  # noqa: BLE001 - preserve original failure context
                print(f"[qml_test_wallet_migration] failed to save failure screenshot: {screenshot_err}", file=sys.stderr)
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
    args = parse_args()
    screenshot_root = None
    if args.save_screenshots:
        screenshot_root = make_screenshot_root()
        print(f"Checkpoint screenshots will be saved under: {screenshot_root}")
    sys.exit(run_test(save_screenshots=args.save_screenshots, screenshot_root=screenshot_root))
