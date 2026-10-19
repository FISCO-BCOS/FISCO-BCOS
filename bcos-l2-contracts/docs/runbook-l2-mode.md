# Runbook: running a node in L2 mode

How to stand up a single FISCO-BCOS node in OP-Stack L2 mode, what changes
versus the default `pbft` mode, the error strings you hit if the genesis is
wrong, and the upgrade paths.

An L2 node is an `executor.version = 3` node driven by op-node: `version = 3` in
the `[executor]` section of `config.genesis` puts the chain on the OP lane, and
on that lane the block driver is the Engine API (`NodeConfig::blockDriver()`),
which `config.ini` `[engine_rpc]` serves to op-node. There is no mode key, no
`chain_mode` key and no feature flag; the executor version, the `[alloc.*]`
sections, `[eth_genesis_header]` and `[op_fork_timestamps]` must agree
(`NodeConfig::validateL2Invariants` in `bcos-tool/bcos-tool/NodeConfig.cpp`).

This is node-bring-up only. The op-node / sequencer wiring and the L1 bridge
are A8-workstream concerns and are not covered here.

## Smallest failing scenario this prevents

You set `executor.version = 3` in `config.genesis` but leave the
`[alloc.*]` sections out. The node refuses to start:

```
executor.version >= 3 (the OP lane) requires a non-empty [alloc.*] section in config.genesis (only executor.version=2 may run an empty-alloc genesis)
```

(`NodeConfig::validateL2Invariants`. Only the L1 EL lane — `executor.version = 2` —
is exempt: its empty-alloc genesis publishes the canonical empty-trie root.) L2 mode has no contracts unless genesis allocs
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

### 3. Assemble `config.genesis` and `config.ini`

Start from the node directory `tools/BcosAirBuilder/build_chain.sh` generates
(an `executor.version = 1` chain), then move its `config.genesis` to the OP
lane and append the generated `allocs.ini`.
`tools/.ci/ci_opnode_startup_check.sh` does exactly this for a one-alloc devnet.

```ini
[chain]
    sm_crypto = false
    group_id  = group0
    ; the L2 chain id, decimal (rollup.json l2_chain_id). eth_chainId and EIP-155
    ; read it; a [web3] section is not read on executor.version >= 2
    chain_id  = 901

[executor]
    version = 3

; L2 block timestamps in seconds, from rollup.json (isthmus_time, jovian_time,
; karst_time, ...). An absent key is a fork that never activates.
[op_fork_timestamps]
    isthmus_time = 0
    jovian_time  = 0
    karst_time   = 0

; --- appended from allocs.ini ---
[alloc.0]
    address = 0x43000000000000000000000000000000000000c0
    balance = 0
    nonce   = 0
    code    = 0x60806040...
[alloc.0.storage]
    0x0000...0000 = 0x...
; ... 12 more predeploys ...

; the L2 genesis block header, from the genesis artifact (tools/op-genesis-to-allocs.py
; emits it): 15 required header fields, the six fork-gated ones an L2 artifact
; always carries, and hash
[eth_genesis_header]
    parent_hash = 0x0000000000000000000000000000000000000000000000000000000000000000
    state_root  = 0x...
    ; ... the remaining header fields ...
    hash        = 0x...
```

`executor.version = 3` is the OP lane (an OP-Stack L2); `= 2` is the plain
Ethereum lane (L1 EL). The lane is fixed at genesis — there is no runtime
switch. With `executor.version < 2`, any `[alloc.*]` section is rejected
(`NodeConfig::validateL2Invariants`). The `[version]` and `[features]` sections
of the generated file are not read on `executor.version >= 2`.

In `config.ini`, add the Engine API listener op-node drives the node through.
It has no `enable` key: `executor.version >= 2` always serves it.

```ini
[engine_rpc]
    listen_ip = 127.0.0.1
    listen_port = 8551
    ; 32-byte hex secret; op-node --l2.jwt-secret points at the same file
    jwt_secret_file = conf/engine/jwt.hex

[web3_rpc]
    enable = true
    ; op-batcher calls miner_setMaxDASize on its --l2-eth-rpc, the web3 port
    enable_miner_api = true
```

