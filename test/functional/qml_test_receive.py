#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""End-to-end GUI test for the Receive Requests flow.

Covers issue #518 acceptance criteria: creating a receive request stores
a real address + metadata, exposes the QR through an explicit button, matching
request metadata remains reachable from fulfilled Activity transactions, and
history survives a GUI restart.

This test requires:
  - bitcoin-core-app built with -DENABLE_TEST_AUTOMATION=ON
  - bitcoind binary (searched alongside bitcoin-core-app, in build/bin/,
    or set BITCOIND env var)
"""

import os
import signal
import subprocess
import sys
import time
from urllib.parse import urlparse

from qml_driver import QmlDriver, QmlDriverError
from qml_test_harness import GUI_STARTUP_TIMEOUT, dump_qml_tree
from qml_wallet_test_lib import WalletFlowHarness, rpc_call, wait_for_rpc


WALLET_NAME = "receive_requests"


def _stop_gui(harness):
    if harness.driver:
        harness.driver.close()
        harness.driver = None
    if harness.gui_process and harness.gui_process.poll() is None:
        harness.gui_process.send_signal(signal.SIGTERM)
        try:
            harness.gui_process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            harness.gui_process.kill()
            harness.gui_process.wait()
    harness.gui_process = None


def _relaunch_gui(harness):
    env = dict(os.environ)
    env["QT_QPA_PLATFORM"] = "offscreen"
    os.makedirs(harness.config_home, exist_ok=True)
    env["XDG_CONFIG_HOME"] = harness.config_home
    args = [
        harness.gui_binary,
        f"-datadir={harness.gui_datadir}",
        f"-test-automation={harness.socket_path}",
        "-logtimemicros",
        "-debug",
        "-debugexclude=libevent",
        "-debugexclude=leveldb",
        "-nolisten",
    ]
    try:
        os.unlink(harness.socket_path)
    except FileNotFoundError:
        pass
    harness.gui_process = subprocess.Popen(
        args, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE
    )
    harness.driver = QmlDriver(harness.socket_path, timeout=GUI_STARTUP_TIMEOUT)


def _import_wallet(harness):
    harness.start_source_node()
    backup_path = os.path.join(harness.tmpdir, f"{WALLET_NAME}.bak")
    rpc_call(harness.source_rpc_port, "createwallet", {"wallet_name": WALLET_NAME})
    rpc_call(harness.source_rpc_port, "backupwallet", [backup_path], wallet=WALLET_NAME)
    harness.stop_source_node()

    harness.start_gui()
    gui = harness.driver
    gui.wait_for_property("walletBadge", "loading", False, timeout_ms=20000)
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
    gui.click("importWalletChooseFileButton")
    gui.wait_for_page("importWalletSuccessPage", timeout_ms=30000)
    gui.click("importWalletSuccessOverviewButton")
    gui.wait_for_property("walletBadge", "text", WALLET_NAME, timeout_ms=20000)
    return gui


def _open_receive(gui):
    gui.click("receiveTabButton")
    gui.wait_for_page("requestPaymentPage", timeout_ms=10000)


def _open_activity(gui):
    gui.click("activityTabButton")
    time.sleep(0.5)


def _address_from_bip21(uri):
    parsed = urlparse(uri)
    assert parsed.scheme == "bitcoin", f"Unexpected BIP21 scheme: {uri!r}"
    assert parsed.path, f"BIP21 URI missing address: {uri!r}"
    return parsed.path


def _mine_to_address(harness, address):
    wait_for_rpc(harness.gui_rpc_port)
    blocks = rpc_call(harness.gui_rpc_port, "generatetoaddress", [1, address])
    assert len(blocks) == 1, f"Expected one generated block, got {blocks!r}"
    block = rpc_call(harness.gui_rpc_port, "getblock", [blocks[0]])
    return block["tx"][0]


def _create_request(gui, amount, label, message):
    """Fill the form and click Generate QR. Stays on the same page (lock-on-generate)."""
    gui.set_text("requestPaymentAmountInput", amount)
    gui.set_text("requestPaymentYourNameInput", label)
    gui.set_text("requestPaymentMessageInput", message)
    before = gui.get_property("requestHistoryCount", "count")
    gui.click("requestPaymentGenerateButton")
    gui.wait_for_property("requestHistoryCount", "count", before + 1, timeout_ms=20000)


def _request_qr_payload(gui):
    gui.wait_for_property("requestPaymentQRButton", "visible", True, timeout_ms=10000)
    gui.click("requestPaymentQRButton")
    gui.wait_for_property("requestPaymentQRPopup", "opened", True, timeout_ms=10000)
    payload = gui.get_property("requestPaymentQRPopup", "code")
    gui.click("requestPaymentQRPopupCloseButton")
    gui.wait_for_property("requestPaymentQRPopup", "opened", False, timeout_ms=10000)
    return payload


def run_test():
    harness = WalletFlowHarness("qml_receive_requests", port_offset=70)
    try:
        print("[qml_receive_requests] starting")
        gui = _import_wallet(harness)
        _open_receive(gui)

        # Create first request — verify QR is available from the generated-state button.
        _create_request(gui, "0.0001", "Alice", "pizza")
        gui.wait_for_property("requestPaymentTitle", "text", "Payment request #1", timeout_ms=10000)
        qr_code = _request_qr_payload(gui)
        assert qr_code.startswith("bitcoin:"), f"QR payload missing BIP21 prefix: {qr_code!r}"
        assert "amount=0.00010000" in qr_code, f"QR payload missing amount: {qr_code!r}"
        assert "label=Alice" in qr_code, f"QR payload missing label: {qr_code!r}"
        assert "message=pizza" in qr_code, f"QR payload missing message: {qr_code!r}"
        print(f"[qml_receive_requests] created request with QR popup payload: {qr_code}")

        # Verify the "Generate QR" button now says "New request"
        button_text = gui.get_text("requestPaymentGenerateButton")
        assert "New request" in button_text, f"Expected 'New request' button, got: {button_text!r}"
        print("[qml_receive_requests] button text correctly shows 'New request'")

        # Click "New request" to reset to editing state
        gui.click("requestPaymentGenerateButton")
        time.sleep(0.5)
        gui.wait_for_property("requestPaymentTitle", "text", "Request a payment", timeout_ms=10000)
        button_text = gui.get_text("requestPaymentGenerateButton")
        assert "Generate payment request" in button_text, f"Expected 'Generate payment request' after clear, got: {button_text!r}"
        print("[qml_receive_requests] form reset to editing state")

        # Fulfill the request and verify the transaction links back to request metadata.
        request_address = _address_from_bip21(qr_code)
        txid = _mine_to_address(harness, request_address)
        print(f"[qml_receive_requests] mined block to request address; txid: {txid}")

        _open_activity(gui)
        activity_item_name = f"activityItem_{txid}"
        gui.wait_for_property(activity_item_name, "visible", True, timeout_ms=30000)
        gui.click(activity_item_name)
        gui.wait_for_property("activityDetailsPaymentRequestsSection", "visible", True, timeout_ms=10000)
        gui.wait_for_property("activityDetailsPaymentRequest_0", "visible", True, timeout_ms=10000)
        gui.click("activityDetailsPaymentRequest_0")
        gui.wait_for_page("paymentRequestDetailPage", timeout_ms=10000)
        gui.wait_for_property("paymentRequestDetailEdit", "visible", True, timeout_ms=10000)
        gui.click("paymentRequestDetailEdit")
        gui.wait_for_page("requestPaymentPage", timeout_ms=10000)
        gui.wait_for_property("requestPaymentTitle", "text", "Payment request #1", timeout_ms=10000)
        print("[qml_receive_requests] fulfilled transaction links to payment request detail")

        # Restart and verify persistence
        _stop_gui(harness)
        time.sleep(0.5)
        _relaunch_gui(harness)
        gui = harness.driver
        gui.wait_for_property("walletBadge", "text", WALLET_NAME, timeout_ms=30000)
        _open_receive(gui)
        gui.wait_for_property("requestHistoryCount", "count", 1, timeout_ms=20000)
        print("[qml_receive_requests] history persisted across restart")

        # Create a second request
        _create_request(gui, "0.005", "Bob", "coffee")
        gui.wait_for_property("requestPaymentTitle", "text", "Payment request #2", timeout_ms=10000)
        qr_code2 = _request_qr_payload(gui)
        assert "amount=0.00500000" in qr_code2, f"Second QR missing amount: {qr_code2!r}"
        assert "label=Bob" in qr_code2, f"Second QR missing label: {qr_code2!r}"
        gui.wait_for_property("requestHistoryCount", "count", 2, timeout_ms=20000)
        print("[qml_receive_requests] second request created, history count is 2")

        print("[qml_receive_requests] PASSED")
        return 0
    except Exception as err:  # noqa: BLE001 - preserve failure context
        print(f"\nFAILED [qml_receive_requests]: {err}", file=sys.stderr)
        import traceback
        traceback.print_exc()
        gui = harness.driver
        if gui is not None:
            dump_qml_tree(gui)
        proc = harness.gui_process
        _stop_gui(harness)
        gui_output = harness.process_output(proc)
        if gui_output:
            print("\n--- GUI process output ---", file=sys.stderr)
            print(gui_output, file=sys.stderr)
        return 1
    finally:
        harness.stop()


if __name__ == "__main__":
    sys.exit(run_test())
