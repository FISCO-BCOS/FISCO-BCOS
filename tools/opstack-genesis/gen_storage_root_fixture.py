#!/usr/bin/env python3
# Copyright (c) FISCO-BCOS, Apache-2.0
"""Reference Ethereum storage-trie roots for the C++ withdrawalsRoot tests.

Independent of the C++ MPT (bcos-ledger/mpt): a minimal RLP encoder and a
from-scratch secure-trie builder live here, keccak256 is imported from
build-allocs.py (itself pinned by standard vectors). The C++ unit tests
hardcode the roots this script prints — if the two implementations ever
disagree, the C++ test fails and the discrepancy must be investigated, not
the fixture regenerated blindly. Same contract as gen_eth_header_fixture.py.

A storage trie is a SECURE trie over one account's live slots:
  key   = keccak256(32-byte slot key)
  value = RLP(slot value with leading zero bytes trimmed)
A slot whose value trims to nothing (all zeros) is not in the trie at all.
An account with no live slot has the empty-trie root
0x56e81f171bcc55a6ff8345e692c0f86e5b48e01b996cadc001622fb5e363b421.

Usage:
  python3 gen_storage_root_fixture.py
"""
import importlib.util
from pathlib import Path

_SPEC = importlib.util.spec_from_file_location(
    "build_allocs", str(Path(__file__).parent / "build-allocs.py"))
_build_allocs = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(_build_allocs)
keccak256 = _build_allocs.keccak256


# ---- minimal RLP ----

def _encode_length(length, offset):
    if length < 56:
        return bytes([offset + length])
    as_bytes = length.to_bytes((length.bit_length() + 7) // 8, "big")
    return bytes([offset + 55 + len(as_bytes)]) + as_bytes


def rlp_bytes(payload):
    """RLP of a byte string."""
    if len(payload) == 1 and payload[0] < 0x80:
        return payload
    return _encode_length(len(payload), 0x80) + payload


def rlp_list(payload):
    """RLP list header for an already-concatenated item payload."""
    return _encode_length(len(payload), 0xC0) + payload


# ---- minimal Merkle-Patricia trie ----

def _nibbles(key):
    out = []
    for byte in key:
        out.append(byte >> 4)
        out.append(byte & 0x0F)
    return out


def _hex_prefix(nibbles, is_leaf):
    flag = 2 if is_leaf else 0
    if len(nibbles) % 2 == 1:
        head = [(flag + 1) * 16 + nibbles[0]]
        rest = nibbles[1:]
    else:
        head = [flag * 16]
        rest = nibbles
    out = bytearray(head)
    for i in range(0, len(rest), 2):
        out.append(rest[i] * 16 + rest[i + 1])
    return bytes(out)


def _node_ref(node_rlp):
    """How a parent splices a child: inline when short, keccak hash otherwise."""
    if len(node_rlp) < 32:
        return node_rlp
    return rlp_bytes(keccak256(node_rlp))


def _build(pairs):
    """pairs: list of (nibble list, value bytes), sorted by nibble path, unique."""
    if len(pairs) == 1:
        path, value = pairs[0]
        return rlp_list(rlp_bytes(_hex_prefix(path, True)) + rlp_bytes(value))

    shortest = min(len(path) for path, _ in pairs)
    common = 0
    while common < shortest and len({path[common] for path, _ in pairs}) == 1:
        common += 1
    if common > 0:
        prefix = pairs[0][0][:common]
        inner = _build([(path[common:], value) for path, value in pairs])
        return rlp_list(rlp_bytes(_hex_prefix(prefix, False)) + _node_ref(inner))

    slots = [[] for _ in range(16)]
    branch_value = b""
    for path, value in pairs:
        if not path:
            branch_value = value
        else:
            slots[path[0]].append((path[1:], value))
    payload = b""
    for group in slots:
        payload += _node_ref(_build(group)) if group else rlp_bytes(b"")
    payload += rlp_bytes(branch_value)
    return rlp_list(payload)


def storage_root(slots):
    """slots: dict of 32-byte slot key -> 32-byte value. Returns the 32-byte root."""
    entries = {}
    for slot, value in slots.items():
        trimmed = value.lstrip(b"\x00")
        if not trimmed:
            continue  # a zero slot is not in the trie
        entries[keccak256(slot)] = rlp_bytes(trimmed)
    if not entries:
        return keccak256(rlp_bytes(b""))
    pairs = [(_nibbles(key), value) for key, value in sorted(entries.items())]
    return keccak256(_build(pairs))


def word(value_int):
    return value_int.to_bytes(32, "big")


# ---- the fixtures the C++ tests hardcode ----

FIXTURES = {
    # AccountStorageRootTest/three_slots_match_offline_reference and the engine-side
    # EngineServiceTest/withdrawals_root_is_the_message_passer_storage_root.
    "three_slots": {
        word(0): word(1),
        word(1): word(0xDEADBEEF),
        bytes([0xFF] * 32): word(0xFF),
    },
    # Adding an all-zero slot must not change the root (zero == not in the trie).
    "three_slots_plus_zero": {
        word(0): word(1),
        word(1): word(0xDEADBEEF),
        bytes([0xFF] * 32): word(0xFF),
        word(2): word(0),
    },
    "single_slot": {word(0): word(1)},
    "empty": {},
}


def main():
    for name, slots in FIXTURES.items():
        print(f"{name:24s} 0x{storage_root(slots).hex()}")


if __name__ == "__main__":
    main()
