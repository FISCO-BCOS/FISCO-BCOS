# Copyright (c) FISCO-BCOS, Apache-2.0
"""Unit tests for build-allocs.py. Uses synthetic forge artifacts in tmp_path;
no real `forge build` is required.

After the pr1 SystemConfig redesign (generic `mapping(string => Entry)`, no
immutables, no fixed slot0/1/2 layout), this tool no longer seeds SystemConfig
storage. Every predeploy — SystemConfig included — is emitted as deployed
bytecode with empty storage. The `feature_flags` entry is injected by the C++
genesis path (Ledger), and the remaining SystemConfig config KV entries are a
follow-up PR. So these tests assert SystemConfig is a plain alloc.
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


def _write_artifact(contracts_dir, sol_file, name, deployed_object, immutable_refs=None):
    out = Path(contracts_dir) / "out" / sol_file
    out.mkdir(parents=True, exist_ok=True)
    artifact = {"deployedBytecode": {"object": deployed_object}}
    if immutable_refs is not None:
        artifact["deployedBytecode"]["immutableReferences"] = immutable_refs
    (out / f"{name}.json").write_text(json.dumps(artifact))


def test_system_config_is_plain_alloc(tmp_path):
    # pr1 SystemConfig has no immutables and no fixed slots -> genesis tooling
    # emits bytecode only, empty storage; feature_flags is injected by C++.
    contracts = tmp_path / "contracts"
    _write_artifact(contracts, "SystemConfig.sol", "SystemConfig", "0x" + ("ab" * 32))
    config = {
        "predeploys": [
            {"name": "SystemConfig",
             "address": "0x42000000000000000000000000000000000000C0",
             "sol_file": "SystemConfig.sol"},
        ],
    }
    allocs = build_allocs.build_allocs(config, str(contracts))
    assert len(allocs) == 1
    assert allocs[0]["address"] == "0x42000000000000000000000000000000000000c0"
    assert allocs[0]["code"] == "0x" + ("ab" * 32)  # bytecode untouched
    assert allocs[0]["storage"] == {}               # no fixed-slot seeding


def test_end_to_end_ini_emission(tmp_path):
    contracts = tmp_path / "contracts"
    _write_artifact(contracts, "SystemConfig.sol", "SystemConfig", "0x" + ("ab" * 32))
    _write_artifact(contracts, "L1Block.sol", "L1Block", "0xDEADBEEF")
    config = {
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

    parser = configparser.ConfigParser()
    parser.read_string(ini_text)

    # SystemConfig: plain bytecode, lowercased address, NO storage section.
    assert parser["alloc.0"]["address"] == "0x42000000000000000000000000000000000000c0"
    assert parser["alloc.0"]["balance"] == "0"
    assert parser["alloc.0"]["nonce"] == "0"
    assert parser["alloc.0"]["code"] == "0x" + ("ab" * 32)
    assert not parser.has_section("alloc.0.storage")

    # L1Block: plain bytecode, no storage.
    assert parser["alloc.1"]["address"] == "0x4200000000000000000000000000000000000015"
    assert parser["alloc.1"]["code"] == "0xdeadbeef"
    assert not parser.has_section("alloc.1.storage")


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
    # (zero-valued immutables, Phase A deferral).
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
