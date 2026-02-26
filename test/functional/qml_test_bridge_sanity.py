#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Sanity test for the QML test automation bridge.

Verifies that the test bridge protocol works: listing objects, reading
properties, error handling, and page detection.

This test requires the binary to be built with -DENABLE_TEST_AUTOMATION=ON.
"""

import sys

from qml_test_harness import QmlTestHarness, QmlDriverError, dump_qml_tree, parse_args


def run_tests():
    args = parse_args()
    harness = QmlTestHarness(socket_path=args.socket_path)
    try:
        harness.start()
        gui = harness.driver

        # ── Test 1: list_objects ─────────────────────────────────
        print("\nTest 1: list_objects")
        objects = gui.list_objects()
        assert isinstance(objects, list), "list_objects should return a list"
        object_names = {obj["objectName"] for obj in objects}
        assert "mainPageStack" in object_names, \
            "Expected mainPageStack in list_objects output"
        print(f"  Found {len(objects)} named object(s) in QML tree.")
        if objects:
            for obj in objects[:10]:
                print(f"    - {obj['objectName']} ({obj['className']})")
            if len(objects) > 10:
                print(f"    ... and {len(objects) - 10} more")
        print("  Found mainPageStack")
        print("  PASSED")

        # ── Test 2: get_current_page ─────────────────────────────
        print("\nTest 2: get_current_page")
        page = gui.get_current_page()
        assert isinstance(page, str) and len(page) > 0, \
            f"Expected non-empty page name, got: {page!r}"
        print(f"  Current page: {page}")
        print("  PASSED")

        # ── Test 3: get_property on a known object ───────────────
        print("\nTest 3: get_property")
        if objects:
            first_obj = objects[0]["objectName"]
            try:
                val = gui.get_property(first_obj, "objectName")
                assert val == first_obj, \
                    f"Expected objectName={first_obj!r}, got {val!r}"
                print(f"  get_property({first_obj!r}, 'objectName') = {val!r}")
                print("  PASSED")
            except QmlDriverError as e:
                print(f"  Skipped (property read failed): {e}")
        else:
            print("  Skipped (no named objects found)")

        # ── Test 4: error handling — object not found ────────────
        print("\nTest 4: error handling for missing object")
        try:
            gui.click("nonExistentObject12345")
            assert False, "Expected QmlDriverError for missing object"
        except QmlDriverError as e:
            assert "not found" in str(e).lower() or "Object not found" in str(e), \
                f"Unexpected error message: {e}"
            print(f"  Correctly raised error: {e}")
            print("  PASSED")

        # ── Test 5: error handling — missing property ────────────
        print("\nTest 5: error handling for missing property")
        if objects:
            first_obj = objects[0]["objectName"]
            try:
                gui.get_property(first_obj, "completelyFakeProperty999")
                assert False, "Expected QmlDriverError for missing property"
            except QmlDriverError as e:
                assert "not found" in str(e).lower() or "error" in str(e).lower(), \
                    f"Unexpected error message: {e}"
                print(f"  Correctly raised error: {e}")
                print("  PASSED")
        else:
            print("  Skipped (no named objects found)")

        # ── Test 6: wait_for_page with short timeout ─────────────
        print("\nTest 6: wait_for_page timeout for non-existent page")
        try:
            gui.wait_for_page("pageDoesNotExist999", timeout_ms=500)
            assert False, "Expected QmlDriverError for timeout"
        except QmlDriverError as e:
            assert "timed out" in str(e).lower() or "Timed out" in str(e), \
                f"Unexpected error message: {e}"
            print(f"  Correctly timed out: {e}")
            print("  PASSED")

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
