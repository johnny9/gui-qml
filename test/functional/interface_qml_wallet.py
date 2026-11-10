#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Wallet creation, encrypted backup, close/reload and restoration."""

import shutil
from pathlib import Path
from qml_scenarios import run_scenarios
from qml_smoke_lib import (
    close_wallet,
    create_wallet,
    import_wallet,
    open_wallet_settings,
    stop_gui_checked,
    wallet_scenario,
)
from qml_wallet_test_lib import (
    open_wallet_selector,
    rpc_call,
)


def wallet_smoke(harness, checkpoint):
    """Exercise wallet management happy paths, including a real process restart."""
    name = "smoke_wallet"
    password = "correct horse battery staple"
    harness.start_gui()
    create_wallet(harness, name, password)
    address = rpc_call(harness.gui_rpc_port, "getnewaddress", ["Smoke signing address", "legacy"], wallet=name)
    checkpoint("encrypted wallet created")

    directory = Path(harness.tmpdir) / "backups"
    directory.mkdir()
    open_wallet_settings(harness.driver)
    gui = harness.driver
    gui.set_text("walletSettingsBackupPathField", str(directory))
    gui.click("walletBackupRow")
    backup = directory / f"{name}.bak"
    gui.wait_for_property("walletSettingsPage", "errorText", "")
    assert backup.is_file() and backup.stat().st_size > 0
    checkpoint("wallet backup created")

    close_wallet(harness, name)
    stop_gui_checked(harness)
    harness.start_gui()
    gui = harness.driver
    gui.wait_for_property("walletBadge", "loading", False, timeout_ms=30000)
    open_wallet_selector(gui)
    gui.click(f"walletSelectItem_{name}")
    gui.wait_for_property("walletBadge", "text", name, timeout_ms=30000)
    checkpoint("closed wallet loaded after restart")

    close_wallet(harness, name)
    restored = Path(harness.tmpdir) / "restored_smoke.bak"
    shutil.copyfile(backup, restored)
    import_wallet(harness, restored)
    assert rpc_call(harness.gui_rpc_port, "getaddressinfo", [address], wallet=restored.stem)["ismine"]
    checkpoint("backup restored through the wallet wizard")

def backup_recovery_disclosure(harness, checkpoint):
    """Set a password in the GUI, then restore separate old/fresh copies."""
    harness.start_gui()
    gui = harness.driver
    name = "recovery_live"
    gui.wait_for_property("walletBadge", "loading", False, timeout_ms=30000)
    rpc_call(harness.gui_rpc_port, "createwallet", {"wallet_name": name})
    gui.wait_for_property("walletBadge", "text", name, timeout_ms=30000)
    original_address = rpc_call(harness.gui_rpc_port, "getnewaddress", wallet=name)
    old_backup = Path(harness.tmpdir) / "before-password.bak"
    fresh_backup = Path(harness.tmpdir) / "after-password.bak"
    open_wallet_settings(gui)
    gui.set_text("walletSettingsBackupPathField", str(old_backup))
    gui.click("walletBackupRow")
    assert old_backup.is_file()
    original_bytes = old_backup.read_bytes()
    gui.click("walletPasswordRow")
    gui.wait_for_page("walletPasswordSettingsPage")
    intro = gui.get_property("walletPasswordIntroTextDescription", "text")
    assert "receiving and change keys" in intro and "earlier backups" in intro
    gui.set_text("walletPasswordNewField", "recovery password")
    gui.set_text("walletPasswordConfirmField", "recovery password")
    gui.click("walletPasswordConfirmToggle")
    gui.wait_for_property("walletPasswordSaveButton", "enabled", True)
    gui.click("walletPasswordSaveButton")
    gui.wait_for_page("walletSettingsPage")
    gui.wait_for_property("walletPasswordRow", "title", "Update password")
    text = gui.get_property("walletBackupRow", "description")
    assert "earlier backups" in text and "Keep your older backups" in text
    assert rpc_call(harness.gui_rpc_port, "getwalletinfo", wallet=name)["unlocked_until"] == 0
    later_address = rpc_call(harness.gui_rpc_port, "getnewaddress", wallet=name)
    gui.set_text("walletSettingsBackupPathField", str(fresh_backup))
    gui.click("walletBackupRow")
    assert fresh_backup.is_file() and fresh_backup.stat().st_size > 0
    checkpoint("password disclosure retained; fresh-backup action wrote a real encrypted backup")
    for restored, backup in (("recovery_old", old_backup), ("recovery_fresh", fresh_backup)):
        rpc_call(harness.gui_rpc_port, "restorewallet", [restored, str(backup)])
        assert rpc_call(harness.gui_rpc_port, "getaddressinfo", [original_address], wallet=restored)["ismine"]
    assert not rpc_call(harness.gui_rpc_port, "getaddressinfo", [later_address], wallet="recovery_old")["ismine"]
    assert rpc_call(harness.gui_rpc_port, "getaddressinfo", [later_address], wallet="recovery_fresh")["ismine"]
    assert rpc_call(harness.gui_rpc_port, "getaddressinfo", [later_address], wallet=name)["ismine"]
    assert old_backup.read_bytes() == original_bytes
    assert name in rpc_call(harness.gui_rpc_port, "listwallets")
    open_wallet_selector(gui)
    gui.click(f"walletSelectItem_{name}")
    gui.wait_for_property("walletBadge", "text", name, timeout_ms=30000)
    checkpoint("separate old/fresh restores verify coverage; original live wallet and old backup preserved")


if __name__ == "__main__":
    raise SystemExit(run_scenarios('Wallet management process journeys', [
        wallet_scenario("backup-reload-restore", 4, wallet_smoke),
        wallet_scenario("password-backup-recovery", 4, backup_recovery_disclosure),
    ]))
