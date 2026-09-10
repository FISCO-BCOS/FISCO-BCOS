#!/usr/bin/env python3
# Copyright (c) FISCO-BCOS, Apache-2.0
"""Merge FISCO's predeploy overlay onto op-deployer terminal allocs.

The op-deployer-generated Karst terminal alloc JSON is the ONLY source for
the OP-Stack side of genesis (every 0x42... predeploy proxy, every 0xc0d3...
implementation, ProxyAdmin ownership, prefunded accounts). This tool never
synthesizes an OP account: `--base-allocs` is a required input, and the tool
does exactly two things —

  (a) overlay the FISCO self-written predeploys (SystemConfig,
      L2ValidatorSet) in their post-deployment terminal state, three layers
      each:
        1. proxy account (0x43...): the canonical Proxy runtime bytecode
           (copied from a designated base-alloc proxy, `proxy_code_source`,
           so the bytes are the deployer's by construction) + EIP-1967
           implementation/admin slots;
        2. implementation account (0xc3d3..., outside every reserved
           namespace): implementation bytecode from the bcos-l2-contracts
           forge build, storage empty except `_initialized = 255` (the OZ
           `_disableInitializers()` terminal state);
        3. contract storage on the PROXY account: `_initialized = 1`, the
           `Ownable.owner` slot (51) = the governance owner, and the
           contract state — SystemConfig packed Entry slots /
           L2ValidatorSet validator records.
  (b) merge base + overlay into one account set and emit it as the
      [alloc.N] INI sections NodeConfig.loadAllocs() parses (and optionally
      as a geth-style alloc JSON via --out-json, so the SAME merged set
      feeds both FISCO and the op-reth oracle genesis).

An overlay address that already exists in the base is a hard error; the
`expected_predeploys` checklist in the chain config asserts the base really
carries the OP predeploys the L2 relies on.

Two-authority split: the EIP-1967 admin slot (upgrade authority) is
ProxyAdmin, while `Ownable.owner` (config/validator write authority) is the
governance entity — two independent slots, never the same entity (enforced).

The `feature_flags` SystemConfig entry is a REQUIRED input for the
SystemConfig predeploy: it must equal the node's
Features::toFlagsNumber() at genesis, it is written here as a packed Entry
slot like every other config key, and the C++ genesis path VERIFIES it
(no longer injects it) — so the genesis state root, the op-reth alloc JSON
and FISCO's readable state all commit the same slot.

If a self-written implementation artifact still carries immutableReferences
(unfilled immutables), the build aborts naming the contract.
"""
import argparse
import hashlib
import json
import sys
from pathlib import Path


# ---------------------------------------------------------------------------
# keccak256 (pure python, no dependency).
#
# hashlib.sha3_256 is NIST SHA-3 (0x06 domain padding) and produces DIFFERENT
# digests from Ethereum's keccak256 (0x01 padding), so it cannot be used here.
# Verified against the standard vectors in test_build_allocs.py
# (keccak256("") = c5d24601..., keccak256("abc") = 4e03657a...).
# ---------------------------------------------------------------------------

_KECCAK_RC = [
    0x0000000000000001, 0x0000000000008082, 0x800000000000808A, 0x8000000080008000,
    0x000000000000808B, 0x0000000080000001, 0x8000000080008081, 0x8000000000008009,
    0x000000000000008A, 0x0000000000000088, 0x0000000080008009, 0x000000008000000A,
    0x000000008000808B, 0x800000000000008B, 0x8000000000008089, 0x8000000000008003,
    0x8000000000008002, 0x8000000000000080, 0x000000000000800A, 0x800000008000000A,
    0x8000000080008081, 0x8000000000008080, 0x0000000080000001, 0x8000000080008008,
]
_KECCAK_ROTC = [1, 3, 6, 10, 15, 21, 28, 36, 45, 55, 2, 14,
                27, 41, 56, 8, 25, 43, 62, 18, 39, 61, 20, 44]
