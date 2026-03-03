#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""End-to-end tests for peer management: disconnect, ban, and unban.

Starts the GUI (bitcoin-core-app) as a regtest node that listens for
inbound connections, then starts a second bitcoind that connects to it.
The test drives the GUI through the Peers page to exercise each action
and verifies the result via JSON-RPC.

This test requires:
  - bitcoin-core-app built with -DENABLE_TEST_AUTOMATION=ON
  - bitcoind binary (searched alongside bitcoin-core-app, in build/bin/,
    in a sibling 'bitcoin' repo, or set BITCOIND env var); falls back to
    using bitcoin-core-app itself as the peer node if no bitcoind is found
"""

import base64
import http.client
import json
import os
import signal
import subprocess
import sys
import tempfile
import time

from qml_test_harness import (
    QmlDriverError,
    GUI_STARTUP_TIMEOUT,
    dump_qml_tree,
    find_gui_binary,
    parse_args,
)
from qml_driver import QmlDriver


# ── Ports used by this test (chosen to avoid conflicts with default regtest) ──
GUI_P2P_PORT = 18555
GUI_RPC_PORT = 18556
PEER_P2P_PORT = 18557
PEER_RPC_PORT = 18558
PEER2_P2P_PORT = 18559
PEER2_RPC_PORT = 18560

GUI_RPC_USER = "qmltest"
GUI_RPC_PASS = "qmltestpass"

BAN_DURATIONS = [
    (3600,      "1 hour"),
    (86400,     "1 day"),
    (604800,    "1 week"),
    (31536000,  "1 year"),
]


# ── Helper: find bitcoind ─────────────────────────────────────────────────────

def find_bitcoind():
    """Locate the bitcoind binary.

    Checks the BITCOIND environment variable first; falls back to
    build/bin/bitcoind relative to the repo root.
    """
    repo_root = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
    default = os.path.join(repo_root, 'build', 'bin', 'bitcoind')
    path = os.getenv('BITCOIND', default)
    if not os.path.isfile(path):
        raise FileNotFoundError(
            f"bitcoind not found at {path}. "
            "Set the BITCOIND environment variable or ensure it is built at build/bin/bitcoind."
        )
    return path


# ── Harness ───────────────────────────────────────────────────────────────────

class PeerQmlTestHarness:
    """Launches the GUI node and a peer bitcoind, connects them, and provides
    a QmlDriver for test automation plus an rpc_call() helper for verification.
    """

    def __init__(self):
        self.gui_binary = find_gui_binary()
        self.bitcoind_binary = find_bitcoind()
        self.tmpdir = tempfile.mkdtemp(prefix="qml_test_peers_")
        self.socket_path = os.path.join(self.tmpdir, "test_bridge.sock")
        self.gui_process = None
        self.peer_process = None
        self.peer2_process = None
        self.driver = None

        self.gui_datadir = self._setup_gui_datadir()
        self.peer_datadir = self._setup_peer_datadir()

    # ── datadir setup ─────────────────────────────────────────────────────────

    def _setup_gui_datadir(self):
        datadir = os.path.join(self.tmpdir, "gui_node")
        os.makedirs(datadir, exist_ok=True)
        conf_path = os.path.join(datadir, "bitcoin.conf")
        with open(conf_path, "w", encoding="utf8") as f:
            f.write("regtest=1\n")
            f.write("[regtest]\n")
            f.write("server=1\n")
            f.write(f"rpcuser={GUI_RPC_USER}\n")
            f.write(f"rpcpassword={GUI_RPC_PASS}\n")
            f.write(f"rpcport={GUI_RPC_PORT}\n")
            f.write(f"port={GUI_P2P_PORT}\n")
            f.write("bind=127.0.0.1\n")
            f.write("listen=1\n")
            f.write("discover=0\n")
            f.write("dnsseed=0\n")
            f.write("fixedseeds=0\n")
            f.write("listenonion=0\n")
            f.write("printtoconsole=0\n")
            f.write("shrinkdebugfile=0\n")
        return datadir

    def _setup_peer_datadir(self):
        datadir = os.path.join(self.tmpdir, "peer_node")
        os.makedirs(datadir, exist_ok=True)
        conf_path = os.path.join(datadir, "bitcoin.conf")
        with open(conf_path, "w", encoding="utf8") as f:
            f.write("regtest=1\n")
            f.write("[regtest]\n")
            f.write("server=1\n")
            f.write(f"rpcuser={GUI_RPC_USER}\n")
            f.write(f"rpcpassword={GUI_RPC_PASS}\n")
            f.write(f"rpcport={PEER_RPC_PORT}\n")
            f.write(f"port={PEER_P2P_PORT}\n")
            f.write("listen=0\n")
            f.write(f"connect=127.0.0.1:{GUI_P2P_PORT}\n")
            f.write("discover=0\n")
            f.write("dnsseed=0\n")
            f.write("fixedseeds=0\n")
            f.write("listenonion=0\n")
            f.write("printtoconsole=0\n")
            f.write("shrinkdebugfile=0\n")
        return datadir

    # ── start / stop ──────────────────────────────────────────────────────────

    def start(self):
        """Start the GUI node, connect QmlDriver, then start the peer node."""
        env = dict(os.environ)
        env["QT_QPA_PLATFORM"] = "offscreen"

        gui_args = [
            self.gui_binary,
            f"-datadir={self.gui_datadir}",
            f"-test-automation={self.socket_path}",
            "-disablewallet",
            "-logtimemicros",
            "-debug",
            "-debugexclude=libevent",
            "-debugexclude=leveldb",
        ]
        print(f"Starting GUI node: {' '.join(gui_args)}")
        self.gui_process = subprocess.Popen(
            gui_args,
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )

        self.driver = QmlDriver(self.socket_path, timeout=GUI_STARTUP_TIMEOUT)
        print("QmlDriver connected to test bridge.")

        # Give the GUI node's P2P listener a moment to bind.
        time.sleep(2)

        peer_args = [
            self.bitcoind_binary,
            f"-datadir={self.peer_datadir}",
        ]
        print(f"Starting peer node: {' '.join(peer_args)}")
        self.peer_process = subprocess.Popen(
            peer_args,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )

    def stop(self):
        """Terminate all processes."""
        for proc in (self.peer2_process, self.peer_process, self.gui_process):
            if proc and proc.poll() is None:
                proc.send_signal(signal.SIGTERM)
                try:
                    proc.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait()
        if self.driver:
            self.driver.close()

    # ── RPC helpers ───────────────────────────────────────────────────────────

    def rpc_call(self, method, params=None):
        """Make a JSON-RPC call to the GUI node and return the result."""
        payload = json.dumps({
            "jsonrpc": "1.0",
            "id": "qml_test",
            "method": method,
            "params": params or [],
        }).encode("utf-8")

        conn = http.client.HTTPConnection("127.0.0.1", GUI_RPC_PORT, timeout=10)
        credentials = base64.b64encode(
            f"{GUI_RPC_USER}:{GUI_RPC_PASS}".encode("utf-8")
        ).decode("ascii")
        conn.request(
            "POST", "/",
            body=payload,
            headers={
                "Content-Type": "application/json",
                "Authorization": f"Basic {credentials}",
            },
        )
        resp = conn.getresponse()
        body = json.loads(resp.read())
        conn.close()
        if body.get("error"):
            raise RuntimeError(f"RPC error: {body['error']}")
        return body["result"]

    def wait_for_peer(self, timeout=30):
        """Poll getpeerinfo until at least one peer is connected."""
        deadline = time.time() + timeout
        while time.time() < deadline:
            try:
                peers = self.rpc_call("getpeerinfo")
                if peers:
                    print(f"  Peer connected: id={peers[0]['id']} addr={peers[0]['addr']}")
                    return peers[0]["id"]
            except Exception:
                pass
            time.sleep(0.5)
        raise RuntimeError(f"No peer connected after {timeout}s")

    def wait_for_no_peers(self, timeout=10):
        """Poll getpeerinfo until no peers remain."""
        deadline = time.time() + timeout
        while time.time() < deadline:
            try:
                if not self.rpc_call("getpeerinfo"):
                    return
            except Exception:
                pass
            time.sleep(0.5)
        raise RuntimeError(f"Peer still connected after {timeout}s")

    def wait_for_no_banned(self, timeout=10):
        """Poll listbanned until the ban list is empty."""
        deadline = time.time() + timeout
        while time.time() < deadline:
            try:
                if not self.rpc_call("listbanned"):
                    return
            except Exception:
                pass
            time.sleep(0.5)
        raise RuntimeError(f"Ban list not empty after {timeout}s")

    def reconnect_peer(self):
        """Restart the peer bitcoind so it reconnects to the GUI node."""
        if self.peer_process and self.peer_process.poll() is None:
            self.peer_process.send_signal(signal.SIGTERM)
            self.peer_process.wait(timeout=10)

        peer_args = [
            self.bitcoind_binary,
            f"-datadir={self.peer_datadir}",
        ]
        print("  Restarting peer node ...")
        self.peer_process = subprocess.Popen(
            peer_args,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        return self.wait_for_peer()

    def restart_gui(self):
        """Terminate the GUI process and start a fresh one with the same datadir.

        Used to verify that data (ban list, settings) persist across restarts.
        After this call, harness.driver is a new QmlDriver connected to the
        restarted process.
        """
        if self.driver:
            self.driver.close()
            self.driver = None
        if self.gui_process and self.gui_process.poll() is None:
            self.gui_process.send_signal(signal.SIGTERM)
            self.gui_process.wait(timeout=10)

        # Remove the stale socket file so the new GUI can bind to the same path.
        if os.path.exists(self.socket_path):
            os.remove(self.socket_path)

        env = dict(os.environ)
        env["QT_QPA_PLATFORM"] = "offscreen"
        gui_args = [
            self.gui_binary,
            f"-datadir={self.gui_datadir}",
            f"-test-automation={self.socket_path}",
            "-disablewallet",
            "-logtimemicros",
            "-debug",
            "-debugexclude=libevent",
            "-debugexclude=leveldb",
        ]
        print(f"  Restarting GUI: {' '.join(gui_args)}")
        self.gui_process = subprocess.Popen(
            gui_args,
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        self.driver = QmlDriver(self.socket_path, timeout=GUI_STARTUP_TIMEOUT)
        print("  QmlDriver reconnected after GUI restart.")

    def start_additional_peer(self):
        """Start a second peer bitcoind that connects to the GUI node."""
        datadir = os.path.join(self.tmpdir, "peer2_node")
        os.makedirs(datadir, exist_ok=True)
        conf_path = os.path.join(datadir, "bitcoin.conf")
        with open(conf_path, "w", encoding="utf8") as f:
            f.write("regtest=1\n")
            f.write("[regtest]\n")
            f.write("server=1\n")
            f.write(f"rpcuser={GUI_RPC_USER}\n")
            f.write(f"rpcpassword={GUI_RPC_PASS}\n")
            f.write(f"rpcport={PEER2_RPC_PORT}\n")
            f.write(f"port={PEER2_P2P_PORT}\n")
            f.write("listen=0\n")
            f.write(f"connect=127.0.0.1:{GUI_P2P_PORT}\n")
            f.write("discover=0\ndnsseed=0\nfixedseeds=0\nlistenonion=0\n")
            f.write("printtoconsole=0\nshrinkdebugfile=0\n")
        peer2_args = [self.bitcoind_binary, f"-datadir={datadir}"]
        print(f"  Starting second peer: {' '.join(peer2_args)}")
        self.peer2_process = subprocess.Popen(
            peer2_args,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )

    def wait_for_n_peers(self, n, timeout=30):
        """Poll getpeerinfo until at least n peers are connected.

        Returns a list of node IDs in connection order.
        """
        deadline = time.time() + timeout
        while time.time() < deadline:
            try:
                peers = self.rpc_call("getpeerinfo")
                if len(peers) >= n:
                    ids = [p["id"] for p in peers]
                    print(f"  {n} peer(s) connected: ids={ids}")
                    return ids
            except Exception:
                pass
            time.sleep(0.5)
        raise RuntimeError(f"Expected {n} peers after {timeout}s")


# ── Navigation helpers ────────────────────────────────────────────────────────

def navigate_to_peers(gui):
    """From the NodeRunner main screen, navigate to the Peers list page."""
    gui.click("nodeSettingsButton")
    gui.wait_for_page("settingsPeers", timeout_ms=3000)
    gui.click("settingsPeers")
    gui.wait_for_page("peers")


# ── Individual test cases ─────────────────────────────────────────────────────

def test_disconnect_peer(gui, harness, node_id):
    print("\n── test_disconnect_peer ──────────────────────────────────────────")

    gui.click(f"peerListItem_{node_id}")
    gui.wait_for_page("peerDetails", timeout_ms=5000)
    print(f"  Opened PeerDetails for node id={node_id}")

    gui.click("peerDisconnectButton")
    print("  Clicked Disconnect")

    harness.wait_for_no_peers()
    peers = harness.rpc_call("getpeerinfo")
    assert peers == [], f"Expected no peers after disconnect, got: {peers}"
    print("  PASSED: peer is disconnected")
    # PeerDetails.qml automatically calls root.back() on the onDisconnected
    # signal, so no manual navigation is needed here.


def test_ban_peer(gui, harness, node_id, duration_secs, duration_label):
    print(f"\n── test_ban_peer ({duration_label}) ──────────────────────────────")

    gui.click(f"peerListItem_{node_id}")
    gui.wait_for_page("peerDetails", timeout_ms=5000)
    print(f"  Opened PeerDetails for node id={node_id}")

    gui.click("peerBanButton")
    gui.wait_for_property(f"banDurationRow_{duration_secs}", "visible", True)

    gui.click(f"banDurationRow_{duration_secs}")

    gui.click("banConfirmButton")
    print(f"  Confirmed ban ({duration_label})")

    harness.wait_for_no_peers()

    peers = harness.rpc_call("getpeerinfo")
    assert peers == [], f"Expected no peers after ban, got: {peers}"

    banned = harness.rpc_call("listbanned")
    assert len(banned) == 1, f"Expected 1 banned entry, got: {banned}"
    entry = banned[0]
    assert abs(entry["ban_duration"] - duration_secs) < 10, \
        f"Expected ban duration ~{duration_secs}s, got {entry['ban_duration']}s"
    print(f"  PASSED: peer banned ({entry['address']}) for ~{duration_label}")


def test_unban_peer(gui, harness):
    print("\n── test_unban_peer ───────────────────────────────────────────────")

    # The ban list button is in the Peers page footer.
    gui.click("viewBannedPeersButton")
    gui.wait_for_page("bannedPeers", timeout_ms=5000)
    print("  Navigated to BannedPeers page")

    gui.click("unbanButton_0")
    harness.wait_for_no_banned()

    banned = harness.rpc_call("listbanned")
    assert banned == [], f"Expected empty ban list after unban, got: {banned}"
    print("  PASSED: ban list is empty")


def test_ban_persistence(harness):
    """Ban a peer, restart the GUI node, verify the ban survives the restart."""
    gui = harness.driver
    print("\n── test_ban_persistence ──────────────────────────────────────")

    node_id = harness.reconnect_peer()
    gui.wait_for_property(f"peerListItem_{node_id}", "visible", True, timeout_ms=5000)
    test_ban_peer(gui, harness, node_id, 3600, "1 hour")

    print("  Restarting GUI node ...")
    harness.restart_gui()

    navigate_to_peers(harness.driver)
    harness.driver.wait_for_property("viewBannedPeersButton", "visible", True)

    assert harness.driver.get_property("viewBannedPeersButton", "visible"), \
        "viewBannedPeersButton should be visible after GUI restart (ban persisted)"

    banned = harness.rpc_call("listbanned")
    assert len(banned) == 1, f"Expected 1 banned entry after restart, got: {banned}"
    print(f"  PASSED: ban for {banned[0]['address']} persisted across GUI restart")

    harness.rpc_call("clearbanned")


def _wait_for_peer_gone(harness, node_id, timeout=5):
    """Poll until node_id is no longer in getpeerinfo."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        peers = harness.rpc_call("getpeerinfo")
        if not any(p["id"] == node_id for p in peers):
            return
        time.sleep(0.3)
    raise RuntimeError(f"Peer {node_id} did not disconnect in time")


