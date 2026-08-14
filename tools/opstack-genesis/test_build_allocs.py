# Copyright (c) FISCO-BCOS, Apache-2.0
"""Unit tests for build-allocs.py. Uses synthetic forge artifacts and a
synthetic op-deployer base-alloc fixture in tmp_path; no real `forge build`
or op-deployer run is required.

Covers the base+overlay contract (op-deployer terminal allocs are the ONLY
source of OP accounts; the tool adds the FISCO self-written proxied
predeploys and merges), the three-layer overlay itself (proxy account =
base-sourced Proxy bytecode + EIP-1967 slots + terminal contract storage;
implementation account = implementation bytecode with initializers
disabled), the SystemConfig packed-Entry seeding, the L2ValidatorSet state
seeding, the reserved-namespace / collision / checklist guards, and the
two-authority enforcement.
"""
import configparser
import importlib.util
import json
from pathlib import Path

import pytest

# build-allocs.py is not an importable module name (hyphen), so load it by path.
_SPEC = importlib.util.spec_from_file_location(
    "build_allocs", str(Path(__file__).parent / "build-allocs.py"))
build_allocs = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(build_allocs)

SYSTEM_CONFIG_ADDR = "0x43000000000000000000000000000000000000C0"
SYSTEM_CONFIG_IMPL = "0xc3d3c3d3c3d3c3d3c3d3c3d3c3d3c3d3c3d300c0"
VALIDATOR_SET_ADDR = "0x43000000000000000000000000000000000000C1"
VALIDATOR_SET_IMPL = "0xc3d3c3d3c3d3c3d3c3d3c3d3c3d3c3d3c3d300c1"
PROXY_ADMIN = "0x4200000000000000000000000000000000000018"
# ProxyAdmin.owner (OZ Ownable slot 0) as seeded in the synthetic base — the
# REAL upgrade authority, distinct from both the ProxyAdmin contract address
# and the governance owner.
PROXY_ADMIN_OWNER = "0x000000000000000000000000000000000000a11c"
GOVERNANCE_OWNER = "0x000000000000000000000000000000000000d00d"

L1BLOCK_PROXY = "0x4200000000000000000000000000000000000015"
L1BLOCK_IMPL = "0xc0d3c0d3c0d3c0d3c0d3c0d3c0d3c0d3c0d30015"
PREFUNDED_EOA = "0x1212121212121212121212121212121212121212"
PROXY_CODE = "0x" + ("cd" * 16)     # canonical Proxy bytecode (from the base)
L1BLOCK_IMPL_CODE = "0x60ff"
PROXY_ADMIN_CODE = "0xadad"


def _write_artifact(contracts_dir, sol_file, name, deployed_object, immutable_refs=None):
    out = Path(contracts_dir) / "out" / sol_file
    out.mkdir(parents=True, exist_ok=True)
    artifact = {"deployedBytecode": {"object": deployed_object}}
    if immutable_refs is not None:
        artifact["deployedBytecode"]["immutableReferences"] = immutable_refs
    (out / f"{name}.json").write_text(json.dumps(artifact))


def _base_contracts(tmp_path):
    contracts = tmp_path / "contracts"
    _write_artifact(contracts, "SystemConfig.sol", "SystemConfig", "0x" + ("ab" * 32))
    return contracts


def _synthetic_base():
    """A minimal op-deployer-shaped terminal alloc mapping (address -> account)."""
    impl_slot = build_allocs.EIP1967_IMPLEMENTATION_SLOT
    admin_slot = build_allocs.EIP1967_ADMIN_SLOT
    return {
        # a proxied OP predeploy: Proxy bytecode + EIP-1967 slots
        L1BLOCK_PROXY: {
            "balance": "0x0",
            "nonce": "0x0",
            "code": PROXY_CODE,
            "storage": {
                "0x" + format(impl_slot, "064x"): L1BLOCK_IMPL,
                "0x" + format(admin_slot, "064x"): PROXY_ADMIN,
                # an explicit zero write: must be dropped on load
                "0x" + format(7, "064x"): "0x0",
            },
        },
        # its implementation in the OP code namespace
        L1BLOCK_IMPL: {"balance": "0x0", "nonce": "0x0", "code": L1BLOCK_IMPL_CODE},
        # ProxyAdmin with its owner (OZ Ownable slot 0) seeded — the upgrade
        # authority the two-authority check compares against.
        PROXY_ADMIN: {
            "code": PROXY_ADMIN_CODE,
            "storage": {"0x" + format(0, "064x"): PROXY_ADMIN_OWNER},
        },
        # a prefunded EOA (no code)
        PREFUNDED_EOA: {"balance": "0xde0b6b3a7640000", "nonce": "0x1"},
    }


