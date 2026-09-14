#!/usr/bin/env python3
# Copyright (c) FISCO-BCOS, Apache-2.0
"""Tests for gen_eth_header_fixture.py.

Merged union of both branches' add/add versions:
- header RLP encoding digests (default 21 fields / London 16-field subset), and
- the withdrawals_root computation from --allocs storage (audit MN-4).
"""
import importlib.util
import pathlib
import subprocess
import sys
import tempfile
import unittest

_HERE = pathlib.Path(__file__).parent
_SPEC = importlib.util.spec_from_file_location(
    "gen_eth_header_fixture", str(_HERE / "gen_eth_header_fixture.py"))
_FIXTURE = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(_FIXTURE)

# keccak256 is re-exported by mpt_state_root (build-allocs.py has a hyphenated filename and
# cannot be imported as a module).
from mpt_state_root import compute_storage_root  # noqa: E402

EMPTY_TRIE = "56e81f171bcc55a6ff8345e692c0f86e5b48e01b996cadc001622fb5e363b421"
OMMERS = "1dcc4de8dec75d7aab85b567b6ccd41ad312451b948a7413f0a142fd40d49347"

PASSER = "0x4200000000000000000000000000000000000016"


class TestHeaderRlpEncoding(unittest.TestCase):
    def test_default_21_fields_unchanged(self):
        digest = _FIXTURE.keccak256(_FIXTURE.encode_header(_FIXTURE.DEFAULT_FIELDS)).hex()
        self.assertEqual(
            digest, "8634eabcf9e6df6b91b63cecab2d7af50a0a4fb8e0cc0aaca07cd8d0da32c069")

    def test_london_16_fields_subset(self):
        fields = {
            "parent_hash": "0x" + "00" * 32,
            "sha3_uncles": "0x" + OMMERS,
            "miner": "0x4200000000000000000000000000000000000011",
            "state_root": "0x" + EMPTY_TRIE,
            "transactions_root": "0x" + EMPTY_TRIE,
            "receipts_root": "0x" + EMPTY_TRIE,
            "logs_bloom": "0x" + "00" * 256,
            "difficulty": "0x0",
            "number": "0x0",
            "gas_limit": "0x1c9c380",
            "gas_used": "0x0",
            "timestamp": "0x648a5ce3",
            "extra_data": "0x616c6c20796f75722062617365206172652062656c6f6e6720746f20796f752e",
            "mix_hash": "0x" + "00" * 32,
            "nonce": "0x0000000000000000",
            "base_fee_per_gas": "0x3b9aca00",
        }
        present = list(fields)  # exactly 16, dict insertion order = go-ethereum London order
        digest = _FIXTURE.keccak256(_FIXTURE.encode_header(fields, present)).hex()
        self.assertEqual(
            digest, "d043c3480e0aa1b2163f2790e622f8cf404bc188a4e4da0097f276a477f459a9")


class TestWithdrawalsRoot(unittest.TestCase):
    def test_allocs_without_passer_keeps_empty_root(self):
        with tempfile.NamedTemporaryFile("w", suffix=".ini", delete=False) as handle:
            handle.write("[alloc.1]\naddress=0x1234567890123456789012345678901234567890\n")
            path = handle.name
        try:
            out = subprocess.run(
                [sys.executable, str(_HERE / "gen_eth_header_fixture.py"),
                 "--toml", "--allocs", path],
                capture_output=True, text=True, check=True).stdout
        finally:
            pathlib.Path(path).unlink()
        self.assertIn("withdrawals_root=" + _FIXTURE.EMPTY_TRIE_ROOT, out)

    def test_passer_storage_drives_withdrawals_root(self):
        # Independent oracle, not a re-derivation: the expected root is the spec-correct
        # leaf keccak256(rlp([HP(keccak256(slot)), rlp(value_trimmed)])) for
        # slot=0x..01 value=0x..02, computed outside mpt_state_root.py and pasted here
        # (cross-checked against py-trie: HexaryTrie({})[keccak256(slot)] = rlp(2)).
        # Using compute_storage_root() here would only pin plumbing against itself.
        slot, value = "0x" + "00" * 31 + "01", "0x" + "00" * 31 + "02"
        expected_hex = "6302d6aa5cf8befc2c23254172197534a8639fc400eb7a11fedbb44c388e2967"
        self.assertEqual(
            "0x" + compute_storage_root([(slot, value)]).hex(), "0x" + expected_hex)
        with tempfile.NamedTemporaryFile("w", suffix=".ini", delete=False) as handle:
            handle.write(f"[alloc.1]\naddress={PASSER}\nbalance=0\n")
            handle.write(f"[alloc.1.storage]\n{slot}={value}\n")
            path = handle.name
        try:
            out = subprocess.run(
                [sys.executable, str(_HERE / "gen_eth_header_fixture.py"),
                 "--toml", "--allocs", path],
                capture_output=True, text=True, check=True).stdout
        finally:
            pathlib.Path(path).unlink()
        self.assertIn("withdrawals_root=0x" + expected_hex, out)


if __name__ == "__main__":
    unittest.main()
