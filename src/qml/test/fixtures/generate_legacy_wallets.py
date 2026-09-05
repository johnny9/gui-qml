#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Generate synthetic fixtures once with the repository-pinned Bitcoin Core 28.2.

No test invokes this generator or downloads historical executables implicitly.
"""

import argparse
from contextlib import contextmanager
import hashlib
import json
from pathlib import Path
import socket
import subprocess
import sys
import tempfile


PASSPHRASE = "qml-legacy-test-only"
BITCOIND_SHA256 = "79011d54248a80c76bf2865f91f540963c83634e170365139180372a4410f3b1"
FIXTURES = ("legacy-unencrypted", "legacy-encrypted", "legacy-companions", "external-signer", "multikey-watchonly")


def validate_binary(binary_dir):
    daemon = binary_dir / "bitcoind"
    if hashlib.sha256(daemon.read_bytes()).hexdigest() != BITCOIND_SHA256:
        raise ValueError("Historical bitcoind checksum does not match the pinned v28.2 Linux fixture generator")
    version = subprocess.check_output([daemon, "--version"], text=True).splitlines()[0]
    if version != "Bitcoin Core version v28.2.0":
        raise ValueError(f"Expected pinned v28.2, got {version}")
    return version


@contextmanager
def historical_node(binary_dir, signer=None):
    """Bounded, isolated historical process shared by fixture and compatibility lanes."""
    validate_binary(binary_dir)
    with socket.socket() as reservation:
        reservation.bind(("127.0.0.1", 0))
        rpc_port = reservation.getsockname()[1]
    with tempfile.TemporaryDirectory(prefix="qml-legacy-generator-") as directory:
        common = [f"-datadir={directory}", "-regtest", f"-rpcport={rpc_port}"]
        command = [binary_dir / "bitcoind", *common, "-server", "-listen=0", "-dnsseed=0", "-connect=0", "-keypool=2", "-deprecatedrpc=create_bdb", "-printtoconsole=0"]
        if signer is not None:
            command.append(f"-signer={sys.executable} {signer}")
        process = subprocess.Popen(command, cwd=directory, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)

        def rpc(*arguments):
            return subprocess.check_output([binary_dir / "bitcoin-cli", *common, "-rpcwait", "-rpcwaittimeout=15", *arguments], text=True, timeout=30).strip()

        try:
            rpc("getblockchaininfo")
            yield rpc
        finally:
            if process.poll() is None:
                try:
                    rpc("stop")
                    process.wait(timeout=20)
                except (subprocess.SubprocessError, OSError):
                    process.terminate()
                    try:
                        process.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait(timeout=10)
            output = process.stderr.read().decode()
            process.stderr.close()
            if process.returncode != 0:
                raise RuntimeError(output)


def create_fixture(rpc, name, destination):
    """Use Core RPC only; canonical fixtures are never opened for modification."""
    password = PASSPHRASE if name == "legacy-encrypted" else ""
    legacy = name.startswith("legacy-")
    args = ["-named", "createwallet", f"wallet_name={name}", f"descriptors={str(not legacy).lower()}", f"passphrase={password}", "load_on_startup=false"]
    if not legacy:
        args.append("disable_private_keys=true")
    if name == "external-signer":
        args.append("external_signer=true")
    rpc(*args)

    def wallet_rpc(*arguments):
        return rpc(f"-rpcwallet={name}", *arguments)

    if legacy or name == "external-signer":
        wallet_rpc("getnewaddress")
    if name in ("legacy-companions", "multikey-watchonly"):
        # Fixed public keys with well-known test-only secrets. Never fund them.
        public_keys = [
            "0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798",
            "02c6047f9441ed7d6d3045406e95c07cd85c778e4b8cef3ca7abac09b95c709ee5",
        ]
        if legacy:
            for watched in (True, False):
                address = wallet_rpc("getnewaddress")
                multisig = json.loads(wallet_rpc("addmultisigaddress", "2", json.dumps([address, *public_keys])))
                if watched:
                    wallet_rpc("importaddress", multisig["address"], "", "false")
        else:
            descriptor = f"wsh(sortedmulti(2,{','.join(public_keys)}))"
            descriptor = json.loads(rpc("getdescriptorinfo", descriptor))["descriptor"]
            result = json.loads(wallet_rpc("importdescriptors", json.dumps([{"desc": descriptor, "timestamp": "now"}])))
            if not result[0]["success"]:
                raise RuntimeError(result)
    rpc(f"-rpcwallet={name}", "backupwallet", str(destination.resolve()))
    return hashlib.sha256(destination.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary-dir", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--fixtures", nargs="+", choices=FIXTURES, default=list(FIXTURES))
    parser.add_argument("--signer", type=Path, default=Path(__file__).resolve().parents[4] / "test/functional/mocks/signer.py")
    args = parser.parse_args()
    daemon = args.binary_dir / "bitcoind"
    version = validate_binary(args.binary_dir)
    args.output.mkdir(parents=True, exist_ok=True)
    filenames = [f"{name}.bak" for name in args.fixtures] + ["metadata.json"]
    if any((args.output / name).exists() for name in filenames):
        raise FileExistsError("Refusing to overwrite canonical fixtures")
    metadata = {
        "generator_version": version,
        "archive_sha256": "98add5f220c01b387343b70edeb6273403fe081e22cd85fda132704cdcaa98aa",
        "bitcoind_sha256": hashlib.sha256(daemon.read_bytes()).hexdigest(),
        "network": "regtest",
        "encrypted_passphrase": PASSPHRASE,
        "command": " ".join(sys.argv),
        "fixtures": {},
    }
    signer = args.signer if "external-signer" in args.fixtures else None
    if signer is not None:
        metadata["synthetic_signer_sha256"] = hashlib.sha256(signer.read_bytes()).hexdigest()
    with historical_node(args.binary_dir, signer) as rpc:
        for name in args.fixtures:
            destination = args.output / f"{name}.bak"
            metadata["fixtures"][destination.name] = create_fixture(rpc, name, destination)
    (args.output / "metadata.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf8")


if __name__ == "__main__":
    main()