def _load_base(tmp_path, base=None, wrap=None):
    data = base if base is not None else _synthetic_base()
    if wrap:
        data = {wrap: data}
    path = tmp_path / "base-allocs.json"
    path.write_text(json.dumps(data))
    return build_allocs.load_base_allocs(str(path))


def _system_config_predeploy(**overrides):
    predeploy = {
        "name": "SystemConfig",
        "address": SYSTEM_CONFIG_ADDR,
        "sol_file": "SystemConfig.sol",
        "proxy": {"implementation": SYSTEM_CONFIG_IMPL},
        "system_config": {"chain_id": 901, "gas_limit": 30_000_000, "feature_flags": 0x2A},
    }
    predeploy.update(overrides)
    return predeploy


def _config(*predeploys, **top):
    config = {
        "proxy_admin": PROXY_ADMIN,
        "governance_owner": GOVERNANCE_OWNER,
        "proxy_code_source": L1BLOCK_PROXY,
        "predeploys": list(predeploys),
    }
    config.update(top)
    return config


# --- keccak256 primitive -----------------------------------------------------

def test_keccak256_standard_vectors():
    # Ethereum keccak256, NOT NIST SHA3-256 — these two vectors tell them apart.
    assert build_allocs.keccak256(b"").hex() == (
        "c5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470")
    assert build_allocs.keccak256(b"abc").hex() == (
        "4e03657aea45a94fc7d47ba826c8d667c0d1e6e33a64a036ec44f58fa12d6c45")


# --- base alloc loading ------------------------------------------------------

def test_base_allocs_load_and_normalize(tmp_path):
    base = _load_base(tmp_path)
    eoa = base[int(PREFUNDED_EOA, 16)]
    assert eoa["balance"] == 10**18
    assert eoa["nonce"] == 1
    assert eoa["code"] == ""
    l1block = base[int(L1BLOCK_PROXY, 16)]
    assert l1block["code"] == PROXY_CODE
    # EIP-1967 slots carried through; the explicit zero write is dropped.
    assert l1block["storage"][build_allocs.EIP1967_IMPLEMENTATION_SLOT] == int(
        L1BLOCK_IMPL, 16)
    assert 7 not in l1block["storage"]


def test_base_allocs_accepts_alloc_wrapper(tmp_path):
    base = _load_base(tmp_path, wrap="alloc")
    assert int(L1BLOCK_PROXY, 16) in base


def test_base_allocs_rejects_empty(tmp_path):
    with pytest.raises(ValueError, match="non-empty"):
        _load_base(tmp_path, base={})


def test_base_allocs_is_required_cli_arg(tmp_path):
    # The op-deployer base is not optional: main() must refuse to run without it.
    with pytest.raises(SystemExit):
        build_allocs.main(["--config", "x.yaml", "--contracts", str(tmp_path)])


# --- merge semantics (base + overlay) ---------------------------------------

def test_merge_preserves_base_and_adds_overlay(tmp_path):
    contracts = _base_contracts(tmp_path)
    base = _load_base(tmp_path)
    allocs = build_allocs.build_allocs(
        _config(_system_config_predeploy()), str(contracts), base)

    by_address = {alloc["address"]: alloc for alloc in allocs}
    # every base account survives untouched
    assert by_address[L1BLOCK_PROXY.lower()]["code"] == PROXY_CODE
    assert by_address[PROXY_ADMIN.lower()]["storage"][0] == int(PROXY_ADMIN_OWNER, 16)
    assert by_address[PREFUNDED_EOA]["balance"] == 10**18
    # overlay added proxy + implementation
    assert SYSTEM_CONFIG_ADDR.lower() in by_address
    assert SYSTEM_CONFIG_IMPL in by_address
    assert len(allocs) == len(base) + 2
    # deterministic ascending address order
    addresses = [int(alloc["address"], 16) for alloc in allocs]
    assert addresses == sorted(addresses)


