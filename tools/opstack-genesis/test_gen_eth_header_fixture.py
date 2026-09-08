#!/usr/bin/env python3
# Copyright (c) FISCO-BCOS, Apache-2.0
"""Tests for gen_eth_header_fixture.py withdrawals_root computation (audit MN-4)."""
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

PASSER = "0x4200000000000000000000000000000000000016"


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
