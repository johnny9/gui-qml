#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""One historical-binary compatibility journey; migration permutations stay in C++."""

import importlib.util
import json
import os
from pathlib import Path
import shutil

from test_framework.authproxy import AuthServiceProxy
from test_framework.test_framework import BitcoinTestFramework, SkipTest
from test_framework.util import assert_equal, get_auth_cookie, rpc_port


class QmlWalletMigrationCompatibilityTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 0
        self.setup_clean_chain = True

    def setup_network(self):
        pass

    def add_options(self, parser):
        parser.add_argument("--historical-binary-dir", type=Path, default=os.getenv("QML_HISTORICAL_BINARY_DIR"))
        parser.add_argument("--require-historical", action="store_true", help="Fail rather than skip when the designated compatibility tool is absent")

    def skip_test_if_missing_module(self):
        self.skip_if_no_qml()
        self.skip_if_no_wallet()
        directory = self.options.historical_binary_dir
        if directory is None or not (directory / "bitcoind").is_file() or not (directory / "bitcoin-cli").is_file():
            message = "Pinned v28.2 tools are absent; supply --historical-binary-dir explicitly (no automatic download)"
            if self.options.require_historical:
                raise FileNotFoundError(message)
            raise SkipTest(message)

    def run_test(self):
        fixtures = Path(__file__).resolve().parents[2] / "src/qml/test/fixtures"
        spec = importlib.util.spec_from_file_location("qml_legacy_fixture_generator", fixtures / "generate_legacy_wallets.py")
        generator = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(generator)
        metadata = json.loads((fixtures / "wallet/metadata.json").read_text(encoding="utf8"))
        assert_equal(metadata["bitcoind_sha256"], generator.BITCOIND_SHA256)
        self.log.info("Generate one fresh legacy backup with the pinned historical executable")
        backup = Path(self.options.tmpdir) / "historical-wallet.bak"
        with generator.historical_node(self.options.historical_binary_dir) as historical_rpc:
            generator.create_fixture(historical_rpc, "legacy-unencrypted", backup)

        harness = None
        try:
            harness = self.start_qml(extra_args=["-qml_onboarded=1", "-server=1", f"-rpcport={rpc_port(0)}", "-keypool=2"])
            gui = harness.driver
            self.wait_until(lambda: gui.get_property("mainWindow", "nodeStatus") == "Node is running", timeout=30)
            username, password = get_auth_cookie(harness.datadir, "regtest")
            rpc = AuthServiceProxy(f"http://{username}:{password}@127.0.0.1:{rpc_port(0)}")
            target = harness.datadir / "regtest/wallets/legacy-compat"
            target.mkdir(parents=True)
            shutil.copyfile(backup, target / "wallet.dat")
            self.log.info("Discover and migrate the historical backup through the production UI")
            gui.click("walletsTabButton")
            self.wait_until(lambda: gui.get_property("walletOverviewPage", "visible"))
            gui.click("walletRefreshButton")
            self.wait_until(lambda: any(item["objectName"] == "selectWallet-legacy-compat" for item in gui.list_objects()))
            gui.click("selectWallet-legacy-compat")
            self.wait_until(lambda: gui.get_property("migrateWalletDialog", "opened"))
            gui.click("migrateWalletSubmitButton")
            self.wait_until(lambda: gui.get_property("selectedWalletName", "text") == "legacy-compat", timeout=30)
            assert_equal(rpc.listwallets(), ["legacy-compat"])
            assert_equal(rpc.getwalletinfo()["descriptors"], True)
            gui.close_window()
            assert_equal(harness.wait_for_exit(), 0)
        except Exception:
            if harness is not None:
                try:
                    self.log.error("Migration UI objects: %s", [item for item in harness.driver.list_objects() if item["objectName"] in {"selectedWalletName", "migrateWalletDialog", "walletLoadError"}])
                except Exception:
                    pass
                self.stop_qml(harness)
                self.log.error("QML output:\n%s", harness.process_output())
            raise
        finally:
            if harness is not None:
                self.stop_qml(harness)


if __name__ == "__main__":
    QmlWalletMigrationCompatibilityTest(__file__).main()
