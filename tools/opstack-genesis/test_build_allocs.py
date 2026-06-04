# Copyright (c) FISCO-BCOS, Apache-2.0
"""Unit tests for build-allocs.py. Uses synthetic forge artifacts in tmp_path;
no real `forge build` is required."""
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


def test_patch_immutables_lands_at_offsets():
    # 8-byte bytecode, two 32-byte windows would not fit; use a longer buffer.
    # Bytecode of 0x60 * 80 bytes; immutable window at byte offset 16, length 32.
    deployed = "0x" + ("60" * 80)
    immutable_refs = {"30": [{"start": 16, "length": 32}]}
    chain_id = 901
    patched = build_allocs.patch_immutables(deployed, immutable_refs, chain_id)

    raw = bytes.fromhex(build_allocs.strip0x(patched))
    assert len(raw) == 80  # length unchanged
    # The 32 bytes at offset 16 must equal big-endian chain_id.
    assert raw[16:48] == chain_id.to_bytes(32, "big")
    # Bytes outside the window are untouched.
    assert raw[:16] == bytes.fromhex("60" * 16)
    assert raw[48:] == bytes.fromhex("60" * 32)


def test_patch_immutables_multiple_windows():
    deployed = "0x" + ("00" * 128)
    immutable_refs = {"7": [{"start": 0, "length": 32}, {"start": 64, "length": 32}]}
    patched = build_allocs.patch_immutables(deployed, immutable_refs, 0xABCD)
    raw = bytes.fromhex(build_allocs.strip0x(patched))
    expected = (0xABCD).to_bytes(32, "big")
    assert raw[0:32] == expected
    assert raw[64:96] == expected
    # gap between windows stays zero
    assert raw[32:64] == bytes(32)


def test_pack_system_config_slot0():
    # gasLimit=0x1c9c380 (30_000_000) in low 8 bytes; version=0x030a0000 in next 4.
    gas_limit = 30_000_000
    version = 0x030A0000
    packed = build_allocs.pack_system_config_slot0(gas_limit, version)
    # Reconstruct: low 64 bits = gasLimit, bits 64..95 = version.
    assert packed & ((1 << 64) - 1) == gas_limit
    assert (packed >> 64) & ((1 << 32) - 1) == version
    # Exact 32-byte hex.
    expected_hex = "0x" + format((version << 64) | gas_limit, "064x")
    assert build_allocs.slot_hex(packed) == expected_hex


def test_pack_slot0_rejects_overflow():
    with pytest.raises(ValueError):
        build_allocs.pack_system_config_slot0(1 << 64, 0)
    with pytest.raises(ValueError):
        build_allocs.pack_system_config_slot0(0, 1 << 32)


def _write_artifact(contracts_dir, sol_file, name, deployed_object, immutable_refs=None):
    out = Path(contracts_dir) / "out" / sol_file
    out.mkdir(parents=True, exist_ok=True)
    artifact = {"deployedBytecode": {"object": deployed_object}}
    if immutable_refs is not None:
        artifact["deployedBytecode"]["immutableReferences"] = immutable_refs
    (out / f"{name}.json").write_text(json.dumps(artifact))


