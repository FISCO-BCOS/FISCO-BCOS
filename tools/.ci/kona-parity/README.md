# kona-client block parity gate

For each L2 block `b` in the gated range, `run.sh` hands kona-host v1.7.0 the pair
(output root of `b-1`, output root of `b`) that op-node reports for the FISCO chain
(`optimism_outputAtBlock`, op-node `node/api.go:146-166`) and runs:

```
kona-host --logs.stdout.format json single --native \
  --l1-head <current_l1> --agreed-l2-head-hash <hash b-1> --agreed-l2-output-root <root b-1> \
  --claimed-l2-output-root <root b> --claimed-l2-block-number <b> \
  --l1-node-address <anvil> --l2-node-address <FISCO web3> --l1-beacon-address <stub> \
  --rollup-config-path rollup.json --l1-config-path l1-config.json --data-dir kona/<b>.kv
```

kona derives block `b` from L1, executes it on FISCO's state of `b-1`, and exits 1 with
`Failed to validate L2 block` when its output root differs (`bin/client/src/single.rs:140-148`
at optimism `64b043ea5bbc`). Flags were read from `kona-host single --help` of the pinned image.
The gate stops at the first block that is not a match and prints FISCO's `hash`, `stateRoot`,
`receiptsRoot`, `transactionsRoot`, `withdrawalsRoot` next to the values in kona's
`Sealed new block` log line (`crates/proof/executor/src/builder/core.rs:336-344`).

## What a green run proves

- Every gated block's output root (stateRoot, MessagePasser storage root, block hash) equals
  what kona-client computes from the same L1 data. That is ADR 0007's requirement
  "FISCO 每块必须与 kona-client 一致".
- The range contains a user deposit, an EIP-7702 transaction with status 1, a reverting call,
  and (overlay=on) a successful `SystemConfig.setValueByKey` on `0x4200…1000`;
  `compare.py coverage_gaps` rejects a range without them.
- kona built each block from the geth route, not from a `debug_executePayload` witness.
  kona-host tries the witness first on every preimage miss (`bin/host/src/single/cfg.rs:189`,
  `bin/host/src/backend/online.rs:145-177`); a success logs nothing, a failure logs
  `Failed to prefetch high-level hint: debug_executePayload failed` (`online.rs:160`). A block
  counts as MATCH only when kona validated it and that line appears at least once in its log;
  preflight also requires FISCO to answer `debug_executePayload` with `-32601`.
- In the first live run (blocks 75, 76, 360) kona read accounts and storage through
  `eth_getProof` (`L2AccountProof`/`L2AccountStorageProof` hints, `handler.rs:303-385`),
  fetched bytecode through `debug_dbGet` (`handler.rs:255-285`), and sent zero `L2StateNode`
  hints. The gate therefore requires no trie-node fetch. The summary prints the `debug_dbGet`
  code-hint count for information; kona logs hints only at trace level (`online.rs:109`), so
  the count is 0 unless `KONA_TRACE=1` runs kona-host with `-vvvvv`.
- kona-host checks `keccak(agreed output) == agreed root` using FISCO's `debug_getRawHeader`
  and `eth_getProof(L2ToL1MessagePasser).storageHash` (`handler.rs:219-250`), so a FISCO
  `eth_getProof` storage hash that disagrees with the header `withdrawalsRoot` fails the run.

## What it cannot prove

- Karst execution. The harness schedules `jovian_time=0` only
  (op-stack-e2e-tests `tools/op-e2e/setup_c2.sh:327`); `run.sh` prints the rollup.json fork
  times, and until the harness activates Karst the gate covers Isthmus+Jovian semantics.
- Blob-carried batches. op-batcher at the pinned monorepo defaults to calldata
  (`op-batcher/flags/flags.go:131-140`); the beacon stub serves only genesis time and slot
  length, so a blob batch fails the run instead of passing it.
- A reproducible genesis hash: the harness writes the L1 head timestamp into the L2 genesis
  header (`setup_c2.sh:269-272`), so `pins.json genesis_hash` stays a placeholder until ticket 12.
- Cannon execution (kona-client under the MIPS VM) and dispute-game play: ticket 14.

## Exit codes

| code | meaning |
|---|---|
| 0 | every gated block matches |
| 1 | first divergence, both sides printed (`compare.py summarize`) |
| 2 | no evidence: a `missing: ...` line names the tool, RPC, or transaction kind, or a `GATE ERROR: ...` line names the replay gap |