def test_overlay_collision_with_base_fails(tmp_path):
    contracts = _base_contracts(tmp_path)
    # base already carries an account at the overlay proxy address
    base_data = _synthetic_base()
    base_data[SYSTEM_CONFIG_ADDR] = {"balance": "0x1"}
    base = _load_base(tmp_path, base=base_data)
    with pytest.raises(ValueError, match="collides"):
        build_allocs.build_allocs(
            _config(_system_config_predeploy()), str(contracts), base)


def test_op_predeploy_entries_are_rejected(tmp_path):
    # OP predeploys come from the base; a non-proxied predeploys entry is a
    # config error, not a bytecode-generation request.
    contracts = _base_contracts(tmp_path)
    base = _load_base(tmp_path)
    plain = {"name": "L1Block", "address": L1BLOCK_PROXY, "sol_file": "L1Block.sol"}
    with pytest.raises(ValueError, match="expected_predeploys"):
        build_allocs.build_allocs(_config(plain), str(contracts), base)


def test_expected_predeploys_checklist(tmp_path):
    contracts = _base_contracts(tmp_path)
    base = _load_base(tmp_path)
    config = _config(_system_config_predeploy(), expected_predeploys=[
        {"name": "L1Block", "address": L1BLOCK_PROXY},
    ])
    build_allocs.build_allocs(config, str(contracts), base)  # present -> fine

    config["expected_predeploys"].append(
        {"name": "OperatorFeeVault",
         "address": "0x420000000000000000000000000000000000001B"})
    with pytest.raises(ValueError, match="OperatorFeeVault"):
        build_allocs.build_allocs(config, str(contracts), base)


# --- proxied self-written predeploy (three-layer overlay) -------------------

def test_proxied_predeploy_emits_proxy_and_implementation(tmp_path):
    contracts = _base_contracts(tmp_path)
    base = _load_base(tmp_path)
    allocs = build_allocs.build_allocs(
        _config(_system_config_predeploy()), str(contracts), base)
    by_address = {alloc["address"]: alloc for alloc in allocs}

    proxy = by_address[SYSTEM_CONFIG_ADDR.lower()]
    impl = by_address[SYSTEM_CONFIG_IMPL]
    # Proxy account: the Proxy bytecode copied from the base proxy_code_source
    # (byte-identical to the op-deployer's), NOT the implementation bytecode.
    assert proxy["code"] == PROXY_CODE
    # Implementation account: implementation bytecode, initializers disabled.
    assert impl["code"] == "0x" + ("ab" * 32)
    assert impl["storage"] == {build_allocs.OZ_INITIALIZED_SLOT:
                               build_allocs.INITIALIZED_DISABLED}

    storage = proxy["storage"]
    # EIP-1967 admin (ProxyAdmin) and implementation slots.
    assert storage[build_allocs.EIP1967_ADMIN_SLOT] == int(PROXY_ADMIN, 16)
    assert storage[build_allocs.EIP1967_IMPLEMENTATION_SLOT] == int(
        SYSTEM_CONFIG_IMPL, 16)
    # Two-authority split: Ownable.owner (slot 51) is the governance entity,
    # not the ProxyAdmin behind the EIP-1967 admin slot.
    assert storage[build_allocs.OZ_OWNER_SLOT] == int(GOVERNANCE_OWNER, 16)
    assert storage[build_allocs.OZ_OWNER_SLOT] != storage[build_allocs.EIP1967_ADMIN_SLOT]
    # Proxy storage is the terminal post-initialize state.
    assert storage[build_allocs.OZ_INITIALIZED_SLOT] == build_allocs.INITIALIZED_RAN


def test_missing_proxy_code_source_fails(tmp_path):
    contracts = _base_contracts(tmp_path)
    base = _load_base(tmp_path)
    config = _config(_system_config_predeploy())
    del config["proxy_code_source"]
    with pytest.raises(ValueError, match="proxy_code_source"):
        build_allocs.build_allocs(config, str(contracts), base)


