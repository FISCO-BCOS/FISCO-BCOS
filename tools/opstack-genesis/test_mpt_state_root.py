#!/usr/bin/env python3
# Copyright (c) FISCO-BCOS, Apache-2.0
# SPDX-License-Identifier: Apache-2.0
"""Consensus oracle pin for mpt_state_root.compute_state_root (5593 review F6).

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


def test_off_width_address_is_rejected_loudly():
    # 5593 review F5: the slot lane raised on len != 32 while the address lane
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
    # 5593 review F15: duplicate slot lines used to survive parsing and crash with
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
