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
| 2 | no evidence: a `missing: ...` line names the tool, RPC, or transaction kind |

A skip is exit 2. `tools/.ci/l2-integration/run-all.sh:78-81` exits 0 when every scenario
skips; this gate has no such path.

## Run locally

Needs a Linux host for the docker fallback, or `KONA_HOST_BIN` pointing at a native build.

```bash
git clone https://github.com/FISCO-BCOS/op-stack-e2e-tests .ci-op-e2e-tests
git -C .ci-op-e2e-tests checkout 0451c8bf3601c502a36be4633c0e2f4cc38e9c9b
bash tools/.ci/c2-e2e.sh            # builds .ci-c2-bins/{op-deployer,op-node,op-batcher}, then runs C2
pip install -r tools/.ci/c2-e2e-requirements.txt
ANVIL_BIN=/path/to/foundry-v1.8.3/anvil OVERLAY=on bash tools/.ci/kona-parity/run.sh --blocks 12
```

`anvil` must come from foundry v1.8.0 or later: kona-host fetches L1 headers and receipts
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

## Not verified on the machine this was written on

That machine had no kona-host binary, op-node, op-batcher, op-deployer, or FISCO OP build, so:

- No end-to-end run of `run.sh`, positive or negative, has happened.
- FISCO's `debug_getRawHeader`/`debug_dbGet` (ticket 02) did not exist yet; preflight against a
  build without them prints `missing: L2 debug_getRawHeader (ticket 02)`.
- The origin-lag limit and the 2 s anvil default have not been exercised in a full run.
- The EIP-7702 `cast send --auth` incantation ran against anvil v1.8.3 (type 0x4, status 1,
  delegation code `0xef0100…`), not against FISCO.
- `compare.py preflight` ran against anvil v1.8.3 (L1 checks pass) and anvil v1.5.1 (reports
  `missing: L1 debug_getRawHeader`); `kona-host single --help` and one offline kona-host run
  (for the JSON log shape) ran from the pinned image.