def test_codeless_proxy_code_source_fails(tmp_path):
    contracts = _base_contracts(tmp_path)
    base = _load_base(tmp_path)
    config = _config(_system_config_predeploy(), proxy_code_source=PREFUNDED_EOA)
    with pytest.raises(ValueError, match="code-less"):
        build_allocs.build_allocs(config, str(contracts), base)


def test_system_config_entry_slots_use_mapping_formula(tmp_path):
    contracts = _base_contracts(tmp_path)
    base = _load_base(tmp_path)
    allocs = build_allocs.build_allocs(
        _config(_system_config_predeploy()), str(contracts), base)
    storage = {a["address"]: a for a in allocs}[SYSTEM_CONFIG_ADDR.lower()]["storage"]

    # entrySlot(key) = keccak256(utf8(key) || be32(101)); genesis entries have
    # enableNumber 0, so the packed word equals the bare value.
    for key, value in (("chain_id", 901), ("gas_limit", 30_000_000),
                       ("feature_flags", 0x2A)):
        slot = int.from_bytes(build_allocs.keccak256(
            key.encode() + (101).to_bytes(32, "big")), "big")
        assert storage[slot] == value


def test_missing_feature_flags_rejected(tmp_path):
    # The C++ genesis path VERIFIES (no longer injects) the feature_flags
    # Entry slot, so the SystemConfig predeploy alloc must carry it — the
    # genesis state root would otherwise not commit the feature set.
    contracts = _base_contracts(tmp_path)
    base = _load_base(tmp_path)
    predeploy = _system_config_predeploy(
        system_config={"chain_id": 901, "gas_limit": 30_000_000})
    with pytest.raises(ValueError, match="feature_flags"):
        build_allocs.build_allocs(_config(predeploy), str(contracts), base)


def test_system_config_value_bounds(tmp_path):
    contracts = _base_contracts(tmp_path)
    base = _load_base(tmp_path)
    oversized = _system_config_predeploy(
        system_config={"chain_id": 1 << 192, "feature_flags": 1})
    with pytest.raises(ValueError, match="uint192"):
        build_allocs.build_allocs(_config(oversized), str(contracts), base)
    # a zero-valued entry writes a zero slot -> unrepresentable in the trie,
    # the loader would see the key as missing; must fail loud.
    zero_valued = _system_config_predeploy(
        system_config={"gas_limit": 0, "feature_flags": 1})
    with pytest.raises(ValueError, match="positive uint192"):
        build_allocs.build_allocs(_config(zero_valued), str(contracts), base)


def test_raw_storage_accepts_int_and_hex_string_values(tmp_path):
    # YAML parses unquoted 0x10 into int 16; the tool must take ints as-is
    # instead of re-reading their decimal digits as hex (16 -> 22).
    contracts = _base_contracts(tmp_path)
    base = _load_base(tmp_path)
    predeploy = _system_config_predeploy(
        system_config={"feature_flags": 1}, storage={16: 16, "0x20": "0x2a"})
    allocs = build_allocs.build_allocs(_config(predeploy), str(contracts), base)
    storage = {a["address"]: a for a in allocs}[SYSTEM_CONFIG_ADDR.lower()]["storage"]
    assert storage[16] == 16      # int slot/value used verbatim
    assert storage[0x20] == 0x2a  # hex strings parsed as hex


# --- authority guards (two-authority split) ---------------------------------

def test_zero_governance_owner_rejected(tmp_path):
    contracts = _base_contracts(tmp_path)
    base = _load_base(tmp_path)
    config = _config(_system_config_predeploy())
    config["governance_owner"] = "0x0000000000000000000000000000000000000000"
    with pytest.raises(ValueError, match="zero"):
        build_allocs.build_allocs(config, str(contracts), base)


