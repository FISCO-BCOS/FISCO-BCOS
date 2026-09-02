"""Pure-function tests for withdraw_claim.py (tier-1 mock layer).

Each case pins one historical bug or load-bearing invariant, per the spec's
§2-2 matrix. Oracle strategy = double anchor: (1) the test builds its OWN
ABI layout bytes (independent of the tool's decode path), (2) frozen hex
constants (generated once by gen_frozen_withdraw_constants.py, human-
reviewed, pasted verbatim). The two anchors are independent against TOOL
drift and against LATER builder drift; their shared blind spot (a builder
wrong from day one) is covered by human review + tier-2's real-portal
MerkleTrie (the ultimate oracle) — see spec §2-3.

Run: python3 -m unittest discover -s tools/op-e2e/tests -v
"""
import subprocess
import sys
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from eth_hash.auto import keccak

import withdraw_claim as wc

TOPIC = "0x02a52367d10742d8032712c1bb8e0144ff1ec5ffda1ed7d70bb05a2744955054"
DEV1 = "0x70997970C51812dc3A010C7d01b50e0d17dc79C8"
L1_RECV = "0xf39Fd6e51aad88F6F4ce6aB8827279cffFb92266"

# Frozen anchor constants (regenerate ONLY with the generator script; never
# hand-edit): see gen_frozen_withdraw_constants.py.
FROZEN = {
    "wh_empty_data": "05abf05894883101c954a3931b4b51013c15d36b76154913017a8bfce2064c91",
    "wh_odd_data": "a865d6637f696dda03fdc17aa113a75575392e1663cb2979dd2ace4d718c7a70",
    "storage_key_empty": "498784440ed28e33fbd5730ff2da2cdf507aa72d624130e9dc2fc451b5090ba3",
    "rocksdb_prefix": "2f617070732f343230303030303030303030303030303030303030303030303030303030303030303030303031363a",
}


def _word(x: int) -> str:
    return f"{x:064x}"


def _addr_word(addr: str) -> str:
    return addr.lower().replace("0x", "").rjust(64, "0")