_KECCAK_PILN = [10, 7, 11, 17, 18, 3, 5, 16, 8, 21, 24, 4,
                15, 23, 19, 13, 12, 2, 20, 14, 22, 9, 6, 1]
_MASK64 = (1 << 64) - 1


def _rotl64(value, shift):
    return ((value << shift) | (value >> (64 - shift))) & _MASK64


def _keccak_f1600(state):
    for round_index in range(24):
        # theta
        parity = [state[i] ^ state[i + 5] ^ state[i + 10] ^ state[i + 15] ^ state[i + 20]
                  for i in range(5)]
        for i in range(5):
            effect = parity[(i + 4) % 5] ^ _rotl64(parity[(i + 1) % 5], 1)
            for j in range(0, 25, 5):
                state[j + i] ^= effect
        # rho + pi
        lane = state[1]
        for i in range(24):
            j = _KECCAK_PILN[i]
            lane, state[j] = state[j], _rotl64(lane, _KECCAK_ROTC[i])
        # chi
        for j in range(0, 25, 5):
            row = state[j:j + 5]
            for i in range(5):
                state[j + i] = row[i] ^ ((row[(i + 1) % 5] ^ _MASK64) & row[(i + 2) % 5])
        # iota
        state[0] ^= _KECCAK_RC[round_index]


