#!/usr/bin/env python3
# Copyright (c) FISCO-BCOS, Apache-2.0
"""Compute the op-geth-compatible genesis state root from FISCO-BCOS allocs.

Pure-Python Ethereum MPT (secure trie) over the genesis [alloc.N] INI sections,
byte-identical to C++ ``Ledger::computeGenesisStateTrie``
(bcos-ledger/bcos-ledger/GenesisStateRoot.cpp). Both reproduce what go-ethereum /
op-geth ``Genesis.ToBlock()`` produces for the same alloc set.

Construction (mirrors the C++ exactly):
  - State trie is a SECURE trie: leaf key = keccak256(address20).
  - Leaf value = RLP([nonce, balance, storageRoot, codeHash]) (Ethereum
    StateAccount): nonce/balance as minimal big-endian RLP integers (0 -> 0x80),
    storageRoot/codeHash as 32-byte RLP strings (0xa0 || 32 bytes).
  - storageRoot is the SECURE storage trie over (keccak256(slot32) ->
    RLP(value-with-leading-zero-bytes-trimmed)); empty storage -> emptyRootHash().
    A zero-valued slot is a no-op in Ethereum state and is skipped.
  - codeHash = keccak256(code); empty code -> emptyCodeHash().
  - Empty alloc set -> emptyRootHash().

MPT node encoding follows the C++ mpt module (bcos-ledger/bcos-ledger/mpt):
  - Leaf      = RLP list [HP(suffix, leaf=true),  value]
  - Extension = RLP list [HP(shared, leaf=false), child_ref_raw]
  - Branch    = RLP list [child0..child15, value]
  - Child ref: absent -> 0x80; inline (RLP < 32 bytes) -> raw bytes spliced;
    hash (RLP >= 32 bytes) -> 0xa0 || keccak256(raw).
  - Root is ALWAYS a 32-byte hash, even when the top node encodes to < 32 bytes.

Verified against an independent implementation: test_mpt_state_root.py
(test_state_root_matches_independent_golden_oracle) pins compute_state_root against
gen_trieroot_golden.py — a separate RLP/MPT implementation whose vectors predate this
tool — so a drift in either changes the genesis hash and fails the committed test
instead of shipping a silently different genesis.
"""
import importlib.util
from pathlib import Path

_SPEC = importlib.util.spec_from_file_location(
    "build_allocs", str(Path(__file__).parent / "build-allocs.py"))
_build_allocs = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(_build_allocs)
keccak256 = _build_allocs.keccak256

EMPTY_TRIE_ROOT = bytes.fromhex(
    "56e81f171bcc55a6ff8345e692c0f86e5b48e01b996cadc001622fb5e363b421")
EMPTY_CODE_HASH = bytes.fromhex(
    "c5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470")


def strip0x(text):
    return text[2:] if text.startswith("0x") else text


# ---------------------------------------------------------------------------
# RLP (minimal subset, independent of the C++ bcos-rlp-protocol encoder)
# ---------------------------------------------------------------------------

