#!/usr/bin/env python3
"""Independent golden-vector generator for bcos-ledger's trie roots.

EthTrieRootsTest.cpp (bcos-ledger/test/unittests/mpt/) anchors
computeIndexedTrieRoot / calculateTransactionsRoot / calculateReceiptsRoot /
calculateWithdrawalsRoot, and GenesisStateRootTest.cpp's GoldenVector anchors
the genesis state root, to this SECOND implementation: the MPT below is a
from-scratch Python hexary-trie (real keccak-256 via pycryptodome), sharing no
code with the C++ HashBuilder, so a bug in either side shows up as a mismatch
instead of cancelling out.

Run:  python3 tools/opstack-genesis/gen_trieroot_golden.py
Exit code is non-zero unless every computed root equals the value pinned in
the C++ test; extend the vectors here and in the test together.
"""

from Crypto.Hash import keccak


def keccak256(data: bytes) -> bytes:
    h = keccak.new(digest_bits=256)
    h.update(data)
    return h.digest()


# --- RLP ---------------------------------------------------------------------

def _len_prefix(offset: int, length: int) -> bytes:
    if length < 56:
        return bytes([offset + length])
    lb = length.to_bytes((length.bit_length() + 7) // 8, "big")
    return bytes([offset + 55 + len(lb)]) + lb


def rlp_encode(x) -> bytes:
    if isinstance(x, int):
        return rlp_encode(b"" if x == 0 else x.to_bytes((x.bit_length() + 7) // 8, "big"))
    if isinstance(x, (bytes, bytearray)):
        x = bytes(x)
        if len(x) == 1 and x[0] < 0x80:
            return x
        return _len_prefix(0x80, len(x)) + x
    payload = b"".join(rlp_encode(i) for i in x)
    return _len_prefix(0xC0, len(payload)) + payload


# --- Merkle Patricia Trie ----------------------------------------------------

def _nibbles(b: bytes):
    return [x for byte in b for x in (byte >> 4, byte & 0x0F)]


def _pack(nibs):
    return bytes((nibs[i] << 4) | nibs[i + 1] for i in range(0, len(nibs), 2))


def _hex_prefix(nibs, is_leaf: bool) -> bytes:
    flag = 2 if is_leaf else 0
    if len(nibs) % 2:
        return bytes([16 * (flag + 1) + nibs[0]]) + _pack(nibs[1:])
    return bytes([16 * flag]) + _pack(nibs)


def _ref(node: list):
    # Reference a child node inside its parent: embedded as the decoded
    # structure (encoded inline, as a nested list, by the parent's own rlp)
    # when its encoding is shorter than 32 bytes; referenced by its
    # keccak hash otherwise (Ethereum's rule).
    encoded = rlp_encode(node)
    return node if len(encoded) < 32 else keccak256(encoded)


def _build(items, depth: int) -> list:
    """Node structure covering `items` (sorted (nibbles, value) pairs) at `depth`."""
    if len(items) == 1:
        nibs, value = items[0]
        return [_hex_prefix(nibs[depth:], True), value]
    first = items[0][0]
    prefix = depth
    while prefix < len(first) and all(
        len(n) > prefix and n[prefix] == first[prefix] for n, _ in items
    ):
        prefix += 1
    if prefix > depth:
        return [_hex_prefix(first[depth:prefix], False), _ref(_build(items, prefix))]
    branch = [b""] * 17
    groups = {}
    for nibs, value in items:
        if len(nibs) == depth:
            branch[16] = value
        else:
            groups.setdefault(nibs[depth], []).append((nibs, value))
    for idx, group in groups.items():
        branch[idx] = _ref(_build(group, depth + 1))
    return branch


def trie_root(mapping: dict) -> bytes:
    if not mapping:
        return keccak256(rlp_encode(b""))
    items = sorted((_nibbles(k), v) for k, v in mapping.items())
    return keccak256(rlp_encode(_build(items, 0)))


def indexed_trie_root(elements) -> bytes:
    """Ethereum's transactions/receipts/withdrawals trie: key = rlp(index)."""
    return trie_root({rlp_encode(i): bytes(e) for i, e in enumerate(elements)})


EMPTY_ROOT = keccak256(rlp_encode(b""))
KECCAK_EMPTY = keccak256(b"")


def state_root(allocs) -> bytes:
    """Ethereum genesis state root over (address, nonce, balance, code, storage)
    allocs — the construction go-ethereum's Genesis.ToBlock() uses: a SECURE
    state trie (leaf key = keccak256(address)) of RLP([nonce, balance,
    storageRoot, codeHash]), where storageRoot is the SECURE storage trie over
    keccak256(slot32) -> RLP(value with leading zero bytes trimmed); zero slots
    are no-ops, and empty storage / empty code use the canonical sentinels.
    Anchors GenesisStateRootTest.cpp's GoldenVector."""
    leaves = {}
    for address, nonce, balance, code, storage in allocs:
        slots = {
            keccak256(k): rlp_encode(v.lstrip(b"\0"))
            for k, v in storage.items()
            if v != bytes(32)
        }
        storage_root = trie_root(slots) if slots else EMPTY_ROOT
        code_hash = keccak256(code) if code else KECCAK_EMPTY
        leaves[keccak256(address)] = rlp_encode([nonce, balance, storage_root, code_hash])
    return trie_root(leaves)


# --- Vectors (byte-identical to EthTrieRootsTest.cpp) ------------------------

TXS = [
    bytes.fromhex(
        "02e2018001825208809411111111111111111111111111111111111111118080c0010102"
    ),
    bytes.fromhex(
        "03f8440180018252089422222222222222222222222222222222222222228080c001e1a00000"
        "000000000000000000000000000000000000000000000000000000000000010102"
    ),
    bytes.fromhex(
        "e301843b9aca0082520894333333333333333333333333333333333333333380801b0304"
    ),
    bytes.fromhex(
        "d102843b9aca00825208800582dead1c0506"
    ),
]

RECEIPTS = [
    bytes.fromhex(
        "02f9010801825208b90100000000000000000000000000000000000000000000000000000000"
        "0000000000000000000000000000000000000000000000000000000000000000000000000000"
        "0000000000000000000000000000000000000000000000000000000000000000000000000000"
        "0000000000000000000000000000000000000000000000000000000000000000000000000000"
        "0000000000000000000000000000000000000000000000000000000000000000000000000000"
        "0000000000000000000000000000000000000000000000000000000000000000000000000000"
        "0000000000000000000000000000000000000000000000000000000000000000000000000000"
        "00c0"
    ),
    bytes.fromhex(
        "03f9010801825208b90100000000000000000000000000000000000000000000000000000000"
        "0000000000000000000000000000000000000000000000000000000000000000000000000000"
        "0000000000000000000000000000000000000000000000000000000000000000000000000000"
        "0000000000000000000000000000000000000000000000000000000000000000000000000000"
        "0000000000000000000000000000000000000000000000000000000000000000000000000000"
        "0000000000000000000000000000000000000000000000000000000000000000000000000000"
        "0000000000000000000000000000000000000000000000000000000000000000000000000000"
        "00c0"
    ),
    bytes.fromhex(
        "f901068080b90100000000000000000000000000000000000000000000000000000000000000"
        "0000000000000000000000000000000000000000000000000000000000000000000000000000"
        "0000000000000000000000000000000000000000000000000000000000000000000000000000"
        "0000000000000000000000000000000000000000000000000000000000000000000000000000"
        "0000000000000000000000000000000000000000000000000000000000000000000000000000"
        "0000000000000000000000000000000000000000000000000000000000000000000000000000"
        "000000000000000000000000000000000000000000000000000000000000000000000000c0"
    ),
    bytes.fromhex(
        "01f901470182c350b90100000000000000000000000000000000000000000000000000000000"
        "0000000000000000000000000000000000000000000000000000000000000000000000000000"
        "0000000000000000000000000000000000000000000000000000000000000000000000000000"
        "0000000000000000000000000000000000000000000000000000000000000000000000000000"
        "0000000000000000000000000000000000000000000000000000000000000000000000000000"
        "0000000000000000000000000000000000000000000000000000000000000000000000000000"
        "0000000000000000000000000000000000000000000000000000000000000000000000000000"
        "00f83ef83c941111111111111111111111111111111111111111e1a022222222222222222222"
        "2222222222222222222222222222222222222222222284deadbeef"
    ),
]

WITHDRAWALS = [
    bytes.fromhex(
        "d8010294333333333333333333333333333333333333333304"
    ),
    bytes.fromhex(
        "de6481c894aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa85e8d4a51000"
    ),
    bytes.fromhex(
        "da8201000194bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb80"
    ),
]

# 200 items force 2-byte rlp keys (index >= 128): item i is bytes([i, 7*i mod 256]).
MANY_ITEMS = [bytes([i, (7 * i) % 256]) for i in range(200)]

# Single-account genesis alloc, byte-identical to GenesisStateRootTest.cpp's
# GoldenVector: (address, nonce, balance, code, {slot32: value32}).
GOLDEN_ALLOC = [
    (
        bytes.fromhex("43000000000000000000000000000000000000c0"),
        0,  # nonce
        0,  # balance
        bytes.fromhex("6080604052"),
        {bytes(32): bytes.fromhex("0385").rjust(32, b"\0")},
    ),
]

EXPECTED = {
    "empty":        "0x56e81f171bcc55a6ff8345e692c0f86e5b48e01b996cadc001622fb5e363b421",
    "transactions": "0xb0324982374a362506a47c4c22316dc9507c08b8f77cf8ee1f763ebbd1bc4806",
    "receipts":     "0x2d82724250d2cb15d23a84a75c0ccce1c4fbd6032110e3be57eccbf68866353d",
    "withdrawals":  "0xa42c3ab8f3dd29c60f0182050b7a77a8603d83e035368004b7450857d3cd172e",
    "many_items":   "0x1e6f2f0fd22412f2ec4284e0c57f0b1032b6f578b1cb3fd64282eecec528a3a3",
    "golden_state": "0xf4ea8faf448deb0ccd599a526b6d8c61e090d6e3c6a1f7618b5e2049327e73f5",
}


def main() -> int:
    actual = {
        "empty": "0x" + indexed_trie_root([]).hex(),
        "transactions": "0x" + indexed_trie_root(TXS).hex(),
        "receipts": "0x" + indexed_trie_root(RECEIPTS).hex(),
        "withdrawals": "0x" + indexed_trie_root(WITHDRAWALS).hex(),
        "many_items": "0x" + indexed_trie_root(MANY_ITEMS).hex(),
        "golden_state": "0x" + state_root(GOLDEN_ALLOC).hex(),
    }
    failed = False
    for name, expected in EXPECTED.items():
        ok = actual[name] == expected
        failed |= not ok
        print(f"{name:<14} {actual[name]}  {'OK' if ok else 'MISMATCH, expected ' + expected}")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