def test_placeholder_addresses_rejected(tmp_path):
    # The template's 0x...dEaD placeholder is not the zero address, so it
    # slips past a non-zero check — but it has no known private key either;
    # shipping it locks governance forever. Reject it explicitly, everywhere
    # an authority or identity address is consumed.
    contracts = _base_contracts(tmp_path)
    base = _load_base(tmp_path)
    placeholder = "0x000000000000000000000000000000000000dEaD"

    config = _config(_system_config_predeploy())
    config["governance_owner"] = placeholder
    with pytest.raises(ValueError, match="placeholder"):
        build_allocs.build_allocs(config, str(contracts), base)

    config = _config(_system_config_predeploy())
    config["proxy_admin"] = placeholder
    with pytest.raises(ValueError, match="placeholder"):
        build_allocs.build_allocs(config, str(contracts), base)

    _write_artifact(contracts, "L2ValidatorSet.sol", "L2ValidatorSet", "0xbeef")
    validators = [{"address": placeholder, "consensus_public_key": "0x" + "bb" * 20}]
    with pytest.raises(ValueError, match="placeholder"):
        build_allocs.build_allocs(
            _config(_validator_set_predeploy(validators)), str(contracts), base)


def test_zero_validator_address_rejected(tmp_path):
    # Mirrors L2ValidatorSet._add's `require(a != address(0))`.
    contracts = _base_contracts(tmp_path)
    _write_artifact(contracts, "L2ValidatorSet.sol", "L2ValidatorSet", "0xbeef")
    base = _load_base(tmp_path)
    validators = [{"address": "0x" + "00" * 20, "consensus_public_key": "0x" + "bb" * 20}]
    with pytest.raises(ValueError, match="L2ValidatorSet.*zero address"):
        build_allocs.build_allocs(
            _config(_validator_set_predeploy(validators)), str(contracts), base)


def test_quoted_hex_config_values_parse_as_hex(tmp_path):
    # A quoted YAML value like "0x03120000" arrives as a string; it must parse
    # as hex, not crash int() or be re-read as decimal.
    contracts = _base_contracts(tmp_path)
    base = _load_base(tmp_path)
    predeploy = _system_config_predeploy(
        system_config={"compatibility_version": "0x03120000", "feature_flags": 1})
    allocs = build_allocs.build_allocs(_config(predeploy), str(contracts), base)
    storage = {a["address"]: a for a in allocs}[SYSTEM_CONFIG_ADDR.lower()]["storage"]
    slot = int.from_bytes(build_allocs.keccak256(
        b"compatibility_version" + (101).to_bytes(32, "big")), "big")
    assert storage[slot] == 0x03120000


def test_quoted_decimal_config_value_rejected(tmp_path):
    # A quoted decimal ("100") would silently re-read as hex 256 under a lax
    # parser; config values are consensus parameters, so it must fail loud.
    contracts = _base_contracts(tmp_path)
    base = _load_base(tmp_path)
    predeploy = _system_config_predeploy(
        system_config={"gas_limit": "100", "feature_flags": 1})
    with pytest.raises(ValueError, match="0x-prefixed"):
        build_allocs.build_allocs(_config(predeploy), str(contracts), base)


def test_placeholder_fee_address_rejected(tmp_path):
    contracts = _base_contracts(tmp_path)
    _write_artifact(contracts, "L2ValidatorSet.sol", "L2ValidatorSet", "0xbeef")
    base = _load_base(tmp_path)
    validators = [{
        "address": "0x1111111111111111111111111111111111111111",
        "fee_address": "0x000000000000000000000000000000000000dEaD",
        "consensus_public_key": "0x" + "bb" * 20,
    }]
    with pytest.raises(ValueError, match="fee_address"):
        build_allocs.build_allocs(
            _config(_validator_set_predeploy(validators)), str(contracts), base)


def test_same_admin_and_owner_rejected(tmp_path):
    # The module contract says the two authorities are never the same role;
    # the tool must enforce it, not just document it.
    contracts = _base_contracts(tmp_path)
    base = _load_base(tmp_path)
    config = _config(_system_config_predeploy())
    config["governance_owner"] = PROXY_ADMIN
    with pytest.raises(ValueError, match="different"):
        build_allocs.build_allocs(config, str(contracts), base)


def test_governance_owner_equal_to_proxy_admin_owner_rejected(tmp_path):
    # The REAL upgrade authority is ProxyAdmin.owner (base alloc, OZ Ownable
    # slot 0) — pointing governance at that entity collapses the two
    # authorities even though the addresses of the CONTRACTS differ.
    contracts = _base_contracts(tmp_path)
    base = _load_base(tmp_path)
    config = _config(_system_config_predeploy())
    config["governance_owner"] = PROXY_ADMIN_OWNER
    with pytest.raises(ValueError, match="ProxyAdmin.owner"):
        build_allocs.build_allocs(config, str(contracts), base)


