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

# ---------------------------------------------------------------------------
# keccak256 (pure python, no dependency).
#
# hashlib.sha3_256 is NIST SHA-3 (0x06 domain padding) and produces DIFFERENT
# digests from Ethereum's keccak256 (0x01 padding), so it cannot be used here.
# Imported by gen_eth_header_fixture.py to compute the eth-genesis-header hash.
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


import argparse
import json
import sys
from pathlib import Path


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

    # Deferred to the CLI path only: importing this module (for keccak256, e.g. from
    # mpt_state_root.py / gen_eth_header_fixture.py) must not require pyyaml.
    try:
        import yaml
    except ImportError:  # pragma: no cover - dependency hint only
        sys.stderr.write("error: pyyaml required (pip install pyyaml)\n")
        raise

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
