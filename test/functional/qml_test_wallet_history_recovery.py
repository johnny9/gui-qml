#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test automatic compact-filter recovery and its native progress UI.

Requires an automation-enabled bitcoin-core-app and bitcoind. The recovering
wallet is created after its receive and no-change spending blocks are pruned.
No external recovery script, wallet-code patch, or recovery-enabling flag is used.
"""

from decimal import Decimal
import os
import sys
import time
import traceback

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "bitcoin", "test", "functional"))

from qml_test_harness import dump_qml_tree
from qml_wallet_test_lib import WalletFlowHarness, rpc_call, wait_for_rpc
from test_framework.descriptors import descsum_create
from test_framework.extendedkey import ExtendedPrivateKey


WALLET = "recovery_test"
MINER = "recovery_miner"
SOURCE_WALLET = "recovery_source"
RECOVERY_ARGS = ["-prune=1", "-fastprune", "-blockfilterindex=1", "-dbcache=64"]


def wait_until(predicate, description, timeout=30):
    deadline = time.monotonic() + timeout
    last_error = None
    while time.monotonic() < deadline:
        try:
            if predicate():
                return
        except (RuntimeError, ConnectionError, OSError) as error:
            last_error = error
        time.sleep(0.05)
    raise AssertionError(f"Timed out waiting for {description}; last error: {last_error}")


def assert_rpc_error(port, method, expected, params=None, wallet=None):
    try:
        rpc_call(port, method, params, wallet)
    except RuntimeError as error:
        assert expected in str(error), f"Expected {expected!r}, got {error!r}"
        return
    raise AssertionError(f"{method} unexpectedly succeeded")


def sync_app(harness):
    target = rpc_call(harness.source_rpc_port, "getbestblockhash")
    wait_until(lambda: rpc_call(harness.gui_rpc_port, "getbestblockhash") == target,
               "GUI node chain synchronization")


def wait_for_filter_index(harness):
    height = rpc_call(harness.gui_rpc_port, "getblockcount")

    def ready():
        info = rpc_call(harness.gui_rpc_port, "getindexinfo").get("basic block filter index", {})
        return info.get("synced") and info.get("best_block_height", -1) >= height

    wait_until(ready, "basic block filter index synchronization")


def connect_archive(harness):
    rpc_call(harness.gui_rpc_port, "setnetworkactive", [True])
    rpc_call(harness.gui_rpc_port, "addnode", [f"127.0.0.1:{harness.source_p2p_port}", "onetry"])
    wait_until(lambda: any(int(peer["services"], 16) & 1
                           for peer in rpc_call(harness.gui_rpc_port, "getpeerinfo")),
               "archival peer connection")


def create_blank_wallet(harness, private_keys=False):
    rpc_call(harness.gui_rpc_port, "createwallet", {
        "wallet_name": WALLET,
        "disable_private_keys": not private_keys,
        "blank": True,
        "load_on_startup": True,
    })
    harness.driver.wait_for_property("walletBadge", "text", WALLET, timeout_ms=20000)
    harness.driver.wait_for_property("walletBadge", "loading", False, timeout_ms=20000)
    harness.driver.settle()


def wait_for_state(gui, expected, timeout_ms=30000):
    return gui.wait_for_property("walletHistoryRecoveryBanner", "recoveryState", expected,
                                 timeout_ms=timeout_ms)


def check_recovered(harness, received, spent, expected_fee):
    transactions = rpc_call(harness.gui_rpc_port, "listtransactions", wallet=WALLET)
    assert len(transactions) == 2, transactions
    assert {entry["txid"] for entry in transactions} == {received, spent}, transactions
    assert Decimal(str(rpc_call(harness.gui_rpc_port, "getbalance", wallet=WALLET))) == 0
    assert rpc_call(harness.gui_rpc_port, "listunspent", wallet=WALLET) == []
    spending = rpc_call(harness.gui_rpc_port, "gettransaction", [spent], wallet=WALLET)
    assert Decimal(str(spending["fee"])) == Decimal(str(expected_fee)), spending
    assert spending["confirmations"] > 0
    assert rpc_call(harness.gui_rpc_port, "getwalletinfo", wallet=WALLET)["txcount"] == 2


def prepare_history(harness, hardened=False):
    harness.start_source_node(extra_args=["-listen=1", f"-bind=127.0.0.1:{harness.source_p2p_port}", "-keypool=2"])
    rpc_call(harness.source_rpc_port, "createwallet", {"wallet_name": MINER})
    rpc_call(harness.source_rpc_port, "createwallet", {"wallet_name": SOURCE_WALLET, "blank": hardened})
    if hardened:
        descriptor = descsum_create(f"pkh({ExtendedPrivateKey.generate().to_string()}/0h/*h)")
        imported = rpc_call(harness.source_rpc_port, "importdescriptors",
                            [[{"desc": descriptor, "range": [0, 1], "timestamp": "now", "active": True}]],
                            wallet=SOURCE_WALLET)
        assert imported[0]["success"], imported
    mining_address = rpc_call(harness.source_rpc_port, "getnewaddress", wallet=MINER)
    receiving_address = rpc_call(harness.source_rpc_port, "getnewaddress", ["", "legacy"], wallet=SOURCE_WALLET)
    if not hardened:
        descriptor = rpc_call(harness.source_rpc_port, "getdescriptorinfo",
                              [f"addr({receiving_address})"])["descriptor"]

    harness.start_gui(extra_args=RECOVERY_ARGS + ["-keypool=2"])
    wait_for_rpc(harness.gui_rpc_port)
    connect_archive(harness)
    rpc_call(harness.source_rpc_port, "generatetoaddress", [101, mining_address])
    received = rpc_call(harness.source_rpc_port, "sendtoaddress", [receiving_address, 1.25], wallet=MINER)
    receive_block = rpc_call(harness.source_rpc_port, "generatetoaddress", [1, mining_address])[0]
    spent = rpc_call(harness.source_rpc_port, "sendall", {"recipients": [mining_address]}, wallet=SOURCE_WALLET)["txid"]
    spend_block = rpc_call(harness.source_rpc_port, "generatetoaddress", [1, mining_address])[0]
    expected_fee = rpc_call(harness.source_rpc_port, "gettransaction", [spent], wallet=SOURCE_WALLET)["fee"]
    rpc_call(harness.source_rpc_port, "generatetoaddress", [899, mining_address])
    sync_app(harness)
    wait_for_filter_index(harness)
    prune_height = rpc_call(harness.gui_rpc_port, "pruneblockchain", [1002])
    assert prune_height >= 103, prune_height
    for blockhash in (receive_block, spend_block):
        assert_rpc_error(harness.gui_rpc_port, "getblock", "pruned data", [blockhash])

    # Import with timestamp=now only after median time has advanced beyond the
    # old payments. Disconnect before the clock jump so it cannot time out an
    # existing P2P connection. The native recovery job must populate history.
    late_time = rpc_call(harness.source_rpc_port, "getblockheader",
                         [rpc_call(harness.source_rpc_port, "getbestblockhash")])["time"] + 10800
    rpc_call(harness.gui_rpc_port, "setnetworkactive", [False])
    wait_until(lambda: not rpc_call(harness.gui_rpc_port, "getpeerinfo"), "peer disconnection before clock change")
    wait_until(lambda: not rpc_call(harness.source_rpc_port, "getpeerinfo"), "archival peer disconnection before clock change")
    rpc_call(harness.source_rpc_port, "setmocktime", [late_time])
    rpc_call(harness.gui_rpc_port, "setmocktime", [late_time])
    rpc_call(harness.source_rpc_port, "generatetoaddress", [12, mining_address])
    connect_archive(harness)
    sync_app(harness)
    wait_for_filter_index(harness)
    rpc_call(harness.gui_rpc_port, "setnetworkactive", [False])
    wait_until(lambda: not rpc_call(harness.gui_rpc_port, "getpeerinfo"), "peer disconnection")
    print(f"  Historical receive and no-change spend pruned through height {prune_height}")
    return descriptor, received, spent, expected_fee, late_time


def case_recovery(harness, hardened=False):
    descriptor, received, spent, expected_fee, late_time = prepare_history(harness, hardened=hardened)
    create_blank_wallet(harness, private_keys=hardened)
    gui = harness.driver
    gui.wait_for_property("walletHistoryRecoveryBanner", "visible", True)
    wait_for_state(gui, "waiting-wallet")
    gui.wait_for_property("recoveryProgressBar", "visible", True)
    gui.wait_for_property("recoveryProgressBar", "indeterminate", True)
    gui.wait_for_property("walletHistoryRecoveryBanner", "recoveryIncomplete", True)
    gui.wait_for_property("walletBadgeRecoveryText", "visible", True)
    gui.save_screenshot(os.path.join(harness.tmpdir, "waiting-for-descriptors.png"))
    print("  Default recovery exposes a visible progress bar before descriptors are ready")

    gui.click("recoveryCancelButton")
    wait_for_state(gui, "cancelled")
    gui.wait_for_property("walletHistoryRecoveryBanner", "recoveryIncomplete", True)
    assert rpc_call(harness.gui_rpc_port, "listtransactions", wallet=WALLET) == []
    request = {"desc": descriptor, "timestamp": "now"}
    if hardened:
        request.update({"range": [0, 1], "active": True})
    imported = rpc_call(harness.gui_rpc_port, "importdescriptors", [[request]], wallet=WALLET)
    assert imported[0]["success"], imported
    if hardened:
        public = rpc_call(harness.gui_rpc_port, "listdescriptors", [False], wallet=WALLET)["descriptors"]
        assert len(public) == 1 and "/*h" in public[0]["desc"], public
    gui.click("recoveryRetryButton")
    wait_for_state(gui, "error")
    gui.wait_for_property("recoveryRetryButton", "visible", True)
    gui.wait_for_property("walletHistoryRecoveryBanner", "recoveryIncomplete", True)
    assert rpc_call(harness.gui_rpc_port, "listtransactions", wallet=WALLET) == []
    gui.save_screenshot(os.path.join(harness.tmpdir, "missing-peer.png"))
    print("  Cancellation and missing peers keep the wallet explicitly incomplete")

    connect_archive(harness)
    gui.click("recoveryRetryButton")
    wait_for_state(gui, "complete", timeout_ms=60000)
    gui.wait_for_property("walletHistoryRecoveryBanner", "recoveryIncomplete", False)
    gui.wait_for_property("walletBadgeRecoveryText", "visible", False)
    gui.wait_for_property("sendTabButton", "enabled", True)
    check_recovered(harness, received, spent, expected_fee)
    if hardened:
        public = rpc_call(harness.gui_rpc_port, "listdescriptors", [False], wallet=WALLET)["descriptors"]
        assert public[0]["range"][1] >= 2, public
    gui.click("activityTabButton")
    gui.wait_for_page("activityPage", timeout_ms=15000)
    gui.wait_for_property("activityFilterProxyModel", "transactionCount", 2, timeout_ms=15000)
    gui.save_screenshot(os.path.join(harness.tmpdir, "recovered-activity.png"))
    print("  Recovery restores both Activity transactions, fee, zero balance and no UTXOs")

    harness.restart_gui(extra_args=RECOVERY_ARGS + [f"-mocktime={late_time}", "-keypool=2"])
    wait_for_rpc(harness.gui_rpc_port)
    gui = harness.driver
    gui.wait_for_property("walletBadge", "text", WALLET, timeout_ms=20000)
    wait_for_state(gui, "complete", timeout_ms=60000)
    check_recovered(harness, received, spent, expected_fee)
    print("  Restart preserves history and automatic recovery does not duplicate entries")


def case_disabled(harness, extra_args):
    harness.start_gui(extra_args=extra_args)
    wait_for_rpc(harness.gui_rpc_port)
    create_blank_wallet(harness)
    wait_for_state(harness.driver, "disabled")
    harness.driver.wait_for_property("walletHistoryRecoveryBanner", "visible", False)
    assert rpc_call(harness.gui_rpc_port, "listtransactions", wallet=WALLET) == []
    print("  Recovery stays disabled unless pruning and the filter index are both enabled")


def case_encrypted_wallet(harness):
    passphrase = "compact-filter-recovery-test-passphrase"
    harness.start_gui(extra_args=RECOVERY_ARGS)
    wait_for_rpc(harness.gui_rpc_port)
    rpc_call(harness.gui_rpc_port, "generatetodescriptor", [1, "raw(51)"])
    wait_for_filter_index(harness)
    rpc_call(harness.gui_rpc_port, "createwallet", {
        "wallet_name": WALLET,
        "passphrase": passphrase,
        "load_on_startup": True,
    })
    gui = harness.driver
    gui.wait_for_property("walletBadge", "text", WALLET, timeout_ms=20000)
    gui.wait_for_property("walletBadge", "loading", False, timeout_ms=20000)
    wait_for_state(gui, "waiting-wallet")
    gui.wait_for_property("recoveryUnlockButton", "visible", True)
    gui.wait_for_property("recoveryUnlockButton", "enabled", True)
    gui.wait_for_property("walletHistoryRecoveryBanner", "recoveryIncomplete", True)
    assert rpc_call(harness.gui_rpc_port, "getwalletinfo", wallet=WALLET)["unlocked_until"] == 0

    gui.click("recoveryUnlockButton")
    gui.wait_for_property("recoveryUnlockPopup", "opened", True)
    gui.set_text("recoveryPassphraseInput", "incorrect-passphrase")
    gui.wait_for_property("recoveryUnlockConfirmButton", "enabled", True)
    gui.click("recoveryUnlockConfirmButton")
    gui.wait_for_property("recoveryUnlockPopup", "visible", False)
    wait_for_state(gui, "error")
    gui.wait_for_property("recoveryErrorText", "text", lambda text: "incorrect" in text.lower())
    gui.wait_for_property("walletHistoryRecoveryBanner", "recoveryIncomplete", True)
    gui.wait_for_property("recoveryPassphraseInput", "text", "")
    assert rpc_call(harness.gui_rpc_port, "getwalletinfo", wallet=WALLET)["unlocked_until"] == 0
    gui.save_screenshot(os.path.join(harness.tmpdir, "incorrect-recovery-passphrase.png"))

    gui.wait_for_property("recoveryUnlockButton", "enabled", True)
    gui.click("recoveryUnlockButton")
    gui.wait_for_property("recoveryUnlockPopup", "opened", True)
    gui.set_text("recoveryPassphraseInput", passphrase)
    gui.wait_for_property("recoveryUnlockConfirmButton", "enabled", True)
    gui.click("recoveryUnlockConfirmButton")
    gui.wait_for_property("recoveryUnlockPopup", "visible", False)
    wait_for_state(gui, "complete", timeout_ms=60000)
    gui.wait_for_property("walletHistoryRecoveryBanner", "recoveryIncomplete", False)
    gui.wait_for_property("recoveryErrorText", "text", "")
    info = rpc_call(harness.gui_rpc_port, "getwalletinfo", wallet=WALLET)
    assert info["unlocked_until"] == 0, info
    assert info["txcount"] == 0, info
    address = rpc_call(harness.gui_rpc_port, "getnewaddress", ["", "legacy"], wallet=WALLET)
    assert_rpc_error(harness.gui_rpc_port, "signmessage", "walletpassphrase",
                     [address, "Recovery must leave the wallet locked"], WALLET)
    print("  Incorrect password stays incomplete; correct password recovers and relocks the wallet")


def case_unload_waiting(harness):
    harness.start_gui(extra_args=RECOVERY_ARGS)
    wait_for_rpc(harness.gui_rpc_port)
    rpc_call(harness.gui_rpc_port, "generatetodescriptor", [1, "raw(51)"])
    wait_for_filter_index(harness)
    create_blank_wallet(harness)
    wait_for_state(harness.driver, "waiting-wallet")
    rpc_call(harness.gui_rpc_port, "unloadwallet", [WALLET])
    wait_until(lambda: not rpc_call(harness.gui_rpc_port, "listwallets"), "wallet unload")
    harness.driver.wait_for_property("walletHistoryRecoveryBanner", "visible", False)
    assert rpc_call(harness.gui_rpc_port, "getblockcount") == 1
    harness.driver.request_quit()
    assert harness.gui_process.wait(timeout=20) == 0
    print("  Wallet unload and application shutdown finish while recovery is waiting")


def run_case(name, body):
    harness = WalletFlowHarness(name, port_offset=0)
    try:
        print(f"[{name}] starting", flush=True)
        body(harness)
        print(f"[{name}] passed", flush=True)
        return 0
    except Exception:
        traceback.print_exc()
        if harness.driver:
            try:
                dump_qml_tree(harness.driver)
                harness.driver.save_screenshot(os.path.join(harness.tmpdir, "failure.png"))
            except Exception:
                pass
        print(f"Test data: {harness.tmpdir}", file=sys.stderr)
        os.environ["KEEP_QML_TEST_TMPDIR"] = "1"
        return 1
    finally:
        harness.stop()


def main():
    failures = run_case("wallet_history_recovery", case_recovery)
    failures += run_case("wallet_history_hardened", lambda harness: case_recovery(harness, hardened=True))
    failures += run_case("wallet_history_without_filter", lambda harness: case_disabled(
        harness, ["-prune=1", "-blockfilterindex=0"]))
    failures += run_case("wallet_history_without_pruning", lambda harness: case_disabled(
        harness, ["-prune=0", "-blockfilterindex=1"]))
    failures += run_case("wallet_history_encrypted", case_encrypted_wallet)
    failures += run_case("wallet_history_unload_waiting", case_unload_waiting)
    return int(failures != 0)


if __name__ == "__main__":
    sys.exit(main())
