#!/usr/bin/env python3
# Copyright (c) FISCO-BCOS, Apache-2.0
"""Unit tests for tools/op-genesis-to-allocs.py: the --alloc replacement and the
state_root / header hash it recomputes.

The genesis JSON and the extra accounts here are UNIT FIXTURES built in the test; they
are NOT op-deployer or build-allocs.py output, so the test needs neither tool.

    python3 -m unittest discover -s tools -p 'test_op_genesis_to_allocs.py' -v
"""
import copy
import importlib.util
import io
import json
import re
import tempfile
import unittest
from contextlib import redirect_stderr
from pathlib import Path

_SPEC = importlib.util.spec_from_file_location(
    "op_genesis_to_allocs", str(Path(__file__).parent / "op-genesis-to-allocs.py"))
tool = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(tool)

PROXY_ADMIN = "0x4200000000000000000000000000000000000018"
L1_BLOCK = "0x4200000000000000000000000000000000000015"
ADMIN_OWNER = "0x00000000000000000000000000000000000000aa"
# Stand-in for the accounts build-allocs.py adds: proxies + implementations (4 accounts).
EXTRA_ALLOC = {
    "0x43000000000000000000000000000000000000c0": {"code": "0x60806040",
                                                   "storage": {"0x0": "0x1"}},
    "0xc3d3c3d3c3d3c3d3c3d3c3d3c3d3c3d3c3d300c0": {"code": "0x6000"},
    "0x43000000000000000000000000000000000000c1": {"code": "0x60806040"},
    "0xc3d3c3d3c3d3c3d3c3d3c3d3c3d3c3d3c3d300c1": {"code": "0x6001"},
}

# keccak256(rlp(header)) of the C++ header fixture (test_GenesisEthHeader.cpp,
# test_NodeConfigEthGenesisHeader.cpp).
HEADER_VECTOR = "0x8634eabcf9e6df6b91b63cecab2d7af50a0a4fb8e0cc0aaca07cd8d0da32c069"
FORKS = {key: 0 for key in ("shanghaiTime", "cancunTime", "pragueTime", "regolithTime",
                            "canyonTime", "ecotoneTime", "fjordTime", "graniteTime",
                            "holoceneTime", "isthmusTime", "jovianTime")}
GENESIS = {
    "config": dict(chainId=914901, londonBlock=0, **FORKS),
    "timestamp": "0x689d5c00",
    "extraData": "0x01000000fa000000060000000000000000",
    "gasLimit": "0x1c9c380",
    "difficulty": "0x0",
    "coinbase": "0x4200000000000000000000000000000000000011",
    "baseFeePerGas": "0x3b9aca00",
    "number": "0x0",
    "alloc": {
        PROXY_ADMIN: {"balance": "0x0", "code": "0x6001",
                      "storage": {"0x0": "0x" + ADMIN_OWNER[2:].rjust(64, "0")}},
        L1_BLOCK: {"balance": "0x0", "code": "0x60806040"},
    },
}

class ConverterTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def run_tool(self, genesis=GENESIS, alloc=None, extra=()):
        """Return the output INI text; raises SystemExit when the tool dies."""
        (self.dir / "genesis.json").write_text(json.dumps(genesis))
        argv = ["--genesis", str(self.dir / "genesis.json"), "-o", str(self.dir / "out.ini")]
        if alloc is not None:
            (self.dir / "alloc.json").write_text(json.dumps(alloc))
            argv += ["--alloc", str(self.dir / "alloc.json")]
        with redirect_stderr(io.StringIO()) as self.stderr:
            tool.main(argv + list(extra))
        return (self.dir / "out.ini").read_text()

    def assert_dies(self, needle, **kwargs):
        with self.assertRaises(SystemExit):
            self.run_tool(**kwargs)
        self.assertIn(needle, self.stderr.getvalue())

    @staticmethod
    def accounts(ini):
        """{address: section text} over the [alloc.N] (+ .storage) sections."""
        out = {}
        for block in re.split(r"\n(?=\[alloc\.\d+\]\n)", ini):
            match = re.search(r"^\[alloc\.\d+\]\n\s+address=(0x[0-9a-f]{40})", block, re.M)
            if match:
                section = block.split("\n\n")[0].strip()
                out[match.group(1)] = re.sub(r"alloc\.\d+", "alloc.N", section)
        return out

    @staticmethod
    def header(ini, key):
        return re.search(rf"^\s+{key}=(0x[0-9a-f]+)$", ini, re.M).group(1)

    def test_plain_genesis_keeps_its_accounts(self):
        ini = self.run_tool()
        self.assertEqual(set(self.accounts(ini)), {PROXY_ADMIN, L1_BLOCK})
        self.assertNotIn("[l2]", ini)

    def test_merged_alloc_adds_accounts_and_reroots(self):
        off = self.run_tool()
        on = self.run_tool(alloc=dict(GENESIS["alloc"], **EXTRA_ALLOC))
        off_accounts, on_accounts = self.accounts(off), self.accounts(on)
        self.assertEqual(set(on_accounts) - set(off_accounts), set(EXTRA_ALLOC))
        for address, section in off_accounts.items():  # input accounts untouched
            self.assertEqual(on_accounts[address], section)
        self.assertNotEqual(self.header(on, "state_root"), self.header(off, "state_root"))
        self.assertNotEqual(self.header(on, "hash"), self.header(off, "hash"))

    def test_alloc_must_keep_the_genesis_accounts(self):
        self.assert_dies(f"account {PROXY_ADMIN} of --genesis is missing or changed",
                         alloc={L1_BLOCK: {"code": "0x60806040"}})
        changed = dict(GENESIS["alloc"], **{L1_BLOCK: {"code": "0x60806041"}})
        self.assert_dies(f"account {L1_BLOCK} of --genesis is missing or changed",
                         alloc=changed)
        # Same accounts, nothing added: accepted, and the plain genesis output results
        # (everything after the header comment, which names the --alloc file).
        self.assertEqual(self.run_tool(alloc=GENESIS["alloc"]).split("[alloc.0]", 1)[1],
                         self.run_tool().split("[alloc.0]", 1)[1])

    def test_extra_accounts_at_any_address_are_accepted(self):
        # Which predeploys a chain carries, and where, is the chain config's business
        # (build-allocs.py); the converter only keeps the genesis accounts intact.
        extra = dict(GENESIS["alloc"])
        added = ["0x" + "42" + "0" * 34 + suffix for suffix in ("1000", "1001")]
        for address in added:
            extra[address] = {"code": "0x60806040"}
        ini = self.run_tool(alloc=extra)
        self.assertEqual(set(self.accounts(ini)), {PROXY_ADMIN, L1_BLOCK, *added})
        self.assertNotEqual(self.header(ini, "state_root"),
                            self.header(self.run_tool(), "state_root"))

    def test_header_hash_matches_cpp_vector(self):
        # RLP/keccak over the C++ fixture's 21 fields (test_GenesisEthHeader.cpp).
        fields = [("parent_hash", tool.ZERO_HASH), ("sha3_uncles", tool.EMPTY_UNCLES_HASH),
                  ("miner", "0x4200000000000000000000000000000000000011"),
                  ("state_root", tool.EMPTY_TRIE_ROOT),
                  ("transactions_root", tool.EMPTY_TRIE_ROOT),
                  ("receipts_root", tool.EMPTY_TRIE_ROOT), ("logs_bloom", tool.ZERO_BLOOM),
                  ("difficulty", "0x0"), ("number", "0x0"), ("gas_limit", "0x1c9c380"),
                  ("gas_used", "0x0"), ("timestamp", "0x689d5c00"),
                  ("extra_data", "0x01000000fa000000060000000000000000"),
                  ("mix_hash", tool.ZERO_HASH), ("nonce", "0x0000000000000000"),
                  ("base_fee_per_gas", "0x3b9aca00"), ("withdrawals_root", tool.EMPTY_TRIE_ROOT),
                  ("blob_gas_used", "0x0"), ("excess_blob_gas", "0x0"),
                  ("parent_beacon_block_root", tool.ZERO_HASH),
                  ("requests_hash", tool.EMPTY_REQUESTS_HASH)]
        self.assertEqual(tool.header_hash(fields), HEADER_VECTOR)

    def test_isthmus_withdrawals_root_is_the_passer_storage_root(self):
        # op-geth: statedb.GetStorageRoot(passer) — zero hash when the account is absent,
        # the empty-trie root when it exists without storage.
        genesis = copy.deepcopy(GENESIS)
        self.assertEqual(self.header(self.run_tool(genesis=genesis), "withdrawals_root"),
                         tool.ZERO_HASH)
        genesis["alloc"]["0x4200000000000000000000000000000000000016"] = {"code": "0x6002"}
        ini = self.run_tool(genesis=genesis)
        self.assertEqual(self.header(ini, "withdrawals_root"), tool.EMPTY_TRIE_ROOT)
        self.assertEqual(self.header(ini, "requests_hash"), tool.EMPTY_REQUESTS_HASH)

    def test_fork_gated_fields_follow_the_fork_schedule(self):
        genesis = copy.deepcopy(GENESIS)
        later = int(genesis["timestamp"], 16) + 1  # scheduled, not active at genesis
        for key in ("cancunTime", "pragueTime", "ecotoneTime", "fjordTime", "graniteTime",
                    "holoceneTime", "isthmusTime", "jovianTime"):
            genesis["config"][key] = later
        ini = self.run_tool(genesis=genesis)  # Shanghai only: withdrawals root, no blob fields
        self.assertEqual(self.header(ini, "withdrawals_root"), tool.EMPTY_TRIE_ROOT)
        self.assertNotIn("blob_gas_used", ini)
        genesis["blobGasUsed"] = "0x0"
        self.assert_dies("blob_gas_used is set but its fork is not active", genesis=genesis)
        genesis = copy.deepcopy(GENESIS)
        genesis["withdrawalsRoot"] = tool.EMPTY_TRIE_ROOT  # disagrees with the derived root
        self.assert_dies("withdrawals_root", genesis=genesis)
        genesis = copy.deepcopy(GENESIS)
        genesis["config"]["amsterdamTime"] = 0
        self.assert_dies("Amsterdam", genesis=genesis)

    def test_input_state_root_and_gas_limit_are_checked(self):
        genesis = copy.deepcopy(GENESIS)
        genesis["stateRoot"] = tool.EMPTY_TRIE_ROOT  # not the root of the two accounts
        self.assert_dies("stateRoot", genesis=genesis)
        genesis = copy.deepcopy(GENESIS)
        genesis["gasLimit"] = "0x0"
        self.assert_dies("missing or zero gasLimit", genesis=genesis)

    def test_off_rejects_a_hash_it_does_not_reproduce(self):
        off_hash = self.header(self.run_tool(), "hash")
        self.run_tool(extra=["--genesis-hash", off_hash])
        self.assert_dies("would not reproduce", extra=["--genesis-hash", "0x" + "11" * 32])

    def test_input_hash_is_checked_against_the_genesis_accounts(self):
        off_hash = self.header(self.run_tool(), "hash")
        merged = dict(GENESIS["alloc"], **EXTRA_ALLOC)
        ini = self.run_tool(alloc=merged, extra=["--genesis-hash", off_hash])
        self.assertNotEqual(self.header(ini, "hash"), off_hash)
        self.assertIn("set rollup.json genesis.l2.hash", self.stderr.getvalue())
        # A hash the --genesis accounts do not produce aborts even when --alloc adds
        # accounts (the merged hash is no excuse for a wrong input).
        self.assert_dies("would not reproduce", alloc=merged,
                         extra=["--genesis-hash", "0x" + "11" * 32])

if __name__ == "__main__":
    unittest.main()
