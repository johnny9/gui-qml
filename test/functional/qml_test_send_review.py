#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Functional coverage for send review page formatting and layout hooks."""

import sys
import time

from qml_test_harness import dump_qml_tree
from qml_wallet_test_lib import WalletFlowHarness, rpc_call


def format_address(address):
    return " ".join(address[i:i + 4] for i in range(0, len(address), 4))


def wait_until(predicate, timeout=20, interval=0.1, description="condition"):
    deadline = time.time() + timeout
    last_error = None
    while time.time() < deadline:
        try:
            if predicate():
                return
        except Exception as err:  # noqa: BLE001 - test polling should tolerate transient UI state
            last_error = err
        time.sleep(interval)
    if last_error:
        raise AssertionError(f"Timed out waiting for {description}: {last_error}")
    raise AssertionError(f"Timed out waiting for {description}")


def create_wallet(gui, wallet_name):
    gui.wait_for_property("createWalletButton", "visible", True, timeout_ms=20000)
    gui.click("createWalletButton")
    gui.wait_for_page("createWalletIntroPage", timeout_ms=10000)
    gui.click("createWalletIntroStartButton")
    gui.wait_for_page("createWalletNamePage", timeout_ms=10000)
    gui.set_text("createWalletNameInput", wallet_name)
    gui.click("createWalletNameContinueButton")
    gui.wait_for_page("createWalletPasswordPage", timeout_ms=10000)
    gui.click("createWalletPasswordSkipButton")
    gui.wait_for_page("createWalletConfirmPage", timeout_ms=10000)
    gui.click("createWalletConfirmNextButton")
    gui.wait_for_page("createWalletBackupPage", timeout_ms=10000)
    gui.click("createWalletBackupDoneButton")
    gui.wait_for_property("walletBadge", "text", wallet_name, timeout_ms=20000)
    gui.settle(timeout_ms=10000)


def fund_wallet(harness, wallet_name):
    mining_address = rpc_call(harness.gui_rpc_port, "getnewaddress", wallet=wallet_name)
    rpc_call(harness.gui_rpc_port, "generatetoaddress", [101, mining_address])
    wait_until(
        lambda: float(rpc_call(harness.gui_rpc_port, "getbalance", wallet=wallet_name)) > 0,
        description="wallet RPC balance",
    )
    wait_until(
        lambda: str(harness.driver.get_property("walletBadge", "balance")) not in {"0", "0.00000000"},
        description="wallet badge balance",
    )


def open_send_page(gui):
    gui.click("desktopWalletsSendTab")
    gui.wait_for_page("sendPage", timeout_ms=10000)
    gui.settle(timeout_ms=10000)


def prepare_single_send(gui, address, amount, amount_unit="btc"):
    open_send_page(gui)
    gui.set_text("sendAddressInput", address)
    if amount_unit == "sat":
        gui.click("sendAmountUnitToggle")
    gui.set_text("sendAmountInput", amount)
    gui.wait_for_property("sendReviewButton", "enabled", True, timeout_ms=10000)
    gui.click("sendReviewButton")
    gui.wait_for_page("sendReviewPage", timeout_ms=10000)


def prepare_multi_send(gui, first_address, first_amount_btc, second_address, second_amount_sat):
    open_send_page(gui)
    gui.click("sendOptionsButton")
    gui.wait_for_property("sendOptionsPopup", "opened", True, timeout_ms=5000)
    gui.click("sendOptionsMultipleRecipientsToggle")
    gui.click("sendOptionsButton")
    gui.wait_for_property("sendOptionsPopup", "opened", False, timeout_ms=5000)

    gui.set_text("sendAddressInput", second_address)
    gui.click("sendAmountUnitToggle")
    gui.set_text("sendAmountInput", second_amount_sat)
    gui.click("sendRecipientPrevButton")
    gui.set_text("sendAddressInput", first_address)
    gui.set_text("sendAmountInput", first_amount_btc)
    gui.wait_for_property("sendReviewButton", "enabled", True, timeout_ms=10000)
    gui.click("sendReviewButton")
    gui.wait_for_page("multipleSendReviewPage", timeout_ms=10000)


