#!/usr/bin/env python3
# Copyright (c) 2024 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test onboarding flows via the test bridge.

Clicks through every onboarding page, verifies the app exits onboarding
into the main view, and exercises storage-location selection.

This test requires the binary to be built with -DENABLE_TEST_AUTOMATION=ON.
"""

import os
import shutil
import sys
import tempfile
import time

from qml_test_harness import QmlTestHarness, dump_qml_tree, parse_args


REGTEST_ARGS = ["-regtest", "-connect=0"]


def wait_for_path(path, timeout=30.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        if os.path.exists(path):
            return
        time.sleep(0.1)
    raise AssertionError(f"Timed out waiting for path: {path}")


def wait_until_left_onboarding(gui):
    deadline = time.time() + 10
    final_page = gui.get_current_page()
    while "onboarding" in final_page.lower() and time.time() < deadline:
        time.sleep(0.1)
        final_page = gui.get_current_page()
    assert "onboarding" not in final_page.lower(), \
        f"Still on an onboarding page after finishing: {final_page}"
    return final_page


def navigate_to_storage_location(gui):
    gui.wait_for_page("onboardingCover", timeout_ms=10000)
    steps = [
        ("onboardingCoverButton", "onboardingStrengthen"),
        ("onboardingStrengthenButton", "onboardingBlockclock"),
        ("onboardingBlockclockButton", "onboardingStorageLocation"),
    ]
    for button, expected_page in steps:
        gui.click(button)
        gui.wait_for_page(expected_page, timeout_ms=5000)


def finish_onboarding_from_storage_location(gui):
    steps = [
        ("onboardingStorageLocationButton", "onboardingStorageAmount"),
        ("onboardingStorageAmountButton", "onboardingConnection"),
    ]
    for button, expected_page in steps:
        gui.click(button)
        gui.wait_for_page(expected_page, timeout_ms=5000)
    gui.click("onboardingConnectionButton")


def start_without_cli_datadir(home_dir, config_home, reset_settings):
    harness = QmlTestHarness(
        use_cli_datadir=False,
        home_dir=home_dir,
        config_home=config_home,
        reset_settings=reset_settings,
        extra_args=REGTEST_ARGS,
    )
    harness.start()
    return harness


def test_full_onboarding_flow(socket_path=None):
    root_dir = None
    if socket_path:
        harness = QmlTestHarness(socket_path=socket_path)
    else:
        root_dir = tempfile.mkdtemp(prefix="qml_onboarding_full_")
        harness = QmlTestHarness(
            use_cli_datadir=False,
            home_dir=os.path.join(root_dir, "home"),
            config_home=os.path.join(root_dir, "config"),
            reset_settings=True,
            extra_args=REGTEST_ARGS,
        )
    gui = None
    try:
        harness.start()
        gui = harness.driver

        # The app starts fresh (-resetguisettings), so we should land on
        # the onboarding cover page.
        page = gui.get_current_page()
        print(f"Initial page: {page}")
        assert "onboardingCover" in page or "Cover" in page, \
            f"Expected to start on onboardingCover, got: {page}"

        # Define the expected progression through onboarding.
        # Each entry is (button_to_click, expected_next_page).
        onboarding_steps = [
            ("onboardingCoverButton",           "onboardingStrengthen"),
            ("onboardingStrengthenButton",      "onboardingBlockclock"),
            ("onboardingBlockclockButton",      "onboardingStorageLocation"),
            ("onboardingStorageLocationButton", "onboardingStorageAmount"),
            ("onboardingStorageAmountButton",   "onboardingConnection"),
        ]

        for button, expected_page in onboarding_steps:
            print(f"Click {button} ...")
            gui.click(button)
            gui.wait_for_page(expected_page, timeout_ms=5000)
            current = gui.get_current_page()
            assert expected_page in current, \
                f"Expected {expected_page}, got: {current}"
            print(f"  -> page: {current}")

        # Click Next on the final connection page to finish onboarding.
        print("Click onboardingConnectionButton (finish onboarding) ...")
        gui.click("onboardingConnectionButton")

        final_page = wait_until_left_onboarding(gui)
        print(f"  -> post-onboarding page: {final_page}")

    except Exception:
        if gui is not None:
            dump_qml_tree(gui)
        raise
    finally:
        harness.stop()
        if root_dir:
            shutil.rmtree(root_dir, ignore_errors=True)


def test_default_directory_is_used(root_dir):
    print("\nTest storage location: default directory")
    home_dir = os.path.join(root_dir, "default-home")
    config_home = os.path.join(root_dir, "default-config")
    harness = start_without_cli_datadir(home_dir, config_home, reset_settings=True)
    try:
        gui = harness.driver
        navigate_to_storage_location(gui)
        assert gui.get_property("storageDefaultOption", "checked"), "Default storage option should be selected"
        assert not gui.get_property("storageCustomOption", "checked"), "Custom storage option should not be selected"

        finish_onboarding_from_storage_location(gui)
        wait_until_left_onboarding(gui)
        wait_for_path(os.path.join(home_dir, ".bitcoin", "regtest", "blocks"))
    except Exception:
        if harness.driver:
            dump_qml_tree(harness.driver)
        raise
    finally:
        harness.stop()


def test_custom_directory_invalid_path_and_restart(root_dir):
    print("\nTest storage location: invalid custom path, custom directory, restart")
    home_dir = os.path.join(root_dir, "custom-home")
    config_home = os.path.join(root_dir, "custom-config")
    custom_dir = os.path.join(root_dir, "selected-data")
    invalid_path = os.path.join(root_dir, "not-a-directory")
    with open(invalid_path, "w", encoding="utf8") as f:
        f.write("not a directory\n")

    harness = start_without_cli_datadir(home_dir, config_home, reset_settings=True)
    try:
        gui = harness.driver
        navigate_to_storage_location(gui)

        result = gui.invoke_method("storageLocations", "selectCustomDataDir", invalid_path)
        assert result is False, "Invalid data directory path should be rejected"
        assert gui.get_property("storageLocationErrorText", "visible"), "Expected visible data directory error"
        assert "not a directory" in gui.get_property("storageLocationErrorText", "text")
        assert gui.get_property("storageDefaultOption", "checked"), "Invalid custom path should keep default selected"

        result = gui.invoke_method("storageLocations", "selectCustomDataDir", custom_dir)
        assert result is True, "Custom data directory should be accepted"
        assert gui.get_property("storageCustomOption", "checked"), "Custom storage option should be selected"
        assert gui.get_property("storageCustomOption", "customDir") == custom_dir
        assert not os.path.exists(custom_dir), "Custom data directory should not be created before onboarding finishes"

        finish_onboarding_from_storage_location(gui)
        wait_until_left_onboarding(gui)
        wait_for_path(os.path.join(custom_dir, "regtest", "blocks"))
    except Exception:
        if harness.driver:
            dump_qml_tree(harness.driver)
        raise
    finally:
        harness.stop(cleanup=False)

    restart = start_without_cli_datadir(home_dir, config_home, reset_settings=False)
    try:
        gui = restart.driver
        gui.wait_for_object("walletBadge", timeout_ms=10000)
        current_page = gui.get_current_page()
        assert "onboarding" not in current_page.lower(), f"Unexpected onboarding page after restart: {current_page}"
        wait_for_path(os.path.join(custom_dir, "regtest", "blocks"))
    except Exception:
        if restart.driver:
            dump_qml_tree(restart.driver)
        raise
    finally:
        restart.stop()


def test_existing_core_directory_is_rejected(root_dir):
    print("\nTest storage location: existing Bitcoin Core directory")
    home_dir = os.path.join(root_dir, "existing-home")
    config_home = os.path.join(root_dir, "existing-config")
    existing_core_dir = os.path.join(root_dir, "existing-core")
    os.makedirs(os.path.join(existing_core_dir, "regtest", "blocks", "index"), exist_ok=True)

    harness = start_without_cli_datadir(home_dir, config_home, reset_settings=True)
    try:
        gui = harness.driver
        navigate_to_storage_location(gui)

        result = gui.invoke_method("storageLocations", "selectCustomDataDir", existing_core_dir)
        assert result is False, "Existing Bitcoin Core data directory should be rejected"
        assert gui.get_property("storageLocationErrorText", "visible"), "Expected visible data directory error"
        assert "already contains Bitcoin Core data" in gui.get_property("storageLocationErrorText", "text")
        assert not gui.get_property("onboardingStorageLocationButton", "enabled"), \
            "Continue button should be disabled while an existing Core profile is selected"
        assert "onboardingStorageLocation" in gui.get_current_page(), \
            "Existing Core profile rejection should keep the user on the storage-location page"
    except Exception:
        if harness.driver:
            dump_qml_tree(harness.driver)
        raise
    finally:
        harness.stop()


def run_tests():
    args = parse_args()

    try:
        test_full_onboarding_flow(socket_path=args.socket_path)

        if args.socket_path:
            print("Skipping storage location restart scenarios with --socket-path; they require isolated GUI launches.")
        else:
            root_dir = tempfile.mkdtemp(prefix="qml_onboarding_")
            try:
                test_default_directory_is_used(root_dir)
                test_custom_directory_invalid_path_and_restart(root_dir)
                test_existing_core_directory_is_rejected(root_dir)
            finally:
                shutil.rmtree(root_dir, ignore_errors=True)

        print("\n" + "=" * 50)
        print("All tests PASSED")
        print("=" * 50)

    except Exception as e:
        print(f"\nFAILED: {e}", file=sys.stderr)
        import traceback
        traceback.print_exc()
        sys.exit(1)


if __name__ == '__main__':
    run_tests()