def test_disconnect_specific_peer(gui, harness):
    """With 2 peers connected, disconnect one and verify the other stays."""
    print("\n── test_disconnect_specific_peer ────────────────────────────")

    harness.start_additional_peer()
    peer_ids = harness.wait_for_n_peers(2)
    target_id = peer_ids[0]
    other_id = peer_ids[1]

    gui.wait_for_property(f"peerListItem_{target_id}", "visible", True, timeout_ms=5000)

    gui.click(f"peerListItem_{target_id}")
    gui.wait_for_page("peerDetails", timeout_ms=5000)
    gui.click("peerDisconnectButton")
    print(f"  Clicked Disconnect for peer {target_id}")

    _wait_for_peer_gone(harness, target_id)

    peers = harness.rpc_call("getpeerinfo")
    remaining_ids = [p["id"] for p in peers]
    assert target_id not in remaining_ids, \
        f"Peer {target_id} should be disconnected"
    assert other_id in remaining_ids, \
        f"Peer {other_id} should still be connected"
    print(f"  PASSED: peer {target_id} gone; peer {other_id} still present")


def test_ban_one_of_two_peers(gui, harness):
    """Banning a peer bans its IP subnet, disconnecting all peers from that IP."""
    print("\n── test_ban_one_of_two_peers ─────────────────────────────────")

    peer_ids = harness.wait_for_n_peers(2)
    target_id = peer_ids[0]

    gui.wait_for_property(f"peerListItem_{target_id}", "visible", True, timeout_ms=5000)

    gui.click(f"peerListItem_{target_id}")
    gui.wait_for_page("peerDetails", timeout_ms=5000)
    gui.click("peerBanButton")
    gui.wait_for_property("banDurationRow_3600", "visible", True)
    gui.click("banDurationRow_3600")
    gui.click("banConfirmButton")
    print(f"  Banned peer {target_id} (subnet: 127.0.0.1/32)")

    # Both peers share 127.0.0.1, so banning one peer's subnet disconnects both.
    harness.wait_for_no_peers()

    peers = harness.rpc_call("getpeerinfo")
    assert peers == [], \
        f"Expected no peers after banning 127.0.0.1/32 (subnet ban), got: {peers}"

    banned = harness.rpc_call("listbanned")
    assert len(banned) == 1, f"Expected 1 banned entry, got: {banned}"
    print(f"  PASSED: banning peer {target_id} disconnected all peers from "
          f"{banned[0]['address']} (subnet ban)")

    harness.rpc_call("clearbanned")


