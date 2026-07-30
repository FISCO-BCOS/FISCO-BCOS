# Scenario A — Enabling the MPT State Root on an Existing Chain

How to switch a running FISCO-BCOS chain's `BlockHeader::stateRoot` from the
legacy XOR fold to the Ethereum MPT root, and how to roll back. No data
migration happens at any point: flat KV stays the source of truth, and the trie
absorbs accounts lazily as they are written after activation.

## Smallest failing scenario this prevents

You enable `feature_mpt_state_root` on 3 of 4 consensus nodes. At the first
post-activation block the upgraded nodes commit an MPT root, the fourth commits
the XOR root, consensus on the block header fails and the chain halts. The
pre-flight checklist below exists to make the activation a single, coordinated,
block-numbered event.

## Pre-flight checklist

- [ ] Every consensus node runs a binary that knows the flag (the flag is
      `feature_mpt_state_root` in
      `bcos-framework/bcos-framework/ledger/Features.h`; a pre-flag binary
      rejects the governance write as an unknown system key —
      `SystemConfigPrecompiled` validates keys against `Features::featureKeys()`).
- [ ] `feature_raw_address` is NOT enabled — the combination aborts node boot
      (`validateMPTFlagMatrix`,
      `transaction-scheduler/bcos-transaction-scheduler/BaselineSchedulerMPTHelpers.h`).
- [ ] Do NOT also enable `feature_l2_ethereum_compat`: it is genesis-only and a
      mid-chain enable aborts boot with "must be enabled at genesis (activation
      block 0)" (same file).
- [ ] `datadir` backups taken on every node — this is the ONLY rollback path
      (see Rollback below: feature flags cannot be disabled on-chain).
- [ ] Expect additional disk growth from `/mpt/` trie-node rows — unpruned in
      v1, they grow monotonically with write activity.

## Activation

1. Through governance, write the feature system config — e.g. from the console:

   ```
   [group0]: /> setSystemConfigByKey feature_mpt_state_root 1
   ```

   This creates the `SystemConfig` row `{value="1", enableNumber=N}` the
   scheduler reads back (`ledger::readFromStorage` turns it into
   `set(flag)` + `activationBlockOf(flag) == N` for every block ≥ N).
2. Block `N` still commits the XOR root — the strictly-greater rule in
   `shouldBuildMPT` keeps the activation block itself as the transition
   boundary.
3. Block `N+1` commits the first MPT root, built from the **empty trie**: only
   accounts written at `N+1` are in it. Every later block extends the trie
   incrementally from the parent header's root.

## What changes for applications

- `stateRoot` in block headers changes meaning at `N+1`. Anything that compared
  it across the boundary must treat `N` as an epoch break.
- `eth_getProof` works for state written after activation. Dormant accounts
  return JSON-RPC error `-32004` ("Account not in trie (dormant in scenario
  A)") — a deliberate, explicit answer, not an empty proof
  (`bcos-rpc/bcos-rpc/web3jsonrpc/endpoints/EthEndpoint.cpp`). Cold slots of
  touched accounts are not individually provable in v1 (see release notes,
  known limitation 3-4).
- No latency cliff is expected: first touch of an account costs O(rows written
  this block), regardless of its flat-KV footprint. The acceptance bench pins
  this (`ctest -L acceptance`, `MPTFirstTouchLatencyBench` /
  `MPTSubsequentTouchP99Gate`).

## Rollback

**There is no on-chain rollback.** Feature flags are one-way:
`SystemConfigPrecompiled::validate` rejects any feature value other than `"1"`
("The value for &lt;key&gt; must be 1.",
`bcos-executor/src/precompiled/SystemConfigPrecompiled.cpp`), so
`feature_mpt_state_root` cannot be written back to 0 through governance.

The only way back is off-protocol: stop all nodes and restore every node's
`datadir` from the pre-activation backup (rewinding the chain to a height
before block `N`). This discards all blocks committed since the backup — treat
activation as irreversible in planning, and take the pre-flight backups
seriously.

Committed `/mpt/` rows never need cleanup in either case: they are inert data
outside the MPT read path's own epoch.
