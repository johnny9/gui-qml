#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Check block-clock history from live connect/disconnect events and on restart."""

import time

from qml_wallet_test_lib import WalletFlowHarness, rpc_call, wait_for_rpc


def run_tests():
    harness = WalletFlowHarness("qml_blockclock_history", 0)

    def rpc(method, params=None):
        return rpc_call(harness.gui_rpc_port, method, params)

    def expect_times(timestamps):
        def matches(fractions):
            # Use the model's local period on every poll, including if the
            # test crosses noon/midnight while waiting for notifications.
            period_start = harness.driver.get_property("blockClock", "blockClockModelRef.periodStart")
            expected = sorted((timestamp - period_start) / 43200 for timestamp in timestamps
                              if period_start <= timestamp < period_start + 43200)
            return (isinstance(fractions, list) and len(fractions) == len(expected)
                    and all(abs(actual - wanted) < 1e-10 for actual, wanted in zip(fractions, expected)))

        harness.driver.wait_for_property(
            "blockClockDial", "blockTimeFractions", matches, timeout_ms=10000)

    def stop_cleanly():
        process = harness.gui_process
        harness.stop_gui()
        output = harness.process_output(process)
        if output:
            print(output)
        assert process.returncode == 0, f"GUI exited with status {process.returncode}"

    try:
        harness.start_gui(extra_args=["-disablewallet"])
        wait_for_rpc(harness.gui_rpc_port)
        harness.driver.wait_for_page("blockClock", timeout_ms=10000)

        descriptor = rpc("getdescriptorinfo", ["raw(51)"])["descriptor"]
        rpc("setmocktime", [1300000000])
        rpc("generatetodescriptor", [100, descriptor])
        expect_times([])

        # Distinct blocks can share a timestamp. Each must remain a separate
        # confirmation, even when many events arrive between GUI refreshes.
        timestamp = int(time.time())
        rpc("setmocktime", [timestamp])
        blocks = rpc("generatetodescriptor", [3, descriptor])
        expect_times([timestamp] * 3)

        rpc("invalidateblock", [blocks[1]])
        expect_times([timestamp])

        rpc("setmocktime", [timestamp + 1])
        rpc("generatetodescriptor", [2, descriptor])
        expect_times([timestamp, timestamp + 1, timestamp + 1])

        # Existing confirmations must be restored without requiring a new tip.
        stop_cleanly()
        harness.start_gui(extra_args=["-disablewallet"])
        wait_for_rpc(harness.gui_rpc_port)
        harness.driver.wait_for_page("blockClock", timeout_ms=10000)
        expect_times([timestamp, timestamp + 1, timestamp + 1])
        stop_cleanly()
        print("BlockClock notification/history test PASSED")
    finally:
        harness.stop()


if __name__ == "__main__":
    run_tests()
