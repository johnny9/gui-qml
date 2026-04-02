#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""End-to-end GUI test for send preview fee parity."""

from decimal import Decimal, InvalidOperation
import re
import sys
import time

from qml_test_harness import dump_qml_tree
from qml_wallet_test_lib import WalletFlowHarness, rpc_call


SEND_AMOUNT = "1.00000000"
GUI_WALLET_NAME = "send_flow_wallet"
RECEIVER_WALLET_NAME = "send_flow_receiver"
LOW_FEE_OPTION_INDEX = 2
LOW_FEE_TARGET_BLOCKS = 6


def wait_until(predicate, *, timeout=20, interval=0.1, description="condition"):
    deadline = time.time() + timeout
    last_error = None
    while time.time() < deadline:
        try:
            if predicate():
                return
        except Exception as err:  # noqa: BLE001 - retry helper preserves latest error context
            last_error = err
        time.sleep(interval)
    if last_error is not None:
        raise AssertionError(f"Timed out waiting for {description}: {last_error}") from last_error
    raise AssertionError(f"Timed out waiting for {description}")


def wait_for_non_empty_text(gui, object_name, *, timeout=20):
    value = ""

    def has_text():
        nonlocal value
        value = gui.get_text(object_name).strip()
        return bool(value)

    wait_until(has_text, timeout=timeout, description=f"{object_name} text")
    return value


def btc_text_to_sats(text):
    match = re.search(r"([0-9]+(?:\.[0-9]+)?)", text.replace(",", ""))
    if match is None:
        raise AssertionError(f"Could not parse BTC amount from {text!r}")
    try:
        return int((Decimal(match.group(1)) * Decimal("100000000")).to_integral_value())
    except InvalidOperation as err:
        raise AssertionError(f"Invalid BTC amount {text!r}") from err


def sat_text_to_sats(text):
    match = re.search(r"(-?[0-9]+)", text.replace(",", ""))
    if match is None:
        raise AssertionError(f"Could not parse satoshi amount from {text!r}")
    return int(match.group(1))


def wait_for_wallet_balance(port, wallet_name, *, minimum_balance):
    balance = Decimal("0")

    def has_balance():
        nonlocal balance
        balance = Decimal(str(rpc_call(port, "getbalance", wallet=wallet_name)))
        return balance >= minimum_balance

    wait_until(
        has_balance,
        timeout=30,
        interval=0.25,
        description=f"{wallet_name} balance >= {minimum_balance}",
    )
    return balance


def wait_for_single_mempool_tx(port):
    txids = []

    def has_tx():
        nonlocal txids
        txids = rpc_call(port, "getrawmempool")
        return len(txids) == 1

    wait_until(has_tx, timeout=20, interval=0.25, description="single mempool transaction")
    return txids[0]


def run_test():
    harness = WalletFlowHarness("qml_test_send_receive", port_offset=60)
    gui = None
    try:
        harness.start_source_node()
        rpc_call(harness.source_rpc_port, "createwallet", {"wallet_name": RECEIVER_WALLET_NAME})
        receiver_address = rpc_call(
            harness.source_rpc_port,
            "getnewaddress",
            wallet=RECEIVER_WALLET_NAME,
        )

        harness.start_gui()
        gui = harness.driver
        gui.wait_for_property("walletBadge", "loading", False, timeout_ms=20000)
        gui.wait_for_property("walletBadge", "visible", True, timeout_ms=10000)
        assert gui.get_property("walletBadge", "noWalletLoaded") is True, "Expected no wallet at startup"

        rpc_call(harness.gui_rpc_port, "createwallet", {"wallet_name": GUI_WALLET_NAME})
        gui.wait_for_property("walletBadge", "text", GUI_WALLET_NAME, timeout_ms=20000)
        gui.wait_for_property("walletBadge", "noWalletLoaded", False, timeout_ms=10000)

        mining_address = rpc_call(
            harness.gui_rpc_port,
            "getnewaddress",
            wallet=GUI_WALLET_NAME,
        )
        rpc_call(harness.gui_rpc_port, "generatetoaddress", [101, mining_address])
        wait_for_wallet_balance(
            harness.gui_rpc_port,
            GUI_WALLET_NAME,
            minimum_balance=Decimal("50"),
        )

        gui.click("walletSendTabButton")
        gui.wait_for_page("walletSendPage", timeout_ms=10000)

        gui.set_text("sendAddressInput", receiver_address)
        gui.set_text("sendAmountInput", SEND_AMOUNT)
        gui.wait_for_property("sendContinueButton", "enabled", True, timeout_ms=20000)

        gui.click("feeSelectionDropdownButton")
        gui.wait_for_property("feeSelectionPopup", "opened", True, timeout_ms=5000)
        low_fee_option_estimate = wait_for_non_empty_text(gui, f"feeSelectionOptionEstimate{LOW_FEE_OPTION_INDEX}")
        gui.click(f"feeSelectionOption{LOW_FEE_OPTION_INDEX}")
        gui.wait_for_property("feeSelectionPopup", "opened", False, timeout_ms=5000)
        gui.wait_for_property("feeSelectionControl", "selectedTarget", LOW_FEE_TARGET_BLOCKS, timeout_ms=5000)
        gui.wait_for_property("feeSelectionEstimateLabel", "text", low_fee_option_estimate, timeout_ms=20000)

        estimated_fee_text = gui.get_text("feeSelectionEstimateLabel")
        estimated_fee_sats = btc_text_to_sats(estimated_fee_text)
        assert estimated_fee_sats > 0, f"Expected a positive estimated fee, got {estimated_fee_text!r}"

        gui.click("sendContinueButton")
        gui.wait_for_page("walletSendReviewPage", timeout_ms=10000)
        review_fee_text = gui.get_text("sendReviewFeeValue")
        review_fee_sats = sat_text_to_sats(review_fee_text)
        assert review_fee_sats == estimated_fee_sats, (
            f"Preview estimate {estimated_fee_text!r} ({estimated_fee_sats} sats) "
            f"did not match review fee {review_fee_text!r} ({review_fee_sats} sats)"
        )

        gui.click("sendReviewSendButton")

        txid = wait_for_single_mempool_tx(harness.gui_rpc_port)
        tx_details = rpc_call(harness.gui_rpc_port, "gettransaction", [txid], wallet=GUI_WALLET_NAME)
        rpc_fee_sats = int(
            (
                abs(Decimal(str(tx_details["fee"]))) * Decimal("100000000")
            ).to_integral_value()
        )
        assert rpc_fee_sats == estimated_fee_sats, (
            f"Broadcast fee {rpc_fee_sats} sats did not match preview estimate {estimated_fee_sats} sats"
        )

        print(
            "Send flow passed: selected low fee target, preview estimate matched "
            "review fee, and broadcast fee matched preview."
        )
        return 0
    except Exception as err:  # noqa: BLE001 - preserve failure context for functional test output
        print(f"\\nFAILED: {err}", file=sys.stderr)
        import traceback
        traceback.print_exc()
        gui_output = harness.process_output(harness.gui_process)
        if gui_output:
            print("\\n--- GUI process output ---", file=sys.stderr)
            print(gui_output, file=sys.stderr)
        if gui is not None:
            dump_qml_tree(gui)
        return 1
    finally:
        harness.stop()


if __name__ == "__main__":
    sys.exit(run_test())
