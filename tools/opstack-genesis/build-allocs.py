#!/usr/bin/env python3
# Copyright (c) FISCO-BCOS, Apache-2.0
"""Generate FISCO-BCOS genesis [alloc.N] INI fragments from forge artifacts.

The L2 mode (chain_mode = l2) materializes every predeploy directly into genesis
state: runtime bytecode + storage slots, no constructor ever runs on-chain. This
tool reads the forge build artifacts under <contracts>/out/<Sol>/<Name>.json plus
a chain-config YAML and emits the INI sections NodeConfig.loadAllocs() parses.

Two predeploy-specific concerns (see bcos-l2-contracts/README.md "Genesis
deployment notes"):

  1. SystemConfig.chainId is `immutable` -> it lives in the runtime bytecode at the
     byte offsets listed in deployedBytecode.immutableReferences, NOT in a storage
     slot. We splice the 32-byte big-endian chainId into each such window. Skipping
     this makes on-chain getChainConfig() return chainId == 0.

  2. SystemConfig storage is seeded from the YAML, respecting Solidity slot packing:
       slot 0 = l2BlockGasLimit (uint64, offset 0) | compatibilityVersion (uint32, offset 8)
       slot 1 = featureFlags (uint256)
       slot 2 = owner (address)
     "offset" is bytes from the least-significant end, so gasLimit occupies the low
     8 bytes and version the next 4.

All other (vendored OP fork) predeploys ship deployed bytecode as-is with empty
storage; op-node writes L1Block etc. at runtime (Phase A).
"""
import argparse
import json
import sys
from pathlib import Path

try:
    import yaml
except ImportError:  # pragma: no cover - dependency hint only
    sys.stderr.write("error: pyyaml required (pip install pyyaml)\n")
    raise

SYSTEM_CONFIG = "SystemConfig"

# Solidity slot packing for SystemConfig slot 0 (see storage-layout/SystemConfig.json).
_GAS_LIMIT_OFFSET_BITS = 0    # l2BlockGasLimit, byte offset 0
_VERSION_OFFSET_BITS = 8 * 8  # compatibilityVersion, byte offset 8 -> 64 bits


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


def patch_immutables(deployed_hex, immutable_references, value):
    """Splice a 32-byte big-endian `value` into `deployed_hex` at every window
    described by `immutable_references` (the forge artifact's
    deployedBytecode.immutableReferences: {astId: [{start, length}, ...]}).

    start/length are BYTE offsets into the bytecode; hex string indexing is 2x.
    Returns the patched 0x-prefixed hex string.
    """
    body = strip0x(deployed_hex)
    raw = bytearray.fromhex(body)
    value_bytes = int(value).to_bytes(32, byteorder="big")
    for windows in immutable_references.values():
        for window in windows:
            start = int(window["start"])
            length = int(window["length"])
            if length != 32:
                raise ValueError(
                    f"unexpected immutable window length {length} (expected 32)")
            if start + length > len(raw):
                raise ValueError(
                    f"immutable window [{start},{start + length}) exceeds bytecode "
                    f"length {len(raw)}")
            raw[start:start + length] = value_bytes
    return "0x" + raw.hex()


def pack_system_config_slot0(l2_block_gas_limit, compatibility_version):
    """Pack slot 0 = (compatibilityVersion << 64) | l2BlockGasLimit.

    gasLimit is uint64 (low 8 bytes), version is uint32 (next 4 bytes).
    """
    gas_limit = int(l2_block_gas_limit)
    version = int(compatibility_version)
    if not 0 <= gas_limit < (1 << 64):
        raise ValueError(f"l2_block_gas_limit {gas_limit} out of uint64 range")
    if not 0 <= version < (1 << 32):
        raise ValueError(f"compatibility_version {version} out of uint32 range")
    packed = (gas_limit << _GAS_LIMIT_OFFSET_BITS) | (version << _VERSION_OFFSET_BITS)
    return packed


def slot_hex(slot_int):
    """32-byte 0x-prefixed hex for a storage slot key or value."""
    return "0x" + format(int(slot_int), "064x")


def address_to_slot_value(address):
    """An address right-aligned into a 32-byte slot (uint160 -> 0x00..<20 bytes>)."""
    return slot_hex(int(strip0x(address), 16))


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


