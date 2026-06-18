# opstack-genesis: build-allocs.py

Generates FISCO-BCOS genesis `[alloc.N]` INI fragments for L2 mode from the
`bcos-l2-contracts` forge artifacts plus a chain-config YAML. L2 mode is enabled
by the `feature_l2_ethereum_compat` flag in `[features]` (there is no
`chain_mode`). Every predeploy is materialized directly into genesis state as
runtime bytecode, because predeploy constructors never run on-chain.

This tool emits **bytecode only, empty storage** — it does not seed any
SystemConfig storage:

- `SystemConfig` (bcos-l2-contracts) is a generic `mapping(string => Entry)` with
  no immutables and no fixed slots. Its `feature_flags` entry is written by the
  C++ genesis path (`Ledger::importGenesisState`), which alone knows the
  version-gated feature set after `Features::setGenesisFeatures(version)`. Its
  other config entries (owner, chain_id, gas/version) are a follow-up PR.
- The vendored OP-fork predeploys ship deployed bytecode as-is; op-node writes
  their runtime state (Phase A).

## Usage

```bash
# 1. build the contracts (self-written + vendored OP fork)
make contracts CONTRACTS=../../bcos-l2-contracts

# 2. copy the template and list your predeploys
cp chain-config.template.yaml chain-config.yaml

# 3. emit allocs.ini
make allocs CONFIG=chain-config.yaml OUT=allocs.ini
# (equivalently)
python3 build-allocs.py --config chain-config.yaml \
    --contracts ../../bcos-l2-contracts --out allocs.ini
```

`allocs.ini` is appended to `config.genesis`; `NodeConfig::loadAllocs()` parses
the `[alloc.N]` / `[alloc.N.storage]` sections, and the Ledger materializes them
at genesis. L2 mode is gated by `feature_l2_ethereum_compat` in `[features]`:
allocs require the flag and the flag requires allocs (`validateL2Invariants`).

Dependencies: Python 3 stdlib + `pyyaml`. Tests also need `pytest`.

## chain-config.yaml fields

| field | meaning |
|-------|---------|
| `predeploys` | list of `{name, address, sol_file, allow_unpatched_immutables?}` |

That is the only field the tool reads. SystemConfig configuration values
(feature_flags, owner, chain_id, gas/version) are not seeded here.

## Immutables policy

A predeploy whose forge artifact still carries `immutableReferences` has unfilled
immutables. Shipping that bytecode as-is would deploy zero placeholders into
genesis state — broken runtime code (e.g. a FeeVault with `RECIPIENT ==
address(0)`). So the build **fails loud**, naming the contract, unless the
predeploy opts in with `allow_unpatched_immutables: true`, which emits the
bytecode with zero-valued immutables (a Phase A deferral).

The Phase-A opt-outs are the three `FeeVault` predeploys — `SequencerFeeVault`,
`BaseFeeVault`, `L1FeeVault`. Consequence: `RECIPIENT == address(0)`, so fee
withdrawal targets the zero address and is unusable until a re-genesis or a
future patch rule. Do not enable the flag for any other contract without review.

SystemConfig itself has no immutables, so it ships as plain bytecode.

## Testing

```bash
python3 -m pytest test_build_allocs.py -v
```

The suite uses synthetic forge artifacts written to `tmp_path`; it does not run
`forge`. It checks that SystemConfig and the OP-fork predeploys emit plain
bytecode with no storage section, that addresses are lowercased, and that
unpatched immutables fail loud unless opted out.
