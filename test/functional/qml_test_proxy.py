#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Functional test for the proxy settings UI.

Tests navigation to the proxy settings page from within the onboarding
connection step, toggling the default proxy and Tor proxy enable switches,
entering valid and invalid addresses, and verifying that settings are
persisted to disk when onboarding completes.

This test requires the binary to be built with -DENABLE_TEST_AUTOMATION=ON.
"""

import json
import os
import sys
import time

from qml_driver import QmlDriverError
from qml_test_harness import (
    QmlTestHarness,
    dump_qml_tree,
    parse_args,
)


def walk_onboarding_to_connection(gui):
    """Walk through onboarding pages up to (but not past) onboardingConnection."""
    gui.wait_for_page("onboardingCover", timeout_ms=10000)
    steps = [
        ("onboardingCoverButton",           "onboardingStrengthen"),
        ("onboardingStrengthenButton",      "onboardingBlockclock"),
        ("onboardingBlockclockButton",      "onboardingStorageLocation"),
        ("onboardingStorageLocationButton", "onboardingStorageAmount"),
        ("onboardingStorageAmountButton",   "onboardingConnection"),
    ]
    for button, expected_page in steps:
        gui.click(button)
        gui.wait_for_page(expected_page, timeout_ms=5000)


def navigate_to_proxy_settings(gui):
    """Navigate from the onboardingConnection page to the Proxy Settings page."""
    gui.wait_for_page("onboardingConnection", timeout_ms=5000)
    gui.click("connectionSettingsButton")
    gui.wait_for_page("gotoProxy", timeout_ms=5000)
    gui.click("gotoProxy")
    gui.wait_for_page("settingsProxy", timeout_ms=5000)
    print("  Navigated to Proxy Settings page.")


def navigate_back_from_proxy_settings(gui):
    """Navigate back from Proxy Settings to the onboardingConnection page."""
    gui.click("settingsProxyBack")
    gui.wait_for_page("gotoProxy", timeout_ms=5000)
    gui.click("connectionSettingsDoneButton")
    gui.wait_for_page("onboardingConnectionButton", timeout_ms=5000)
    print("  Navigated back to onboardingConnection page.")


def test_default_proxy_toggle(gui):
    print("\n── test_default_proxy_toggle ──────────────────────────────────────")

    # Proxy should be disabled by default (fresh datadir, no prior config).
    checked = gui.get_property("proxyEnableSwitch", "checked")
    assert not checked, f"Expected proxy disabled by default, got checked={checked}"

    # Enable proxy.
    gui.click("proxyEnableSwitch")
    time.sleep(0.1)
    checked = gui.get_property("proxyEnableSwitch", "checked")
    assert checked, "Expected proxyEnableSwitch to be checked after click"
    print("  Default proxy toggled ON: OK")

    # Disable proxy.
    gui.click("proxyEnableSwitch")
    time.sleep(0.1)
    checked = gui.get_property("proxyEnableSwitch", "checked")
    assert not checked, "Expected proxyEnableSwitch to be unchecked after second click"
    print("  Default proxy toggled OFF: OK")


def test_proxy_valid_address(gui):
    print("\n── test_proxy_valid_address ────────────────────────────────────────")

    # Enable proxy so the address field becomes active.
    if not gui.get_property("proxyEnableSwitch", "checked"):
        gui.click("proxyEnableSwitch")
        time.sleep(0.1)

    gui.set_text("proxyAddressInput", "10.0.0.1:9050")
    time.sleep(0.15)  # Allow onTextChanged to process.

    valid = gui.get_property("proxyAddressInput", "validInput")
    assert valid, f"Expected '10.0.0.1:9050' to pass validation, got validInput={valid}"
    print("  Valid address accepted: OK")


def test_proxy_invalid_address(gui):
    print("\n── test_proxy_invalid_address ──────────────────────────────────────")

    # Enable proxy so the address field becomes active.
    if not gui.get_property("proxyEnableSwitch", "checked"):
        gui.click("proxyEnableSwitch")
        time.sleep(0.1)

    # Enter an address with invalid IP octets.
    gui.set_text("proxyAddressInput", "999.999.999.999:9050")
    time.sleep(0.15)

    valid = gui.get_property("proxyAddressInput", "validInput")
    assert not valid, f"Expected invalid address to fail validation, got validInput={valid}"
    print("  Invalid address rejected: OK")

    # Restore to a valid address for subsequent tests.
    gui.set_text("proxyAddressInput", "127.0.0.1:9050")
    time.sleep(0.15)


def test_tor_proxy_toggle(gui):
    print("\n── test_tor_proxy_toggle ───────────────────────────────────────────")

    # Tor proxy should be disabled by default.
    checked = gui.get_property("torEnableSwitch", "checked")
    assert not checked, f"Expected Tor proxy disabled by default, got checked={checked}"

    # Enable Tor proxy.
    gui.click("torEnableSwitch")
    time.sleep(0.1)
    checked = gui.get_property("torEnableSwitch", "checked")
    assert checked, "Expected torEnableSwitch to be checked after click"
    print("  Tor proxy toggled ON: OK")

    # Disable Tor proxy.
    gui.click("torEnableSwitch")
    time.sleep(0.1)
    checked = gui.get_property("torEnableSwitch", "checked")
    assert not checked, "Expected torEnableSwitch to be unchecked after second click"
    print("  Tor proxy toggled OFF: OK")


def wait_for_settings_key(datadir, key, timeout=10.0):
    """Poll regtest/settings.json until `key` is present, up to timeout seconds."""
    settings_path = os.path.join(datadir, "regtest", "settings.json")
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if os.path.exists(settings_path):
            try:
                with open(settings_path, encoding="utf-8") as f:
                    data = json.load(f)
                if key in data:
                    return data
            except (json.JSONDecodeError, OSError):
                pass
        time.sleep(0.1)
    raise AssertionError(
        f"Timed out after {timeout}s waiting for '{key}' in {settings_path}"
    )


def test_proxy_settings_persist(datadir, settings):
    print("\n── test_proxy_settings_persist ─────────────────────────────────────")

    assert settings.get("proxy") == "10.0.0.1:9050", (
        f"Expected proxy='10.0.0.1:9050' in settings.json, got: {settings}"
    )
    print(f"  settings.json proxy entry '{settings['proxy']}': OK")


def run_tests():
    args = parse_args()
    harness = QmlTestHarness(socket_path=args.socket_path)
    gui = None
    try:
        harness.start()
        gui = harness.driver

        walk_onboarding_to_connection(gui)
        navigate_to_proxy_settings(gui)

        test_default_proxy_toggle(gui)
        test_proxy_valid_address(gui)
        test_proxy_invalid_address(gui)
        test_tor_proxy_toggle(gui)

        # Prepare state for the persistence test: enable proxy and set a
        # specific address before completing onboarding (which triggers
        # optionsModel.onboard() and writes settings to disk).
        if harness.datadir:
            if not gui.get_property("proxyEnableSwitch", "checked"):
                gui.click("proxyEnableSwitch")
                time.sleep(0.1)
            gui.set_text("proxyAddressInput", "10.0.0.1:9050")
            time.sleep(0.2)

        navigate_back_from_proxy_settings(gui)

        # Complete onboarding — triggers optionsModel.onboard() which
        # persists all settings (including proxy) to regtest/settings.json.
        gui.click("onboardingConnectionButton")

        if harness.datadir:
            settings = wait_for_settings_key(harness.datadir, "proxy")
            test_proxy_settings_persist(harness.datadir, settings)
        else:
            print("\n── test_proxy_settings_persist: skipped (external harness) ──")

        print("\n" + "=" * 50)
        print("All tests PASSED")
        print("=" * 50)

    except Exception as e:
        print(f"\nFAILED: {e}", file=sys.stderr)
        import traceback
        traceback.print_exc()
        if gui is not None:
            dump_qml_tree(gui)
        sys.exit(1)
    finally:
        harness.stop()


if __name__ == '__main__':
    run_tests()