# ── Entry point ───────────────────────────────────────────────────────────────

def run_tests():
    harness = PeerQmlTestHarness()
    try:
        harness.start()
        gui = harness.driver

        # ── Wait for peer to connect ─────────────────────────────────────────
        print("\nWaiting for peer to connect ...")
        node_id = harness.wait_for_peer()

        # ── Navigate to Peers page ───────────────────────────────────────────
        print("\nNavigating to Peers page ...")
        navigate_to_peers(gui)
        gui.wait_for_property(f"peerListItem_{node_id}", "visible", True, timeout_ms=5000)

        assert not gui.get_property("viewBannedPeersButton", "visible"), \
            "viewBannedPeersButton should be hidden when ban list is empty"
        print("  Verified: viewBannedPeersButton hidden (no bans)")

        # ── Test 1: Disconnect ───────────────────────────────────────────────
        test_disconnect_peer(gui, harness, node_id)

        assert not gui.get_property("viewBannedPeersButton", "visible"), \
            "viewBannedPeersButton should still be hidden after disconnect"
        print("  Verified: viewBannedPeersButton hidden (no bans after disconnect)")

        # ── Tests 2–5: Ban with each duration ────────────────────────────────
        for i, (duration_secs, duration_label) in enumerate(BAN_DURATIONS):
            if i > 0:
                harness.rpc_call("clearbanned")
            print(f"\nReconnecting peer for ban test ({duration_label}) ...")
            node_id = harness.reconnect_peer()
            gui.wait_for_property(f"peerListItem_{node_id}", "visible", True, timeout_ms=5000)

            assert not gui.get_property("viewBannedPeersButton", "visible"), \
                f"viewBannedPeersButton should be hidden before ban ({duration_label})"
            print("  Verified: viewBannedPeersButton hidden (no active ban)")

            test_ban_peer(gui, harness, node_id, duration_secs, duration_label)

            gui.wait_for_property("viewBannedPeersButton", "visible", True)

            assert gui.get_property("viewBannedPeersButton", "visible"), \
                f"viewBannedPeersButton should be visible after ban ({duration_label})"
            print("  Verified: viewBannedPeersButton visible (peer banned)")

        # ── Test 6: Unban ────────────────────────────────────────────────────
        # The 1-year ban from the last iteration is still in the ban list.
        test_unban_peer(gui, harness)

        gui.click("bannedPeersBackButton")
        gui.wait_for_page("peers")

        assert not gui.get_property("viewBannedPeersButton", "visible"), \
            "viewBannedPeersButton should be hidden after UI unban"
        print("  Verified: viewBannedPeersButton hidden (ban cleared via UI)")

        # ── Test 7: Ban persistence across GUI restart ────────────────────
        print("\nRunning ban persistence test ...")
        test_ban_persistence(harness)
        gui = harness.driver

        # ── Tests 8–9: Multi-peer isolation ──────────────────────────────
        print("\nReconnecting peer for multi-peer disconnect test ...")
        peer1_id = harness.reconnect_peer()
        navigate_to_peers(gui)
        gui.wait_for_property(f"peerListItem_{peer1_id}", "visible", True, timeout_ms=5000)
        test_disconnect_specific_peer(gui, harness)

        # peer2 may still be running; reconnect peer1 then run ban test.
        print("\nReconnecting peer for multi-peer ban test ...")
        peer1_id = harness.reconnect_peer()
        navigate_to_peers(gui)
        gui.wait_for_property(f"peerListItem_{peer1_id}", "visible", True, timeout_ms=5000)
        test_ban_one_of_two_peers(gui, harness)

        print("\n" + "=" * 60)
        print("All peer management tests PASSED")
        print("=" * 60)

    except Exception as e:
        print(f"\nFAILED: {e}", file=sys.stderr)
        import traceback
        traceback.print_exc()
        if harness.gui_process:
            try:
                harness.gui_process.send_signal(signal.SIGTERM)
                try:
                    stderr_bytes = harness.gui_process.communicate(timeout=5)[1]
                except subprocess.TimeoutExpired:
                    harness.gui_process.kill()
                    stderr_bytes = harness.gui_process.communicate()[1]
                if stderr_bytes:
                    print("\n--- GUI node stderr ---", file=sys.stderr)
                    print(stderr_bytes.decode("utf-8", errors="replace")[-4000:], file=sys.stderr)
            except Exception:
                pass
        if harness.driver:
            dump_qml_tree(harness.driver)
        sys.exit(1)
    finally:
        harness.stop()


if __name__ == '__main__':
    run_tests()