def test_end_to_end_ini_emission(tmp_path):
    contracts = tmp_path / "contracts"
    # SystemConfig: bytecode with an immutable window for chainId at offset 4.
    _write_artifact(contracts, "SystemConfig.sol", "SystemConfig",
                    "0x" + ("ab" * 64), immutable_refs={"30": [{"start": 4, "length": 32}]})
    # A plain predeploy: bytecode as-is, no storage.
    _write_artifact(contracts, "L1Block.sol", "L1Block", "0xDEADBEEF")

    config = {
        "chain_id": 901,
        "l2_block_gas_limit": 30_000_000,
        "compatibility_version": 0x030A0000,
        "feature_flags": 0,
        "system_config_owner": "0x00000000000000000000000000000000000000aB",
        "predeploys": [
            {"name": "SystemConfig",
             "address": "0x42000000000000000000000000000000000000C0",
             "sol_file": "SystemConfig.sol"},
            {"name": "L1Block",
             "address": "0x4200000000000000000000000000000000000015",
             "sol_file": "L1Block.sol"},
        ],
    }
    allocs = build_allocs.build_allocs(config, str(contracts))
    ini_text = build_allocs.emit_ini(allocs)

    # Parse back with configparser to assert well-formed sections.
    parser = configparser.ConfigParser()
    parser.read_string(ini_text)

    # SystemConfig section: address lowercased.
    assert parser["alloc.0"]["address"] == "0x42000000000000000000000000000000000000c0"
    assert parser["alloc.0"]["balance"] == "0"
    assert parser["alloc.0"]["nonce"] == "0"
    # chainId immutable spliced at byte offset 4.
    code = build_allocs.strip0x(parser["alloc.0"]["code"])
    assert bytes.fromhex(code)[4:36] == (901).to_bytes(32, "big")

    # SystemConfig storage: slot 0 packed value, slot 2 owner; slot 1 omitted (flags=0).
    storage0 = parser["alloc.0.storage"]
    packed = (0x030A0000 << 64) | 30_000_000
    slot0_key = "0x" + format(0, "064x")
    slot2_key = "0x" + format(2, "064x")
    assert storage0[slot0_key] == "0x" + format(packed, "064x")
    assert slot2_key in storage0
    assert storage0[slot2_key] == "0x" + format(0xAB, "064x")
    assert ("0x" + format(1, "064x")) not in storage0  # feature_flags=0 omitted

    # L1Block: plain bytecode, no storage section emitted.
    assert parser["alloc.1"]["address"] == "0x4200000000000000000000000000000000000015"
    assert parser["alloc.1"]["code"] == "0xdeadbeef"
    assert not parser.has_section("alloc.1.storage")


def test_unpatched_immutables_fail_loud(tmp_path):
    # A non-SystemConfig predeploy whose artifact still carries immutables and
    # no opt-out flag must abort the build, naming the contract.
    contracts = tmp_path / "contracts"
    _write_artifact(contracts, "BaseFeeVault.sol", "BaseFeeVault", "0x" + ("00" * 64),
                    immutable_refs={"9976": [{"start": 0, "length": 32}]})
    config = {
        "chain_id": 901,
        "l2_block_gas_limit": 30_000_000,
        "compatibility_version": 0x030A0000,
        "feature_flags": 0,
        "system_config_owner": "0x0000000000000000000000000000000000000000",
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
    # (zero-valued immutables, Phase A deferral).
    contracts = tmp_path / "contracts"
    deployed = "0x" + ("00" * 64)
    _write_artifact(contracts, "BaseFeeVault.sol", "BaseFeeVault", deployed,
                    immutable_refs={"9976": [{"start": 0, "length": 32}]})
    config = {
        "chain_id": 901,
        "l2_block_gas_limit": 30_000_000,
        "compatibility_version": 0x030A0000,
        "feature_flags": 0,
        "system_config_owner": "0x0000000000000000000000000000000000000000",
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


def test_storage_slots_sorted(tmp_path):
    contracts = tmp_path / "contracts"
    _write_artifact(contracts, "SystemConfig.sol", "SystemConfig", "0x00")
    config = {
        "chain_id": 1,
        "l2_block_gas_limit": 1,
        "compatibility_version": 1,
        "feature_flags": 7,  # non-zero -> slot 1 present
        "system_config_owner": "0x0000000000000000000000000000000000000001",
        "predeploys": [
            {"name": "SystemConfig",
             "address": "0x42000000000000000000000000000000000000C0",
             "sol_file": "SystemConfig.sol"},
        ],
    }
    allocs = build_allocs.build_allocs(config, str(contracts))
    ini_text = build_allocs.emit_ini(allocs)
    # Slot keys must appear in numeric (0,1,2) order.
    lines = [ln for ln in ini_text.splitlines() if ln.startswith("0x")]
    slot_ints = [int(build_allocs.strip0x(ln.split("=")[0]), 16) for ln in lines]
    assert slot_ints == sorted(slot_ints)
    assert slot_ints == [0, 1, 2]