def test_burned_proxy_admin_owner_rejected(tmp_path):
    # ProxyAdmin.owner zero/absent in the base = the upgrade authority is
    # burned; minting such a chain deserves its own loud error.
    contracts = _base_contracts(tmp_path)
    base_data = _synthetic_base()
    del base_data[PROXY_ADMIN]["storage"]
    base = _load_base(tmp_path, base=base_data)
    with pytest.raises(ValueError, match="burned"):
        build_allocs.build_allocs(
            _config(_system_config_predeploy()), str(contracts), base)


def test_missing_proxy_admin_account_rejected(tmp_path):
    contracts = _base_contracts(tmp_path)
    base_data = _synthetic_base()
    del base_data[PROXY_ADMIN]
    base = _load_base(tmp_path, base=base_data)
    with pytest.raises(ValueError, match="proxy_admin.*missing"):
        build_allocs.build_allocs(
            _config(_system_config_predeploy()), str(contracts), base)


def test_base_provenance_sha256(tmp_path):
    path = tmp_path / "base.json"
    path.write_text(json.dumps(_synthetic_base()))
    import hashlib
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    # pinned and matching -> passes and returns the digest
    assert build_allocs.verify_base_provenance(str(path), digest) == digest
    # pinned and mismatching -> the file is not the frozen artifact
    with pytest.raises(ValueError, match="sha256 mismatch"):
        build_allocs.verify_base_provenance(str(path), "ab" * 32)
    # unpinned (pre-freeze) -> warns but passes
    assert build_allocs.verify_base_provenance(str(path), "") == digest


def test_reserved_namespace_rejected(tmp_path):
    contracts = _base_contracts(tmp_path)
    base = _load_base(tmp_path)
    # Proxy address inside the OP predeploy range 0x4200...0000-07FF.
    bad_proxy = _system_config_predeploy(
        address="0x42000000000000000000000000000000000000C0")
    with pytest.raises(ValueError, match="reserved namespace"):
        build_allocs.build_allocs(_config(bad_proxy), str(contracts), base)
    # Implementation inside the OP code namespace 0xc0d3...XXXX.
    bad_impl = _system_config_predeploy(
        proxy={"implementation": "0xc0d3c0d3c0d3c0d3c0d3c0d3c0d3c0d3c0d300c0"})
    with pytest.raises(ValueError, match="reserved namespace"):
        build_allocs.build_allocs(_config(bad_impl), str(contracts), base)


# --- L2ValidatorSet state seeding -------------------------------------------

def _validator_set_predeploy(validators):
    return {
        "name": "L2ValidatorSet",
        "address": VALIDATOR_SET_ADDR,
        "sol_file": "L2ValidatorSet.sol",
        "proxy": {"implementation": VALIDATOR_SET_IMPL},
        "validators": validators,
    }


def test_validator_storage_layout(tmp_path):
    contracts = _base_contracts(tmp_path)
    _write_artifact(contracts, "L2ValidatorSet.sol", "L2ValidatorSet", "0xbeef")
    base = _load_base(tmp_path)
    validator_addr = "0x1111111111111111111111111111111111111111"
    fee_addr = "0x2222222222222222222222222222222222222222"
    long_key = "0x" + ("aa" * 64)  # 64-byte consensus key -> long bytes encoding
    predeploy = _validator_set_predeploy([{
        "address": validator_addr,
        "consensus_public_key": long_key,
        "fee_address": fee_addr,
        "voting_power": 7,
    }])
    allocs = build_allocs.build_allocs(_config(predeploy), str(contracts), base)
    storage = {a["address"]: a for a in allocs}[VALIDATOR_SET_ADDR.lower()]["storage"]
    keccak = build_allocs.keccak256

    # EnumerableSet: length at slot 101, element at keccak(101), index+1 in the
    # slot-102 mapping.
    assert storage[101] == 1
    values_base = int.from_bytes(keccak((101).to_bytes(32, "big")), "big")
    assert storage[values_base] == int(validator_addr, 16)
    index_slot = int.from_bytes(keccak(
        int(validator_addr, 16).to_bytes(32, "big") + (102).to_bytes(32, "big")), "big")
    assert storage[index_slot] == 1

    # Validator record: packed (feeAddress | jailed=0 | votingPower) then the
    # dynamic consensusPublicKey; incoming (base+2) stays zero and is omitted.
    record_base = int.from_bytes(keccak(
        int(validator_addr, 16).to_bytes(32, "big") + (103).to_bytes(32, "big")), "big")
    assert storage[record_base] == (7 << 168) | int(fee_addr, 16)
    # 64-byte bytes value: slot holds 2*len+1, data at keccak(slot).
    assert storage[record_base + 1] == 2 * 64 + 1
    data_base = int.from_bytes(keccak((record_base + 1).to_bytes(32, "big")), "big")
    assert storage[data_base] == int("aa" * 32, 16)
    assert storage[data_base + 1] == int("aa" * 32, 16)
    assert record_base + 2 not in storage