def _pad32_hex(data: bytes) -> str:
    return data.hex().ljust((len(data) + 31) // 32 * 32 * 2, "0")


def encode_event_data(value: int, gas_limit: int, data: bytes, wh: bytes) -> str:
    """ABI-encode the non-indexed args of MessagePassed: value, gasLimit,
    data (dynamic, offset=4*32 bytes from the START of the data blob),
    withdrawalHash. Independent of the tool's decode path."""
    head = _word(value) + _word(gas_limit) + _word(4 * 32) + wh.hex()
    tail = _word(len(data)) + _pad32_hex(data)
    return "0x" + head + tail


def encode_withdrawal_tuple(nonce: int, sender: str, target: str, value: int,
                            gas_limit: int, data: bytes) -> bytes:
    """abi.encode(uint256,address,address,uint256,uint256,bytes) — the Solidity
    Hashing.hashWithdrawal preimage (224B for empty data). Independent of the tool."""
    head = (_word(nonce) + _addr_word(sender) + _addr_word(target) + _word(value)
            + _word(gas_limit) + _word(6 * 32))
    tail = _word(len(data)) + _pad32_hex(data)
    return bytes.fromhex(head + tail)


def make_log(nonce: int, value: int, gas_limit: int, data: bytes) -> dict:
    wh = keccak(encode_withdrawal_tuple(nonce, DEV1, L1_RECV, value, gas_limit, data))
    return {
        "topics": [TOPIC, "0x" + _word(nonce), "0x" + _addr_word(DEV1),
                   "0x" + _addr_word(L1_RECV)],
        "data": encode_event_data(value, gas_limit, data, wh),
        "blockNumber": "0x15",
    }


class DecodeMessagePassed(unittest.TestCase):
    def test_empty_data_decodes_to_empty_not_length_word(self):
        # Historical bug #1 (the fatal one): using the LENGTH word as content
        # substituted 32 zero bytes for empty data -> L1-recomputed withdrawal
        # hash diverged -> portal MerkleTrie "invalid large internal hash".
        w = wc.decode_message_passed(make_log(7, 10**18, 100_000, b""))
        self.assertEqual(w["data"], "0x")
        # decode returns the topic VERBATIM ("0x" + t[1][2:]) — 32-byte
        # zero-padded quantity, NOT a trimmed "0x7".
        self.assertEqual(w["nonce"], "0x" + "0" * 63 + "7")
        self.assertEqual(w["sender"], DEV1.lower())
        self.assertEqual(w["target"], L1_RECV.lower())
        self.assertEqual(w["value"], 10**18)
        self.assertEqual(w["gasLimit"], 100_000)
        self.assertEqual(w["block"], 0x15)

    def test_nonempty_data_decodes_exactly(self):
        # Historical bug #5 (found by review B1, 2026-08-23): the tool's old
        # `args = data[4*64:]; off = words[2]*2` double-counted the head — the
        # ABI offset is relative to the START of the data blob, so every
        # non-empty data decoded to "0x". Live flows only ever used empty data
        # ("0x"), which is why this never fired on chain.
        w = wc.decode_message_passed(make_log(9, 5, 21000, b"\xde\xad\xbe\xef"))
        self.assertEqual(w["data"], "0xdeadbeef")
        big = bytes([0x11]) * 33  # crosses the 32-byte word boundary
        w2 = wc.decode_message_passed(make_log(9, 5, 21000, big))
        self.assertEqual(w2["data"], "0x" + big.hex())

    def test_withdrawal_hash_recompute_matches_event(self):
        # Cross-check the decode against the hash the EVENT carries: garbling
        # any field breaks the recomputation. Also anchors the frozen constant.
        log = make_log(7, 10**18, 100_000, b"")
        w = wc.decode_message_passed(log)
        recomputed = keccak(encode_withdrawal_tuple(
            7, DEV1, L1_RECV, 10**18, 100_000, b""))
        self.assertEqual("0x" + recomputed.hex(), w["withdrawalHash"])
        self.assertEqual(w["withdrawalHash"], "0x" + FROZEN["wh_empty_data"])

    def test_frozen_wh_nonempty_data(self):
        log = make_log(3, 1, 2, b"\xde\xad\xbe\xef")
        self.assertEqual(wc.decode_message_passed(log)["withdrawalHash"],
                         "0x" + FROZEN["wh_odd_data"])


class StorageKey(unittest.TestCase):
    def test_encodepacked_form_not_rlp_form(self):
        # Historical bug #2: keccak(rlp([wh, ""])) lands on an absent key; the
        # Solidity mapping slot is keccak256(abi.encodePacked(wh, bytes32(0))).
        wh = keccak(encode_withdrawal_tuple(7, DEV1, L1_RECV, 10**18, 100_000, b""))
        key = wc.message_passer_storage_key(wh)
        self.assertEqual(key, keccak(wh + b"\x00" * 32))
        self.assertEqual(key.hex(), FROZEN["storage_key_empty"])


class CastFailLoud(unittest.TestCase):
    def test_nonzero_rc_raises_with_stderr_tail(self):
        # Historical bug #3: swallowing stderr made "proven"/"finalized: ?"
        # print false greens during the 08-24 bring-up. The tool keeps only
        # the LAST 400 chars of stderr — the marker must sit at the tail.
        proc = subprocess.CompletedProcess(
            args=[], returncode=1, stdout="", stderr="x" * 450 + " boom")
        with mock.patch.object(wc.subprocess, "run", return_value=proc):
            with self.assertRaises(SystemExit) as ctx:
                wc.cast("send", "0xabc")
        self.assertIn("boom", str(ctx.exception))
        self.assertIn("cast send", str(ctx.exception))

    def test_stderr_only_stdout_empty_raises(self):
        proc = subprocess.CompletedProcess(args=[], returncode=0, stdout="",
                                           stderr="warning but no output")
        with mock.patch.object(wc.subprocess, "run", return_value=proc):
            with self.assertRaises(SystemExit):
                wc.cast("call", "0xabc")

    def test_success_returns_stdout(self):
        proc = subprocess.CompletedProcess(args=[], returncode=0,
                                           stdout="0x123\n", stderr="")
        with mock.patch.object(wc.subprocess, "run", return_value=proc):
            self.assertEqual(wc.cast("call", "0xabc"), "0x123")


class RocksdbPrefix(unittest.TestCase):
    def test_colon_is_hex_3a_not_ascii_colon(self):
        # Historical bug #4: the bypass prefix used a literal ":" so table
        # lookups missed every physical key ("<table-ascii>:<slot>").
        tbl = "/apps/" + "4200000000000000000000000000000000000016"
        self.assertEqual(wc.rocksdb_table_prefix(tbl), tbl.encode().hex() + "3a")
        self.assertEqual(wc.rocksdb_table_prefix(tbl), FROZEN["rocksdb_prefix"])
        self.assertNotIn(":", wc.rocksdb_table_prefix(tbl))


if __name__ == "__main__":
    unittest.main()
