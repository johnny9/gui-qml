#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Exercise interface evolution without changing the pinned Core headers."""

import re
import unittest
from unittest.mock import patch

import generate_thread_audit as audit


class ThreadAuditGeneratorTests(unittest.TestCase):
    def setUp(self):
        self.headers = audit.read_headers()
        replacement = patch.object(audit, "read_headers", return_value=self.headers)
        replacement.start()
        self.addCleanup(replacement.stop)

    def add_declaration(self, interface, declaration):
        header = "chain" if interface == "ChainClient" else "node"
        source = self.headers[header]
        opening = re.search(r"class " + interface + r"\s*\{", source).end()
        self.headers[header] = source[:opening] + "\npublic:\n" + declaration + "\n" + source[opening:]

    def test_new_value_methods_default_to_worker_including_inherited_defaults(self):
        self.add_declaration("Node", "virtual std::vector<std::optional<int>> newSnapshot() = 0;")
        self.add_declaration("ChainClient", "virtual bool newDefault() { return true; }")
        generated = audit.generate()
        self.assertIn('check("Node::newSnapshot", ThreadPolicy::Worker)', generated)
        self.assertIn('check("WalletLoader::newDefault", ThreadPolicy::Worker)', generated)

    def test_handle_results_require_explicit_wrapping(self):
        for result in ("Wallet*", "Wallet&", "std::unique_ptr<Wallet>",
                       "std::vector<std::shared_ptr<Wallet>>", "UnknownReply"):
            with self.subTest(result=result):
                original = self.headers["node"]
                self.add_declaration("Node", f"virtual {result} newResult() = 0;")
                with self.assertRaisesRegex(ValueError, "Review backend handle or result type for Node::newResult"):
                    audit.generate()
                self.headers["node"] = original

    def test_handle_output_parameters_require_review(self):
        self.add_declaration("Node", "virtual void newOutput(std::unique_ptr<Wallet>& wallet) = 0;")
        with self.assertRaisesRegex(ValueError, "Review backend handle or result type for Node::newOutput"):
            audit.generate()

    def test_callback_alias_cannot_deliver_an_unchecked_wallet(self):
        self.add_declaration("Node", """
            using BackendAlias = Wallet;
            using DeliveryFn = std::function<void(std::unique_ptr<BackendAlias> wallet)>;
            virtual std::unique_ptr<Handler> newCallback(DeliveryFn fn) = 0;
        """)
        with self.assertRaisesRegex(ValueError, "Review backend handle or result type for Node::newCallback"):
            audit.generate()

    def test_existing_value_method_cannot_start_returning_handles(self):
        self.headers["node"] = self.headers["node"].replace("bool getNetworkActive()", "Wallet* getNetworkActive()")
        with self.assertRaisesRegex(ValueError, "Review backend handle or result type for Node::getNetworkActive"):
            audit.generate()

    def test_exception_signature_changes_require_review(self):
        self.headers["node"] = self.headers["node"].replace("bool baseInitialize()", "bool baseInitialize(int mode)")
        with self.assertRaisesRegex(ValueError, "Review changed GUI-thread exception for Node::baseInitialize"):
            audit.generate()

    def test_removed_exceptions_must_be_deleted(self):
        self.headers["node"] = self.headers["node"].replace("virtual bool baseInitialize() = 0;", "")
        with self.assertRaisesRegex(ValueError, "Remove stale exceptions.*Node::baseInitialize"):
            audit.generate()

    def test_handle_forwarding_signature_changes_require_review(self):
        self.headers["node"] = self.headers["node"].replace("WalletLoader& walletLoader()", "WalletLoader* walletLoader()")
        with self.assertRaisesRegex(ValueError, "Review changed handle forwarding for Node::walletLoader"):
            audit.generate()

    def test_unsupported_virtual_declaration_cannot_escape_auditing(self):
        self.add_declaration("Node", "virtual bool newConstMethod() const = 0;")
        with self.assertRaisesRegex(ValueError, "Unsupported virtual declaration in Node"):
            audit.generate()

    def test_overload_cannot_inherit_an_exception_by_name(self):
        self.headers["node"] = self.headers["node"].replace(
            "virtual bool baseInitialize() = 0;",
            "virtual bool baseInitialize() = 0;\nvirtual bool baseInitialize(int mode) = 0;")
        with self.assertRaisesRegex(ValueError, "Review overloaded method: Node::baseInitialize"):
            audit.generate()


if __name__ == "__main__":
    unittest.main()
