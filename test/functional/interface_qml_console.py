#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Keep secret command arguments out of a real GUI process's console and logs."""

from pathlib import Path
from qml_scenarios import run_scenarios
from qml_smoke_lib import create_wallet, stop_gui_checked, wallet_scenario
from qml_wallet_test_lib import rpc_call, wait_for_rpc


def console_privacy(harness, checkpoint, wallet_enabled):
    harness.start_gui(extra_args=[] if wallet_enabled else ["-disablewallet"])
    gui = harness.driver
    wait_for_rpc(harness.gui_rpc_port)
    name = "privacy_process"
    if wallet_enabled:
        create_wallet(harness, name, "owned-fixture-password")
    before = rpc_call(harness.gui_rpc_port, "listwallets") if wallet_enabled else None
    if gui.get_property("appWindow", "desktopWalletMode"):
        gui.click("desktopWalletSettingsTabButton")
        gui.wait_for_property("desktopWalletSettingsTabButton", "checked", True)
    else:
        gui.click("nodeSettingsButton")
    gui.click("settingsSidebar_rpc-console")
    gui.wait_for_page("rpcConsole")
    secret = "unique-process-secret-92"
    commands = [
        f"createwallet {name} false false {secret}",
        f"migratewallet {name} {secret}",
        f"addhdkey {secret}",
        f"walletpassphrase {secret} 10",
    ]
    for command in commands:
        gui.set_text("consoleInput", command)
        gui.invoke("rpcConsole", "submitCommand")
        gui.wait_for_property("rpcConsole", "outputCount", lambda count: count >= 2)
        gui.wait_for_property("rpcConsole", "executing", False, timeout_ms=30000)
        count = gui.get_property("rpcConsole", "outputCount")
        for row in range(count):
            gui.wait_for_property(f"consoleOutputArea_content_{row}", "text", lambda text: secret not in text)
        gui.invoke("rpcConsole", "clearInputOrOutput")
        gui.wait_for_property("rpcConsole", "outputCount", 0)
    checkpoint("secret-bearing requests and failures remain redacted")
    gui.set_text("consoleInput", "help getblockcount")
    gui.invoke("rpcConsole", "submitCommand")
    gui.wait_for_property("rpcConsole", "outputCount", 2)
    gui.wait_for_property("rpcConsole", "executing", False, timeout_ms=30000)
    gui.wait_for_property("consoleOutputArea_content_0", "text", lambda text: "help getblockcount" in text)
    gui.wait_for_property("consoleOutputArea_content_1", "text", lambda text: "Returns the height" in text)
    if wallet_enabled:
        assert rpc_call(harness.gui_rpc_port, "listwallets") == before
        assert rpc_call(harness.gui_rpc_port, "getwalletinfo", wallet=name)["unlocked_until"] == 0
    stop_gui_checked(harness)
    logs = list(Path(harness.gui_datadir).rglob("debug.log"))
    assert logs, "No Core debug log was inspected"
    assert all(secret not in log.read_text(encoding="utf8", errors="replace") for log in logs)
    checkpoint("ordinary arguments work; shutdown stderr, exit and Core diagnostics inspected")


if __name__ == "__main__":
    raise SystemExit(run_scenarios("RPC argument privacy", [
        wallet_scenario("console-secrets-enabled", 3, lambda harness, checkpoint: console_privacy(harness, checkpoint, True)),
        wallet_scenario("console-secrets-disabled", 2, lambda harness, checkpoint: console_privacy(harness, checkpoint, False)),
    ]))
