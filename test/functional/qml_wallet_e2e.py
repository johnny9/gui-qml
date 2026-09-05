#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""One minimal UI wallet journey; regression permutations belong in integration."""

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

            gui.close_window()
            assert_equal(harness.wait_for_exit(), 0)
        except Exception:
            if harness is not None:
                try:
                    snapshot = [entry for entry in harness.driver.list_objects()
                                if entry["objectName"] in {"mainWindow", "walletOverviewPage", "selectedWalletName", "createWalletDialog", "createWalletError"}]
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