def assert_unit_suffix(gui, object_name, unit_suffix):
    text = gui.get_text(object_name)
    assert text.endswith(f" {unit_suffix}"), f"Expected {object_name} to end with {unit_suffix!r}, got {text!r}"


def case_single_btc():
    harness = WalletFlowHarness("qml_send_review_single_btc", port_offset=70)
    try:
        wallet_name = "single_btc_review"
        harness.start_gui(reset_gui_settings=True)
        harness.finish_onboarding()
        create_wallet(harness.driver, wallet_name)
        fund_wallet(harness, wallet_name)

        recipient_address = rpc_call(harness.gui_rpc_port, "getnewaddress", wallet=wallet_name)
        prepare_single_send(harness.driver, recipient_address, "1.25000000", amount_unit="btc")

        assert harness.driver.get_text("sendReviewAddressField") == format_address(recipient_address)
        assert harness.driver.get_text("sendReviewAmountField") == "1.25000000 ₿"
        assert_unit_suffix(harness.driver, "sendReviewFeeField", "₿")
        assert_unit_suffix(harness.driver, "sendReviewTotalField", "₿")
    finally:
        harness.stop()


def case_single_sat():
    harness = WalletFlowHarness("qml_send_review_single_sat", port_offset=80)
    try:
        wallet_name = "single_sat_review"
        harness.start_gui(reset_gui_settings=True)
        harness.finish_onboarding()
        create_wallet(harness.driver, wallet_name)
        fund_wallet(harness, wallet_name)

        recipient_address = rpc_call(harness.gui_rpc_port, "getnewaddress", wallet=wallet_name)
        prepare_single_send(harness.driver, recipient_address, "1250", amount_unit="sat")

        assert harness.driver.get_text("sendReviewAddressField") == format_address(recipient_address)
        assert harness.driver.get_text("sendReviewAmountField") == "1250 sat"
        assert_unit_suffix(harness.driver, "sendReviewFeeField", "sat")
        assert_unit_suffix(harness.driver, "sendReviewTotalField", "sat")
    finally:
        harness.stop()


def case_multi_review():
    harness = WalletFlowHarness("qml_send_review_multi", port_offset=90)
    try:
        wallet_name = "multi_review"
        harness.start_gui(reset_gui_settings=True)
        harness.finish_onboarding()
        create_wallet(harness.driver, wallet_name)
        fund_wallet(harness, wallet_name)

        first_address = rpc_call(harness.gui_rpc_port, "getnewaddress", wallet=wallet_name)
        second_address = rpc_call(harness.gui_rpc_port, "getnewaddress", wallet=wallet_name)
        prepare_multi_send(
            harness.driver,
            first_address=first_address,
            first_amount_btc="0.50000000",
            second_address=second_address,
            second_amount_sat="2000",
        )

        assert harness.driver.get_text("multipleSendReviewRecipient0Address") == format_address(first_address)
        assert harness.driver.get_text("multipleSendReviewRecipient0Amount") == "0.50000000 ₿"
        assert harness.driver.get_text("multipleSendReviewRecipient1Address") == format_address(second_address)
        assert harness.driver.get_text("multipleSendReviewRecipient1Amount") == "2000 sat"
        assert_unit_suffix(harness.driver, "multipleSendReviewFeeField", "₿")
        assert_unit_suffix(harness.driver, "multipleSendReviewTotalField", "₿")
    finally:
        harness.stop()


def run_tests():
    try:
        case_single_btc()
        case_single_sat()
        case_multi_review()
        print("Send review flows passed.")
    except Exception as err:  # noqa: BLE001 - preserve UI context on failure
        print(f"\nFAILED: {err}", file=sys.stderr)
        import traceback
        traceback.print_exc()
        raise


if __name__ == "__main__":
    try:
        run_tests()
    except Exception:
        # The per-case harness cleanup already ran; keep the failure shape aligned
        # with the other functional scripts.
        sys.exit(1)
