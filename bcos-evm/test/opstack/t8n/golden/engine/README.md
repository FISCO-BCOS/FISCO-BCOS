# engine gate golden values (Task 2, op-validator-minimal-loop spec §7.1)

`vectors/*.json` (M-B3+M6 block-level corpus) has no `blockHash` /
`transactionsRoot` / `extraData` / `excessBlobGas`, and no raw bytes for
deposit (0x7E) transactions — a structural blind spot, not an edge-case gap
(spec §7.1). This directory is the off-line, op-geth-backed golden table
that fills exactly those fields, so the engine `newPayload` gate can assert
`payload.blockHash == golden.blockHash` / `result.txRoot ==
golden.transactionsRoot` against an **independent** source (diagnostic
honesty: not "self-consistency" — see spec §7.1 closing paragraph).

`vectors/*.json` itself is **not modified**: golden output and vector
regeneration are written to disjoint paths (see "Ritual" below); `git diff
--stat -- ../../vectors/` is empty by construction, not by after-the-fact
cleanup.

## Contents

- `manifest.txt` — the 33 `<id>.golden.json` filenames (set-equal to
  `../../cases/*.in.json` and `../../vectors/*.json`, checked at generation
  time).
- `<id>.golden.json` × 33 — flat schema, exactly:
  ```json
  {
    "blockHash": "0x...",
    "transactionsRoot": "0x...",
    "extraData": "0x...",
    "excessBlobGas": "0x0",
    "rawTransactions": ["0x...", ...],
    "encodedHeaderHex": "0x..."
  }
  ```
  `rawTransactions[i] = txs[i].MarshalBinary()`, **including the 0x7E
  deposit envelope** that `vectors/*.json`'s `outputDepositTx` shape does not
  otherwise carry — this doubles as the Task 3 `OpDepositEncode` byte-level
  golden (deposit envelope reconstructed from structured fields, cross-checked
  against these bytes). `encodedHeaderHex` is the full header RLP hex, for
  the field-level `encode()==golden` assertion that must precede the
  `hash()` assertion (spec §7.5, decision C3).
- `chained/` — the off-line 1→2 chained pair (spec §7.1 rev.3 decision A2,
  Step 2), **not** part of the 33-set (no `vectors/` counterpart):
  - `chainA.golden.json` / `chainB.golden.json` — the **full** v3-block
    payload (`_info`/`env`/`pre`/`block`/`postState`/`_op_expected`, same
    shape as `outputVector`) merged flatly with the golden extension above.
  - `chain{A,B}.pre.json` / `chain{A,B}.post.json` — the `pre`/`postState`
    seed extracted from the corresponding `.golden.json` (convenience
    fixtures for Task 6's gate test; not independently generated — sliced
    from the same source). `chainB.pre.json` and `chainA.post.json` carry the
    same underlying state in the corpus's two conventional encodings (EF
    account shape vs. `GenesisAlloc` shape, per `../../generator/README.md`
    §5) — **not a re-seed**: `chainB.pre.json`'s key set equals
    `chainA.post.json`'s key set (verified below), and `chainB`'s `env.
    parentHash` equals `chainA`'s `blockHash` — real `InsertChain`-validated
    parent-child linkage, not two independent single blocks spliced by hand
    (rev.2's splice scheme is retired — see generator source comment on
    `processChainPair`).

## extraData: verbatim emission, no hand-picked value

`extraData` is taken **verbatim from the generated block's `header.Extra`**
(`eip1559.EncodeOptimismExtraData`), not hand-picked — rev.2's hand-picked-
value scheme is retired (conflicts with the generator's own convention, spec
§7.1/§5.1 rev.3). Observed shapes in this batch:

- **Isthmus, 9 bytes**: `0x00` (version) ‖ `denominator` (uint32 BE) ‖
  `elasticity` (uint32 BE). Example (`isthmus_transfer_basic`):
  `0x000000003200000006` (denom=0x32=50, elasticity=6).
- **Jovian, 17 bytes**: `0x01` (version) ‖ `denominator` ‖ `elasticity` ‖
  `minBaseFee` (uint64 BE). Example (`jovian_transfer_basic`):
  `0x0100000032000000060000000000000000` (minBaseFee=0). Note the version
  byte is `0x01` here, not `0x00` — op-geth's own encoder bumps it to signal
  the appended `minBaseFee` field; this was **not** assumed going in, it is
  what `EncodeOptimismExtraData` actually emits (原样发射 in practice, not
  just in principle).

`excessBlobGas` is asserted `== 0` at generation time (no-blob OP chain) and
always emitted as the constant `"0x0"` — a real assertion against
`header.ExcessBlobGas`, not an assumed literal.

## Ritual (reproducible)

Pinned op-geth checkout: `/Users/octopus/octo/code/blockchain-impl/op-geth`
@ `e8800cffe53d459cde8a07c8e8f1de9d86e79e07` (tag `v1.101702.2`). Same pin as
`../../vectors/manifest.txt` / `generator_commit` in every vector's
`_op_test_vectors` block — this is a golden *extension* of the same
generation run's corpus, not a second/different op-geth version.

