#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.
"""Assemble explicitly untrusted Linux or macOS gui-qml HWI test artifacts."""

import argparse
import gzip
import json
import os
from pathlib import Path
import plistlib
import re
import shutil
import stat
import tarfile
import tempfile
from typing import Optional


NOTICE = """UNTRUSTED TEST BUILD

This artifact uses a throwaway or local HWI manifest key. It is intended only
for integration and hardware testing and does not establish publisher identity.
"""
LINUX_ROOT = "bitcoin-qml-hwi-test"
PUBLIC_KEY_PATTERN = re.compile(r"^[0-9a-fA-F]{64}$")


def validate_regular(path: Path, description: str, executable: bool = False) -> None:
    status = path.lstat()
    if path.is_symlink() or not stat.S_ISREG(status.st_mode):
        raise ValueError(f"{description} must be a regular file: {path}")
    if executable and not status.st_mode & stat.S_IXUSR:
        raise ValueError(f"{description} is not executable: {path}")


def validate_inputs(app: Path, hwi_dir: Path, public_key: str) -> None:
    validate_regular(app, "application executable", executable=True)
    if not hwi_dir.is_dir() or hwi_dir.is_symlink():
        raise ValueError(f"HWI bundle must be a real directory: {hwi_dir}")
    validate_regular(hwi_dir / "hwi", "HWI entry point", executable=True)
    validate_regular(hwi_dir / "hwi-manifest.json", "HWI manifest")
    validate_regular(hwi_dir / "hwi-manifest.sig", "HWI signature")
    if not PUBLIC_KEY_PATTERN.fullmatch(public_key):
        raise ValueError("HWI public key must be 32 bytes of hexadecimal")


def write_metadata(root: Path, public_key: str, abi_report: Optional[Path]) -> None:
    (root / "UNTRUSTED-TEST-BUILD.txt").write_text(NOTICE, encoding="utf8")
    metadata = {
        "hwi_manifest_public_key": public_key.lower(),
        "schema": 1,
        "test_only": True,
    }
    (root / "HWI-TEST-METADATA.json").write_text(
        json.dumps(metadata, indent=2, sort_keys=True) + "\n",
        encoding="utf8",
    )
    if abi_report is not None:
        validate_regular(abi_report, "Linux ABI report")
        shutil.copy2(abi_report, root / "LINUX-ABI-REPORT.json")


def normalized_tar_info(info: tarfile.TarInfo, epoch: int) -> tarfile.TarInfo:
    info.uid = 0
    info.gid = 0
    info.uname = "root"
    info.gname = "root"
    info.mtime = epoch
    return info


def add_tree_to_tar(archive: tarfile.TarFile, source: Path, archive_root: str, epoch: int) -> None:
    paths = [source, *sorted(source.rglob("*"), key=lambda path: path.relative_to(source).as_posix())]
    for path in paths:
        relative = path.relative_to(source)
        archive_name = archive_root if relative == Path(".") else f"{archive_root}/{relative.as_posix()}"
        info = normalized_tar_info(archive.gettarinfo(str(path), archive_name), epoch)
        if info.isreg():
            with path.open("rb") as contents:
                archive.addfile(info, contents)
        else:
            archive.addfile(info)


def package_linux(
    app: Path,
    hwi_dir: Path,
    output: Path,
    public_key: str,
    abi_report: Optional[Path],
    epoch: int,
) -> None:
    if output.exists() or output.is_symlink():
        raise FileExistsError(f"refusing to overwrite artifact: {output}")
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=output.parent) as temporary_directory:
        root = Path(temporary_directory) / LINUX_ROOT
        (root / "bin").mkdir(parents=True)
        (root / "libexec").mkdir()
        shutil.copy2(app, root / "bin" / "bitcoin-core-app")
        shutil.copytree(hwi_dir, root / "libexec" / "hwi", symlinks=True)
        write_metadata(root, public_key, abi_report)

        with output.open("xb") as raw_output:
            with gzip.GzipFile(filename="", mode="wb", fileobj=raw_output, mtime=epoch) as compressed:
                with tarfile.open(fileobj=compressed, mode="w", format=tarfile.PAX_FORMAT) as archive:
                    add_tree_to_tar(archive, root, LINUX_ROOT, epoch)


def package_macos(
    app: Path,
    hwi_dir: Path,
    output: Path,
    public_key: str,
    icon: Optional[Path] = None,
) -> None:
    if output.exists() or output.is_symlink():
        raise FileExistsError(f"refusing to overwrite app bundle: {output}")
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=output.parent) as temporary_directory:
        app_root = Path(temporary_directory) / output.name
        macos = app_root / "Contents" / "MacOS"
        resources = app_root / "Contents" / "Resources"
        macos.mkdir(parents=True)
        resources.mkdir()
        shutil.copy2(app, macos / "bitcoin-core-app")
        shutil.copytree(hwi_dir, macos / "libexec" / "hwi", symlinks=True)
        icon_path = icon or Path(__file__).parents[1] / "bitcoin" / "src" / "qt" / "res" / "icons" / "bitcoin.icns"
        validate_regular(icon_path, "macOS application icon")
        shutil.copy2(icon_path, resources / "bitcoin.icns")
        write_metadata(resources, public_key, None)
        plist = {
            "CFBundleDevelopmentRegion": "en",
            "CFBundleDisplayName": "Bitcoin QML HWI Test",
            "CFBundleExecutable": "bitcoin-core-app",
            "CFBundleIconFile": "bitcoin.icns",
            "CFBundleIdentifier": "org.bitcoincore.qml.hwi-test",
            "CFBundleName": "Bitcoin QML HWI Test",
            "CFBundlePackageType": "APPL",
            "CFBundleShortVersionString": "0.1-test",
            "CFBundleVersion": "1",
            "LSMinimumSystemVersion": "14.0",
            "NSHighResolutionCapable": True,
        }
        with (app_root / "Contents" / "Info.plist").open("wb") as plist_file:
            plistlib.dump(plist, plist_file, sort_keys=True)
        os.replace(app_root, output)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--platform", required=True, choices=("linux", "macos"))
    parser.add_argument("--app", required=True, type=Path)
    parser.add_argument("--hwi-dir", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--public-key", required=True)
    parser.add_argument("--abi-report", type=Path)
    parser.add_argument("--icon", type=Path)
    parser.add_argument("--source-date-epoch", type=int, default=1546300800)
    args = parser.parse_args()

    validate_inputs(args.app, args.hwi_dir, args.public_key)
    if args.platform == "linux":
        package_linux(
            args.app,
            args.hwi_dir,
            args.output,
            args.public_key,
            args.abi_report,
            args.source_date_epoch,
        )
    else:
        if args.abi_report is not None:
            raise ValueError("--abi-report is only valid for Linux")
        package_macos(args.app, args.hwi_dir, args.output, args.public_key, args.icon)
    print(args.output)


if __name__ == "__main__":
    main()