def test_short_consensus_key_inline_encoding(tmp_path):
    contracts = _base_contracts(tmp_path)
    _write_artifact(contracts, "L2ValidatorSet.sol", "L2ValidatorSet", "0xbeef")
    base = _load_base(tmp_path)
    validator_addr = "0x1111111111111111111111111111111111111111"
    predeploy = _validator_set_predeploy([{
        "address": validator_addr,
        "consensus_public_key": "0x" + ("bb" * 20),  # 20 bytes < 32
    }])
    allocs = build_allocs.build_allocs(_config(predeploy), str(contracts), base)
    storage = {a["address"]: a for a in allocs}[VALIDATOR_SET_ADDR.lower()]["storage"]
    record_base = int.from_bytes(build_allocs.keccak256(
        int(validator_addr, 16).to_bytes(32, "big") + (103).to_bytes(32, "big")), "big")
    # Short bytes: data left-aligned in the slot word, lowest byte = 2*len.
    expected = int.from_bytes(bytes.fromhex("bb" * 20).ljust(32, b"\0"), "big") | (2 * 20)
    assert storage[record_base + 1] == expected
    # Defaults: fee_address = validator address, voting_power = 1.
    assert storage[record_base] == (1 << 168) | int(validator_addr, 16)


def test_empty_consensus_public_key_rejected(tmp_path):
    contracts = _base_contracts(tmp_path)
    _write_artifact(contracts, "L2ValidatorSet.sol", "L2ValidatorSet", "0xbeef")
    base = _load_base(tmp_path)
    predeploy = _validator_set_predeploy([{
        "address": "0x1111111111111111111111111111111111111111",
        "consensus_public_key": "0x",
    }])
    with pytest.raises(ValueError, match="consensus_public_key is empty"):
        build_allocs.build_allocs(_config(predeploy), str(contracts), base)


# --- emission ----------------------------------------------------------------

def test_end_to_end_ini_emission(tmp_path):
    contracts = _base_contracts(tmp_path)
    base = _load_base(tmp_path)
    allocs = build_allocs.build_allocs(
        _config(_system_config_predeploy()), str(contracts), base)
    ini_text = build_allocs.emit_ini(allocs)

    parser = configparser.ConfigParser()
    parser.read_string(ini_text)

    sections = {parser[s]["address"]: s for s in parser.sections()
                if not s.endswith(".storage")}
    # Base proxied predeploy carried through with its storage.
    l1block = sections[L1BLOCK_PROXY.lower()]
    impl_key = "0x" + format(build_allocs.EIP1967_IMPLEMENTATION_SLOT, "064x")
    assert parser[f"{l1block}.storage"][impl_key] == \
        "0x" + format(int(L1BLOCK_IMPL, 16), "064x")
    # Prefunded EOA: balance decimal, NO code line (NodeConfig EOA alloc shape).
    eoa = sections[PREFUNDED_EOA]
    assert parser[eoa]["balance"] == str(10**18)
    assert parser[eoa]["nonce"] == "1"
    assert "code" not in parser[eoa]
    # Overlay proxy: storage slots/values full-width 32-byte hex words.
    proxy = sections[SYSTEM_CONFIG_ADDR.lower()]
    owner_key = "0x" + format(build_allocs.OZ_OWNER_SLOT, "064x")
    owner_value = parser[f"{proxy}.storage"][owner_key]
    assert owner_value == "0x" + format(int(GOVERNANCE_OWNER, 16), "064x")
    assert len(owner_value) == 66
    # Overlay implementation with initializers disabled.
    impl = sections[SYSTEM_CONFIG_IMPL]
    init_key = "0x" + format(build_allocs.OZ_INITIALIZED_SLOT, "064x")
    assert parser[f"{impl}.storage"][init_key] == "0x" + format(255, "064x")


