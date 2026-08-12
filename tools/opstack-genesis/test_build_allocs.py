# Copyright (c) FISCO-BCOS, Apache-2.0
"""Unit tests for build-allocs.py. Uses synthetic forge artifacts in tmp_path;
no real `forge build` is required.

Covers the three-layer overlay for self-written proxied predeploys (proxy
account = Proxy bytecode + EIP-1967 slots + terminal contract storage;
implementation account = implementation bytecode with initializers disabled),
the SystemConfig packed-Entry seeding, the L2ValidatorSet state seeding, the
reserved-namespace guard, and the plain bytecode-only path the OP-fork
predeploys still use.
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
GOVERNANCE_OWNER = "0x000000000000000000000000000000000000d00d"


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
    _write_artifact(contracts, "Proxy.sol", "Proxy", "0x" + ("cd" * 16))
    return contracts


def _system_config_predeploy(**overrides):
    predeploy = {
        "name": "SystemConfig",
        "address": SYSTEM_CONFIG_ADDR,
        "sol_file": "SystemConfig.sol",
        "proxy": {"implementation": SYSTEM_CONFIG_IMPL},
        "system_config": {"chain_id": 901, "gas_limit": 30_000_000},
    }
    predeploy.update(overrides)
    return predeploy


def _config(*predeploys):
    return {
        "proxy_admin": PROXY_ADMIN,
        "governance_owner": GOVERNANCE_OWNER,
        "predeploys": list(predeploys),
    }


# --- keccak256 primitive -----------------------------------------------------

def test_keccak256_standard_vectors():
    # Ethereum keccak256, NOT NIST SHA3-256 — these two vectors tell them apart.
    assert build_allocs.keccak256(b"").hex() == (
        "c5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470")
    assert build_allocs.keccak256(b"abc").hex() == (
        "4e03657aea45a94fc7d47ba826c8d667c0d1e6e33a64a036ec44f58fa12d6c45")


# --- proxied self-written predeploy (three-layer overlay) -------------------

def test_proxied_predeploy_emits_proxy_and_implementation(tmp_path):
    contracts = _base_contracts(tmp_path)
    allocs = build_allocs.build_allocs(
        _config(_system_config_predeploy()), str(contracts))
    assert len(allocs) == 2

    proxy, impl = allocs
    # Proxy account: Proxy bytecode (NOT the implementation bytecode).
    assert proxy["address"] == SYSTEM_CONFIG_ADDR.lower()
    assert proxy["code"] == "0x" + ("cd" * 16)
    # Implementation account: implementation bytecode, initializers disabled.
    assert impl["address"] == SYSTEM_CONFIG_IMPL
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


def test_system_config_entry_slots_use_mapping_formula(tmp_path):
    contracts = _base_contracts(tmp_path)
    allocs = build_allocs.build_allocs(
        _config(_system_config_predeploy()), str(contracts))
    storage = allocs[0]["storage"]

    # entrySlot(key) = keccak256(utf8(key) || be32(101)); genesis entries have
    # enableNumber 0, so the packed word equals the bare value.
    for key, value in (("chain_id", 901), ("gas_limit", 30_000_000)):
        slot = int.from_bytes(build_allocs.keccak256(
            key.encode() + (101).to_bytes(32, "big")), "big")
        assert storage[slot] == value


def test_system_config_rejects_feature_flags(tmp_path):
    contracts = _base_contracts(tmp_path)
    predeploy = _system_config_predeploy(system_config={"feature_flags": 1})
    with pytest.raises(ValueError, match="feature_flags"):
        build_allocs.build_allocs(_config(predeploy), str(contracts))


def test_system_config_value_must_fit_uint192(tmp_path):
    contracts = _base_contracts(tmp_path)
    predeploy = _system_config_predeploy(system_config={"chain_id": 1 << 192})
    with pytest.raises(ValueError, match="uint192"):
        build_allocs.build_allocs(_config(predeploy), str(contracts))


def test_proxied_predeploy_requires_both_authorities(tmp_path):
    contracts = _base_contracts(tmp_path)
    config = _config(_system_config_predeploy())
    del config["governance_owner"]
    with pytest.raises(ValueError, match="governance_owner"):
        build_allocs.build_allocs(config, str(contracts))


def test_raw_storage_accepts_int_and_hex_string_values(tmp_path):
    # YAML parses unquoted 0x10 into int 16; the tool must take ints as-is
    # instead of re-reading their decimal digits as hex (16 -> 22).
    contracts = _base_contracts(tmp_path)
    predeploy = _system_config_predeploy(
        system_config={}, storage={16: 16, "0x20": "0x2a"})
    allocs = build_allocs.build_allocs(_config(predeploy), str(contracts))
    storage = allocs[0]["storage"]
    assert storage[16] == 16      # int slot/value used verbatim
    assert storage[0x20] == 0x2a  # hex strings parsed as hex


def test_empty_consensus_public_key_rejected(tmp_path):
    contracts = _base_contracts(tmp_path)
    _write_artifact(contracts, "L2ValidatorSet.sol", "L2ValidatorSet", "0xbeef")
    predeploy = {
        "name": "L2ValidatorSet",
        "address": VALIDATOR_SET_ADDR,
        "sol_file": "L2ValidatorSet.sol",
        "proxy": {"implementation": VALIDATOR_SET_IMPL},
        "validators": [{
            "address": "0x1111111111111111111111111111111111111111",
            "consensus_public_key": "0x",
        }],
    }
    with pytest.raises(ValueError, match="consensus_public_key is empty"):
        build_allocs.build_allocs(_config(predeploy), str(contracts))


def test_zero_governance_owner_rejected(tmp_path):
    contracts = _base_contracts(tmp_path)
    config = _config(_system_config_predeploy())
    config["governance_owner"] = "0x0000000000000000000000000000000000000000"
    with pytest.raises(ValueError, match="zero"):
        build_allocs.build_allocs(config, str(contracts))


def test_reserved_namespace_rejected(tmp_path):
    contracts = _base_contracts(tmp_path)
    # Proxy address inside the OP predeploy range 0x4200...0000-07FF.
    bad_proxy = _system_config_predeploy(
        address="0x42000000000000000000000000000000000000C0")
    with pytest.raises(ValueError, match="reserved namespace"):
        build_allocs.build_allocs(_config(bad_proxy), str(contracts))
    # Implementation inside the OP code namespace 0xc0d3...XXXX.
    bad_impl = _system_config_predeploy(
        proxy={"implementation": "0xc0d3c0d3c0d3c0d3c0d3c0d3c0d3c0d3c0d300c0"})
    with pytest.raises(ValueError, match="reserved namespace"):
        build_allocs.build_allocs(_config(bad_impl), str(contracts))


# --- L2ValidatorSet state seeding -------------------------------------------

def test_validator_storage_layout(tmp_path):
    contracts = _base_contracts(tmp_path)
    _write_artifact(contracts, "L2ValidatorSet.sol", "L2ValidatorSet", "0xbeef")
    validator_addr = "0x1111111111111111111111111111111111111111"
    fee_addr = "0x2222222222222222222222222222222222222222"
    long_key = "0x" + ("aa" * 64)  # 64-byte consensus key -> long bytes encoding
    predeploy = {
        "name": "L2ValidatorSet",
        "address": VALIDATOR_SET_ADDR,
        "sol_file": "L2ValidatorSet.sol",
        "proxy": {"implementation": VALIDATOR_SET_IMPL},
        "validators": [{
            "address": validator_addr,
            "consensus_public_key": long_key,
            "fee_address": fee_addr,
            "voting_power": 7,
        }],
    }
    allocs = build_allocs.build_allocs(_config(predeploy), str(contracts))
    storage = allocs[0]["storage"]
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
    validator_addr = "0x1111111111111111111111111111111111111111"
    predeploy = {
        "name": "L2ValidatorSet",
        "address": VALIDATOR_SET_ADDR,
        "sol_file": "L2ValidatorSet.sol",
        "proxy": {"implementation": VALIDATOR_SET_IMPL},
        "validators": [{
            "address": validator_addr,
            "consensus_public_key": "0x" + ("bb" * 20),  # 20 bytes < 32
        }],
    }
    allocs = build_allocs.build_allocs(_config(predeploy), str(contracts))
    storage = allocs[0]["storage"]
    record_base = int.from_bytes(build_allocs.keccak256(
        int(validator_addr, 16).to_bytes(32, "big") + (103).to_bytes(32, "big")), "big")
    # Short bytes: data left-aligned in the slot word, lowest byte = 2*len.
    expected = int.from_bytes(bytes.fromhex("bb" * 20).ljust(32, b"\0"), "big") | (2 * 20)
    assert storage[record_base + 1] == expected
    # Defaults: fee_address = validator address, voting_power = 1.
    assert storage[record_base] == (1 << 168) | int(validator_addr, 16)


# --- plain (OP-fork) predeploys and INI emission ----------------------------

def test_plain_predeploy_is_bytecode_only(tmp_path):
    contracts = tmp_path / "contracts"
    _write_artifact(contracts, "L1Block.sol", "L1Block", "0xDEADBEEF")
    config = {
        "predeploys": [
            {"name": "L1Block",
             "address": "0x4200000000000000000000000000000000000015",
             "sol_file": "L1Block.sol"},
        ],
    }
    allocs = build_allocs.build_allocs(config, str(contracts))
    assert len(allocs) == 1
    assert allocs[0]["code"] == "0xdeadbeef"
    assert allocs[0]["storage"] == {}


def test_end_to_end_ini_emission(tmp_path):
    contracts = _base_contracts(tmp_path)
    _write_artifact(contracts, "L1Block.sol", "L1Block", "0xDEADBEEF")
    config = _config(
        _system_config_predeploy(),
        {"name": "L1Block",
         "address": "0x4200000000000000000000000000000000000015",
         "sol_file": "L1Block.sol"},
    )
    allocs = build_allocs.build_allocs(config, str(contracts))
    ini_text = build_allocs.emit_ini(allocs)

    parser = configparser.ConfigParser()
    parser.read_string(ini_text)

    # Proxy account with a storage section; slots/values are full-width
    # 32-byte hex words (Ledger's unhex is fixed-width).
    assert parser["alloc.0"]["address"] == SYSTEM_CONFIG_ADDR.lower()
    assert parser.has_section("alloc.0.storage")
    owner_key = "0x" + format(build_allocs.OZ_OWNER_SLOT, "064x")
    owner_value = parser["alloc.0.storage"][owner_key]
    assert owner_value == "0x" + format(int(GOVERNANCE_OWNER, 16), "064x")
    assert len(owner_value) == 66
    # Implementation account follows its proxy.
    assert parser["alloc.1"]["address"] == SYSTEM_CONFIG_IMPL
    init_key = "0x" + format(build_allocs.OZ_INITIALIZED_SLOT, "064x")
    assert parser["alloc.1.storage"][init_key] == "0x" + format(255, "064x")
    # Plain OP predeploy: bytecode only, no storage section.
    assert parser["alloc.2"]["address"] == "0x4200000000000000000000000000000000000015"
    assert parser["alloc.2"]["code"] == "0xdeadbeef"
    assert not parser.has_section("alloc.2.storage")


def test_template_config_builds(tmp_path):
    # The checked-in template must stay loadable end-to-end: synthesize an
    # artifact for every predeploy it lists and run the full pipeline.
    template = Path(__file__).parent / "chain-config.template.yaml"
    import yaml
    config = yaml.safe_load(template.read_text())
    contracts = tmp_path / "contracts"
    _write_artifact(contracts, "Proxy.sol", "Proxy", "0x" + ("cd" * 16))
    for predeploy in config["predeploys"]:
        _write_artifact(contracts, predeploy.get("sol_file", f"{predeploy['name']}.sol"),
                        predeploy["name"], "0x" + ("ab" * 8))
    allocs = build_allocs.build_allocs(config, str(contracts))
    # 2 proxied self-written predeploys -> 4 allocs, + 11 plain OP predeploys.
    assert len(allocs) == 15
    build_allocs.emit_ini(allocs)


# --- immutables guard --------------------------------------------------------

def test_unpatched_immutables_fail_loud(tmp_path):
    # A predeploy whose artifact still carries immutables and no opt-out flag
    # must abort the build, naming the contract.
    contracts = tmp_path / "contracts"
    _write_artifact(contracts, "BaseFeeVault.sol", "BaseFeeVault", "0x" + ("00" * 64),
                    immutable_refs={"9976": [{"start": 0, "length": 32}]})
    config = {
        "predeploys": [
            {"name": "BaseFeeVault",
             "address": "0x4200000000000000000000000000000000000019",
             "sol_file": "BaseFeeVault.sol"},
        ],
    }
    with pytest.raises(ValueError, match="BaseFeeVault"):
        build_allocs.build_allocs(config, str(contracts))


def test_unpatched_immutables_opt_out_emits_as_is(tmp_path):
    # Same artifact + allow_unpatched_immutables: true -> bytecode emitted as-is
    # (zero-valued immutables, deliberate deferral).
    contracts = tmp_path / "contracts"
    deployed = "0x" + ("00" * 64)
    _write_artifact(contracts, "BaseFeeVault.sol", "BaseFeeVault", deployed,
                    immutable_refs={"9976": [{"start": 0, "length": 32}]})
    config = {
        "predeploys": [
            {"name": "BaseFeeVault",
             "address": "0x4200000000000000000000000000000000000019",
             "sol_file": "BaseFeeVault.sol",
             "allow_unpatched_immutables": True},
        ],
    }
    allocs = build_allocs.build_allocs(config, str(contracts))
    assert len(allocs) == 1
    # Bytecode passed through untouched (immutables left at zero placeholders).
    assert build_allocs.strip0x(allocs[0]["code"]) == "00" * 64
    assert allocs[0]["storage"] == {}
