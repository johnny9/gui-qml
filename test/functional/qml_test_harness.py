#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Shared test harness for QML test automation.

Provides QmlTestHarness which launches bitcoin-core-app with the test
bridge enabled and connects a QmlDriver instance.
"""

import argparse
import os
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import traceback

from qml_driver import QmlDriver, QmlDriverError


# How long to wait for the GUI process to start (seconds).
GUI_STARTUP_TIMEOUT = 30
PROCESS_OUTPUT_TAIL_BYTES = 4000
PROCESS_TERMINATION_TIMEOUT = 5


def find_gui_binary():
    """Locate the bitcoin-core-app binary.

    Search order:
      1. BITCOIN_CORE_APP environment variable
      2. <repo_root>/build/bin/bitcoin-core-app
    """
    env_path = os.getenv("BITCOIN_CORE_APP")
    if env_path and os.path.isfile(env_path):
        return env_path

    repo_root = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
    build_path = os.path.join(repo_root, 'build', 'bin', 'bitcoin-core-app')
    if os.path.isfile(build_path):
        return build_path

    raise FileNotFoundError(
        "Cannot find bitcoin-core-app binary. "
        "Set BITCOIN_CORE_APP env var or build with -DENABLE_TEST_AUTOMATION=ON."
    )


def setup_datadir(tmpdir):
    """Create a minimal regtest data directory with bitcoin.conf."""
    datadir = os.path.join(tmpdir, "node0")
    os.makedirs(datadir, exist_ok=True)
    conf_path = os.path.join(datadir, "bitcoin.conf")
    with open(conf_path, "w", encoding="utf8") as f:
        f.write("regtest=1\n")
        f.write("[regtest]\n")
        f.write("discover=0\n")
        f.write("dnsseed=0\n")
        f.write("fixedseeds=0\n")
        f.write("listenonion=0\n")
        f.write("printtoconsole=0\n")
        f.write("connect=0\n")
        f.write("shrinkdebugfile=0\n")
        f.write("fallbackfee=0.0001\n")
    return datadir


def _decode_process_output(output):
    if output is None:
        return ""
    if isinstance(output, str):
        return output
    return output.decode("utf-8", errors="replace")


def _tail_output(output):
    if not output:
        return ""
    encoded = output.encode("utf-8", errors="replace")
    if len(encoded) <= PROCESS_OUTPUT_TAIL_BYTES:
        return output
    return encoded[-PROCESS_OUTPUT_TAIL_BYTES:].decode("utf-8", errors="replace")


def print_process_output(label, stdout, stderr):
    """Print bounded stdout/stderr captured from a terminated process."""
    stdout = _tail_output(stdout)
    stderr = _tail_output(stderr)
    if stdout:
        print(f"\n--- {label} stdout ---", file=sys.stderr)
        print(stdout, file=sys.stderr)
    if stderr:
        print(f"\n--- {label} stderr ---", file=sys.stderr)
        print(stderr, file=sys.stderr)


def terminate_process(process, timeout=PROCESS_TERMINATION_TIMEOUT, capture_output=False):
    """Terminate a process, kill it on timeout, and optionally capture output."""
    stdout = ""
    stderr = ""
    killed = False
    if process is None:
        return stdout, stderr, killed

    try:
        if process.poll() is None:
            process.send_signal(signal.SIGTERM)
            if capture_output:
                try:
                    stdout_bytes, stderr_bytes = process.communicate(timeout=timeout)
                except subprocess.TimeoutExpired:
                    process.kill()
                    stdout_bytes, stderr_bytes = process.communicate()
                    killed = True
                stdout = _decode_process_output(stdout_bytes)
                stderr = _decode_process_output(stderr_bytes)
            else:
                try:
                    process.wait(timeout=timeout)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
                    killed = True
        elif capture_output:
            stdout_bytes, stderr_bytes = process.communicate()
            stdout = _decode_process_output(stdout_bytes)
            stderr = _decode_process_output(stderr_bytes)
    except Exception as err:  # noqa: BLE001 - diagnostics must not mask the original failure
        stderr = f"(could not terminate or capture process output: {err})"

    return stdout, stderr, killed


def report_qml_test_failure(
    error,
    *,
    driver=None,
    process=None,
    case_name=None,
    checkpoint=None,
    process_label="GUI process",
    output_timeout=PROCESS_TERMINATION_TIMEOUT,
):
    """Emit consistent diagnostics for QML functional test failures."""
    if case_name:
        print(f"\nFAILED [{case_name}]: {error}", file=sys.stderr)
    else:
        print(f"\nFAILED: {error}", file=sys.stderr)
    traceback.print_exception(type(error), error, error.__traceback__)

    if driver is not None and checkpoint is not None:
        try:
            checkpoint("failure state", driver)
        except Exception as checkpoint_err:  # noqa: BLE001 - preserve original failure context
            context = f"[{case_name}] " if case_name else ""
            print(
                f"{context}failed to save failure checkpoint: {checkpoint_err}",
                file=sys.stderr,
            )

    if driver is not None:
        dump_qml_tree(driver)

    stdout, stderr, killed = terminate_process(
        process,
        timeout=output_timeout,
        capture_output=True,
    )
    if killed:
        print(
            f"\n--- {process_label} did not terminate within {output_timeout}s; killed ---",
            file=sys.stderr,
        )
    print_process_output(process_label, stdout, stderr)


def parse_args():
    """Parse common CLI arguments for QML test scripts."""
    parser = argparse.ArgumentParser(
        description="QML test automation",
        add_help=True,
    )
    parser.add_argument(
        "--socket-path",
        help="Connect to an already-running bitcoin-core-app instance at "
             "this Unix socket path instead of launching a new one.  "
             "Start the app with: bitcoin-core-app -test-automation=<path>",
    )
    return parser.parse_args()


class QmlTestHarness:
    """Test harness that launches the GUI and connects the test bridge.

    If socket_path is provided, attaches to an already-running instance
    instead of launching a new one.
    """

    def __init__(self, socket_path=None, extra_args=None, reset_settings=True, datadir=None):
        self.external = socket_path is not None
        self.process = None
        self.driver = None
        self.extra_args = extra_args or []
        self.reset_settings = reset_settings

        if self.external:
            self.socket_path = socket_path
            self.tmpdir = None
            self.datadir = None
        else:
            self.gui_binary = find_gui_binary()
            if datadir is not None:
                # Reuse an existing datadir; don't create or delete a tmpdir.
                self.tmpdir = None
                self.datadir = datadir
                self.socket_path = os.path.join(datadir, "test_bridge.sock")
            else:
                self.tmpdir = tempfile.mkdtemp(prefix="qml_test_bridge_")
                self.datadir = setup_datadir(self.tmpdir)
                self.socket_path = os.path.join(self.tmpdir, "test_bridge.sock")

    def start(self):
        """Launch bitcoin-core-app or attach to an existing instance."""
        if self.external:
            print(f"Connecting to existing GUI at {self.socket_path} ...")
            self.driver = QmlDriver(self.socket_path, timeout=GUI_STARTUP_TIMEOUT)
            print("QmlDriver connected to test bridge.")
            return

        env = dict(os.environ)
        env["QT_QPA_PLATFORM"] = "offscreen"

        args = [
            self.gui_binary,
            f"-datadir={self.datadir}",
            f"-test-automation={self.socket_path}",
        ] + (["-resetguisettings"] if self.reset_settings else []) + [
            "-logtimemicros",
            "-debug",
            "-debugexclude=libevent",
            "-debugexclude=leveldb",
            "-nolisten",
        ] + self.extra_args

        print(f"Starting GUI: {' '.join(args)}")
        self.process = subprocess.Popen(
            args,
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )

        # Connect the QmlDriver (retries internally until the socket appears).
        self.driver = QmlDriver(self.socket_path, timeout=GUI_STARTUP_TIMEOUT)
        print("QmlDriver connected to test bridge.")

    def stop(self, cleanup=True):
        """Shut down the GUI process (only if we launched it).

        If cleanup is False, the tmpdir and datadir are preserved on disk so
        a second harness can reuse the same datadir for a restart-persistence test.
        """
        terminate_process(self.process, timeout=10)
        if self.driver:
            self.driver.close()
        if cleanup and self.tmpdir:
            shutil.rmtree(self.tmpdir, ignore_errors=True)
            self.tmpdir = None


def complete_onboarding(gui):
    """Click through all onboarding pages to reach the main node/wallet screen.

    Assumes the app was started with -resetguisettings or a fresh datadir so
    that onboarding is active.
    """
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
    gui.click("onboardingConnectionButton")
    time.sleep(1)  # Allow navigation to the post-onboarding screen to settle.


def dump_qml_tree(driver):
    """Print the full QML object tree for debugging."""
    try:
        print("\n--- QML object tree at failure ---")
        all_objects = driver.list_objects()
        for obj in all_objects:
            print(f"  {obj['objectName']} ({obj['className']})")
        print(f"--- {len(all_objects)} objects total ---")
    except Exception:
        print("  (could not retrieve object tree)")
