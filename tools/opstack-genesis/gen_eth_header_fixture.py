#!/usr/bin/env python3
# Copyright (c) FISCO-BCOS, Apache-2.0
"""Reference keccak256(rlp(header)) for the B0 eth-genesis-header C++ tests.

Independent of the C++ RLP encoder (bcos-rlp-protocol): a minimal RLP encoder
lives here, keccak256 is imported from build-allocs.py (itself pinned by
standard vectors). The C++ unit tests hardcode the hash this script prints —
if the two implementations ever disagree, the C++ test fails and the
discrepancy must be investigated, not the fixture regenerated blindly.

Field order is the go-ethereum types.Header order (21 items, Prague-era):
  parentHash, sha3Uncles, miner, stateRoot, transactionsRoot, receiptsRoot,
  logsBloom, difficulty, number, gasLimit, gasUsed, timestamp, extraData,
  mixHash, nonce, baseFeePerGas, withdrawalsRoot, blobGasUsed, excessBlobGas,
  parentBeaconBlockRoot, requestsHash

Usage:
  python3 gen_eth_header_fixture.py            # prints the default fixture
  python3 gen_eth_header_fixture.py my.json    # fields overridden from JSON
"""
import importlib.util
import json
import sys
from pathlib import Path

_SPEC = importlib.util.spec_from_file_location(
    "build_allocs", str(Path(__file__).parent / "build-allocs.py"))
_build_allocs = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(_build_allocs)
keccak256 = _build_allocs.keccak256

EMPTY_TRIE_ROOT = "0x56e81f171bcc55a6ff8345e692c0f86e5b48e01b996cadc001622fb5e363b421"
EMPTY_OMMERS_HASH = "0x1dcc4de8dec75d7aab85b567b6ccd41ad312451b948a7413f0a142fd40d49347"
# sha256 of empty input: the Prague empty-requests hash (EIP-7685).
EMPTY_REQUESTS_HASH = "0xe3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"

# Default fixture: an empty-alloc post-Karst L2 genesis header. Kept in sync
# with test_GenesisEthHeader.cpp (bcos-ledger) and
# test_NodeConfigEthGenesisHeader.cpp (bcos-tool).
DEFAULT_FIELDS = {
    "parent_hash": "0x" + "00" * 32,
    "sha3_uncles": EMPTY_OMMERS_HASH,
    "miner": "0x4200000000000000000000000000000000000011",
    "state_root": EMPTY_TRIE_ROOT,
    "transactions_root": EMPTY_TRIE_ROOT,
    "receipts_root": EMPTY_TRIE_ROOT,
    "logs_bloom": "0x" + "00" * 256,
    "difficulty": "0x0",
    "number": "0x0",
    "gas_limit": "0x1c9c380",
    "gas_used": "0x0",
    "timestamp": "0x689d5c00",
    # Jovian 17-byte extraData: version 0x01 || denominator u32 BE ||
    # elasticity u32 BE || minBaseFee u64 BE. calcOpBaseFee rejects 17B/0x00.
    "extra_data": "0x01000000fa000000060000000000000000",
    "mix_hash": "0x" + "00" * 32,
    "nonce": "0x0000000000000000",
    "base_fee_per_gas": "0x3b9aca00",
    "withdrawals_root": EMPTY_TRIE_ROOT,
    "blob_gas_used": "0x0",
    "excess_blob_gas": "0x0",
    "parent_beacon_block_root": "0x" + "00" * 32,
    "requests_hash": EMPTY_REQUESTS_HASH,
}


def _strip0x(text):
    return text[2:] if text.startswith("0x") else text


def rlp_encode_bytes(payload):
    """RLP-encode a byte string."""
    length = len(payload)
    if length == 1 and payload[0] < 0x80:
        return payload
    if length < 56:
        return bytes([0x80 + length]) + payload
    length_bytes = length.to_bytes((length.bit_length() + 7) // 8, "big")
    return bytes([0xB7 + len(length_bytes)]) + length_bytes + payload


def rlp_encode_scalar(value):
    """RLP-encode an unsigned integer (minimal big-endian, 0 -> empty string)."""
    if value == 0:
        return rlp_encode_bytes(b"")
    return rlp_encode_bytes(value.to_bytes((value.bit_length() + 7) // 8, "big"))


def rlp_encode_list(items):
    payload = b"".join(items)
    length = len(payload)
    if length < 56:
        return bytes([0xC0 + length]) + payload
    length_bytes = length.to_bytes((length.bit_length() + 7) // 8, "big")
    return bytes([0xF7 + len(length_bytes)]) + length_bytes + payload


def encode_header(fields):
    def as_bytes(key):
        return rlp_encode_bytes(bytes.fromhex(_strip0x(fields[key])))

    def as_scalar(key):
        return rlp_encode_scalar(int(fields[key], 16))

    items = [
        as_bytes("parent_hash"),
        as_bytes("sha3_uncles"),
        as_bytes("miner"),
        as_bytes("state_root"),
        as_bytes("transactions_root"),
        as_bytes("receipts_root"),
        as_bytes("logs_bloom"),
        as_scalar("difficulty"),
        as_scalar("number"),
        as_scalar("gas_limit"),
        as_scalar("gas_used"),
        as_scalar("timestamp"),
        as_bytes("extra_data"),
        as_bytes("mix_hash"),
        as_bytes("nonce"),
        as_scalar("base_fee_per_gas"),
        as_bytes("withdrawals_root"),
        as_scalar("blob_gas_used"),
        as_scalar("excess_blob_gas"),
        as_bytes("parent_beacon_block_root"),
        as_bytes("requests_hash"),
    ]
    return rlp_encode_list(items)


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    fields = dict(DEFAULT_FIELDS)
    if argv:
        with open(argv[0]) as handle:
            fields.update(json.load(handle))
    encoded = encode_header(fields)
    digest = keccak256(encoded)
    print("rlp    = 0x" + encoded.hex())
    print("keccak = 0x" + digest.hex())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
