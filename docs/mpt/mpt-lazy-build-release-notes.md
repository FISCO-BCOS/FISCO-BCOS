# MPT Lazy-Build State Storage — v1 Release Notes

FISCO-BCOS can now commit an Ethereum-style Merkle Patricia Trie root as
`BlockHeader::stateRoot`, feature-flagged and off by default. Two activation
scenarios exist, selected by two flags in `bcos-framework/bcos-framework/ledger/Features.h`:

| Flag | Scenario | Activation |
|------|----------|------------|
| `feature_mpt_state_root` | A — existing chain hot upgrade | mid-chain, via a governance `SystemConfig` write |
| `feature_l2_ethereum_compat` | B — new L2 chain | genesis only (enforced, see below) |

`shouldBuildMPT` (`transaction-scheduler/bcos-transaction-scheduler/BaselineSchedulerMPTHelpers.h`)
checks the L2 flag first — an L2 chain builds the MPT from block 0 regardless of
scenario-A activation state. Neither flag set means the legacy XOR state root,
byte-for-byte unchanged.

## Scenario A: existing chain hot upgrade

- Activation is a plain `SystemConfig` feature row `{value="1", enableNumber=N}`.
  Block `N` itself still commits the XOR root (strictly-greater transition rule,
  `shouldBuildMPT`); block `N+1` commits the first MPT root, starting from the
  **empty trie**.
- **Lazy build**: an account enters the MPT only when first written after
  activation. Its first touch commits exactly the rows written in that block —
  account nonce/balance are read once from flat state, but storage slots written
  before activation stay outside the storage trie. There is **no bootstrap scan
  and no preheat tool**: first-touch cost is O(touched rows), independent of how
  many flat rows the account owns (measured: a 100k-flat-slot account's first
  touch costs the same sub-millisecond as a fresh account's —
  `transaction-scheduler/tests/MPTFirstTouchLatencyBench.cpp`).
- Consequence: the MPT commits to **touched state only**. Dormant accounts and
  the cold slots of touched accounts are never provable from the root; flat KV
  remains the source of truth for values.

## Scenario B: new L2 chain

- `feature_l2_ethereum_compat=1` in `config.genesis` `[features]`, plus
  non-empty `[alloc.N]` / `[alloc.N.storage]` sections (both directions enforced
  by `NodeConfig::validateL2Invariants`, `bcos-tool/bcos-tool/NodeConfig.cpp`).
- The genesis header's `stateRoot` is the op-geth-compatible MPT root over the
  allocs, and every genesis trie node is persisted so block 1 builds
  incrementally on top of it (`bcos-ledger/bcos-ledger/Ledger.cpp`,
  `buildGenesisBlock`).
- 18 FISCO-private precompiles (`0x1000`… range) are hidden in this mode
  (`bcos-executor/src/precompiled/L2DisabledSet.h`).
- Genesis-only rule: enabling the L2 flag with a non-zero activation block
  aborts node boot (`validateMPTFlagMatrix`,
  `BaselineSchedulerMPTHelpers.h` — "must be enabled at genesis").
- Node bring-up, predeploy generation and the frozen-genesis guards are covered
  by `bcos-l2-contracts/docs/runbook-l2-mode.md`.

## eth_getProof

EIP-1186 proofs over the committed MPT (`bcos-rpc/.../EthEndpoint.cpp`):

- `-32004` — the request cannot be proven: dormant account in scenario A
  ("Account not in trie (dormant in scenario A)"), or the requested block's
  state root is not in node storage ("Block stateRoot not in MPT node
  storage").
- `-32603` — node configuration: the MPT node reader is not wired on this node
  ("MPT not enabled on this node"). Tars/MAX services never wire the reader, so
  MAX deployments always answer `-32603`; the AIR wiring ships with the
  reader-injection change (in flight at this release).
- A storage slot absent from the trie returns value `0x0` with an exclusion-style
  proof. Distinguishing "cold slot with a live flat-KV value" (`SlotNotInMPT`)
  is in flight and not in this release.

## Durability

A block's header, flat state, trie nodes and indices are written in **one**
RocksDB WriteBatch — a crash cannot leave the trie half-committed, which is why
this release ships no half-commit scanner (see the triage section of
`docs/mpt/mpt-deploy-runbook.md`). A trie node missing at read time is treated
as corruption: `MPTInvariantViolation` ("Trie: missing node hash",
`bcos-ledger/bcos-ledger/mpt/Trie.h`), fail-loud, no silent XOR fallback.

## Known limitations (v1)

1. **No trie pruning**: obsoleted nodes are tracked per commit but not deleted;
   `/mpt/` rows grow monotonically. Pruning hooks (`CommitObserver`) are
   reserved.
2. **No reorg support** in scenario B; finalized-only sync is assumed.
3. **The MPT never converges to the full state** in scenario A (by design since
   the 2026-07-09 slot-level revision): cold slots stay unproven forever unless
   rewritten.
4. **Cold-slot proof semantics (`SlotNotInMPT`) and historical `eth_call`** are
   in flight, not in this release.
5. `feature_raw_address` is mutually exclusive with both MPT flags
   (`validateMPTFlagMatrix`).

## Release verification

Two entry points, both required:

```bash
# The build dir must be configured with -DMPT_ACCEPTANCE_GATES=ON (off by default so the
# armed full-scale gates never run in per-PR CI).
cd build && ctest -L acceptance --output-on-failure   # full-scale latency gates + replay
tools/mpt_acceptance_gate.sh build --strict           # spec §11 cross-reference, exit 0/1
```

The latency gates: subsequent-touch p99 < 200 ms at the 1000-account-per-block
scale, and big-account first-touch within the subsequent-touch magnitude
(`transaction-scheduler/tests/MPTSubsequentTouchP99Gate.cpp`).

## Upgrade paths

- Scenario A migration: `docs/mpt/mpt-migration-scenario-a.md`
- Deployment (both scenarios): `docs/mpt/mpt-deploy-runbook.md`