Tool: `../../generator/main.go`'s `opt8n-ref`, extended in place ("扩展
opt8n-ref 发射段", decision A3) with:
- `--golden-output <path>` (companion to `--input`/`--output`): after the
  same `GenerateChainWithGenesis` + `InsertChain` self-check the existing
  33-vector path always ran, additionally emits
  `block.Hash()`/`header.TxHash`/`header.Extra`/`tx.MarshalBinary()`/the
  RLP-encoded header to `<path>` as the flat golden schema above. The
  existing vector output path/bytes are **completely unchanged** — the new
  code is pure addition (`assembleOutput`/`buildGoldenRecord`), not a
  rewrite of the generation pipeline; see the generator source comment
  above `assembleOutput` for the exact refactor description.
- `--chain-output-dir <dir>`: a new, self-contained code path
  (`processChainPair`/`runChainPair`) that drives
  `core.GenerateChainWithGenesis(genesis, engine, 2, gen)` for a **real**
  1→2 chain (not two single-block calls spliced by hand), self-checks both
  blocks together via `InsertChain`, and writes the `chained/` outputs
  above. Reuses the same `assembleOutput`/`buildGoldenRecord` assembly as
  the 33-case path, fed the two blocks in turn (block B's `in.Pre` is set to
  block A's emitted `postState` *after* generation, per decision A2).

Commands actually run (2026-07-28):

```bash
OP_GETH=/Users/octopus/octo/code/blockchain-impl/op-geth
PIN=e8800cffe53d459cde8a07c8e8f1de9d86e79e07
GEN_DIR=<repo>/bcos-evm/test/opstack/t8n/generator
T8N_DIR=<repo>/bcos-evm/test/opstack/t8n

# pin + clean-tree check (as in ../../generator/regen.sh)
[ "$(git -C "$OP_GETH" rev-parse HEAD)" = "$PIN" ] || exit 1
[ -z "$(git -C "$OP_GETH" status --porcelain)" ] || exit 1

rm -rf "$OP_GETH/cmd/opt8n-ref"
cp -r "$GEN_DIR" "$OP_GETH/cmd/opt8n-ref"
( cd "$OP_GETH" && go build ./cmd/opt8n-ref )

# 33 golden files, one per case. Vector output goes to a SCRATCH dir (never
# vectors/), then byte-diffed against the checked-in vector as an extra
# regeneration-parity check (all 33 came back byte-identical).
for in_json in "$T8N_DIR"/cases/*.in.json; do
  base="$(basename "$in_json" .in.json)"
  "$OP_GETH/opt8n-ref" --input "$in_json" \
    --output "$SCRATCH/vec/${base}.json" \
    --golden-output "$T8N_DIR/golden/engine/${base}.golden.json" \
    --op-geth-commit "$PIN"
  diff -q "$SCRATCH/vec/${base}.json" "$T8N_DIR/vectors/${base}.json"  # all 33: identical
done

# chained pair
"$OP_GETH/opt8n-ref" --chain-output-dir "$T8N_DIR/golden/engine/chained" --op-geth-commit "$PIN"

# cleanup (README's existing discipline: nothing committed inside op-geth)
rm -rf "$OP_GETH/cmd/opt8n-ref" "$OP_GETH/opt8n-ref"
```

## Self-check (task brief 裁定 B4, five items)

- **(a) header field cross-account (7 common fields)** — verified with a
  throwaway verification tool (`cmd/opt8n-verify`, same build-inside-op-geth
  ritual, discarded after use, never committed): for each of the 33 vectors
  + 2 chained blocks, a `types.Header` was reconstructed **purely from the
  vector's own `env`/`_op_expected.header` fields plus this directory's
  `extraData`/`excessBlobGas`/`transactionsRoot`** (i.e. NOT by reading
  `encodedHeaderHex` — a real field-level cross-check, not a tautology), then
  RLP-encoded and hashed. Result: **35/35 PASS** — `encodedHeaderHex` byte-
  for-byte match and `keccak256(header) == blockHash` for every one of
  `receiptsRoot`/`gasUsed`/`logsBloom`/`withdrawalsRoot`/`blobGasUsed`/
  `stateRoot`/`requestsHash`.
- **(b) typed-tx byte assertion** — `golden.rawTransactions[i] ==
  vectors/<id>.json.block.transactions[i]._op_raw` for every **non-deposit**
  tx (167 of them across the 33 vectors): **167/167 match**. Deposit txs (39
  of them) have no `_op_raw` in `vectors/*.json` today (that field doesn't
  exist there) — this half is explicitly deferred to Task 3, where
  `OpDepositEncode`'s reconstructed bytes get cross-checked against
  `golden.rawTransactions[i]` for those same 39 indices, per the task brief.
- **(c) `encodedHeaderHex` presence** — all 33 + 2 chained files carry it;
  folded into the (a) check above (which both encodes-and-compares AND
  hashes-and-compares).
- **(d) 33-file/manifest set equality** — `ls cases/*.in.json` basenames
  == `manifest.txt` (minus comments) == `ls golden/engine/*.golden.json`
  basenames, all three sets equal, cardinality 33.
- **(e) vectors/ untouched** — `git diff --stat -- ../../vectors/` empty
  (checked both mid-ritual, after all 33 regenerations, and again after
  cleanup); `git status --porcelain -- ../../vectors/` empty. Zero bytes
  written to that path in this task.

## op-geth checkout cleanup

`cmd/opt8n-ref/`, `cmd/opt8n-verify/`, `opt8n-ref`, `opt8n-verify` were all
removed from the op-geth checkout after use; `git status --porcelain` there
is empty and `HEAD` is unchanged at the pin. Nothing from this ritual was
committed to op-geth.
