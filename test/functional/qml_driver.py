#!/usr/bin/env python3
# Copyright (c) 2024 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Driver for the QML test automation bridge.

Connects to the TestBridge Unix domain socket exposed by bitcoin-core-app
when launched with --test-automation=<socket_path>.  Provides a Pythonic
interface for functional tests to observe and drive the QML UI.
"""

import json
import socket
import time


class QmlDriverError(Exception):
    """Raised when the test bridge returns an error response."""


class QmlDriver:
    """Drives the QML GUI via the TestBridge Unix domain socket."""

    def __init__(self, socket_path, timeout=30):
        """Connect to the test bridge.

        Args:
            socket_path: Path to the Unix domain socket.
            timeout: Socket timeout in seconds for individual operations.
        """
        self.socket_path = socket_path
        self.timeout = timeout
        self.sock = None
        self._connect()

    def _connect(self):
        """Establish connection to the test bridge, retrying briefly."""
        deadline = time.time() + self.timeout
        last_err = None
        while time.time() < deadline:
            try:
                self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                self.sock.settimeout(self.timeout)
                self.sock.connect(self.socket_path)
                return
            except (ConnectionRefusedError, FileNotFoundError) as e:
                last_err = e
                if self.sock:
                    self.sock.close()
                    self.sock = None
                time.sleep(0.25)
        raise QmlDriverError(
            f"Could not connect to test bridge at {self.socket_path}: {last_err}"
        )

    def close(self):
        """Close the connection."""
        if self.sock:
            self.sock.close()
            self.sock = None

    # ── High-level commands ──────────────────────────────────────────

    def get_current_page(self):
        """Return the objectName (or class name) of the current page."""
        resp = self._send({"cmd": "get_current_page"})
        if "error" in resp:
            raise QmlDriverError(
                f"get_current_page() failed: {resp['error']}"
            )
        return resp["page"]

    def click(self, object_name):
        """Simulate a click on the named QML object."""
        resp = self._send({"cmd": "click", "objectName": object_name})
        if "error" in resp:
            raise QmlDriverError(f"click({object_name!r}) failed: {resp['error']}")

    def set_text(self, object_name, text):
        """Set the text property of the named QML object."""
        resp = self._send({"cmd": "set_text", "objectName": object_name, "text": text})
        if "error" in resp:
            raise QmlDriverError(
                f"set_text({object_name!r}) failed: {resp['error']}"
            )

    def get_text(self, object_name):
        """Return the text property of the named QML object."""
        resp = self._send({"cmd": "get_text", "objectName": object_name})
        if "error" in resp:
            raise QmlDriverError(
                f"get_text({object_name!r}) failed: {resp['error']}"
            )
        return resp["text"]

    def get_property(self, object_name, prop):
        """Return an arbitrary property value from a named QML object."""
        resp = self._send(
            {"cmd": "get_property", "objectName": object_name, "prop": prop}
        )
        if "error" in resp:
            raise QmlDriverError(
                f"get_property({object_name!r}, {prop!r}) failed: {resp['error']}"
            )
        return resp["value"]

    def wait_for_page(self, page_name, timeout_ms=5000):
        """Block until the named page/object is visible.

        Args:
            page_name: objectName of the page to wait for.
            timeout_ms: Maximum wait time in milliseconds.
        """
        resp = self._send(
            {"cmd": "wait_for_page", "page": page_name, "timeout": timeout_ms}
        )
        if "error" in resp:
            raise QmlDriverError(
                f"wait_for_page({page_name!r}) failed: {resp['error']}"
            )

    def list_objects(self):
        """Return a list of dicts with objectName and className for all
        named objects in the QML tree.  Useful for debugging."""
        resp = self._send({"cmd": "list_objects"})
        if "error" in resp:
            raise QmlDriverError(f"list_objects failed: {resp['error']}")
        return resp["objects"]

    # ── Transport layer ──────────────────────────────────────────────

    def _send(self, cmd):
        """Send a JSON command and return the parsed JSON response."""
        payload = json.dumps(cmd) + "\n"
        self.sock.sendall(payload.encode("utf-8"))
        return self._recv()

    def _recv(self):
        """Read a newline-delimited JSON response."""
        buf = b""
        while True:
            chunk = self.sock.recv(4096)
            if not chunk:
                raise QmlDriverError("Connection closed by test bridge")
            buf += chunk
            if b"\n" in buf:
                line, _ = buf.split(b"\n", 1)
                return json.loads(line)
