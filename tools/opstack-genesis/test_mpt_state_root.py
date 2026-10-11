#!/usr/bin/env python3
# Copyright (c) FISCO-BCOS, Apache-2.0
# SPDX-License-Identifier: Apache-2.0
"""Consensus oracle pin for mpt_state_root.compute_state_root.

compute_state_root produces the C2 genesis state root; before this test it had no
committed vector — its docstring anchors were /tmp paths no third party can replay.
The expected root here comes from gen_trieroot_golden.py, an independent RLP/MPT
implementation (its vectors predate this tool), so the two Python implementations are
pinned to each other and to a committed literal: a drift in either changes the genesis
hash and this test goes red instead of shipping a silently different genesis.
"""
import importlib.util
from pathlib import Path

HERE = Path(__file__).resolve().parent


def _load(name):
    spec = importlib.util.spec_from_file_location(name, HERE / f"{name}.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


golden = _load("gen_trieroot_golden")
mpt = _load("mpt_state_root")


def test_state_root_matches_independent_golden_oracle():
    # Translate gen_trieroot_golden's (address, nonce, balance, code, {slot32: value32})
    # tuples into mpt_state_root's dict form and require byte-identical roots.
    # storage is a list of (slotHex, valueHex) pairs, 64-char each — the width
    # compute_storage_root enforces.
    allocs = [
        {
            "address": "0x" + address.hex(),
            "nonce": str(nonce),
            "balance": str(balance),
            "code": "0x" + code.hex(),
            "storage": [
                ("0x" + slot.hex(), "0x" + value.hex()) for slot, value in storage.items()
            ],
        }
        for address, nonce, balance, code, storage in golden.GOLDEN_ALLOC
    ]
    assert "0x" + mpt.compute_state_root(allocs).hex() == golden.EXPECTED["golden_state"]


# The one-account pin alone would not catch a divergence that only shows with several
# accounts (ordering, per-account storage subtries). Three shapes: storage-bearing,
# code-bearing, empty. Both implementations must agree AND the joint value is pinned.
MULTI_ALLOC = [
    (bytes.fromhex("43000000000000000000000000000000000000c0"), 1, 10**18,
     bytes.fromhex("6080604052"), {bytes(32): bytes.fromhex("0385").rjust(32, b"\0")}),
    (bytes.fromhex("4200000000000000000000000000000000001000"), 0, 0, b"", {}),
    (bytes.fromhex("deaddeaddeaddeaddeaddeaddeaddeaddead0001"), 5, 7,
     bytes.fromhex("6001"), {}),
]
MULTI_ALLOC_AS_DICTS = [
    {"address": "0x" + a.hex(), "nonce": str(n), "balance": str(b), "code": "0x" + c.hex(),
     "storage": [("0x" + s.hex(), "0x" + v.hex()) for s, v in st.items()]}
    for a, n, b, c, st in MULTI_ALLOC
]
# Pinned once from the joint value (both implementations agree at pin time).
EXPECTED_MULTI = "0x85011e86ef34676ba15a6637572a3df94b3c3045813cd6dfdbef03dfaee56fcc"


def test_multi_account_cross_pin():
    assert "0x" + mpt.compute_state_root(MULTI_ALLOC_AS_DICTS).hex() == EXPECTED_MULTI
    assert "0x" + golden.state_root(MULTI_ALLOC).hex() == EXPECTED_MULTI


def test_off_width_address_is_rejected_loudly():
    # The slot lane raised on len != 32 while the address lane
    # silently keccak'd a 19/21-byte address into a different trie. The guard must
    # reject before any hashing happens.
    for bad in ("0x" + "11" * 19, "0x" + "11" * 21, "11" * 19):
        allocs = [{"address": bad, "balance": "1", "nonce": "0", "storage": []}]
        try:
            mpt.compute_state_root(allocs)
        except ValueError as exc:
            assert "exactly 20 bytes" in str(exc), exc
        else:
            raise AssertionError(f"off-width address {bad!r} was not rejected")


def test_duplicate_slot_key_inside_one_section_is_rejected(tmp_path):
    # Duplicate slot lines used to survive parsing and crash with
    # an IndexError at depth 64 inside build_branch — the failure mode the
    # duplicate-address guard's comment says was fixed, left open for slots.
    import pytest

    ini = tmp_path / "allocs.ini"
    ini.write_text(
        "[alloc.1111111111111111111111111111111111111111]\n"
        "balance = 5\n"
        "[alloc.1111111111111111111111111111111111111111.storage]\n"
        "0x" + "01" * 32 + " = 0x" + "22" * 32 + "\n"
        "0x" + "01" * 32 + " = 0x" + "33" * 32 + "\n"
    )
    with pytest.raises(ValueError, match="duplicate storage slot"):
        mpt.parse_allocs_ini(str(ini))


def test_duplicate_address_key_across_alloc_n_sections_is_rejected(tmp_path):
    # The [alloc.N] + address= layout build-allocs emits bypasses the section-suffix
    # guard: two indices carrying the same address= used to survive parsing and only
    # fail deep inside build_branch.
    import pytest

    ini = tmp_path / "allocs.ini"
    ini.write_text(
        "[alloc.1]\n"
        "address = 0x" + "11" * 20 + "\n"
        "balance = 5\n"
        "[alloc.2]\n"
        "address = 0x" + "11" * 20 + "\n"
        "balance = 6\n"
    )
    with pytest.raises(ValueError, match="duplicate alloc address"):
        mpt.parse_allocs_ini(str(ini))
