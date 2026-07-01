# Runbook: upgrading the pinned OP fork

How to bump the pinned upstream OP-Stack contracts commit that FISCO-BCOS L2
mode targets. The upstream source tree is NOT vendored into this repo —
`bcos-l2-contracts/op-fork-pin.toml` records the commit + four external
Solidity dependency SHAs, and CI (`.github/workflows/l2-contracts.yml`) clones
the pinned commit into `/tmp` before `forge build` against vendored OP
sources. Bumping the pin is a 3-step change: edit the toml, regenerate the
storage-layout baseline (only if drift is real and intentional), and merge.

## Smallest failing scenario this prevents

Someone bumps the `commit` field in `op-fork-pin.toml` to a tag that moved a
storage slot on `SystemConfig` (e.g. inherited a new field from a shared OP
base contract). Genesis allocs in production already wrote the old layout, so
the new bytecode reads a non-existent slot and `getChainConfig()` returns
garbage at block 0 — silently. The storage-layout drift gate (PR-7 CI step)
fails the bump before merge:

```
::error::storage-layout drift detected for SystemConfig; if intentional,
regenerate storage-layout/SystemConfig.json (forge inspect SystemConfig
storage-layout --json) and document the migration
```

Drift is treated as a hard stop. If it is intentional, a storage migration
design is required before regenerating the golden fixture.

## Current pinned state (authoritative)

Read `bcos-l2-contracts/op-fork-pin.toml` for the live values. As of this
writing:

- Upstream: `github.com/ethereum-optimism/optimism`, tag `op-contracts/v1.6.0`,
  commit `33f06d2d5e4034125df02264a5ffe84571bd0359`
- External Solidity dep pins (upstream submodules at the same commit):
  - `openzeppelin-contracts` `ecd2ca2cd7cac116f7a37d0e474bbb3d7d5e1c4d`
  - `openzeppelin-contracts-upgradeable` `0a2cb9a445c365870ed7a8ab461b12acf3e27d63`
  - `solady` `502cc1ea718e6fa73b380635ee0868b0740595f0`
  - `solmate` `8f9b23f8838670afda0fd8983f2c41e8037ae6bc`
- 11 entry-point predeploys (transitive closure built by `forge build` in
  upstream's `packages/contracts-bedrock` root) are listed under
  `[predeploys]` in `op-fork-pin.toml`.

CI step `Build pinned OP fork tree` runs `cd /tmp/op-fork/packages/contracts-bedrock && forge build`,
so the OP fork is compiled by **its own** `foundry.toml` + remappings + solc
0.8.15 pragma — no patching of OP source files happens in this repo.

## Bump procedure (3 steps)

### 1. Edit `op-fork-pin.toml`

Update four blocks:

- `tag` + `commit` to the new upstream release (browse
  `https://github.com/ethereum-optimism/optimism/releases` for an
  `op-contracts/vX.Y.Z` tag and resolve to its commit SHA).
- `date_pinned` to today (YYYY-MM-DD).
- All four `[deps]` SHAs to whatever upstream pins at the new tag. Read them
  with:

  ```bash
  git clone --filter=blob:none --depth=1 --branch <new-tag> \
      https://github.com/ethereum-optimism/optimism /tmp/op-check
  git -C /tmp/op-check submodule status packages/contracts-bedrock/lib | \
      awk '{print $2, $1}'
  ```

  Copy each `<path> <SHA>` pair into the corresponding `[deps]` entry.

- `[predeploys]` list: only update if upstream added/removed/renamed an
  entry-point predeploy at the new tag. The 11 we materialize at genesis is a
  policy choice tied to FISCO-BCOS L2 spec §A6; check upstream's
  `genesis.json` / predeploy address table before changing this list.

### 2. Trigger CI and read the drift gate

Push the branch. The `forge` job will:

- read the new `commit` from `op-fork-pin.toml`
- restore (or re-clone, on cache miss) `/tmp/op-fork` at that commit
- `forge build` self-written `src/`
- `forge build` upstream's `packages/contracts-bedrock`
- `forge test`
- run the storage-layout drift gate against the checked-in
  `storage-layout/*.json` baseline

If the drift gate is green, the bump is mechanical — merge.

### 3. Handle drift (only if the gate fails)

**Policy: any normalized drift is a hard stop.** A bump of the OP fork should
not move a slot in the self-written `SystemConfig` / `L2ValidatorSet`
predeploys; if it does, something in a shared library struct changed shape.
Never silently regenerate the golden fixture to make the gate pass.

A real intentional storage change requires:

1. A storage migration design (genesis allocs are written at fixed slots — a
   moved slot breaks every existing chain). Document it under
   `bcos-l2-contracts/docs/`.
2. Regenerating the golden fixture:

   ```bash
   cd bcos-l2-contracts
   forge inspect SystemConfig   storage-layout --json > storage-layout/SystemConfig.json
   forge inspect L2ValidatorSet storage-layout --json > storage-layout/L2ValidatorSet.json
   ```

3. Committing the migration doc + new fixtures in the same PR as the pin bump.

`--json` is mandatory in forge 1.5.1 — plain output is an ASCII table that does
not diff stably.

## Manual review checklist

- [ ] `op-fork-pin.toml` updated: `tag`, `commit`, `date_pinned`, all 4
      `[deps]` SHAs
- [ ] `[predeploys]` list reviewed against upstream's predeploy address table
      at the new tag (no surprise additions/renames)
- [ ] `forge build` (both src/ and pinned OP fork) and `forge test` pass in CI
- [ ] Storage-layout drift gate is green, or — if drift is real — a migration
      design doc and regenerated fixtures are included in this PR

## PR description template

```
chore(l2-contracts): bump pinned OP fork to <new-tag>

- op-fork-pin.toml: commit <old-sha> -> <new-sha> (op-contracts/<old> -> <new>)
- [deps] SHAs refreshed: OZ <..> / OZ-upgradeable <..> / solady <..> / solmate <..>
- date_pinned: YYYY-MM-DD
- CI re-clones into /tmp/op-fork at new commit; storage-layout drift gate: green
- forge build (src/ + pinned OP fork) + forge test: green

Verification: re-run the L2 contracts workflow against this branch.
```