`[web3_rpc] enable_miner_api` registers the miner namespace on the web3
listener only. `[engine_rpc] enable_miner_api` does the same on the Engine API
listener, for a caller that holds the JWT; neither key reaches the other port
(`RpcFactory.h`).

### 4. Start the node

```bash
./fisco-bcos -c config.ini -g config.genesis
```

On first init with allocs, `Ledger::buildGenesisBlock` computes an
op-geth-compatible Ethereum state root over the allocs
(`computeGenesisStateTrie`) and stores the `[eth_genesis_header]` block as
block 0 after checking that its `state_root` and `hash` match. Both are
re-checked on every later startup (see [Immutability](#immutability)).

### 5. Verify

```bash
# every merged predeploy carries code (spot-check; genesis-bootstrap.sh sweeps them all)
cast code 0x43000000000000000000000000000000000000C0 --rpc-url http://127.0.0.1:8545

# SystemConfig returns the chain_id the allocs seeded (value, enableNumber)
cast call 0x43000000000000000000000000000000000000C0 \
    "getValueByKey(string)" "chain_id" --rpc-url http://127.0.0.1:8545

# eth_chainId (config.genesis [chain] chain_id) agrees with the SystemConfig chain_id
cast chain-id --rpc-url http://127.0.0.1:8545
```

`tools/.ci/l2-integration/run-all.sh` automates the equivalent checks against a
running devnet.

## L2 mode vs pbft mode

| Aspect | `pbft` (default) | L2 (`executor.version = 3`) |
|--------|------------------|-----------------------------|
| Block driver | PBFT | op-node over `[engine_rpc]` (`NodeConfig::blockDriver()`) |
| `[alloc.*]` genesis allocs | rejected | required (non-empty) |
| Predeploys at block 0 | none | 13 (2 self-written + 11 pinned OP fork) |
| FISCO-private precompiles (`0x1000`..) | live | 18 of them hidden from `executor.version >= 2` on (`kL2DisabledSet`, `bcos-executor/src/precompiled/L2DisabledSet.h`) |
| KZG point-evaluation `0x0a` | not registered | registered (A6.14) |
| Genesis block `stateRoot` | empty | op-geth MPT root over allocs (`Ledger::buildGenesisBlock`) |
| Fork schedule | `[features]` / `compatibility_version` | `[op_fork_timestamps]` |
| `eth_chainId` source | `[web3] chain_id` | `[chain] chain_id` |

## Common errors

Genesis-config errors are thrown as `bcos::tool::InvalidConfig`. The verbatim
error string is the stable anchor. Exact strings:

| Error string (verbatim) | Cause | Source |
|-------------------------|-------|--------|
| `executor.version >= 3 (the OP lane) requires a non-empty [alloc.*] section in config.genesis (only executor.version=2 may run an empty-alloc genesis)` | OP lane, no allocs | `NodeConfig::validateL2Invariants` |
| `[alloc.*] section requires executor.version >= 2 (the Ethereum lane) in config.genesis` | allocs present, `executor.version < 2` | `NodeConfig::validateL2Invariants` |
| `executor.version >= 2 (the Ethereum lane) requires an [eth_genesis_header] section in config.genesis (the 21 header fields plus hash, from the merged genesis artifact); ...` | Ethereum lane on, no `[eth_genesis_header]` section | `NodeConfig::validateL2Invariants` |
| `[eth_genesis_header] section requires executor.version >= 2 (the Ethereum lane) in config.genesis` | `[eth_genesis_header]` present on a consortium (`executor.version < 2`) genesis | `NodeConfig::validateL2Invariants` |
| `executor.version >= 3 (OP lane) requires an [op_fork_timestamps] section carrying at least one entry: ...` | OP lane, no `[op_fork_timestamps]` | `laneKeyRules` (`ChainLaneConfig.h`) |
| `[op_fork_timestamps] requires executor.version >= 3 (OP lane)` | `[op_fork_timestamps]` on `executor.version < 3` | `laneKeyRules` (`ChainLaneConfig.h`) |
| `executor.version >= 2 requires [chain] chain_id in config.genesis (the Ethereum chain id, e.g. 11155111 for Sepolia); [web3] chain_id is not read on this lane` | no `[chain] chain_id` | `ethLaneChainId` (`NodeConfig.cpp`) |
| `[chain].chain_id must be decimal digits on executor.version >= 2 (...): chain0` | a v0/v1 chain name left in `[chain] chain_id` | `ethLaneChainId` (`NodeConfig.cpp`) |
| `executor.is_wasm=true is not supported: WASM support was removed in FISCO-BCOS 3.18; use the EVM executor (set is_wasm=false)` | `is_wasm = true` (any lane) | `NodeConfig::loadExecutorConfig` |
| `[alloc.N].address duplicate: <addr>` | two alloc entries share an address | `NodeConfig::loadAllocs` |
| `[alloc.N].nonce must fit in uint64: <v>` | alloc `nonce` exceeds `uint64` (RLP-encoded as a uint64 in the state root) | `NodeConfig::loadAllocs` |
| `[alloc.N] malformed: <detail>` | malformed alloc hex (bad length / not 0x-prefixed / odd nibble count) | `NodeConfig::loadAllocs` |
| `genesis allocs changed since first init (op-geth state root mismatch); refuse to start. stored=<h> computed=<h>` | allocs edited after first init | `Ledger::buildGenesisBlock` |

## Immutability

Three frozen-genesis guards run in `Ledger::buildGenesisBlock` on every startup
after the first, all keyed off the already-persisted genesis block:

- **Genesis header.** `[eth_genesis_header].hash` must equal the stored block 0
  hash, or the node aborts with *"[eth_genesis_header].hash does not match the
  stored genesis block"*.
- **Chain config fields** (chain id, group id, sm_crypto, consensus, gas limit,
  executor, the OP forks active at genesis) are the genesis pin
  `generateGenesisData` produces. On
  an `[eth_genesis_header]` chain block 0's `extraData` carries the artifact
  bytes, so the pin is stored in the `eth_genesis_data` SYS_CONFIG row instead.
  On restart the pin is regenerated and compared; a mismatch aborts with
  *"The Genesis Data is inconsistent with the initial Genesis Data"*.
- **Allocs** are captured by the genesis block's `stateRoot` (the op-geth MPT
  root). On restart, if the pin still matches but a freshly computed state
  root differs, the node aborts with *"genesis allocs changed since first
  init"*. `computeGenesisStateTrie` reads only `genesis.m_allocs`, so this fires
  precisely when an alloc's address, balance, nonce, code, or storage changed.

None of these is ever rewritten: the genesis block is the source of truth, and
editing a frozen field means the node is now pointed at a different chain.

## Upgrade paths

| Change | Path |
|--------|------|
| Enable/disable L2 mode (`executor.version = 3`) | the executor version and allocs are pinned by genesis; immutable after first init — start a **new chain** |
| Move an existing consortium chain (`executor_version` 0/1) onto the Ethereum/OP lane by governance | **not possible**: at compatibility >= 3.18 `SystemConfigPrecompiled` refuses `executor_version` writes of 2 or above in both directions — lane membership is a genesis property (a mid-chain flip would mix two state-transition rules on one chain). Below compatibility 3.18 only 0/1 are writable. Start a **new chain** |
| Add / change a predeploy (different allocs) | allocs are pinned by the genesis `stateRoot`; start a **new chain** |
| Bump the pinned OP fork to a new tag | edit `op-fork-pin.toml` — see `runbook-op-fork-upgrade.md` |
| Phase B governance handover (DAO switch) | runtime `transferOwnership` txs, not a genesis change: `Ownable.owner` of SystemConfig / L2ValidatorSet (config + validator authority) and/or `ProxyAdmin` ownership (upgrade authority) — two independent roles, hand over each deliberately |

There is no in-place migration for any frozen genesis field by design: the
guards above exist to stop a node from silently running on a
genesis that differs from the one it was initialized with.
