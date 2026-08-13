#!/usr/bin/env python3
# Copyright (c) FISCO-BCOS, Apache-2.0
"""Generate an op-node rollup.json for the FISCO-BCOS post-Karst L2 (K0).

The chain has no fork history: every fork time through Karst is 0 (activated at
genesis). The emitted config carries every field op-node v1.19.3's
rollup.Config.Check() validates plus a non-empty chain_op_config, so a freshly
generated file passes startup validation without relying on the embedded
superchain registry (this chain is not registered there).

Field names follow op-node v1.19.3 op-node/rollup/types.go JSON tags
(snake_case top level) and op-service/eth SystemConfig JSON tags (camelCase
inside genesis.system_config).

Usage (all hashes/times come from the running L1/L2 nodes):

    python3 gen_rollup_config.py \
        --l1-chain-id 900 --l2-chain-id 901 \
        --l1-genesis-hash 0x<anvil block 0 hash> \
        --l2-genesis-hash 0x<FISCO eth_getBlockByNumber(0) hash> \
        --l2-genesis-time <FISCO genesis timestamp, Unix seconds> \
        --out rollup.json
"""
import argparse
import json
import sys

ZERO_ADDRESS = "0x" + "00" * 20
ZERO_HASH = "0x" + "00" * 32
# Ecotone-style scalar encoding: version byte 0x01 in the most significant byte,
# baseFeeScalar/blobBaseFeeScalar zero. Non-zero (Check requires it) and decodable.
DEFAULT_SCALAR = "0x01" + "00" * 31

# All OP fork activation times, oldest first. Post-Karst genesis => all zero.
FORK_TIME_KEYS = [
    "regolith_time",
    "canyon_time",
    "delta_time",
    "ecotone_time",
    "fjord_time",
    "granite_time",
    "holocene_time",
    "isthmus_time",
    "jovian_time",
    "karst_time",
]


class RollupConfigError(ValueError):
    """A generated config would fail op-node rollup.Config.Check()."""


def _require_hex(value, nbytes, what):
    text = str(value).strip().lower()
    if not text.startswith("0x"):
        text = "0x" + text
    body = text[2:]
    if len(body) != nbytes * 2 or any(c not in "0123456789abcdef" for c in body):
        raise RollupConfigError(f"{what}: expected 0x-prefixed {nbytes}-byte hex, got {value!r}")
    return text


def build_rollup_config(args):
    """Assemble the rollup.json dict from parsed CLI args."""
    config = {
        "genesis": {
            "l1": {
                "hash": _require_hex(args.l1_genesis_hash, 32, "l1 genesis hash"),
                "number": args.l1_genesis_number,
            },
            "l2": {
                "hash": _require_hex(args.l2_genesis_hash, 32, "l2 genesis hash"),
                "number": 0,
            },
            "l2_time": args.l2_genesis_time,
            "system_config": {
                "batcherAddr": _require_hex(args.batcher_addr, 20, "batcherAddr"),
                "overhead": _require_hex(args.overhead, 32, "overhead"),
                "scalar": _require_hex(args.scalar, 32, "scalar"),
                "gasLimit": args.gas_limit,
            },
        },
        "block_time": args.block_time,
        "max_sequencer_drift": args.max_sequencer_drift,
        "seq_window_size": args.seq_window_size,
        "channel_timeout": args.channel_timeout,
        "l1_chain_id": args.l1_chain_id,
        "l2_chain_id": args.l2_chain_id,
        **{key: 0 for key in FORK_TIME_KEYS},
        "batch_inbox_address": _require_hex(
            args.batch_inbox_address, 20, "batch_inbox_address"),
        "deposit_contract_address": _require_hex(
            args.deposit_contract_address, 20, "deposit_contract_address"),
        "l1_system_config_address": _require_hex(
            args.l1_system_config_address, 20, "l1_system_config_address"),
        "protocol_versions_address": _require_hex(
            args.protocol_versions_address, 20, "protocol_versions_address"),
        "chain_op_config": {
            "eip1559Elasticity": args.eip1559_elasticity,
            "eip1559Denominator": args.eip1559_denominator,
            "eip1559DenominatorCanyon": args.eip1559_denominator_canyon,
        },
    }
    return config


