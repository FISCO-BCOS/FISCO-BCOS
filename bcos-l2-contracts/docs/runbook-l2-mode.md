# Runbook: running a node in L2 mode

How to stand up a single FISCO-BCOS node in OP-Stack L2 mode, what changes
versus the default `pbft` mode, the error strings you hit if the genesis is
wrong, and the upgrade paths.

L2 mode is signalled by the genesis executor version — `version = 3` (or higher)
in the `[executor]` section of `config.genesis`, together with
`[ethereum] mode = opstack-el`. There is no `chain_mode` key and no feature
flag; the executor version and the `[alloc.*]` sections must agree
(`NodeConfig::validateL2Invariants` in `bcos-tool/bcos-tool/NodeConfig.cpp`).

This is node-bring-up only. The op-node / sequencer wiring and the L1 bridge
are A8-workstream concerns and are not covered here.

## Smallest failing scenario this prevents

You set `executor.version = 3` in `config.genesis` but leave the
`[alloc.*]` sections out. The node refuses to start:

```
executor.version >= 2 (the Ethereum lane) requires a non-empty [alloc.*] section in config.genesis
```

(`NodeConfig::validateL2Invariants`.) L2 mode has no contracts unless genesis allocs
materialize them — predeploy constructors never run on-chain, so all runtime
bytecode and storage must be written directly into genesis state. The quick
start below produces those allocs.

## Quick start (5 steps)

The allocs generator lives in FISCO-BCOS/op-stack-e2e-tests under
`tools/opstack-genesis/` (check it out, e.g. into `.ci-op-e2e-tests`);
`bcos-l2-contracts/` holds the Solidity suite. Paths in steps 1–2 are relative
to that harness checkout (or to wherever `OP_E2E_DIR` points); the node steps
that follow are relative to the FISCO-BCOS repo root.

### 1. Build the contracts, obtain the base allocs, edit a chain config

```bash
FISCO_REPO=${FISCO_REPO:-.}    # path to the FISCO-BCOS checkout (holds bcos-l2-contracts/)
cd "${OP_E2E_DIR:-.ci-op-e2e-tests}/tools/opstack-genesis"
# CONTRACTS must point at bcos-l2-contracts in the FISCO-BCOS checkout; the
# Makefile's relative default only fits a sibling layout.
make contracts CONTRACTS="$FISCO_REPO/bcos-l2-contracts"    # forge build of bcos-l2-contracts/src (the only artifacts genesis needs)
# obtain final-allocs.json: the op-deployer terminal alloc JSON for the pinned
# Karst release (bcos-l2-contracts/op-fork-pin.toml [karst_pin]); generating it
# needs the op-deployer binary — run it wherever that binary is available.
cp chain-config.template.yaml chain-config.yaml
$EDITOR chain-config.yaml      # set chain_id, owner, gas/version fields
```

The op-deployer output is the ONLY source of the OP-Stack accounts (every
0x42... proxy, every 0xc0d3... implementation, ProxyAdmin ownership,
prefunded accounts). The OP fork source tree is NOT part of this pipeline;
`make op-fork-build` still exists for compiling the pinned sources when you
need to inspect them (`runbook-op-fork-upgrade.md` covers bumping the pin).

### 2. Generate the genesis allocs

```bash
make allocs CONFIG=chain-config.yaml BASE=final-allocs.json OUT=allocs.ini
# equivalently:
python3 build-allocs.py --config chain-config.yaml \
    --contracts ../../bcos-l2-contracts \
    --base-allocs final-allocs.json --out allocs.ini --out-json allocs.json
```

`allocs.ini` holds one `[alloc.N]` (+ `[alloc.N.storage]`) section per merged
account: every base-alloc account carried through verbatim, plus 4 overlay
accounts — the two self-written predeploys each expand to a proxy account
(EIP-1967 slots + seeded contract storage) and an implementation account.
`SystemConfig`'s `chain_id` and the other config entries are seeded as packed
Entry slots on the proxy account. `allocs.json` is the same merged set in
geth-style alloc shape — feed it to the op-reth oracle genesis so both chains
share one account set.

