#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test QML shutdown while startup wallets are still loading."""

import signal
import subprocess
import sys
import time

from qml_wallet_test_lib import WalletFlowHarness, find_bitcoind, rpc_call, wait_for_rpc


STARTUP_WALLET_COUNT = 5
SHUTDOWN_TIMEOUT_SECS = 30


def create_startup_wallets(harness):
    bitcoind = find_bitcoind()
    process = subprocess.Popen(
        [bitcoind, f"-datadir={harness.gui_datadir}"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    try:
        wait_for_rpc(harness.gui_rpc_port)
        for index in range(STARTUP_WALLET_COUNT):
            rpc_call(
                harness.gui_rpc_port,
                "createwallet",
                {"wallet_name": f"shutdown_probe_{index}"},
            )
        rpc_call(harness.gui_rpc_port, "stop")
        process.wait(timeout=SHUTDOWN_TIMEOUT_SECS)
    except Exception:
        if process.poll() is None:
            process.send_signal(signal.SIGTERM)
            process.wait(timeout=10)
        raise


def wait_for_process_exit(process):
    deadline = time.time() + SHUTDOWN_TIMEOUT_SECS
    while time.time() < deadline:
        if process.poll() is not None:
            return process.returncode
        time.sleep(0.25)
    raise TimeoutError("GUI did not exit after closing during startup wallet loading")


def run_tests():
    harness = WalletFlowHarness("qml_shutdown", 970)
    try:
        create_startup_wallets(harness)

        startup_wallet_args = [
            f"-wallet=shutdown_probe_{index}"
            for index in range(STARTUP_WALLET_COUNT)
        ]
        harness.start_gui(extra_args=startup_wallet_args)

        gui = harness.driver
        gui.close_window()
        gui.wait_for_page("Shutdown", timeout_ms=5000)

        return_code = wait_for_process_exit(harness.gui_process)
        assert return_code == 0, f"Expected GUI exit code 0, got {return_code}"

        print("Shutdown during startup wallet loading completed successfully.")
    except Exception as err:
        print(f"\nFAILED: {err}", file=sys.stderr)
        if harness.gui_process:
            print(harness.process_output(harness.gui_process), file=sys.stderr)
        raise
    finally:
        harness.stop()


if __name__ == "__main__":
    run_tests()
