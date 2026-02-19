#!/usr/bin/env python3
# Copyright (c) 2024 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test that walks through the full onboarding flow via the test bridge.

Clicks through every onboarding page and verifies the app exits
onboarding into the main view.

This test requires the binary to be built with -DENABLE_TEST_AUTOMATION=ON.
"""

import sys
import time

from qml_test_harness import QmlTestHarness, QmlDriverError, dump_qml_tree, parse_args


def run_tests():
    args = parse_args()
    harness = QmlTestHarness(socket_path=args.socket_path)
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

        # After onboarding completes, we should no longer be on an
        # onboarding page. The app navigates to either the desktop
        # wallets view or the node runner, depending on configuration.
        time.sleep(1)  # Allow navigation to settle.
        final_page = gui.get_current_page()
        print(f"  -> post-onboarding page: {final_page}")
        assert "onboarding" not in final_page.lower(), \
            f"Still on an onboarding page after finishing: {final_page}"

        print("\n" + "=" * 50)
        print("All tests PASSED")
        print("=" * 50)

    except Exception as e:
        print(f"\nFAILED: {e}", file=sys.stderr)
        import traceback
        traceback.print_exc()
        dump_qml_tree(gui)
        sys.exit(1)
    finally:
        harness.stop()


if __name__ == '__main__':
    run_tests()
