#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

import importlib.util
import json
from pathlib import Path
import plistlib
import tarfile
import tempfile
import unittest


MODULE_PATH = Path(__file__).parents[1] / "contrib" / "package_hwi_app.py"
SPEC = importlib.util.spec_from_file_location("package_hwi_app", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
package = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(package)
PUBLIC_KEY = "11" * 32


class HwiPackagingTest(unittest.TestCase):
    def make_inputs(self, root: Path) -> tuple[Path, Path]:
        app = root / "bitcoin-core-app"
        app.write_bytes(b"app")
        app.chmod(0o755)
        hwi = root / "hwi"
        (hwi / "_internal").mkdir(parents=True)
        (hwi / "hwi").write_bytes(b"hwi")
        (hwi / "hwi").chmod(0o755)
        (hwi / "_internal" / "library").write_bytes(b"library")
        (hwi / "_internal" / "alias").symlink_to("library")
        (hwi / "hwi-manifest.json").write_text("{}\n", encoding="ascii")
        (hwi / "hwi-manifest.sig").write_text("00\n", encoding="ascii")
        return app, hwi

    def test_linux_archive_preserves_layout_modes_and_symlinks(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            app, hwi = self.make_inputs(root)
            output = root / "artifact.tar.gz"
            package.package_linux(app, hwi, output, PUBLIC_KEY, None, 1234)

            with tarfile.open(output, "r:gz") as archive:
                names = archive.getnames()
                entrypoint = archive.getmember(f"{package.LINUX_ROOT}/libexec/hwi/hwi")
                alias = archive.getmember(f"{package.LINUX_ROOT}/libexec/hwi/_internal/alias")
                metadata_file = archive.extractfile(f"{package.LINUX_ROOT}/HWI-TEST-METADATA.json")
                assert metadata_file is not None
                metadata = json.load(metadata_file)

            self.assertIn(f"{package.LINUX_ROOT}/bin/bitcoin-core-app", names)
            self.assertEqual(entrypoint.mode & 0o777, 0o755)
            self.assertTrue(alias.issym())
            self.assertEqual(alias.linkname, "library")
            self.assertTrue(metadata["test_only"])
            self.assertEqual(metadata["hwi_manifest_public_key"], PUBLIC_KEY)

    def test_macos_app_has_expected_relative_sidecar_and_identity(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            app, hwi = self.make_inputs(root)
            icon = root / "bitcoin.icns"
            icon.write_bytes(b"icon")
            output = root / "Bitcoin-QML.app"
            package.package_macos(app, hwi, output, PUBLIC_KEY, icon)

            self.assertTrue((output / "Contents" / "MacOS" / "bitcoin-core-app").is_file())
            self.assertTrue((output / "Contents" / "MacOS" / "libexec" / "hwi" / "hwi").is_file())
            with (output / "Contents" / "Info.plist").open("rb") as plist_file:
                plist = plistlib.load(plist_file)
            self.assertEqual(plist["CFBundleExecutable"], "bitcoin-core-app")
            self.assertEqual(plist["LSMinimumSystemVersion"], "14.0")

    def test_rejects_invalid_key_and_non_executable_hwi(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            app, hwi = self.make_inputs(root)
            with self.assertRaisesRegex(ValueError, "32 bytes"):
                package.validate_inputs(app, hwi, "abcd")
            (hwi / "hwi").chmod(0o644)
            with self.assertRaisesRegex(ValueError, "not executable"):
                package.validate_inputs(app, hwi, PUBLIC_KEY)


if __name__ == "__main__":
    unittest.main()
