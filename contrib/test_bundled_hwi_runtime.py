#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.
"""Exercise valid and tampered HWI bundles through a packaged gui-qml RPC."""

import argparse
import base64
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time
import urllib.error
import urllib.request


def rpc(cookie: Path, port: int, method: str) -> dict:
    credentials = cookie.read_text(encoding="ascii").strip()
    request = urllib.request.Request(
        f"http://127.0.0.1:{port}/",
        data=json.dumps({"jsonrpc": "2.0", "id": "hwi-test", "method": method, "params": []}).encode("utf8"),
        headers={
            "Authorization": "Basic " + base64.b64encode(credentials.encode("ascii")).decode("ascii"),
            "Content-Type": "application/json",
        },
    )
    try:
        with urllib.request.urlopen(request, timeout=5) as response:
            return json.load(response)
    except urllib.error.HTTPError as error:
        return json.loads(error.read())


def wait_for_cookie(cookie: Path, process: subprocess.Popen[bytes], timeout: int = 90) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if cookie.is_file():
            return
        status = process.poll()
        if status is not None:
            raise RuntimeError(f"bitcoin-core-app exited before RPC startup with status {status}")
        time.sleep(0.1)
    raise TimeoutError("timed out waiting for bitcoin-core-app RPC cookie")


def wait_for_rpc_ready(cookie: Path, port: int, process: subprocess.Popen[bytes], timeout: int = 90) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        status = process.poll()
        if status is not None:
            raise RuntimeError(f"bitcoin-core-app exited during RPC startup with status {status}")
        try:
            response = rpc(cookie, port, "getblockchaininfo")
            if response.get("error") is None:
                return
            if response.get("error", {}).get("code") != -28:
                raise RuntimeError(f"unexpected RPC startup response: {response}")
        except (OSError, ValueError):
            pass
        time.sleep(0.1)
    raise TimeoutError("timed out waiting for bitcoin-core-app RPC warmup")


def run_case(root: Path, executable_relative: Path, expect_tamper_rejection: bool) -> None:
    executable = root / executable_relative
    if not executable.is_file():
        raise ValueError(f"packaged application executable is missing: {executable}")

    with tempfile.TemporaryDirectory() as datadir_name:
        datadir = Path(datadir_name)
        port = 19443 if expect_tamper_rejection else 19442
        log_path = datadir / "process.log"
        environment = os.environ.copy()
        environment.setdefault("QT_QPA_PLATFORM", "offscreen")
        environment.setdefault("QT_QUICK_BACKEND", "software")
        command = [
            str(executable),
            "-regtest",
            f"-datadir={datadir}",
            f"-rpcport={port}",
            "-server=1",
            "-listen=0",
            "-dnsseed=0",
            "-discover=0",
            "-qml_onboarded=1",
        ]
        with log_path.open("wb") as log:
            process = subprocess.Popen(command, cwd=root, env=environment, stdout=log, stderr=subprocess.STDOUT)
        try:
            cookie = datadir / "regtest" / ".cookie"
            wait_for_cookie(cookie, process)
            wait_for_rpc_ready(cookie, port, process)
            response = rpc(cookie, port, "enumeratesigners")
            if expect_tamper_rejection:
                message = response.get("error", {}).get("message", "")
                if "missing or unlisted files" not in message:
                    raise AssertionError(f"tampered HWI was not rejected: {response}")
            else:
                result = response.get("result")
                if not isinstance(result, dict) or not isinstance(result.get("signers"), list):
                    raise AssertionError(f"valid bundled HWI did not enumerate: {response}")
            try:
                rpc(cookie, port, "stop")
            except (OSError, ValueError):
                pass
            process.wait(timeout=20)
        except Exception as error:
            process.terminate()
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=10)
            output = log_path.read_text(encoding="utf8", errors="replace")
            debug_log = datadir / "regtest" / "debug.log"
            debug_output = (
                debug_log.read_text(encoding="utf8", errors="replace")
                if debug_log.is_file()
                else "(regtest debug log was not created)"
            )
            raise RuntimeError(
                "packaged HWI runtime case failed\n"
                f"{type(error).__name__}: {error}\n"
                f"process output:\n{output}\n"
                f"regtest debug log:\n{debug_output}"
            ) from None


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package-root", required=True, type=Path)
    parser.add_argument("--executable-relative", required=True, type=Path)
    parser.add_argument("--hwi-relative", required=True, type=Path)
    args = parser.parse_args()

    if not args.package_root.is_dir():
        raise ValueError(f"package root does not exist: {args.package_root}")
    run_case(args.package_root, args.executable_relative, expect_tamper_rejection=False)

    with tempfile.TemporaryDirectory() as temporary_directory:
        tampered_root = Path(temporary_directory) / args.package_root.name
        shutil.copytree(args.package_root, tampered_root, symlinks=True)
        hwi_dir = tampered_root / args.hwi_relative
        if not hwi_dir.is_dir():
            raise ValueError(f"packaged HWI directory is missing: {hwi_dir}")
        (hwi_dir / "UNLISTED-TAMPER").write_bytes(b"tampered")
        run_case(tampered_root, args.executable_relative, expect_tamper_rejection=True)

    print("valid bundled HWI launched and undeclared-file tampering was rejected")


if __name__ == "__main__":
    main()
