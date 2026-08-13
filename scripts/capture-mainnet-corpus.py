#!/usr/bin/env python3
"""Capture finalized transaction bytes using only read-only mainnet RPC methods."""

from __future__ import annotations

import argparse
import base64
import binascii
import datetime
import hashlib
import json
import pathlib
import subprocess
import sys
import time
import urllib.error
import urllib.request
from typing import Any

MAINNET_GENESIS_HASH = "5eykt4UsFv8P8NJdTREpY1vzqKqZKvdpKuc147dw2N9d"
SYSTEM_PROGRAM_ID = "11111111111111111111111111111111"
READ_ONLY_METHODS = frozenset(
    {"getGenesisHash", "getSignaturesForAddress", "getTransaction"}
)


class RpcError(RuntimeError):
    pass


def rpc_call(url: str, method: str, params: list[Any]) -> Any:
    if method not in READ_ONLY_METHODS:
        raise RpcError(f"refusing non-read-only RPC method {method}")
    payload = json.dumps(
        {"jsonrpc": "2.0", "id": 1, "method": method, "params": params},
        separators=(",", ":"),
    ).encode("utf-8")
    request = urllib.request.Request(
        url,
        data=payload,
        headers={"Content-Type": "application/json", "User-Agent": "solc-corpus/1"},
        method="POST",
    )
    last_error: Exception | None = None
    for attempt in range(5):
        try:
            with urllib.request.urlopen(request, timeout=30) as response:
                document = json.loads(response.read())
            if "error" in document:
                raise RpcError(f"{method}: {document['error']}")
            return document["result"]
        except (OSError, KeyError, json.JSONDecodeError, urllib.error.HTTPError) as error:
            last_error = error
            if isinstance(error, urllib.error.HTTPError) and error.code not in (429, 503):
                break
            time.sleep(2**attempt)
    raise RpcError(f"{method} failed after retries: {last_error}")


def run_wire(project_root: pathlib.Path, command: str, transaction: pathlib.Path) -> str:
    process = subprocess.run(
        [
            "cargo",
            "run",
            "--quiet",
            "-p",
            "solc-orchestrator",
            "--bin",
            "solc-wire",
            "--",
            command,
            f"@{transaction}",
        ],
        cwd=project_root,
        check=False,
        capture_output=True,
        text=True,
    )
    if process.returncode != 0:
        detail = process.stderr.strip() or process.stdout.strip()
        raise RuntimeError(f"solc-wire {command} rejected {transaction.name}: {detail}")
    return process.stdout.strip()


def create_output(path: pathlib.Path) -> None:
    if path.exists() and any(path.iterdir()):
        raise ValueError(f"output directory is not empty: {path}")
    path.mkdir(parents=True, exist_ok=True)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=pathlib.Path)
    parser.add_argument("--count", type=int, default=8)
    parser.add_argument("--rpc-url", default="https://api.mainnet-beta.solana.com")
    parser.add_argument("--address", default=SYSTEM_PROGRAM_ID)
    arguments = parser.parse_args()
    if arguments.count < 1 or arguments.count > 32:
        parser.error("--count must be between 1 and 32")

    project_root = pathlib.Path(__file__).resolve().parent.parent
    output = arguments.output.resolve()
    try:
        create_output(output)
        genesis_hash = rpc_call(arguments.rpc_url, "getGenesisHash", [])
        if genesis_hash != MAINNET_GENESIS_HASH:
            raise RpcError(
                f"mainnet genesis mismatch: expected {MAINNET_GENESIS_HASH}, got {genesis_hash}"
            )
        signatures = rpc_call(
            arguments.rpc_url,
            "getSignaturesForAddress",
            [
                arguments.address,
                {"commitment": "finalized", "limit": min(arguments.count * 8, 256)},
            ],
        )
        entries: list[dict[str, Any]] = []
        for signature_record in signatures:
            signature = signature_record.get("signature")
            if not isinstance(signature, str):
                continue
            transaction_result = rpc_call(
                arguments.rpc_url,
                "getTransaction",
                [
                    signature,
                    {
                        "commitment": "finalized",
                        "encoding": "base64",
                        "maxSupportedTransactionVersion": 0,
                    },
                ],
            )
            if transaction_result is None:
                continue
            encoded = transaction_result.get("transaction")
            if (
                not isinstance(encoded, list)
                or len(encoded) != 2
                or encoded[1] != "base64"
                or not isinstance(encoded[0], str)
            ):
                raise RpcError(f"unexpected getTransaction encoding for {signature}")
            try:
                transaction_bytes = base64.b64decode(encoded[0], validate=True)
            except binascii.Error as error:
                raise RpcError(f"invalid transaction base64 for {signature}: {error}") from error

            transaction_path = output / f"{len(entries):02d}-{signature}.bin"
            transaction_path.write_bytes(transaction_bytes)
            summary_text = run_wire(project_root, "inspect", transaction_path)
            run_wire(project_root, "verify", transaction_path)
            roundtrip_hex = run_wire(project_root, "roundtrip", transaction_path)
            if bytes.fromhex(roundtrip_hex) != transaction_bytes:
                raise RuntimeError(f"byte-for-byte round trip changed {signature}")
            entries.append(
                {
                    "file": transaction_path.name,
                    "signature": signature,
                    "slot": transaction_result.get("slot"),
                    "blockTime": transaction_result.get("blockTime"),
                    "bytes": len(transaction_bytes),
                    "sha256": hashlib.sha256(transaction_bytes).hexdigest(),
                    "summary": json.loads(summary_text),
                }
            )
            if len(entries) == arguments.count:
                break
        if len(entries) != arguments.count:
            raise RpcError(
                f"only found {len(entries)} compatible finalized transactions; "
                f"requested {arguments.count}"
            )
        manifest = {
            "schema": "solc-mainnet-corpus-v1",
            "capturedAt": datetime.datetime.now(datetime.UTC).isoformat(),
            "genesisHash": genesis_hash,
            "address": arguments.address,
            "readOnlyMethods": sorted(READ_ONLY_METHODS),
            "transactions": entries,
        }
        (output / "manifest.json").write_text(
            json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
    except (OSError, RpcError, RuntimeError, ValueError, json.JSONDecodeError) as error:
        print(f"mainnet corpus capture failed: {error}", file=sys.stderr)
        return 1

    print(f"captured and verified {len(entries)} finalized mainnet transactions in {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
