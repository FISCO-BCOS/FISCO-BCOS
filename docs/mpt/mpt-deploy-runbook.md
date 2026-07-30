# Runbook: deploying and operating MPT state storage

Operational entry point for both activation scenarios of the MPT lazy-build
state root, plus the fault-triage section. Feature background and limitations:
`docs/mpt/mpt-lazy-build-release-notes.md`.

## Scenario A — hot upgrade of an existing chain

Follow `docs/mpt/mpt-migration-scenario-a.md` (pre-flight checklist, the
one-way-flag warning, activation, rollback-by-backup). Deployment summary:

1. Roll the new binary across all nodes (rolling restart; the flag is off, so
   behavior is unchanged).
2. `setSystemConfigByKey feature_mpt_state_root 1` through governance.
3. Watch the first two post-activation blocks: block `N` (activation) commits
   XOR, block `N+1` commits the first MPT root. All nodes must stay in
   consensus — the pre-flight checklist's binary-version item exists precisely
   because a node still on an old binary diverges at this point.
4. Verify with any account written after activation:

   ```bash
   curl -s http://127.0.0.1:8545 -X POST -H 'Content-Type: application/json' -d \
     '{"jsonrpc":"2.0","id":1,"method":"eth_getProof","params":["0x<touched-address>",[],"latest"]}'
   ```

   A dormant (never-touched-since-activation) account instead returns error
   `-32004` "Account not in trie (dormant in scenario A)" — expected, not a
   fault.

## Scenario B — new L2 chain from genesis

Node bring-up is the existing L2 runbook:
`bcos-l2-contracts/docs/runbook-l2-mode.md` (allocs generation, `config.genesis`
assembly, verification with `cast`, frozen-genesis immutability guards). MPT
specifics on top of it:

- The genesis header's `stateRoot` is the op-geth-compatible root over the
  allocs, and the genesis trie nodes are persisted at first init — block 1
  builds incrementally on them (`Ledger::buildGenesisBlock`,
  `bcos-ledger/bcos-ledger/Ledger.cpp`).
- `feature_l2_ethereum_compat` must come from genesis. Writing it mid-chain via
  governance makes every node refuse to boot at the next restart:
  "feature_l2_ethereum_compat must be enabled at genesis (activation block 0)"
  (`validateMPTFlagMatrix`, called from `libinitializer/LedgerInitializer.cpp`).
- Do not also set `feature_raw_address` — rejected by the same matrix check.
- All state is in the MPT from block 0: `eth_getProof` has no dormant-account
  case on an L2 chain. Error `-32004` can still appear with the message "Block
  stateRoot not in MPT node storage" when the requested block's root cannot be
  resolved (`EthEndpoint.cpp` distinguishes the two `-32004` causes by
  message).

## RPC / deployment-shape caveats

- `eth_getProof` needs the MPT node reader wired into the RPC's `NodeService`.
  Tars/MAX services never wire it (`bcos-rpc/bcos-rpc/groupmgr/NodeService.h`
  documents the default-unset contract), so under MAX every `eth_getProof`
  answers `-32603` "MPT not enabled on this node". AIR wiring ships with the
  reader-injection change; on binaries without it, AIR answers the same
  `-32603`. Route proof traffic to AIR nodes with the wiring, or treat
  `-32603` as "this node cannot serve proofs", never as data loss.
- Disk: `/mpt/` trie-node rows share the state column family and grow without
  pruning in v1. Budget for monotonic growth proportional to write activity;
  there is nothing to compact away yet.

## Fault triage: "missing trie node"

Symptom: a node aborts block execution or proof generation with
`MPTInvariantViolation` — "Trie: missing node hash; storage lacks a referenced
node" (`bcos-ledger/bcos-ledger/mpt/Trie.h`).

What it is NOT: a crash artifact. A block's header, flat state, trie nodes and
indices go into **one** RocksDB WriteBatch behind a single `Write()` with WAL —
all-or-nothing. A kill -9 or power cut cannot leave the trie referencing nodes
that were never written; the WAL either replays the whole block or none of it.
There is deliberately no half-commit scanner tool in this release: the crash
window it would scan for no longer exists, and the guarantee is instead pinned
in CI by unit tripwires
(`TestRocksDBStorage2/mergeAppliesSourcesInArgumentOrder`,
`TestMPTSchedulerWiring/commitAtomicityAndObserverTiming`,
`TestExecuteStateRootRegression/executeWritesStayInViewUntilCommit` — the
`§11 #14` row of `tools/mpt_acceptance_gate.sh`).

So a missing node on a live chain means real data loss or a real bug, and the
node is doing the right thing by failing loudly instead of silently committing
a wrong root (there is no XOR fallback on the MPT path — pinned by
`TestMPTSchedulerWiring/buildFailureNoXorFallback`). Triage in order:

1. **Manual interference**: was the datadir copied while the node ran, restored
   from mismatched partial backups, or edited with RocksDB tools? Restore a
   consistent full backup or resync the node from peers.
2. **Disk-level corruption**: check RocksDB's own error log and kernel I/O
   errors. Same remedy: restore or resync — the chain's other nodes hold the
   data.
3. **Neither**: treat as a product bug. Capture the exception text (it carries
   the missing hash), the block height, and the `/mpt/` row count, and file an
   issue. Do not attempt to hand-repair the trie.

## Release verification

Before promoting a build that includes these features:

```bash
# Configure the build dir with -DMPT_ACCEPTANCE_GATES=ON first — the armed full-scale
# gates are not registered in a default configure (deliberate: keeps per-PR CI un-gated).
cd build && ctest -L acceptance --output-on-failure   # fine-grained: replay + latency gates
tools/mpt_acceptance_gate.sh build --strict           # spec §11 table; exit 0 required
```

`--strict` also fails on spec items whose implementing PR is still in flight —
drop it only for intermediate progress checks.