**Pre-freeze output is throwaway.** While `base_allocs_sha256` in the chain
config is still empty (the tool warns loudly), any `allocs.ini`/genesis you
produce is for integration testing ONLY. Before launching a real chain,
regenerate from the FROZEN op-deployer artifact and fill in
`base_allocs_sha256` (mirrored from `op-fork-pin.toml`
`[karst_pin].base_allocs_sha256`) — a SHA recorded after the fact will not
match the base an already-started chain was actually built from, and genesis
is immutable: there is no fixing it post-launch.

### 3. Assemble `config.genesis`

Declare the OP lane via the executor version and EL mode, then append the
generated `allocs.ini`:

```ini
[executor]
    version = 3

[ethereum]
    mode = opstack-el

[chain]
    sm_crypto = false
    group_id  = group0
    chain_id  = chain0

; --- appended from allocs.ini ---
[alloc.0]
    address = 43000000000000000000000000000000000000c0
    balance = 0
    nonce   = 0
    code    = 0x60806040...
[alloc.0.storage]
    0x0000...0000 = 0x...
; ... 12 more predeploys ...
```

`executor.version >= 3` is the OP lane (an OP-Stack L2); `= 2` is the plain
Ethereum lane (L1 EL). The lane is fixed at genesis — there is no runtime
switch. With `executor.version < 2`, any `[alloc.*]` section is rejected
(`NodeConfig::validateL2Invariants`).

### 4. Start the node

```bash
./fisco-bcos -c config.ini -g config.genesis
```

