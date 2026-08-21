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

state_root: when --allocs is given, the op-geth-compatible secure-MPT root over
the allocs is computed (mpt_state_root.py, byte-identical to C++
computeGenesisStateTrie) and fills the field; otherwise the empty-trie root is
used (the default fixture). The root MUST match what the node derives from the
merged [alloc.*] sections of config.genesis, or applyEthGenesisHeader refuses
to start.

Usage:
  python3 gen_eth_header_fixture.py            # prints the default fixture
  python3 gen_eth_header_fixture.py my.json    # fields overridden from JSON
  python3 gen_eth_header_fixture.py --toml --allocs allocs.ini  # [eth_genesis_header] TOML
"""
import argparse
import importlib.util
import json
from pathlib import Path

_SPEC = importlib.util.spec_from_file_location(
    "build_allocs", str(Path(__file__).parent / "build-allocs.py"))
_build_allocs = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(_build_allocs)
keccak256 = _build_allocs.keccak256

from mpt_state_root import parse_allocs_ini, compute_state_root, compute_storage_root

EMPTY_TRIE_ROOT = "0x56e81f171bcc55a6ff8345e692c0f86e5b48e01b996cadc001622fb5e363b421"
EMPTY_OMMERS_HASH = "0x1dcc4de8dec75d7aab85b567b6ccd41ad312451b948a7413f0a142fd40d49347"
# sha256 of empty input: the Prague empty-requests hash (EIP-7685).
EMPTY_REQUESTS_HASH = "0xe3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"

# L2ToL1MessagePasser predeploy. Isthmus+ genesis requires withdrawalsRoot = its
# storage root (isthmus/exec-engine.md:100-101; op-geth core/genesis.go:711-719),
# not the empty-trie root.
PASSER_ADDRESS = "0x4200000000000000000000000000000000000016"

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
    # Jovian 17-byte extraData: version(1) || denominator u32 BE ||
    # elasticity u32 BE || minBaseFee u64 BE.
    "extra_data": "0x00000000fa000000060000000000000000",
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


def to_toml_section(fields):
    """Emit the [eth_genesis_header] section (22 fields + hash) for config.genesis.

    Field order and names match NodeConfig::loadEthGenesisHeader. The hash is the
    keccak256(rlp(header)) checksum recomputed by Ledger::buildGenesisBlock from the
    other 21 fields; it must match for the node to start.
    """
    encoded = encode_header(fields)
    digest = keccak256(encoded)
    order = [
        "parent_hash", "sha3_uncles", "miner", "state_root", "transactions_root",
        "receipts_root", "logs_bloom", "difficulty", "number", "gas_limit", "gas_used",
        "timestamp", "extra_data", "mix_hash", "nonce", "base_fee_per_gas",
        "withdrawals_root", "blob_gas_used", "excess_blob_gas", "parent_beacon_block_root",
        "requests_hash",
    ]
    lines = ["[eth_genesis_header]"]
    for key in order:
        lines.append(f"{key}={fields[key]}")
    lines.append(f"hash=0x{digest.hex()}")
    return "\n".join(lines) + "\n"


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--toml", action="store_true",
                        help="emit the [eth_genesis_header] TOML section for config.genesis")
    parser.add_argument("--allocs", metavar="INI",
                        help="allocs INI path ([alloc.N] sections, plus SENDER etc.); "
                             "state_root is computed as the op-geth-compatible secure-MPT root "
                             "over these allocs instead of the empty-trie root")
    parser.add_argument("json", nargs="?", help="optional JSON file overriding header fields")
    args = parser.parse_args(argv)

    fields = dict(DEFAULT_FIELDS)
    if args.json:
        with open(args.json) as handle:
            fields.update(json.load(handle))
    if args.allocs:
        allocs = parse_allocs_ini(args.allocs)
        fields["state_root"] = "0x" + compute_state_root(allocs).hex()
        # Isthmus+ genesis: withdrawalsRoot = L2ToL1MessagePasser storage root
        # (isthmus/exec-engine.md:100-101; op-geth core/genesis.go:711-719). Phase A deploys
        # the passer with empty storage -> empty-trie root; a proxied op-deployer layout
        # carries storage and the tool must track it.
        for alloc in allocs:
            if alloc["address"].lower() == PASSER_ADDRESS:
                fields["withdrawals_root"] = "0x" + compute_storage_root(
                    alloc.get("storage", [])).hex()
                break
    if args.toml:
        print(to_toml_section(fields), end="")
        return 0
    encoded = encode_header(fields)
    digest = keccak256(encoded)
    print("rlp    = 0x" + encoded.hex())
    print("keccak = 0x" + digest.hex())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
