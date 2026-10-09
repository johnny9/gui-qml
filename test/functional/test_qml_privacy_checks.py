#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Regression tests for batched GUI passphrase checks."""

import unittest
from unittest import mock

from qml_driver import QmlDriver
from qml_test_password_wallet import assert_passphrase_not_in_ui


class GuiPrivacyChecksTest(unittest.TestCase):
    def test_seeded_passphrase_in_duplicate_named_object_fails(self):
        gui = mock.Mock()
        gui.list_objects.return_value = [
            {"objectName": "duplicate", "className": "TextInput", "text": "public text"},
            {"objectName": "duplicate", "className": "TextInput", "text": "seeded passphrase"},
        ]
        with self.assertRaisesRegex(AssertionError, "Passphrase leaked through QML text properties"):
            assert_passphrase_not_in_ui(gui, "seeded passphrase")
        gui.list_objects.assert_called_once_with(include_text=True)
        gui.get_property.assert_not_called()

    def test_cleared_and_non_text_objects_pass(self):
        gui = mock.Mock()
        gui.list_objects.return_value = [
            {"objectName": "password", "text": ""},
            {"objectName": "label", "text": "Enter wallet password"},
            {"objectName": "container"},
            {"objectName": "numeric", "text": 123},
        ]
        assert_passphrase_not_in_ui(gui, "seeded passphrase")
        gui.list_objects.assert_called_once_with(include_text=True)
        gui.get_property.assert_not_called()

    def test_driver_requests_text_only_when_explicit(self):
        driver = QmlDriver.__new__(QmlDriver)
        driver._send = mock.Mock(return_value={"objects": []})
        self.assertEqual(driver.list_objects(), [])
        driver._send.assert_called_once_with({"cmd": "list_objects"})
        driver._send.reset_mock()
        self.assertEqual(driver.list_objects(include_text=True), [])
        driver._send.assert_called_once_with({"cmd": "list_objects", "includeText": True})


if __name__ == "__main__":
    unittest.main()