On first init with allocs, `Ledger::buildGenesisBlock` computes an
op-geth-compatible Ethereum state root over the allocs
(`computeGenesisStateRoot`, `bcos-ledger/bcos-ledger/Ledger.cpp:1903`) and
stores it as the genesis block's `stateRoot` (`Ledger.cpp:2064`). The chain
config fields are serialized into the genesis block's `extraData`. Both are
re-derived and checked on every later startup (see [Immutability](#immutability)).

### 5. Verify

```bash
# every merged predeploy carries code (spot-check; genesis-bootstrap.sh sweeps them all)
cast code 0x43000000000000000000000000000000000000C0 --rpc-url http://127.0.0.1:8545

# SystemConfig returns the seeded chain_id (value, enableNumber)
cast call 0x43000000000000000000000000000000000000C0 \
    "getValueByKey(string)" "chain_id" --rpc-url http://127.0.0.1:8545

# eth_chainId agrees with SystemConfig chain_id (PR-4/PR-6 path consistency)
cast chain-id --rpc-url http://127.0.0.1:8545
```

`tools/.ci/l2-integration/run-all.sh` automates the equivalent checks against a
running devnet.

## L2 mode vs pbft mode

| Aspect | `pbft` (default) | L2 (`executor.version >= 3`, `mode=opstack-el`) |
|--------|------------------|-----------------------------------|
| `[alloc.*]` genesis allocs | rejected | required (non-empty) |
| Predeploys at block 0 | none | 13 (2 self-written + 11 pinned OP fork) |
| FISCO-private precompiles (`0x1000`..) | live | `disabledInL2()` removes 13 of them (PR-5) |
| KZG point-evaluation `0x0a` | not registered | registered (A6.14) |
| Genesis block `stateRoot` | empty | op-geth MPT root over allocs (`Ledger.cpp:2064`) |
| Per-block config source | static node config | `L2ConfigLoader` reads `SystemConfig._config` slots directly, no EVM staticcall (PR-4) |
| `eth_chainId` source | node config | `LedgerConfig.chainId` from SystemConfig `chain_id` (A6.9) |
| WASM executor | allowed | rejected (`is_wasm=true` unsupported) |

## Common errors

Genesis-config errors are thrown as `bcos::tool::InvalidConfig`; the per-block
config-load errors are `std::runtime_error`. Line numbers are as of
release-3.18.0 and will drift; the verbatim error string is the stable anchor.
Exact strings:

| Error string (verbatim) | Cause | Source |
|-------------------------|-------|--------|
| `executor.version >= 2 (the Ethereum lane) requires a non-empty [alloc.*] section in config.genesis` | Ethereum lane on (`executor.version >= 2`), no allocs | `NodeConfig::validateL2Invariants` |
| `[alloc.*] section requires executor.version >= 2 (the Ethereum lane) in config.genesis` | allocs present, `executor.version < 2` | `NodeConfig::validateL2Invariants` |
| `executor.version >= 2 (the Ethereum lane) requires an [eth_genesis_header] section in config.genesis (all 22 fields from the merged genesis artifact); ...` | Ethereum lane on, no `[eth_genesis_header]` section | `NodeConfig::validateL2Invariants` |
| `[eth_genesis_header] section requires executor.version >= 2 (the Ethereum lane) in config.genesis` | `[eth_genesis_header]` present on a consortium (`executor.version < 2`) genesis | `NodeConfig::validateL2Invariants` |
| `executor.is_wasm=true is not supported: WASM support was removed in FISCO-BCOS 3.18; use the EVM executor (set is_wasm=false)` | `is_wasm = true` (any lane; WASM removed in 3.18) | `NodeConfig::loadExecutorConfig` |
| `[alloc.N].address duplicate: <addr>` | two alloc entries share an address | `NodeConfig.cpp:239` |
| `[alloc.N].nonce must fit in uint64: <v>` | alloc `nonce` exceeds `uint64` (RLP-encoded as a uint64 in the state root) | `NodeConfig.cpp:260` |
| `[alloc.N] malformed: <detail>` | malformed alloc hex (bad length / not 0x-prefixed / odd nibble count) | `NodeConfig.cpp:287` |
| `genesis allocs changed since first init (op-geth state root mismatch); refuse to start. stored=<h> computed=<h>` | allocs edited after first init | `Ledger.cpp:1930` |
| `L2ConfigLoader: SystemConfig key '<k>' is not set (slot empty); ...` | a config key was never seeded into SystemConfig storage | `L2ConfigLoader.h:268` |
| `L2ConfigLoader: chain_id == 0 breaks EIP-155 replay protection` | `chain_id` slot seeded as 0 | `L2ConfigLoader.h:301` |
| `L2ConfigLoader: slot value must be <N> bytes, got <n>` | SystemConfig slot value has the wrong width (layout drift) | `L2ConfigLoader.h:145` |

## Immutability

Two frozen-genesis guards run in `Ledger::buildGenesisBlock` on every startup
after the first, both keyed off the already-persisted genesis block:

- **Chain config fields** (chainID, groupID, sm_crypto, is_wasm, gas limits,
  the feature set) live in the genesis block's `extraData`, produced by
  `generateGenesisData`. On restart the fields are regenerated and compared; a
  mismatch aborts with *"The Genesis Data is inconsistent with the initial
  Genesis Data"* (`Ledger.cpp:1980`).
- **Allocs** are captured by the genesis block's `stateRoot` (the op-geth MPT
  root). On restart, if the config fields still match but a freshly computed
  state root differs, the node aborts with *"genesis allocs changed since first
  init"* (`Ledger.cpp:1930`). `computeGenesisStateRoot` reads only
  `genesis.m_allocs`, so this fires precisely when an alloc's address, balance,
  nonce, code, or storage changed.

Neither guard is ever rewritten: the genesis block is the source of truth, and
editing a frozen field means the node is now pointed at a different chain.

## Upgrade paths

| Change | Path |
|--------|------|
| Enable/disable L2 mode (`executor.version >= 3` + `[ethereum] mode=opstack-el`) | the executor version and allocs are pinned by genesis; immutable after first init — start a **new chain** |
| Add / change a predeploy (different allocs) | allocs are pinned by the genesis `stateRoot`; start a **new chain** |
| Bump the pinned OP fork to a new tag | edit `op-fork-pin.toml` — see `runbook-op-fork-upgrade.md` |
| Phase B governance handover (DAO switch) | runtime `transferOwnership` txs, not a genesis change: `Ownable.owner` of SystemConfig / L2ValidatorSet (config + validator authority) and/or `ProxyAdmin` ownership (upgrade authority) — two independent roles, hand over each deliberately |

There is no in-place migration for any frozen genesis field by design: the
`stateRoot` / `extraData` guards exist to stop a node from silently running on a
genesis that differs from the one it was initialized with.
