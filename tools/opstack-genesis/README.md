# opstack-genesis: build-allocs.py

Generates FISCO-BCOS genesis `[alloc.N]` INI fragments for L2 mode
(`chain_mode = l2`) from the `bcos-l2-contracts` forge artifacts plus a
chain-config YAML. In L2 mode every predeploy is materialized directly into
genesis state (runtime bytecode + storage slots) because predeploy constructors
never run on-chain — see `bcos-l2-contracts/README.md` "Genesis deployment
notes".

## Usage

```bash
# 1. build the contracts (self-written + vendored OP fork)
make contracts CONTRACTS=../../bcos-l2-contracts

# 2. copy the template and edit chain_id / gas limit / owner / etc.
cp chain-config.template.yaml chain-config.yaml

# 3. emit allocs.ini
make allocs CONFIG=chain-config.yaml OUT=allocs.ini
# (equivalently)
python3 build-allocs.py --config chain-config.yaml \
    --contracts ../../bcos-l2-contracts --out allocs.ini
```

`allocs.ini` is appended to `config.genesis` under the `[chain] chain_mode = l2`
node; `NodeConfig::loadAllocs()` parses the `[alloc.N]` / `[alloc.N.storage]`
sections (PR-2). The Ledger consumes them at genesis (PR-3).

Dependencies: Python 3 stdlib + `pyyaml`. Tests also need `pytest`.

## chain-config.yaml fields

| field | meaning |
|-------|---------|
| `chain_id` | L2 chain id; patched into SystemConfig's `immutable chainId` (bytecode, not storage) |
| `l2_block_gas_limit` | uint64, SystemConfig slot 0 low 8 bytes |
| `compatibility_version` | uint32, SystemConfig slot 0 next 4 bytes (packed) |
| `feature_flags` | uint256, SystemConfig slot 1 (0 omitted — unset slot reads 0) |
| `system_config_owner` | address, SystemConfig slot 2 |
| `predeploys` | list of `{name, address, sol_file}` |

## Why SystemConfig.chainId needs bytecode patching

`SystemConfig.chainId` is `uint256 public immutable`. Solidity stores immutables
inside the runtime bytecode, not in a storage slot, and resolves them at deploy
time. A genesis predeploy is written straight into state and its constructor is
never executed, so the immutable value would stay at its zero placeholder and
on-chain `getChainConfig()` would return `chainId == 0`.

`build-allocs.py` therefore reads the forge artifact's
`deployedBytecode.immutableReferences` (a map `astId -> [{start, length}, ...]`
of byte windows inside the deployed bytecode) and splices the 32-byte big-endian
`chain_id` into each window. This is `patch_immutables()`; the contract is
documented in `bcos-l2-contracts/src/SystemConfig.sol:24-29` and
`bcos-l2-contracts/README.md` "Genesis deployment notes".

Writing `chain_id` to a storage slot (as an earlier plan draft did) would be
wrong: slot 0 holds the packed `(l2BlockGasLimit, compatibilityVersion)` and
chainId never occupies a slot at all (`storage-layout/SystemConfig.json` lists
no `chainId` entry).

## Immutables policy

`SystemConfig.chainId` is the only immutable with a patch rule: `build-allocs.py`
splices it into the runtime bytecode via `deployedBytecode.immutableReferences`.

Any other contract whose forge artifact still carries `immutableReferences`
fails the build. Shipping that bytecode as-is would deploy its zero placeholders
into genesis state — broken runtime code. To accept that for a specific
predeploy, set `allow_unpatched_immutables: true` on its `predeploys` entry; the
build then emits the bytecode with zero-valued immutables (a Phase A deferral).

The Phase-A opt-outs are the three `FeeVault` predeploys — `SequencerFeeVault`,
`BaseFeeVault`, `L1FeeVault` (AST ids 9976/9979/9983 = `RECIPIENT`,
`MIN_WITHDRAWAL_AMOUNT`, `WITHDRAWAL_NETWORK` from `FeeVault.sol`). Consequence:
`RECIPIENT == address(0)`, so fee withdrawal targets the zero address and is
unusable until a re-genesis or a future patch rule. Do not enable the flag for
any other contract without review.

## SystemConfig slot 0 packing

`storage-layout/SystemConfig.json`:

```
slot 0: l2BlockGasLimit (uint64, offset 0) | compatibilityVersion (uint32, offset 8)
slot 1: featureFlags (uint256)
slot 2: owner (address)
slot 3: _hardforkActivations (mapping base)
```

Solidity packs lower-offset variables into the least-significant bytes, so slot 0
= `(compatibilityVersion << 64) | l2BlockGasLimit`. `pack_system_config_slot0()`
computes this.

## Testing

```bash
python3 -m pytest test_build_allocs.py -v
```

The suite uses synthetic forge artifacts written to `tmp_path`; it does not run
`forge`. It checks (1) `patch_immutables` lands the chainId at the right offsets
and leaves the rest of the bytecode untouched, (2) slot 0 packing is exact, and
(3) INI emission lowercases addresses, sorts storage slots, and omits zero
feature flags.