def test_alloc_json_emission_matches_geth_shape(tmp_path):
    contracts = _base_contracts(tmp_path)
    base = _load_base(tmp_path)
    allocs = build_allocs.build_allocs(
        _config(_system_config_predeploy()), str(contracts), base)
    data = json.loads(build_allocs.emit_alloc_json(allocs))

    eoa = data[PREFUNDED_EOA]
    assert eoa["balance"] == hex(10**18)
    assert eoa["nonce"] == "0x1"
    assert "code" not in eoa
    proxy = data[SYSTEM_CONFIG_ADDR.lower()]
    assert proxy["code"] == PROXY_CODE
    owner_key = "0x" + format(build_allocs.OZ_OWNER_SLOT, "064x")
    assert proxy["storage"][owner_key] == \
        "0x" + format(int(GOVERNANCE_OWNER, 16), "064x")


def _template_pipeline(tmp_path):
    """Load the checked-in template + synthetic artifacts/base for it."""
    template = Path(__file__).parent / "chain-config.template.yaml"
    import yaml
    config = yaml.safe_load(template.read_text())
    contracts = tmp_path / "contracts"
    for predeploy in config["predeploys"]:
        _write_artifact(contracts, predeploy.get("sol_file", f"{predeploy['name']}.sol"),
                        predeploy["name"], "0x" + ("ab" * 8))
    base = {expected["address"]: {"code": "0x60ff"}
            for expected in config["expected_predeploys"]}
    base[config["proxy_code_source"]] = {"code": PROXY_CODE}
    # seed ProxyAdmin.owner (slot 0): the two-authority check reads the real
    # upgrade authority from the base allocs
    base[config["proxy_admin"]]["storage"] = {
        "0x" + format(0, "064x"): PROXY_ADMIN_OWNER}
    return config, contracts, _load_base(tmp_path, base=base)


def test_template_config_builds_after_placeholder_replaced(tmp_path):
    # The checked-in template must run end-to-end ONCE the operator replaced
    # the governance placeholder with a real address.
    config, contracts, base_accounts = _template_pipeline(tmp_path)
    config["governance_owner"] = GOVERNANCE_OWNER
    allocs = build_allocs.build_allocs(config, str(contracts), base_accounts)
    # every base account + 2 accounts per self-written proxied predeploy
    assert len(allocs) == len(base_accounts) + 2 * len(config["predeploys"])
    build_allocs.emit_ini(allocs)


def test_template_config_verbatim_is_rejected(tmp_path):
    # Running the template VERBATIM must fail: its governance_owner is the
    # 0x...dEaD placeholder, and accepting it would mint a chain whose
    # governance is locked forever. This test pins the placeholder rejection
    # to the actual template content.
    config, contracts, base_accounts = _template_pipeline(tmp_path)
    with pytest.raises(ValueError, match="placeholder"):
        build_allocs.build_allocs(config, str(contracts), base_accounts)


# --- immutables guard --------------------------------------------------------

def test_unpatched_immutables_fail_loud(tmp_path):
    # A self-written implementation whose artifact still carries immutables
    # must abort the build, naming the contract.
    contracts = tmp_path / "contracts"
    _write_artifact(contracts, "SystemConfig.sol", "SystemConfig", "0x" + ("00" * 64),
                    immutable_refs={"9976": [{"start": 0, "length": 32}]})
    base = _load_base(tmp_path)
    with pytest.raises(ValueError, match="SystemConfig"):
        build_allocs.build_allocs(
            _config(_system_config_predeploy()), str(contracts), base)
