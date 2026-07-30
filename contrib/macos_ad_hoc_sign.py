#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.
"""Ad-hoc sign every Mach-O file in an HWI sidecar, leaf-first."""

import argparse
from pathlib import Path
import platform
import subprocess


def is_macho(path: Path) -> bool:
    result = subprocess.run(
        ["file", "-b", str(path)],
        check=True,
        capture_output=True,
        text=True,
    )
    return "Mach-O" in result.stdout


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("bundle", type=Path)
    args = parser.parse_args()

    if platform.system() != "Darwin":
        raise RuntimeError("Mach-O signing must run natively on macOS")
    if not args.bundle.is_dir():
        raise ValueError(f"HWI bundle does not exist: {args.bundle}")

    files = [
        path
        for path in args.bundle.rglob("*")
        if path.is_file() and not path.is_symlink() and is_macho(path)
    ]
    files.sort(key=lambda path: (len(path.parts), path.as_posix()), reverse=True)
    if not files:
        raise RuntimeError("HWI bundle contains no Mach-O files")

    for path in files:
        subprocess.run(
            ["codesign", "--force", "--sign", "-", "--timestamp=none", str(path)],
            check=True,
        )
        subprocess.run(
            ["codesign", "--verify", "--strict", str(path)],
            check=True,
        )
    print(f"ad-hoc signed {len(files)} HWI Mach-O files")


if __name__ == "__main__":
    main()
