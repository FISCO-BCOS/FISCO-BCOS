#!/usr/bin/env python3
# Copyright (c) FISCO-BCOS, Apache-2.0
"""Compare the genesis (block 0) header of two RPC endpoints field by field.

K1 acceptance tooling: extracts the 22 consensus fields from
eth_getBlockByNumber("0x0", false) on both endpoints, normalizes them, and
diffs. Exit 0 when all fields agree, 1 on any mismatch (or RPC failure).
Never diff the raw RPC JSON — non-consensus fields, null defaults and
transaction-expansion shape differ between clients and would false-fail.

Usage:
  python3 diff_genesis_header.py --a http://127.0.0.1:8545 --b http://127.0.0.1:8547
"""
import argparse
import json
import sys
import urllib.request

# The 22 consensus fields of a Prague-era header (hash last: it is derived
# from the other 21, so a hash-only mismatch means a field this list misses).
FIELDS = [
    "parentHash", "sha3Uncles", "miner", "stateRoot", "transactionsRoot",
    "receiptsRoot", "logsBloom", "difficulty", "number", "gasLimit",
    "gasUsed", "timestamp", "extraData", "mixHash", "nonce",
    "baseFeePerGas", "withdrawalsRoot", "blobGasUsed", "excessBlobGas",
    "parentBeaconBlockRoot", "requestsHash", "hash",
]

# Quantity fields: leading zeros are not significant ("0x0" == "0x00").
QUANTITY_FIELDS = {
    "difficulty", "number", "gasLimit", "gasUsed", "timestamp",
    "baseFeePerGas", "blobGasUsed", "excessBlobGas",
}


def rpc_call(endpoint, method, params):
    payload = json.dumps({
        "jsonrpc": "2.0", "id": 1, "method": method, "params": params,
    }).encode()
    request = urllib.request.Request(
        endpoint, data=payload, headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(request, timeout=10) as response:
        body = json.load(response)
    if body.get("error"):
        raise RuntimeError(f"{endpoint}: {method} returned error: {body['error']}")
    return body.get("result")


def normalize(field, value):
    """Canonical comparable form: lowercase; quantities stripped of zeros."""
    if value is None:
        return None
    text = str(value).strip().lower()
    if field in QUANTITY_FIELDS:
        digits = text[2:] if text.startswith("0x") else text
        digits = digits.lstrip("0")
        return "0x" + (digits or "0")
    if not text.startswith("0x"):
        text = "0x" + text
    return text


def genesis_header(endpoint):
    block = rpc_call(endpoint, "eth_getBlockByNumber", ["0x0", False])
    if not isinstance(block, dict):
        raise RuntimeError(f"{endpoint}: eth_getBlockByNumber(0x0) returned {block!r}")
    return {field: normalize(field, block.get(field)) for field in FIELDS}


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Diff the 22 genesis header consensus fields of two RPC endpoints")
    parser.add_argument("--a", required=True, help="first RPC endpoint URL")
    parser.add_argument("--b", required=True, help="second RPC endpoint URL")
    args = parser.parse_args(argv)

    try:
        header_a = genesis_header(args.a)
        header_b = genesis_header(args.b)
    except Exception as error:  # noqa: BLE001 - CLI boundary, report and exit 1
        print(f"error: {error}", file=sys.stderr)
        return 1

    mismatches = 0
    for field in FIELDS:
        value_a, value_b = header_a[field], header_b[field]
        if value_a != value_b:
            mismatches += 1
            print(f"MISMATCH {field}:\n  a = {value_a}\n  b = {value_b}")
    if mismatches:
        print(f"{mismatches} field(s) differ")
        return 1
    print(f"all {len(FIELDS)} genesis header fields agree")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
