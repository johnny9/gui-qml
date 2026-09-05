#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""One minimal UI wallet journey; regression permutations belong in integration."""

from decimal import Decimal

from test_framework.address import ADDRESS_BCRT1_UNSPENDABLE
from test_framework.authproxy import AuthServiceProxy
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, get_auth_cookie, rpc_port


class QmlWalletE2ETest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 0
        self.setup_clean_chain = True

    def setup_network(self):
        pass

    def skip_test_if_missing_module(self):
        self.skip_if_no_qml()
        self.skip_if_no_wallet()

    def run_test(self):
        harness = None
        try:
            harness = self.start_qml(extra_args=[
                "-qml_onboarded=1", "-server=1", f"-rpcport={rpc_port(0)}", "-keypool=2",
            ])
            gui = harness.driver
            self.wait_until(lambda: gui.get_property("mainWindow", "nodeStatus") == "Node is running", timeout=30)
            username, password = get_auth_cookie(harness.datadir, "regtest")
            rpc = AuthServiceProxy(f"http://{username}:{password}@127.0.0.1:{rpc_port(0)}")

            self.log.info("Create one standard wallet through the UI")
            gui.click("walletsTabButton")
            self.wait_until(lambda: gui.get_property("walletOverviewPage", "visible"))
            gui.click("createWalletButton")
            self.wait_until(lambda: gui.get_property("createWalletDialog", "opened"))
            gui.type_text("createWalletNameInput", "e2e_wallet")
            gui.click("createWalletSubmitButton")
            self.wait_until(lambda: gui.get_property("selectedWalletName", "text") == "e2e_wallet", timeout=30)
            assert_equal(rpc.listwallets(), ["e2e_wallet"])
            assert_equal(rpc.getwalletinfo()["walletname"], "e2e_wallet")

            self.log.info("Close and reload the persisted wallet through the UI")
            gui.click("closeWalletButton")
            self.wait_until(lambda: gui.get_property("selectedWalletName", "text") == "")
            self.wait_until(lambda: rpc.listwallets() == [])
            gui.click("selectWallet-e2e_wallet")
            self.wait_until(lambda: gui.get_property("selectedWalletName", "text") == "e2e_wallet", timeout=30)
            assert_equal(rpc.listwallets(), ["e2e_wallet"])
            assert_equal(rpc.getwalletinfo()["walletname"], "e2e_wallet")

            self.log.info("Create one receive request through the UI")
            gui.click("walletReceiveButton")
            self.wait_until(lambda: gui.get_property("walletReceivePage", "visible"))
            self.wait_until(lambda: gui.get_property("saveReceiveRequestButton", "enabled"))
            gui.type_text("receiveAmountInput", "0.001")
            gui.type_text("receiveLabelInput", "E2E receipt")
            gui.click("saveReceiveRequestButton")
            self.wait_until(lambda: gui.get_property("receiveRequestId", "text") != "")
            address = gui.get_property("receiveAddress", "text")
            assert rpc.validateaddress(address)["isvalid"]
            assert gui.get_property("receiveUri", "text").startswith(f"bitcoin:{address}?amount=0.001")
            gui.click("receiveBackButton")
            self.wait_until(lambda: gui.get_property("walletOverviewPage", "visible"))
            gui.click("walletActivityButton")
            self.wait_until(lambda: gui.get_property("walletActivityList", "count") == 1)

            self.log.info("Fund the saved address locally and observe the live receipt")
            # One coinbase goes to the request. Unrelated blocks only mature it;
            # this is fixture setup, not an additional GUI workflow.
            rpc.generatetoaddress(1, address)
            rpc.generatetoaddress(100, ADDRESS_BCRT1_UNSPENDABLE)
            self.wait_until(lambda: rpc.getbalance() == Decimal("50"))

            def receipt_visible():
                rows = [entry["objectName"] for entry in gui.list_objects()
                        if entry["objectName"].startswith("activityRow-") and ":request:" not in entry["objectName"]]
                return len(rows) == 1 and "E2E receipt" in gui.get_property(rows[0], "text") and "Confirmed" in gui.get_property(rows[0], "text")

            self.wait_until(receipt_visible, timeout=30)
            gui.click("activityBackButton")
            self.wait_until(lambda: gui.get_property("walletOverviewPage", "visible"))
            self.wait_until(lambda: Decimal(gui.get_property("selectedWalletBalance", "text").split()[0]) == Decimal("50"))

            gui.close_window()
            assert_equal(harness.wait_for_exit(), 0)
        except Exception:
            if harness is not None:
                try:
                    focused_names = {"mainWindow", "walletOverviewPage", "selectedWalletName", "selectedWalletBalance",
                                     "createWalletDialog", "createWalletError", "walletReceivePage", "receiveError",
                                     "receiveRequestId", "walletActivityPage", "walletActivityList"}
                    snapshot = [entry for entry in harness.driver.list_objects()
                                if entry["objectName"] in focused_names or entry["objectName"].startswith("activityRow-")]
                    for entry in snapshot:
                        if entry["objectName"].startswith("activityRow-"):
                            entry["text"] = harness.driver.get_property(entry["objectName"], "text")
                    self.log.error("Focused wallet UI snapshot: %s", snapshot)
                except Exception:
                    pass
                self.stop_qml(harness)
                self.log.error("QML process output:\n%s", harness.process_output())
            raise
        finally:
            if harness is not None:
                self.stop_qml(harness)


if __name__ == "__main__":
    QmlWalletE2ETest(__file__).main()
