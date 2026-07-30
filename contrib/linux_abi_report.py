#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.
"""Report GLIBC symbol requirements and dynamic libraries for an artifact tree."""

import argparse
import json
from pathlib import Path
import re
import subprocess
from typing import Any, Dict, List


GLIBC_PATTERN = re.compile(r"GLIBC_(\d+(?:\.\d+)+)")
NEEDED_PATTERN = re.compile(r"\(NEEDED\).*\[(.+)]")


def version_key(version: str) -> tuple[int, ...]:
    return tuple(int(part) for part in version.split("."))


def is_elf(path: Path) -> bool:
    result = subprocess.run(
        ["file", "-b", str(path)],
        check=True,
        capture_output=True,
        text=True,
    )
    return "ELF" in result.stdout


def inspect_elf(path: Path, root: Path) -> Dict[str, Any]:
    versions = subprocess.run(
        ["readelf", "--version-info", "--wide", str(path)],
        check=True,
        capture_output=True,
        text=True,
    ).stdout
    dynamic = subprocess.run(
        ["readelf", "--dynamic", "--wide", str(path)],
        check=True,
        capture_output=True,
        text=True,
    ).stdout
    glibc = sorted(set(GLIBC_PATTERN.findall(versions)), key=version_key)
    needed = sorted(set(NEEDED_PATTERN.findall(dynamic)))
    return {
        "glibc_versions": glibc,
        "needed": needed,
        "path": path.relative_to(root).as_posix(),
    }


def build_report(root: Path) -> Dict[str, Any]:
    if not root.is_dir():
        raise ValueError(f"artifact tree does not exist: {root}")
    entries: List[Dict[str, Any]] = []
    all_versions = set()
    for path in sorted(root.rglob("*")):
        if path.is_symlink() or not path.is_file() or not is_elf(path):
            continue
        entry = inspect_elf(path, root)
        entries.append(entry)
        all_versions.update(entry["glibc_versions"])
    if not entries:
        raise RuntimeError("artifact tree contains no ELF files")
    ordered_versions = sorted(all_versions, key=version_key)
    return {
        "elf_files": entries,
        "format": 1,
        "maximum_glibc": ordered_versions[-1] if ordered_versions else None,
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    report = build_report(args.root)
    args.output.write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n",
        encoding="utf8",
    )
    print(f"maximum GLIBC requirement: {report['maximum_glibc']}")


if __name__ == "__main__":
    main()
