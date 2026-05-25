#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Compatibility smoke test for a bitcoin-qt wallet profile opened by QML."""

import os
import json
import signal
import subprocess
import sys
import time

from qml_test_harness import complete_onboarding
from qml_wallet_test_lib import WalletFlowHarness, rpc_call, wait_for_rpc


WALLET_NAME = "qt_compat_wallet"


def find_bitcoin_qt():
    explicit = os.getenv("BITCOIN_QT")
    if explicit and os.path.isfile(explicit):
        return explicit

    repo_root = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
    candidates = [
        os.path.join(repo_root, "build", "bin", "bitcoin-qt"),
        os.path.join(repo_root, "releases", "v28.0", "bin", "bitcoin-qt"),
    ]
    for candidate in candidates:
        if os.path.isfile(candidate):
            return candidate

    raise FileNotFoundError("bitcoin-qt not found. Set BITCOIN_QT or provide releases/v28.0/bin/bitcoin-qt.")


def start_bitcoin_qt(harness, bitcoin_qt):
    env = dict(os.environ)
    env["QT_QPA_PLATFORM"] = "offscreen"
    args = [
        bitcoin_qt,
        f"-datadir={harness.gui_datadir}",
        "-regtest",
        "-server=1",
        "-connect=0",
        "-listen=0",
        "-listenonion=0",
        "-logtimemicros",
    ]
    print(f"Starting bitcoin-qt: {' '.join(args)}")
    process = subprocess.Popen(args, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        wait_for_rpc(harness.gui_rpc_port)
    except Exception:
        process.send_signal(signal.SIGTERM)
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
        stdout = process.stdout.read().decode("utf-8", errors="replace") if process.stdout else ""
        stderr = process.stderr.read().decode("utf-8", errors="replace") if process.stderr else ""
        if stdout:
            print(f"\n--- bitcoin-qt stdout ---\n{stdout}", file=sys.stderr)
        if stderr:
            print(f"\n--- bitcoin-qt stderr ---\n{stderr}", file=sys.stderr)
        raise
    return process


def stop_bitcoin_qt(harness, process):
    if not process or process.poll() is not None:
        return
    try:
        rpc_call(harness.gui_rpc_port, "stop")
    except Exception:
        process.send_signal(signal.SIGTERM)
    try:
        process.wait(timeout=20)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait()


def create_wallet_with_bitcoin_qt(harness, bitcoin_qt):
    process = start_bitcoin_qt(harness, bitcoin_qt)
    try:
        rpc_call(
            harness.gui_rpc_port,
            "createwallet",
            {
                "wallet_name": WALLET_NAME,
                "descriptors": True,
                "load_on_startup": True,
            },
        )
        wallets = rpc_call(harness.gui_rpc_port, "listwallets")
        assert WALLET_NAME in wallets, f"bitcoin-qt did not load created wallet: {wallets!r}"
        print(f"  bitcoin-qt created and loaded wallet {WALLET_NAME!r}")
    finally:
        stop_bitcoin_qt(harness, process)


def verify_bitcoin_qt_reopens_wallet(harness, bitcoin_qt):
    process = start_bitcoin_qt(harness, bitcoin_qt)
    try:
        deadline = time.time() + 30
        wallets = []
        while time.time() < deadline:
            wallets = rpc_call(harness.gui_rpc_port, "listwallets")
            if WALLET_NAME in wallets:
                print("  bitcoin-qt reopened wallet after QML onboarding")
                return
            time.sleep(0.25)
        raise AssertionError(f"bitcoin-qt did not reopen wallet after QML onboarding: {wallets!r}")
    finally:
        stop_bitcoin_qt(harness, process)


def run_test():
    bitcoin_qt = find_bitcoin_qt()
    harness = WalletFlowHarness("qml_bitcoin_qt_compatibility", port_offset=120)
    try:
        create_wallet_with_bitcoin_qt(harness, bitcoin_qt)

        harness.start_gui(reset_gui_settings=False, auto_onboard=False)
        gui = harness.driver
        gui.wait_for_page("onboardingCover", timeout_ms=10000)
        print("  QML showed onboarding for existing bitcoin-qt profile")
        complete_onboarding(gui)
        gui.wait_for_property("walletBadge", "loading", False, timeout_ms=30000)
        gui.wait_for_property("walletBadge", "text", WALLET_NAME, timeout_ms=30000)

        wallets = rpc_call(harness.gui_rpc_port, "listwallets")
        assert WALLET_NAME in wallets, f"QML node did not keep bitcoin-qt wallet loaded: {wallets!r}"
        with open(harness.gui_settings_path, "r", encoding="utf8") as settings_file:
            settings = json.load(settings_file)
        assert settings.get("qml_onboarded") is True, "QML onboarding marker was not written"
        print("  QML preserved and loaded the bitcoin-qt wallet")

        harness.stop_gui()
        verify_bitcoin_qt_reopens_wallet(harness, bitcoin_qt)
    except Exception as err:
        print(f"\nFAILED: {err}", file=sys.stderr)
        print(harness.process_output(harness.gui_process), file=sys.stderr)
        raise
    finally:
        harness.stop()

    print("\n" + "=" * 60)
    print("bitcoin-qt compatibility test PASSED")
    print("=" * 60)


if __name__ == "__main__":
    run_test()