def build_system_config_alloc(artifact, address, config):
    """Build the SystemConfig alloc: patched bytecode + seeded storage."""
    code, immutable_refs = deployed_bytecode(artifact)
    if immutable_refs:
        code = patch_immutables(code, immutable_refs, int(config["chain_id"]))
    storage = {}
    storage[slot_hex(0)] = slot_hex(
        pack_system_config_slot0(config["l2_block_gas_limit"],
                                 config["compatibility_version"]))
    feature_flags = int(config.get("feature_flags", 0))
    if feature_flags != 0:  # default 0 omitted: an unset slot already reads 0
        storage[slot_hex(1)] = slot_hex(feature_flags)
    owner = config.get("system_config_owner")
    if owner:
        storage[slot_hex(2)] = address_to_slot_value(owner)
    return {
        "address": normalize_hex(address),
        "balance": 0,
        "nonce": 0,
        "code": normalize_hex(code),
        "storage": storage,
    }


def build_plain_alloc(artifact, address, name, allow_unpatched_immutables=False):
    """A predeploy with deployed bytecode as-is and empty storage (Phase A).

    Only SystemConfig has a patch rule (chainId via immutableReferences). Any
    other contract whose artifact still carries immutableReferences would ship
    its zero placeholders straight into genesis state, deploying broken runtime
    code (e.g. a FeeVault with RECIPIENT == address(0)). Fail loud unless the
    predeploy entry explicitly opts out via allow_unpatched_immutables.
    """
    code, immutable_refs = deployed_bytecode(artifact)
    if immutable_refs and not allow_unpatched_immutables:
        ast_ids = ", ".join(str(k) for k in immutable_refs)
        raise ValueError(
            f"{name}: artifact has unpatched immutables (immutableReferences "
            f"AST ids {ast_ids}); unpatched immutables would deploy broken "
            f"genesis code; set allow_unpatched_immutables: true for this "
            f"predeploy to accept zero-valued immutables (Phase A deferral), "
            f"or add a patch rule")
    return {
        "address": normalize_hex(address),
        "balance": 0,
        "nonce": 0,
        "code": normalize_hex(code),
        "storage": {},
    }


def build_allocs(config, contracts_dir):
    """Return a list of alloc dicts, one per predeploy in the YAML config."""
    allocs = []
    for predeploy in config["predeploys"]:
        name = predeploy["name"]
        address = predeploy["address"]
        sol_file = predeploy.get("sol_file", f"{name}.sol")
        artifact = load_artifact(contracts_dir, sol_file, name)
        if name == SYSTEM_CONFIG:
            allocs.append(build_system_config_alloc(artifact, address, config))
        else:
            allow_unpatched = bool(predeploy.get("allow_unpatched_immutables", False))
            allocs.append(
                build_plain_alloc(artifact, address, name, allow_unpatched))
    return allocs


def emit_ini(allocs):
    """Render alloc dicts as FISCO-BCOS genesis INI sections.

    Addresses are lowercased; storage slot keys are emitted in sorted order so the
    output is deterministic regardless of dict insertion order.
    """
    lines = []
    for index, alloc in enumerate(allocs):
        lines.append(f"[alloc.{index}]")
        lines.append(f"address={normalize_hex(alloc['address'])}")
        lines.append(f"balance={int(alloc.get('balance', 0))}")
        lines.append(f"nonce={int(alloc.get('nonce', 0))}")
        lines.append(f"code={normalize_hex(alloc['code'])}")
        storage = alloc.get("storage", {})
        if storage:
            lines.append(f"[alloc.{index}.storage]")
            for key in sorted(storage, key=lambda k: int(strip0x(k), 16)):
                lines.append(f"{normalize_hex(key)}={normalize_hex(storage[key])}")
    return "\n".join(lines) + "\n"


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Generate genesis [alloc.N] INI from forge artifacts + chain config")
    parser.add_argument("--config", required=True, help="chain-config YAML path")
    parser.add_argument("--contracts", required=True,
                        help="bcos-l2-contracts dir (containing out/)")
    parser.add_argument("--out", help="output INI path (default: stdout)")
    args = parser.parse_args(argv)

    with open(args.config) as handle:
        config = yaml.safe_load(handle)
    allocs = build_allocs(config, args.contracts)
    ini = emit_ini(allocs)
    if args.out:
        Path(args.out).write_text(ini)
        sys.stderr.write(f"wrote {len(allocs)} alloc section(s) -> {args.out}\n")
    else:
        sys.stdout.write(ini)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