def rlp_bytes(payload):
    """RLP-encode a byte string."""
    if len(payload) == 1 and payload[0] < 0x80:
        return payload
    if len(payload) < 56:
        return bytes([0x80 + len(payload)]) + payload
    length_bytes = len(payload).to_bytes((len(payload).bit_length() + 7) // 8, "big")
    return bytes([0xB7 + len(length_bytes)]) + length_bytes + payload


def rlp_int(value):
    """RLP-encode an unsigned integer (minimal big-endian, 0 -> 0x80)."""
    if value == 0:
        return b"\x80"
    return rlp_bytes(value.to_bytes((value.bit_length() + 7) // 8, "big"))


def rlp_list_payload(payload):
    """RLP-encode a list from its already-encoded payload bytes."""
    if len(payload) < 56:
        return bytes([0xC0 + len(payload)]) + payload
    length_bytes = len(payload).to_bytes((len(payload).bit_length() + 7) // 8, "big")
    return bytes([0xF7 + len(length_bytes)]) + length_bytes + payload


# ---------------------------------------------------------------------------
# MPT
# ---------------------------------------------------------------------------

def bytes_to_nibbles(data):
    out = []
    for byte in data:
        out.append((byte >> 4) & 0x0F)
        out.append(byte & 0x0F)
    return out


def common_prefix_len(a, b):
    i = 0
    while i < min(len(a), len(b)) and a[i] == b[i]:
        i += 1
    return i


def hex_prefix_encode(nibbles, is_leaf):
    """Hex-Prefix (compact) encoding, Yellow Paper Appendix C. nibbles: [0..15]."""
    if not nibbles:
        raise ValueError("hex-prefix encoding requires at least one nibble")
    odd = len(nibbles) % 2 == 1
    first = (0x20 if is_leaf else 0) | (0x10 if odd else 0)
    out = bytearray()
    start = 0
    if odd:
        first |= nibbles[0]
        start = 1
    out.append(first)
    for i in range(start, len(nibbles) - 1, 2):
        out.append(((nibbles[i] & 0x0F) << 4) | (nibbles[i + 1] & 0x0F))
    return bytes(out)


def encode_node(node):
    """node: ('leaf', nibbles, value) | ('ext', nibbles, child_ref) | ('branch', children, value).
    child_ref is ALREADY a complete RLP-encoded child reference (inline raw or 0xa0||hash).
    """
    kind = node[0]
    if kind == "leaf":
        _, nibbles, value = node
        hpe = hex_prefix_encode(nibbles, True)
        return rlp_list_payload(rlp_bytes(hpe) + rlp_bytes(value))
    if kind == "ext":
        _, nibbles, child_ref = node
        hpe = hex_prefix_encode(nibbles, False)
        return rlp_list_payload(rlp_bytes(hpe) + child_ref)
    if kind == "branch":
        _, children, value = node
        payload = b"".join(b"\x80" if c is None else c for c in children)
        payload += rlp_bytes(value)
        return rlp_list_payload(payload)
    raise ValueError(f"unknown node kind: {kind}")


def node_ref(node):
    """Compute (kind, data): inline raw bytes (< 32) or keccak256 digest (>= 32)."""
    raw = encode_node(node)
    if len(raw) < 32:
        return ("inline", raw)
    return ("hash", keccak256(raw))


def _node_ref_raw(node):
    kind, data = node_ref(node)
    return data if kind == "inline" else b"\xa0" + data


def build_trie(entries):
    """Build a canonical MPT over (keyHash bytes32, value bytes) pairs; return root hash."""
    sorted_entries = sorted((bytes_to_nibbles(k), v) for k, v in entries)
    if not sorted_entries:
        return EMPTY_TRIE_ROOT

    def build(entries_list, depth):
        if len(entries_list) == 1:
            nibbles, value = entries_list[0]
            return ("leaf", nibbles[depth:], value)
        first_suffix = entries_list[0][0][depth:]
        last_suffix = entries_list[-1][0][depth:]
        cpl = common_prefix_len(first_suffix, last_suffix)
        if cpl > 0:
            child = build_branch(entries_list, depth + cpl)
            shared = entries_list[0][0][depth:depth + cpl]
            return ("ext", shared, _node_ref_raw(child))
        return build_branch(entries_list, depth)

    def build_branch(entries_list, depth):
        children = [None] * 16
        i = 0
        n = len(entries_list)
        while i < n:
            nib = entries_list[i][0][depth]
            j = i + 1
            while j < n and entries_list[j][0][depth] == nib:
                j += 1
            children[nib] = _node_ref_raw(build(entries_list[i:j], depth + 1))
            i = j
        return ("branch", children, b"")

    root_ref = node_ref(build(sorted_entries, 0))
    if root_ref[0] == "hash":
        return root_ref[1]
    return keccak256(root_ref[1])


def encode_storage_value(value_bytes):
    """Ethereum storage-value leaf: RLP of value with leading zeros trimmed.
    Returns None for a zero value (not part of the storage trie)."""
    offset = 0
    while offset < len(value_bytes) and value_bytes[offset] == 0:
        offset += 1
    if offset == len(value_bytes):
        return None
    return rlp_bytes(value_bytes[offset:])


def compute_storage_root(storage):
    """storage: iterable of (slotHex, valueHex) 0x-prefixed 64-char pairs."""
    entries = []
    for slot_hex, value_hex in storage:
        value_bytes = bytes.fromhex(strip0x(value_hex))
        rlp_value = encode_storage_value(value_bytes)
        if rlp_value is None:
            continue  # zero value: skipped
        slot_bytes = bytes.fromhex(strip0x(slot_hex))
        # The genesis contract keccaks the slot bytes AS CONFIGURED (see
        # bcos-ledger GenesisStateRoot.cpp: "not mpt::slotKeyHash, which right-aligns
        # into a fixed 32 bytes"). geth instead keccaks the zero-padded slot32 — so a
        # short slot here silently roots a DIFFERENT trie than geth would. build-allocs
        # always emits 64-char pairs; reject anything else loudly instead of rooting
        # over bytes the input never meant.
        if len(slot_bytes) != 32:
            raise ValueError(
                f"storage slot must be exactly 32 bytes (64 hex chars), got "
                f"{len(slot_bytes)} bytes: {slot_hex!r} — pad the slot or fix the input")
        slot_key_hash = keccak256(slot_bytes)
        entries.append((slot_key_hash, rlp_value))
    return build_trie(entries)


def compute_state_root(allocs):
    """allocs: list of dicts with address/balance/nonce/code/[storage]."""
    state_entries = []
    for alloc in allocs:
        storage_root = compute_storage_root(alloc.get("storage", []))
        code_bytes = bytes.fromhex(strip0x(alloc.get("code", "")))
        code_hash = EMPTY_CODE_HASH if not code_bytes else keccak256(code_bytes)
        account_rlp = rlp_list_payload(b"".join([
            rlp_int(int(alloc.get("nonce", 0))),
            rlp_int(int(alloc.get("balance", 0))),
            rlp_bytes(storage_root),
            rlp_bytes(code_hash),
        ]))
        addr_bytes = bytes.fromhex(strip0x(alloc["address"]))
        # Same loud-reject contract as the slot lane above: the state key is
        # keccak256(address) over EXACTLY 20 bytes, so a 19/21-byte address would
        # silently root a different trie (keccak over different-width bytes) — reject
        # anything the input never meant instead of rooting over it.
        if len(addr_bytes) != 20:
            raise ValueError(
                f"alloc address must be exactly 20 bytes (40 hex chars), got "
                f"{len(addr_bytes)} bytes: {alloc['address']!r} — fix the allocs INI")
        addr_key_hash = keccak256(addr_bytes)
        state_entries.append((addr_key_hash, account_rlp))
    return build_trie(state_entries)


def parse_allocs_ini(path):
    """Parse [alloc.N] sections (and optional [alloc.N.storage] subsections) from an INI file.
    Matches NodeConfig::loadAllocs field semantics (address/balance/nonce/code/storage)."""
    allocs = []
    current = None
    current_storage = None
    with open(path) as handle:
        for raw in handle:
            line = raw.strip()
            if not line or line.startswith("#") or line.startswith(";"):
                continue
            if line.startswith("[") and line.endswith("]"):
                section = line[1:-1]
                if section.startswith("alloc.") and section.endswith(".storage"):
                    if current is None:
                        raise ValueError(
                            f"{path}: [alloc.N.storage] section before any [alloc.N] — "
                            "fix the INI section order")
                    current_storage = []
                    current["storage"] = current_storage
                elif section.startswith("alloc."):
                    address = section[len("alloc."):]
                    if any(a.get("address", "").lower() == address.lower() for a in allocs):
                        # Two sections for one address used to survive parsing and only
                        # fail deep inside build_branch (IndexError at depth 64); the
                        # genesis hash for a duplicated alloc is meaningless either way.
                        raise ValueError(
                            f"{path}: duplicate [alloc.{address}] section — merge the "
                            "storage entries into one section")
                    current = {"address": address, "storage": []}
                    allocs.append(current)
                    current_storage = None
                else:
                    current = None
                    current_storage = None
                continue
            if current is None:
                continue
            key, _, value = line.partition("=")
            key = key.strip()
            value = value.strip()
            if current_storage is not None:
                # Duplicate slot keys inside one section survive parsing only to crash
                # deep inside build_branch (IndexError at depth 64) — the same
                # late-failure mode the duplicate-address guard above closes. Two
                # entries for one slot overwrite each other in any real MPT, so the
                # root would be meaningless either way; reject at parse time.
                if any(existing == key for existing, _ in current_storage):
                    raise ValueError(
                        f"{path}: duplicate storage slot {key!r} inside one "
                        "[alloc.N.storage] section — merge the entries into one line")
                current_storage.append((key, value))
            else:
                current[key] = value
    return allocs


if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("allocs", help="allocs INI path ([alloc.N] sections)")
    args = parser.parse_args()
    allocs = parse_allocs_ini(args.allocs)
    root = compute_state_root(allocs)
    print(f"state_root = 0x{root.hex()}")
