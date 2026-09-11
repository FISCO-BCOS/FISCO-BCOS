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