def keccak256(data):
    """Ethereum keccak256 of `data` (bytes) -> 32-byte digest."""
    rate = 136  # 1088-bit rate for 256-bit output
    state = [0] * 25
    padded = bytearray(data)
    padded.append(0x01)
    while len(padded) % rate:
        padded.append(0x00)
    padded[-1] |= 0x80
    for offset in range(0, len(padded), rate):
        for i in range(rate // 8):
            state[i] ^= int.from_bytes(padded[offset + i * 8:offset + (i + 1) * 8], "little")
        _keccak_f1600(state)
    return b"".join(state[i].to_bytes(8, "little") for i in range(4))


# ---------------------------------------------------------------------------
# Storage-slot layout constants.
#
# The OZ slots are pinned by the storage-layout drift gate
# (bcos-l2-contracts/storage-layout/*.json): _initialized shares slot 0,
# _owner is slot 51, and both contracts' own state starts at slot 101.
# ---------------------------------------------------------------------------

# EIP-1967: keccak256("eip1967.proxy.implementation") - 1 / ...proxy.admin - 1
EIP1967_IMPLEMENTATION_SLOT = int(
    "360894a13ba1a3210667c828492db98dca3e2076cc3735a920a3ca505d382bbc", 16)
EIP1967_ADMIN_SLOT = int(
    "b53127684a568b3173ae13b9f8a6016e243e63b6e8ee1178d6a717850b5d6103", 16)

OZ_INITIALIZED_SLOT = 0   # Initializable._initialized (uint8, offset 0)
OZ_OWNER_SLOT = 51        # OwnableUpgradeable._owner

SYSTEM_CONFIG_BASE_SLOT = 101      # SystemConfig._config mapping
VALIDATOR_SET_VALUES_SLOT = 101    # L2ValidatorSet._validatorSet._inner._values
VALIDATOR_SET_INDEXES_SLOT = 102   # L2ValidatorSet._validatorSet._inner._indexes
VALIDATOR_BY_ADDR_SLOT = 103       # L2ValidatorSet._validatorByAddr

# `_initialized` terminal states (OZ v4.7 Initializable):
INITIALIZED_RAN = 1        # proxy storage: as if initialize() already ran
INITIALIZED_DISABLED = 255  # implementation storage: _disableInitializers()

# OP-Stack reserved namespaces (Predeploys.sol): proxies live in
# 0x4200...0000-0x4200...07FF, their implementations in
# 0xc0d3...0000 | (last 2 bytes). Self-written predeploys must avoid both.
_OP_PREDEPLOY_NAMESPACE_BASE = 0x4200000000000000000000000000000000000000
_OP_CODE_NAMESPACE_BASE = 0xC0D3C0D3C0D3C0D3C0D3C0D3C0D3C0D3C0D30000


# The chain-config template's governance_owner placeholder. Like the zero
# address it has no known private key, so shipping it locks governance
# forever; unlike the zero address it slips past a non-zero check — reject it
# explicitly.
PLACEHOLDER_ADDRESS = 0xDEAD

# The FISCO SystemConfig predeploy (0x43...00C0). Its alloc MUST carry the
# feature_flags Entry slot: the C++ genesis path verifies (and no longer
# injects) that slot, so omitting it here would leave the genesis state root
# not committing the feature set the node actually runs with.
SYSTEM_CONFIG_PREDEPLOY = 0x43000000000000000000000000000000000000C0

# OP ProxyAdmin storage layout: the contract is plain OZ `Ownable`, so its
# `_owner` lives at slot 0. The upgrade authority of every proxied predeploy
# is ProxyAdmin.owner — NOT the ProxyAdmin contract address — and the base
# allocs are the source of truth for it.
PROXY_ADMIN_OWNER_SLOT = 0


def in_reserved_namespace(address_int):
    """True if the address collides with an OP-Stack reserved namespace."""
    if (address_int >> 11) == (_OP_PREDEPLOY_NAMESPACE_BASE >> 11):
        return True
    return (address_int & ~0xFFFF) == _OP_CODE_NAMESPACE_BASE


def normalize_hex(value):
    """Return a lowercase 0x-prefixed hex string from a hex string or int."""
    if isinstance(value, int):
        return "0x" + format(value, "x")
    text = str(value).strip().lower()
    if not text.startswith("0x"):
        text = "0x" + text
    return text


def strip0x(value):
    text = str(value).strip().lower()
    return text[2:] if text.startswith("0x") else text


def parse_amount(value, label):
    """Parse a balance/nonce that may arrive as int, 0x-hex or decimal string."""
    if isinstance(value, int):
        return value
    text = str(value).strip().lower()
    if text.startswith("0x"):
        return int(text, 16) if len(text) > 2 else 0
    return int(text, 10) if text else 0


def verify_base_provenance(path, expected_sha256):
    """Pin the base alloc file to the frozen artifact by whole-file SHA-256.

    The op-deployer output carries no version metadata of its own, and the
    Karst predeploy codehashes are unknown until the real artifact is
    generated — so the strongest static pin available today is the SHA-256 of
    the whole file, recorded next to the frozen quintuple
    (bcos-l2-contracts/op-fork-pin.toml [karst_pin].base_allocs_sha256) and
    mirrored into the chain config. When the chain config carries no
    expected hash yet (pre-freeze iteration), warn loudly instead of failing.
    """
    digest = hashlib.sha256(Path(path).read_bytes()).hexdigest()
    if not expected_sha256:
        sys.stderr.write(
            f"WARNING: base allocs provenance unpinned - set base_allocs_sha256 "
            f"in the chain config once the artifact is frozen "
            f"(sha256 of {path} = {digest})\n")
        return digest
    expected = strip0x(expected_sha256).lower()
    if digest != expected:
        raise ValueError(
            f"{path}: sha256 mismatch - file is {digest} but the chain config "
            f"pins base_allocs_sha256 = {expected}; this is not the frozen "
            f"op-deployer artifact")
    return digest


def load_base_allocs(path):
    """Load the op-deployer terminal alloc JSON: {address: account} keyed by int.

    Accepts either a bare {address: account} mapping or a genesis file whose
    accounts sit under an "alloc" / "accounts" key. Zero-valued storage slots
    are dropped: a zero slot does not exist in the canonical state trie, so
    carrying it forward could only create a FISCO/op-reth divergence.
    """
    with open(path) as handle:
        data = json.load(handle)
    for wrapper in ("alloc", "accounts"):
        if isinstance(data, dict) and isinstance(data.get(wrapper), dict):
            data = data[wrapper]
            break
    if not isinstance(data, dict) or not data:
        raise ValueError(
            f"{path}: expected a non-empty op-deployer alloc mapping "
            f"(address -> account), optionally wrapped in 'alloc'/'accounts'")
    accounts = {}
    for address, account in data.items():
        addr = address_int(address)
        if addr in accounts:
            raise ValueError(f"{path}: duplicate base account 0x{addr:040x}")
        nonce = parse_amount(account.get("nonce", 0), "nonce")
        if nonce >= (1 << 64):
            raise ValueError(f"{path}: 0x{addr:040x} nonce does not fit uint64")
        storage = {}
        for slot, value in (account.get("storage") or {}).items():
            value_int = word_int(value)
            if value_int:
                storage[word_int(slot)] = value_int
        code = account.get("code") or ""
        accounts[addr] = {
            "address": "0x" + format(addr, "040x"),
            "balance": parse_amount(account.get("balance", 0), "balance"),
            "nonce": nonce,
            "code": normalize_hex(code) if strip0x(code) else "",
            "storage": storage,
        }
    return accounts


def word_int(value):
    """Parse a storage slot/value that may arrive as int or hex string.

    YAML parses an unquoted `0x10` into the int 16; feeding that through
    `int(strip0x(...), 16)` would silently re-read the decimal digits as hex
    (16 -> 22). Ints are taken as-is; only strings go through hex parsing.
    """
    if isinstance(value, int):
        return value
    return int(strip0x(value), 16)


def config_word(value, label):
    """word_int with a strict-prefix rule for quoted values.

    A quoted decimal like "100" would silently re-read as hex (256) under
    word_int; config values are consensus parameters, so require quoted
    values to carry an explicit 0x prefix instead.
    """
    if isinstance(value, int):
        return value
    text = str(value).strip().lower()
    if not text.startswith("0x"):
        raise ValueError(
            f"{label}: quoted values must be 0x-prefixed hex, got '{value}' "
            f"(write it unquoted for decimal, or 0x... for hex)")
    return int(text[2:] or "0", 16)


def address_int(value):
    """Parse a 20-byte address (hex string) into an int, validating width."""
    text = strip0x(value)
    if len(text) != 40:
        raise ValueError(f"address must be 20 bytes (40 hex chars): {value}")
    return int(text, 16)


def word_hex(value_int):
    """Render an int as a full-width 32-byte 0x-hex word.

    Ledger::importGenesisState unhexes storage keys/values with fixed-width
    32-byte buffers (left-aligned), so short hex would be misread — always
    emit the full 64 chars.
    """
    return "0x" + format(value_int, "064x")


def mapping_slot(key_bytes, base_slot):
    """Solidity mapping value slot: keccak256(key_bytes || be32(base_slot))."""
    digest = keccak256(bytes(key_bytes) + base_slot.to_bytes(32, "big"))
    return int.from_bytes(digest, "big")


def put_dynamic_bytes(storage, slot, data):
    """Write a Solidity `bytes` value rooted at `slot` (standard encoding)."""
    length = len(data)
    if length < 32:
        word = int.from_bytes(data.ljust(32, b"\0"), "big") | (2 * length)
        if word:
            storage[slot] = word
    else:
        storage[slot] = 2 * length + 1
        data_base = int.from_bytes(keccak256(slot.to_bytes(32, "big")), "big")
        for i in range(0, length, 32):
            storage[data_base + i // 32] = int.from_bytes(
                data[i:i + 32].ljust(32, b"\0"), "big")


def system_config_entry_storage(entries):
    """Packed Entry slots for SystemConfig `mapping(string => Entry)`.

    entrySlot(key) = keccak256(utf8(key) || be32(101)); the word packs
    (enableNumber:uint64 high 8 bytes || value:uint192 low 24 bytes).
    Genesis entries activate at block 0, so the word is just the value.
    """
    storage = {}
    for key, value in entries.items():
        # YAML ints verbatim; quoted values must be explicit 0x-hex.
        value = config_word(value, f"system_config['{key}']")
        if value <= 0 or value >= (1 << 192):
            raise ValueError(
                f"system_config['{key}'] must be a positive uint192, got {value} "
                f"(a zero-valued entry writes a zero slot, which does not exist "
                f"in the canonical state trie — the loader would see the key as "
                f"missing)")
        storage[mapping_slot(key.encode("utf-8"), SYSTEM_CONFIG_BASE_SLOT)] = value
    return storage


def validator_storage(validators, name="L2ValidatorSet"):
    """L2ValidatorSet state: EnumerableSet.AddressSet + per-validator records.

    Layout (pinned by storage-layout/L2ValidatorSet.json):
      slot 101         _validatorSet._inner._values.length
      keccak(101)+i    _values[i] = validator address (as bytes32)
      slot 102 map     _indexes[addr] = i + 1
      slot 103 map     _validatorByAddr[addr] -> Validator struct base:
        base+0   feeAddress(20B) | jailed(1B, always 0 in Phase A) |
                 votingPower(8B)  — packed, low-to-high by declaration order
        base+1   consensusPublicKey (dynamic bytes)
        base+2   incoming (always 0 in Phase A — omitted)
    """
    storage = {}
    if not validators:
        return storage
    storage[VALIDATOR_SET_VALUES_SLOT] = len(validators)
    values_base = int.from_bytes(
        keccak256(VALIDATOR_SET_VALUES_SLOT.to_bytes(32, "big")), "big")
    seen = set()
    for index, validator in enumerate(validators):
        addr = address_int(validator["address"])
        if addr == 0:
            # Mirrors L2ValidatorSet._add's `require(a != address(0))`.
            raise ValueError(f"{name}: validator address must not be the zero address")
        if addr == PLACEHOLDER_ADDRESS:
            raise ValueError(
                f"{name}: validator address is still the 0x...dEaD template "
                f"placeholder - replace it with the real address")
        if addr in seen:
            raise ValueError(f"{name}: duplicate validator address: {validator['address']}")
        seen.add(addr)
        addr_word = addr.to_bytes(32, "big")
        storage[values_base + index] = addr
        storage[mapping_slot(addr_word, VALIDATOR_SET_INDEXES_SLOT)] = index + 1

        record_base = mapping_slot(addr_word, VALIDATOR_BY_ADDR_SLOT)
        fee_address = address_int(validator.get("fee_address", validator["address"]))
        if fee_address == 0 or fee_address == PLACEHOLDER_ADDRESS:
            raise ValueError(
                f"{name}: validator {validator['address']}: fee_address is the "
                f"zero address or the 0x...dEaD template placeholder")
        # YAML ints verbatim; quoted values must be explicit 0x-hex.
        voting_power = config_word(
            validator.get("voting_power", 1),
            f"{name}: validator {validator['address']}: voting_power")
        if voting_power < 0 or voting_power >= (1 << 64):
            raise ValueError(
                f"{name}: validator {validator['address']}: voting_power does "
                f"not fit uint64: {voting_power}")
        packed = (voting_power << 168) | fee_address  # jailed byte (160-167) = 0
        if packed:
            storage[record_base] = packed
        try:
            public_key = bytes.fromhex(strip0x(validator["consensus_public_key"]))
        except ValueError as error:
            raise ValueError(
                f"{name}: validator {validator['address']}: consensus_public_key "
                f"is not valid hex: {error}") from error
        if not public_key:
            raise ValueError(
                f"{name}: validator {validator['address']}: consensus_public_key is empty")
        put_dynamic_bytes(storage, record_base + 1, public_key)
    return storage


def load_artifact(contracts_dir, sol_file, name):
    """Load a forge artifact: <contracts>/out/<sol_file>/<name>.json."""
    path = Path(contracts_dir) / "out" / sol_file / f"{name}.json"
    if not path.is_file():
        raise FileNotFoundError(f"forge artifact not found: {path} (run `forge build`?)")
    with path.open() as handle:
        return json.load(handle)


def deployed_bytecode(artifact):
    node = artifact.get("deployedBytecode", {})
    return node.get("object", ""), node.get("immutableReferences", {}) or {}


def checked_bytecode(artifact, name):
    """Deployed bytecode; fail loud on unfilled immutables."""
    code, immutable_refs = deployed_bytecode(artifact)
    if immutable_refs:
        ast_ids = ", ".join(str(k) for k in immutable_refs)
        raise ValueError(
            f"{name}: artifact has unpatched immutables (immutableReferences "
            f"AST ids {ast_ids}); unpatched immutables would deploy broken "
            f"genesis code")
    return code


def build_proxied_allocs(predeploy, config, contracts_dir, base_accounts):
    """Two allocs for a self-written proxied predeploy: proxy + implementation.

    The proxy account carries the canonical Proxy runtime bytecode (copied
    from the base-alloc account named by `proxy_code_source`, so the bytes
    are the op-deployer's by construction), the EIP-1967 slots and ALL
    contract storage (terminal post-initialize state). The implementation
    account carries only the implementation bytecode with initializers
    disabled. See the module docstring for the full three-layer contract.
    """
    name = predeploy["name"]
    proxy_spec = predeploy["proxy"]

    proxy_admin = config.get("proxy_admin")
    governance_owner = config.get("governance_owner")
    if not proxy_admin or not governance_owner:
        raise ValueError(
            f"{name}: proxied predeploys need top-level proxy_admin and "
            f"governance_owner in the chain config")
    for label, value in (("proxy_admin", proxy_admin),
                         ("governance_owner", governance_owner)):
        parsed = address_int(value)
        if parsed == 0:
            raise ValueError(
                f"{name}: {label} must not be the zero address "
                f"(a zero owner bricks governance at genesis)")
        if parsed == PLACEHOLDER_ADDRESS:
            raise ValueError(
                f"{name}: {label} is still the 0x...dEaD template placeholder "
                f"- replace it with the real address (0xdEaD has no known "
                f"private key; shipping it locks governance forever)")
    if address_int(governance_owner) == address_int(proxy_admin):
        raise ValueError(
            f"{name}: proxy_admin and governance_owner must be different "
            f"entities (upgrade authority vs config/validator write "
            f"authority; see the two-authority split in the module doc)")

    # Two-authority split, checked against the REAL upgrade authority: the
    # base alloc's ProxyAdmin.owner (OZ Ownable, slot 0), not merely the
    # ProxyAdmin contract address.
    admin_account = base_accounts.get(address_int(proxy_admin))
    if admin_account is None or not admin_account["code"]:
        raise ValueError(
            f"{name}: proxy_admin {proxy_admin} is missing (or code-less) in "
            f"the base allocs — wrong/incomplete op-deployer output?")
    admin_owner = admin_account["storage"].get(PROXY_ADMIN_OWNER_SLOT, 0)
    if admin_owner == 0:
        raise ValueError(
            f"{name}: ProxyAdmin.owner (slot {PROXY_ADMIN_OWNER_SLOT} of "
            f"{proxy_admin}) is zero/absent in the base allocs — the upgrade "
            f"authority is burned; refuse to mint such a chain")
    if admin_owner == address_int(governance_owner):
        raise ValueError(
            f"{name}: governance_owner equals ProxyAdmin.owner "
            f"(0x{admin_owner:040x}) — upgrade authority and config/validator "
            f"write authority must be different entities")

    proxy_address = address_int(predeploy["address"])
    implementation_address = address_int(proxy_spec["implementation"])
    for label, addr in (("address", proxy_address),
                        ("proxy.implementation", implementation_address)):
        if in_reserved_namespace(addr):
            raise ValueError(
                f"{name}: {label} 0x{addr:040x} falls into an OP-Stack "
                f"reserved namespace (0x4200...0000-07FF predeploys or "
                f"0xc0d3... code namespace)")
    if implementation_address == proxy_address:
        raise ValueError(f"{name}: implementation address equals proxy address")

    implementation_artifact = load_artifact(
        contracts_dir, predeploy.get("sol_file", f"{name}.sol"), name)
    implementation_code = checked_bytecode(implementation_artifact, name)

    # Proxy bytecode is copied from a designated proxied predeploy in the
    # base allocs (e.g. the L1Block proxy) — byte-identical to what the
    # op-deployer emitted, no separate OP forge build involved.
    # PREMISE: every OP predeploy proxy ships the same universal/Proxy.sol
    # runtime bytecode, so any proxied predeploy is a valid donor. If a future
    # op-contracts bump introduces per-predeploy proxy variants, this
    # single-donor scheme must be re-reviewed.
    source = config.get("proxy_code_source")
    if not source:
        raise ValueError(
            f"{name}: chain config needs proxy_code_source — a base-alloc "
            f"proxied predeploy whose Proxy runtime bytecode to reuse "
            f"(e.g. the L1Block proxy 0x42...0015)")
    source_account = base_accounts.get(address_int(source))
    if source_account is None or not source_account["code"]:
        raise ValueError(
            f"{name}: proxy_code_source {source} not found (or code-less) "
            f"in the base allocs")
    proxy_code = source_account["code"]

    system_config = predeploy.get("system_config", {}) or {}
    if proxy_address == SYSTEM_CONFIG_PREDEPLOY and "feature_flags" not in system_config:
        raise ValueError(
            f"{name}: system_config must carry feature_flags (= the node's "
            f"Features::toFlagsNumber() at genesis). The C++ genesis path "
            f"verifies this slot instead of injecting it, so omitting it "
            f"would leave the genesis state root not committing the feature "
            f"set the chain runs with")

    storage = {
        OZ_INITIALIZED_SLOT: INITIALIZED_RAN,
        OZ_OWNER_SLOT: address_int(governance_owner),
        EIP1967_IMPLEMENTATION_SLOT: implementation_address,
        EIP1967_ADMIN_SLOT: address_int(proxy_admin),
    }
    storage.update(system_config_entry_storage(system_config))
    storage.update(validator_storage(predeploy.get("validators", []), name))
    for slot, value in (predeploy.get("storage") or {}).items():
        storage[word_int(slot)] = word_int(value)

    proxy_alloc = {
        "address": normalize_hex(predeploy["address"]),
        "balance": 0,
        "nonce": 0,
        "code": normalize_hex(proxy_code),
        "storage": storage,
    }
    implementation_alloc = {
        "address": normalize_hex(proxy_spec["implementation"]),
        "balance": 0,
        "nonce": 0,
        "code": normalize_hex(implementation_code),
        "storage": {OZ_INITIALIZED_SLOT: INITIALIZED_DISABLED},
    }
    return [proxy_alloc, implementation_alloc]


def build_allocs(config, contracts_dir, base_accounts):
    """Merge the FISCO overlay onto the op-deployer base accounts.

    Returns the full merged account list in ascending address order (the
    deterministic genesis order). The base is authoritative for every OP
    account; the overlay may only ADD accounts — an address collision is a
    hard error. `expected_predeploys` entries assert the base really carries
    the OP predeploys the L2 relies on (present, with code).
    """
    merged = dict(base_accounts)

    for expected in config.get("expected_predeploys") or []:
        addr = address_int(expected["address"])
        account = merged.get(addr)
        if account is None or not account["code"]:
            raise ValueError(
                f"expected predeploy {expected.get('name', '?')} at "
                f"{expected['address']} is missing (or code-less) in the "
                f"base allocs — wrong/incomplete op-deployer output?")

    for predeploy in config["predeploys"]:
        if "proxy" not in predeploy:
            raise ValueError(
                f"{predeploy.get('name', '?')}: only proxied self-written "
                f"predeploys may be overlaid; OP predeploys come from the "
                f"base allocs (list them under expected_predeploys)")
        for alloc in build_proxied_allocs(
                predeploy, config, contracts_dir, base_accounts):
            addr = address_int(alloc["address"])
            if addr in merged:
                raise ValueError(
                    f"overlay account {alloc['address']} collides with an "
                    f"existing base-alloc account")
            merged[addr] = alloc

    return [merged[addr] for addr in sorted(merged)]


def emit_ini(allocs):
    """Render alloc dicts as FISCO-BCOS genesis INI sections.

    Addresses are lowercased; balance/nonce are decimal (NodeConfig
    requireDecimalField). A code-less account (prefunded EOA) omits the
    `code` line — NodeConfig treats a missing code as an EOA alloc. Storage
    slots/values are emitted as full-width 32-byte hex words (Ledger's unhex
    is fixed-width) in ascending slot order so the output is deterministic
    regardless of dict insertion order.
    """
    lines = []
    for index, alloc in enumerate(allocs):
        lines.append(f"[alloc.{index}]")
        lines.append(f"address={normalize_hex(alloc['address'])}")
        lines.append(f"balance={int(alloc.get('balance', 0))}")
        lines.append(f"nonce={int(alloc.get('nonce', 0))}")
        code = alloc.get("code", "")
        if strip0x(code):
            lines.append(f"code={normalize_hex(code)}")
        storage = alloc.get("storage", {})
        if storage:
            lines.append(f"[alloc.{index}.storage]")
            for slot in sorted(storage):
                lines.append(f"{word_hex(slot)}={word_hex(storage[slot])}")
    return "\n".join(lines) + "\n"


def emit_alloc_json(allocs):
    """Render the merged account set as a geth-style alloc JSON mapping.

    This is the shape an op-reth / op-geth genesis `alloc` section consumes,
    so the SAME merged set can feed the oracle chain's genesis.

    balance/nonce use python hex(): minimal-form 0x quantities ("0x0",
    "0x1e8480") — the canonical JSON-RPC QUANTITY encoding geth/op-reth
    genesis parsers accept (the shortest-even-length rule applies to BYTES
    fields, not quantities). If a future consumer chokes on "0x0", normalize
    here rather than at the call sites.
    """
    out = {}
    for alloc in allocs:
        entry = {
            "balance": hex(int(alloc.get("balance", 0))),
            "nonce": hex(int(alloc.get("nonce", 0))),
        }
        code = alloc.get("code", "")
        if strip0x(code):
            entry["code"] = normalize_hex(code)
        storage = alloc.get("storage") or {}
        if storage:
            entry["storage"] = {
                word_hex(slot): word_hex(storage[slot]) for slot in sorted(storage)
            }
        out[alloc["address"]] = entry
    return json.dumps(out, indent=2, sort_keys=True) + "\n"


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Merge the FISCO predeploy overlay onto op-deployer "
                    "terminal allocs and emit genesis [alloc.N] INI")
    parser.add_argument("--config", required=True, help="chain-config YAML path")
    parser.add_argument("--contracts", required=True,
                        help="bcos-l2-contracts dir (containing out/)")
    parser.add_argument("--base-allocs", required=True,
                        help="op-deployer terminal alloc JSON (REQUIRED base; "
                             "every OP account comes from here)")
    parser.add_argument("--out", help="output INI path (default: stdout)")
    parser.add_argument("--out-json",
                        help="also write the merged set as a geth-style alloc "
                             "JSON (feeds the op-reth oracle genesis)")
    args = parser.parse_args(argv)

    # Deferred to the CLI path only: importing this module (for keccak256, e.g. from
    # mpt_state_root.py / gen_eth_header_fixture.py / the e2e suites) must not require pyyaml.
    try:
        import yaml
    except ImportError:  # pragma: no cover - dependency hint only
        sys.stderr.write("error: pyyaml required (pip install pyyaml)\n")
        raise

    with open(args.config) as handle:
        config = yaml.safe_load(handle)
    verify_base_provenance(args.base_allocs, config.get("base_allocs_sha256"))
    base_accounts = load_base_allocs(args.base_allocs)
    allocs = build_allocs(config, args.contracts, base_accounts)
    ini = emit_ini(allocs)
    if args.out:
        Path(args.out).write_text(ini)
        sys.stderr.write(f"wrote {len(allocs)} alloc section(s) -> {args.out}\n")
    else:
        sys.stdout.write(ini)
    if args.out_json:
        Path(args.out_json).write_text(emit_alloc_json(allocs))
        sys.stderr.write(f"wrote merged alloc JSON -> {args.out_json}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