def check(config):
    """Mirror op-node v1.19.3 rollup.Config.Check() so a bad config fails here,
    not at op-node startup. Raises RollupConfigError on the first violation."""
    if config["block_time"] <= 0:
        raise RollupConfigError("block_time must be > 0")
    if config["seq_window_size"] < 2:
        raise RollupConfigError("seq_window_size must be >= 2")
    if config["max_sequencer_drift"] <= 0:
        raise RollupConfigError("max_sequencer_drift must be > 0")
    if config["channel_timeout"] <= 0:
        raise RollupConfigError("channel_timeout must be > 0")

    genesis = config["genesis"]
    if genesis["l1"]["hash"] == ZERO_HASH:
        raise RollupConfigError("genesis.l1.hash must not be zero")
    if genesis["l2"]["hash"] == ZERO_HASH:
        raise RollupConfigError("genesis.l2.hash must not be zero")
    if genesis["l1"]["hash"] == genesis["l2"]["hash"]:
        raise RollupConfigError("l1 and l2 genesis hashes must differ")
    if genesis["l2_time"] <= 0:
        raise RollupConfigError("genesis.l2_time must be > 0")

    system_config = genesis["system_config"]
    if system_config["batcherAddr"] == ZERO_ADDRESS:
        raise RollupConfigError("system_config.batcherAddr must not be zero")
    if int(system_config["scalar"], 16) == 0:
        raise RollupConfigError("system_config.scalar must not be zero")
    if system_config["gasLimit"] <= 0:
        raise RollupConfigError("system_config.gasLimit must be > 0")

    for key in ("batch_inbox_address", "deposit_contract_address",
                "l1_system_config_address"):
        if config[key] == ZERO_ADDRESS:
            raise RollupConfigError(f"{key} must not be zero")

    if config["l1_chain_id"] <= 0 or config["l2_chain_id"] <= 0:
        raise RollupConfigError("chain ids must be positive")
    if config["l1_chain_id"] == config["l2_chain_id"]:
        raise RollupConfigError("l1_chain_id and l2_chain_id must differ")

    previous = 0
    for key in FORK_TIME_KEYS:
        if config[key] < previous:
            raise RollupConfigError(f"fork times must be monotonic, {key} regresses")
        previous = config[key]

    op_config = config.get("chain_op_config") or {}
    if (op_config.get("eip1559Elasticity", 0) <= 0
            or op_config.get("eip1559Denominator", 0) <= 0
            or op_config.get("eip1559DenominatorCanyon", 0) <= 0):
        raise RollupConfigError("chain_op_config must carry positive EIP-1559 parameters")
    return config


def parse_args(argv):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--l1-chain-id", type=int, required=True)
    parser.add_argument("--l2-chain-id", type=int, required=True)
    parser.add_argument("--l1-genesis-hash", required=True,
                        help="L1 block hash the L2 genesis anchors to (anvil block 0)")
    parser.add_argument("--l1-genesis-number", type=int, default=0)
    parser.add_argument("--l2-genesis-hash", required=True,
                        help="FISCO L2 genesis block hash (eth_getBlockByNumber 0)")
    parser.add_argument("--l2-genesis-time", type=int, required=True,
                        help="L2 genesis timestamp, Unix seconds (> 0)")
    parser.add_argument("--block-time", type=int, default=2)
    parser.add_argument("--max-sequencer-drift", type=int, default=600)
    parser.add_argument("--seq-window-size", type=int, default=3600)
    parser.add_argument("--channel-timeout", type=int, default=300)
    parser.add_argument("--gas-limit", type=int, default=30_000_000)
    parser.add_argument("--batcher-addr",
                        default="0x3C44CdDdB6a900fa2b585dd299e03d12FA4293BC",
                        help="dev default: anvil account #2")
    # Zero is the CORRECT default, not an oversight: overhead is a pre-Ecotone L1-fee
    # parameter (deprecated at Ecotone), and op-node v1.19.3 rollup.Config.Check()
    # (op-node/rollup/types.go:317 at tag op-node/v1.19.3) validates no overhead field —
    # verified against a real op-node v1.19.3: a zero-overhead rollup.json passes config
    # validation and reaches L1 dialing, while a zero *scalar* is rejected right there
    # ("missing genesis system config scalar"). Pinned by test_zero_overhead_accepted.
    parser.add_argument("--overhead", default=ZERO_HASH)
    parser.add_argument("--scalar", default=DEFAULT_SCALAR)
    parser.add_argument("--batch-inbox-address",
                        default="0xFF00000000000000000000000000000000000901")
    parser.add_argument("--deposit-contract-address",
                        default="0x1000000000000000000000000000000000000001",
                        help="L1 OptimismPortal (dev placeholder)")
    parser.add_argument("--l1-system-config-address",
                        default="0x1000000000000000000000000000000000000002",
                        help="L1 SystemConfig (dev placeholder)")
    parser.add_argument("--protocol-versions-address", default=ZERO_ADDRESS)
    parser.add_argument("--eip1559-elasticity", type=int, default=6)
    parser.add_argument("--eip1559-denominator", type=int, default=50)
    parser.add_argument("--eip1559-denominator-canyon", type=int, default=250)
    parser.add_argument("--out", default="-", help="output file, '-' for stdout")
    return parser.parse_args(argv)


def main(argv=None):
    args = parse_args(argv)
    try:
        config = check(build_rollup_config(args))
    except RollupConfigError as exc:
        sys.stderr.write(f"error: {exc}\n")
        return 1
    text = json.dumps(config, indent=2) + "\n"
    if args.out == "-":
        sys.stdout.write(text)
    else:
        with open(args.out, "w") as handle:
            handle.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
