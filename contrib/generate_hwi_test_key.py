#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.
"""Create a throwaway BIP340 test key without printing the secret."""

import argparse
import os
from pathlib import Path
import secrets


SECP256K1_ORDER = 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364141


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    if args.output.exists() or args.output.is_symlink():
        raise FileExistsError(f"refusing to overwrite test key: {args.output}")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    secret = secrets.randbelow(SECP256K1_ORDER - 1) + 1
    descriptor = os.open(args.output, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    try:
        os.write(descriptor, f"{secret:064x}\n".encode("ascii"))
    finally:
        os.close(descriptor)
    print(args.output)


if __name__ == "__main__":
    main()