A skip is exit 2. `tools/.ci/l2-integration/run-all.sh:78-81` exits 0 when every scenario
skips; this gate has no such path. `summarize` also refuses a green verdict unless
`verdicts.jsonl` holds exactly one verdict per block of `pairs.tsv` (`incomplete_evidence`):
a replay interrupted partway through the range, or a verdict file carrying blocks the current
`pairs.tsv` never scheduled, reports `GATE ERROR: incomplete evidence: ...` and exits 2.

## CI jobs

- `kona_parity` in `.github/workflows/workflow.yml` runs on every PR whose tree serves
  `debug_getRawHeader` and `debug_dbGet` (#5649): `kona_parity_legs` greps `bcos-rpc/bcos-rpc`
  for the `"debug_getRawHeader"` registration key and, when it is absent, prints a
  `kona parity gate SKIPPED` notice and leaves the matrix empty instead of letting the
  preflight exit 2 on every block. The decision reads the source the workflow builds, so a
  harness pin cannot switch the gate off; once #5649 is in the tree the gate is unconditional.
- The `overlay=off` matrix leg is the one place where "skip" is not a failure, and the decision
  is made outside `run.sh`: the `kona_parity_legs` job greps the pinned harness's
  `tools/op-e2e/setup_c2.sh` for `OVERLAY` and lists the `off` leg only when the harness reads
  it (ticket 12, genesis overlay switch in FISCO-BCOS/op-stack-e2e-tests); otherwise it prints
  a `kona parity gate (overlay=off) SKIPPED` notice and the matrix holds `on` alone. `run.sh`
  makes the same test and exits 2 for `OVERLAY=off` against such a harness, so nothing below
  the workflow can turn that skip into a pass. With the debug methods present, the
  `overlay=on` leg and the preflight keep `SKIP == exit 2 == failure`; a failing
  `kona_parity_legs` job leaves the workflow red.
- `kona_parity_negative_control` in `c2-e2e.yml` (nightly) builds the fee-vault mutant and
  requires exit 1.
- Both jobs install Foundry 1.2.3 for `forge` (the op-deployer build asserts the monorepo's
  `mise.toml` pin) and take `anvil` and `cast` from the sha256-pinned v1.8.3 tarball into a
  directory that only the gate step puts first on `PATH`: the harness's `l2_tx_scenarios.sh`
  orders its `cast send --create` flags for cast >= 1.7.1 (`pins.json cast`), and `run.sh`
  exits 2 with the found version when `cast` is older.

## Run locally

Needs a Linux host for the docker fallback, or `KONA_HOST_BIN` pointing at a native build.

```bash
git clone https://github.com/FISCO-BCOS/op-stack-e2e-tests .ci-op-e2e-tests
git -C .ci-op-e2e-tests checkout 0451c8bf3601c502a36be4633c0e2f4cc38e9c9b
bash tools/.ci/c2-e2e.sh            # builds .ci-c2-bins/{op-deployer,op-node,op-batcher}, then runs C2
pip install -r tools/.ci/c2-e2e-requirements.txt
PATH=/path/to/foundry-v1.8.3:$PATH OVERLAY=on bash tools/.ci/kona-parity/run.sh --blocks 12
```

`cast` on `PATH` must be 1.7.1 or later (`pins.json cast`; `run.sh` checks `cast --version`),
and `anvil` (`ANVIL_BIN`, default: the one on `PATH`) must come from foundry v1.8.0 or later: kona-host fetches L1 headers and receipts
with `debug_getRawHeader`/`debug_getRawReceipts` (`handler.rs:82,109`); anvil registers them
from v1.8.0 (`crates/anvil/core/src/eth/mod.rs:369,377`), v1.7.1 does not, and a live v1.5.1
answers `-32601`. Against a devnet that is already up: `C2=/tmp/c2 bash run.sh --attach`.

`--attach` only works on a young devnet. The sequencer advances its L1 origin by at most one
L1 block per L2 block, so a deposit sent now lands after about
`(head_l1 - unsafe_l2.l1origin) × block_time` seconds. With anvil at 1 s and `block_time` 2 s
that lag grows by one L1 block per L2 block; the first live run measured a ~25 min deposit at
L2 block 500. `compare.py origin-lag` exits 2 when the lag exceeds
`DEPOSIT_BUDGET / block_time` blocks (300 s / 2 s = 150 on the harness devnet), which leaves
half of `wait-mix`'s 600 s budget for the L2 transactions. The throwaway devnet runs anvil at
2 s (`ANVIL_BLOCK_TIME`, equal to `block_time`), so its lag stays at its start-up value.

Evidence stays in `$WORK`: `fisco_blocks.json`, `outputs.json`, `pairs.tsv`, `kona/<b>.log`,
`kona/<b>.kv` (replayable offline with `--data-dir` and no RPC flags), `verdicts.jsonl`.
Those files, plus `l1_head.txt`, `rollup.json`, `l1-config.json`, `setup_c2.log`,
`beacon-stub.log` and the throwaway devnet's `c2/`, are run-owned (`compare.py RUN_OWNED`):
`run.sh` deletes them first (`compare.py clean`), so reusing `WORK`, as the CI jobs do with
`runner.temp/kona-parity`, never appends to an older `verdicts.jsonl`. Other files in `WORK`
are kept. With `--attach`, `C2` must not be `$WORK/c2`; `run.sh` refuses that instead of
deleting the running devnet.

## Negative control

`mutate-fee-vault.sh` repoints `OP_BASE_FEE_VAULT` (`bcos-evm/bcos-evm/opstack/OpPredeploys.h:16-17`)
before the build. `run.sh --mutate fee-vault` must then exit 1 at the first block holding a
non-deposit transaction, and exits 2 if the divergence lands anywhere else or never appears.
A genesis alloc edit cannot serve as the control: kona reads FISCO's own state, so both sides
see the edit. The `kona_parity_negative_control` job in `.github/workflows/c2-e2e.yml` runs it
nightly and asserts exit code 1.

## Preflight

Checked in seconds before any transaction is sent; each failure prints a `missing:` line and
exits 2. The last two cover the FISCO bugs the first live run hit after minutes of kona retries.

- L1 `debug_getRawHeader(latest)` hashes to the block hash, and `debug_getRawReceipts` answers.
- L2 `debug_getRawHeader` for block 0 and for `latest` hashes to the block hash; kona needs the
  genesis header when the agreed block is near genesis.
- L2 `debug_dbGet(stateRoot)` returns bytes whose keccak is the key.
- L2 `eth_getProof(0x…dEaD, [], latest)` returns a proof object. kona hints an account proof
  for every account it reads (`handler.rs:303-338`), absent accounts included.
- L2 `debug_executePayload` answers `-32601`.

## Verification status

Two end-to-end runs on macOS (arm64), harness `0451c8bf`, OP monorepo `da197e45` binaries,
kona-host v1.7.0 as a native `KONA_HOST_BIN`, anvil and cast from foundry v1.8.3:

- Green run: `run.sh --blocks 12`, `OVERLAY=on`. The range grew to cover the mix, blocks
  23–48 (26 blocks); every block `MATCH`, exit 0. Each block's kona log carried 20–31
  `debug_executePayload failed` prefetch lines and zero `L2StateNode` hints.
- Negative control: `mutate-fee-vault.sh`, rebuild, `run.sh --blocks 12 --mutate fee-vault`.
  Block 22 `MATCH`, block 23 (the first block holding a non-deposit transaction) `MISMATCH`,
  exit 1. `stateRoot`, `hash` and `outputRoot` differed while `transactionsRoot` and
  `receiptsRoot` stayed equal, which is what a wrong fee recipient produces.
- The commits that followed the initial gate are what those runs required: a per-block timeout
  that works without coreutils `timeout`, a fresh EIP-7702 authority per run, and sending the
  L1 deposit after the L2 mix so it cannot take a nonce the mix already signed with.
- `compare.py preflight` also ran against anvil v1.5.1, which reports
  `missing: L1 debug_getRawHeader`.

Not exercised, and what it means for the gate:

- Karst: the harness activates `jovian_time=0` only, so the runs above cover Isthmus+Jovian
  execution; Karst parity waits on the harness scheduling `karst_time=0`.
- `OVERLAY=off`: the pinned harness has no overlay switch (ticket 12); `run.sh` exits 2 for it
  and the CI leg is skipped until the harness pin is bumped (see "CI jobs").
- The harness places its overlay `SystemConfig` at `0x4200…1000`, which is what
  `compare.py OVERLAY_SYSTEM_CONFIG` and the coverage check key on; the in-repo contracts
  README says `0x43…`. When the harness moves it, this constant moves with it.
- `pins.json genesis_hash` stays empty: the harness writes the L1 head timestamp into the L2
  genesis header, so the hash changes per run and cannot be asserted.
- `--attach` was used against devnets whose L1-origin lag stayed under the 150-block limit;
  a devnet past that limit is refused (exit 2), not tested through.
- The GitHub Actions jobs themselves have not run yet; the local runs used the same scripts
  with `KONA_HOST_BIN` instead of the docker fallback.
