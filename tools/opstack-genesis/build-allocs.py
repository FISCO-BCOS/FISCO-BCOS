#!/usr/bin/env python3
# Copyright (c) FISCO-BCOS, Apache-2.0
"""Generate FISCO-BCOS genesis [alloc.N] INI fragments from forge artifacts.

L2 mode materializes every predeploy directly into genesis state: runtime
bytecode, no constructor runs on-chain. This tool reads the forge build
artifacts under <contracts>/out/<Sol>/<Name>.json plus a chain-config YAML
listing the predeploys, and emits the INI sections NodeConfig.loadAllocs()
parses.

Storage seeding is intentionally out of scope here:

  * SystemConfig (pr1) is a generic `mapping(string => Entry)` with no immutables
    and no fixed slots. Its `feature_flags` entry is written by the C++ genesis
    path (Ledger), which alone knows the version-gated feature set after
    Features::setGenesisFeatures(version); its other config entries (owner,
    chain_id, gas/version) are a follow-up PR.
  * The OP-fork predeploys ship deployed bytecode as-is; op-node writes their
    runtime state (Phase A).

If a predeploy artifact still carries immutableReferences (unfilled immutables),
the build aborts naming the contract, unless that predeploy opts in via
`allow_unpatched_immutables: true` (accepting zero-valued immutables, Phase A).
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


def build_alloc(artifact, address, name, allow_unpatched_immutables=False):
    """A predeploy emitted as deployed bytecode with empty storage.

    Genesis seeds no storage here (see module docstring). If the artifact still
    carries immutableReferences, those unfilled immutables would deploy broken
    runtime code (e.g. a FeeVault with RECIPIENT == address(0)); fail loud unless
    the predeploy explicitly opts out via allow_unpatched_immutables.
    """
    code, immutable_refs = deployed_bytecode(artifact)
    if immutable_refs and not allow_unpatched_immutables:
        ast_ids = ", ".join(str(k) for k in immutable_refs)
        raise ValueError(
            f"{name}: artifact has unpatched immutables (immutableReferences "
            f"AST ids {ast_ids}); unpatched immutables would deploy broken "
            f"genesis code; set allow_unpatched_immutables: true for this "
            f"predeploy to accept zero-valued immutables (Phase A deferral)")
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
        allow_unpatched = bool(predeploy.get("allow_unpatched_immutables", False))
        allocs.append(build_alloc(artifact, address, name, allow_unpatched))
    return allocs


def emit_ini(allocs):
    """Render alloc dicts as FISCO-BCOS genesis INI sections.

    Addresses are lowercased; storage slot keys (if any) are emitted in sorted
    order so the output is deterministic regardless of dict insertion order.
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
