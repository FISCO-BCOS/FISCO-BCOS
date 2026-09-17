# da-matrix known_divergence registry

This file registers every **known divergence** between the four sources of the
da-matrix (FISCO / op-geth / op-revm / Solidity GasPriceOracle). It is the
authoritative companion to the `known_divergence` field on grid cases
(`da_matrix.json`).

A divergence is a case where the four ends are **not expected to agree
bit-for-bit**, either because one end deliberately chooses different semantics
in undefined territory, or because the fork is not implemented the same way in
every source. The Task 6 `--check` mode reads the grid's `known_divergence`
field, skips the matching cases, and counts them; a case NOT in this registry is
expected to agree exactly on every source.

## Registry

### `karst_alias` — FISCO's `karstConfig` is an explicit OSAKA-based Karst config; op-geth still keys Karst off Jovian (switch_karst)

- **Grid case:** `switch_karst` (already carries `"known_divergence":
  "karst_alias"`).
- **Status:** confirmed consistent across all four ends — but consistent *by
  design*, not because Karst semantics have been verified anywhere.
- **What happens:** FISCO's `karstConfig()` **no longer aliases `jovianConfig()`** —
  it is a materialized config of its own (`OpForkSchedule.cpp:220-236`):
  `.rev = EVMC_OSAKA`, its own `.precompiles = &karstPrecompileOverrides()`,
  `.has_operator_fee` + `.has_jovian_operator_formula` (the ×100 path),
  `.has_da_footprint`, `.deposit_exempt_from_max_tx_gas`, `.l1_fee_model = Fjord`,
  `.disable_prague_requests`. op-geth still selects the Jovian operator-fee-fix via
  `IsJovian` and never consults `KarstTime`; op-revm's `KARST` is `>= JOVIAN` so it
  takes the same `×100` operator path; GasPriceOracle has no Karst branch and
  reports the Jovian formula. All four therefore emit the jovian numbers for this
  row — now because FISCO's explicit config *matches* the jovian-keyed upstreams,
  not because of an alias.
- **Why registered:** the agreement is by construction, not evidence that real
  Karst behaviour (Isthmus→Karst DA changes) is implemented. Real Karst
  adaptation is tracked separately (see the da-matrix plan's "单独立案").
  (2026-09-13 correction: the previous text described `karstConfig()` as a
  placeholder alias, which the code outgrew — see the config above.)
- **Op-geth note:** `run_opgeth` keeps `JovianTime=0` for the karst tag because
  op-geth's cost functions key off `IsJovian`/`IsOptimismIsthmus` only.

### `l1_fee_saturation` — FISCO L1 fee saturates at 2^256-1, op-geth is unbounded

- **Grid cases:** none currently (the grid pins slot1/slot7 to ~1e9 so the L1
  fee stays ≈1.4e16, far below 2^256).
- **Status:** registered, NOT triggered.
- **What happens:** FISCO computes the L1 fee in `intx::uint256` and the Fjord
  formula can saturate at `2^256-1` when the base fees are extreme; op-geth uses
  `big.Int` (unbounded). The grid deliberately pins the max-value rows'
  slot1/slot7 to the baseline (1000e6 / 10e6) so this divergence is never
  exercised. If a future grid row raises slot1/slot7 to `≥ 2^200` scale, it
  MUST carry `"known_divergence": "l1_fee_saturation"`.

### `flz_zero_clamp` — flzLen==0 → FISCO 0 vs op-geth clamp to 100

- **Grid cases:** none currently (no zero-byte envelope in the grid).
- **Status:** registered, NOT triggered.
- **What happens:** FISCO's `estimatedDaSizeFromFlz(0)` returns 0 (a documented
  deliberate divergence in `RollupCost.h`), whereas op-geth's
  `estimatedDASizeScaled` floors at `MinTransactionSizeScaled = 100e6` → a
  charged 100-byte minimum. `flz==0` only occurs for a zero-length envelope,
  which is not a real transaction; the grid carries no such envelope, so no
  case hits it. A future grid row with a zero-byte envelope MUST carry
  `"known_divergence": "flz_zero_clamp"`.

## Solidity L1-fee convention note (NOT a divergence)

The Solidity end's `l1_cost` is **not bit-comparable** with the other three
ends by design:

- `GasPriceOracle.getL1Fee(_data)` eats an **unsigned** RLP tx and adds
  `+68` (`flz(data) + 68`, GasPriceOracle.sol:257-258), while FISCO / op-geth /
  op-revm eat the **signed** envelope with no `+68`.
- The Solidity snapshot therefore records `getL1Fee(signedEnvelope)` as a
  **cross-reference only**; the operator fee (`getOperatorFee(gas)`) IS the
  Solidity authority and is directly comparable.
- The grid carries no `known_divergence` for this — it is a convention
  difference in the Solidity snapshot, documented here and in
  `solidity/OperatorFeeCheck.t.sol`.

## `solidity_l1_uint32_overflow` — GasPriceOracle.getL1Fee panics on extreme scalars

- **Grid cases:** `max_isthmus_scalars`, `max_jovian_scalars`,
  `overflow_isthmus`, `overflow_jovian` (all carry `baseFeeScalar ==
  0xffffffff`).
- **Status:** NEW finding from Task 5 four-source comparison — a latent bug in
  the **real Solidity contract**, not in the FISCO implementation.
- **What happens:** `GasPriceOracle._fjordL1Cost` and `_getL1FeeEcotone`
  compute `baseFeeScalar() * 16 * l1BaseFee()` (GasPriceOracle.sol:248, :283).
  `baseFeeScalar()` returns `uint32`, so `baseFeeScalar() * 16` is evaluated in
  uint32 and **reverts (panic: arithmetic overflow)** when
  `baseFeeScalar >= 2**28`. The grid's max rows set scalar `0xffffffff`, so
  `getL1Fee` reverts for those four cases. `getOperatorFee` is **unaffected**
  (isthmus formula uses `Arithmetic.saturatingMul`; jovian formula fits the
  grid's extreme rows in uint256) and remains the Solidity authority.
- **How the snapshot handles it:** `OperatorFeeCheck.t.sol` wraps `getL1Fee` in
  `try/catch` and records the sentinel `0xfff…ff` for the reverting cases. The
  operator fee is recorded normally for all 16 cases.
- **Why not a grid `known_divergence`:** it is a Solidity-implementation bug
  (real contracts never use scalars that large); the FISCO/op-geth/op-revm
  values are correct. Tracked here and in the task-5 report; a follow-up could
  file an upstream note against GasPriceOracle.sol.

## `rpc_l1_fee_scalar_truncation` — RESOLVED: RPC `l1FeeScalar` now renders like op-geth

- **Grid cases:** none (this is an RPC wire convention, not a da-matrix cost
  case; the grid compares L1 fee / operator fee, not the receipt scalar).
- **Status: RESOLVED (2026-09-17).** `bcos-rpc/bcos-rpc/web3jsonrpc/model/ReceiptResponse.cpp`
  renders the Bedrock-era `l1FeeScalar` as the decimal string op-geth's
  `intToScaledFloat` big.Float emits — `scalar/1e6` with the fractional part kept
  and trailing zeros trimmed ("0.684", "1", "2.000001"); the truncating hex
  quantity is gone. Pinned by `ReceiptFieldBaselineTest` (exact multiple,
  fractional part, canonical Bedrock 684000 -> "0.684", sub-unit) and
  `Web3ResponseTest`.
- **Historical note:** the old entry justified truncation with "every corpus and
  canonical Bedrock config uses a `1e6` multiple" — that premise was wrong. The
  canonical Bedrock scalar is `684000` (`0.684`), which the old integer division
  rendered as `"0x0"`; the divergence was reachable on the canonical config all
  along. (What op-geth does is unchanged: `core/types/rollup_cost.go`
  `intToScaledFloat` computes `scalar / 10^6` as a `*big.Float`, emitted as the
  decimal `l1FeeScalar` field per `core/types/gen_receipt_json.go:40`; it is nil
  from Ecotone on, which FISCO reproduces via the field's presence — the meta
  stores the raw scalar only on the Bedrock formula path.)

### `eip7825_deposit_exemption` — deposits are exempt from the Osaka tx-gas cap on FISCO and on op-revm; op-geth applies the cap (spec is silent)

- **Grid case:** none yet (no da-matrix row exercises a deposit above 2^24); registered
  here because a future differential run (Plan D) against op-geth will surface it.
- **Status: direction unresolved — a previous revision of this entry claimed "op-geth
  deviates from the spec", citing `specs/protocol/karst/overview.md:20`. That citation does
  not exist.** At specs pin `564a0ce` the file is a 464-byte / 16-line stub (TOC plus two
  empty section headings), so there is no line 20; a tree-wide search
  (`grep -rn '7825\|20MGas\|not enabled for deposits' specs/`) hits only `flashblocks.md:615`
  (unrelated), and `git log --all -S'not enabled for deposits'` / `-S'20MGas'` are empty.
  The one real 20M figure is L1/ingress-side resource metering —
  `guaranteed-gas-market.md:48` `MAX_RESOURCE_LIMIT = 20,000,000` — which is not an EL
  transaction-validation rule. **Neither side's alignment is therefore established.**
- **RESOLVED (2026-09-13) by the second implementation:** the OP Rust stack agrees with
  FISCO. `alloy-op-evm/src/env.rs:132-133` (used by op-reth) does enable the cap at Osaka+
  (`cfg_env.tx_gas_limit_cap = Some(revm::primitives::eip7825::TX_GAS_LIMIT_CAP)`), and revm
  enforces it in its **baseline** `validate_env`
  (`revm-handler-20.0.3/src/validation.rs:150-159`: `if tx.gas_limit() > cap →
  TxGasLimitGreaterThanCap`). But op-revm **overrides** `validate_env`
  (`op-revm/src/handler.rs:81-100`) and for `DEPOSIT_TRANSACTION_TYPE` returns `Ok(())`
  directly — `self.mainnet.validate_env(evm)` (the baseline, with the cap) is reached **only
  for non-deposit** transactions. So op-revm exempts deposits exactly as FISCO does
  (`OpForkSchedule.cpp:230` + `OpTransition.cpp:575`), while op-geth applies the cap to
  deposits (`core/state_transition.go:379-383`, not exempted from its failed-deposit
  tolerance at `:489-491`). The specs remain silent on the question, so the honest label is
  **"FISCO matches op-revm; op-geth differs"** — not "spec-aligned".
- **What happens:** FISCO's Karst tier sets `deposit_exempt_from_max_tx_gas = true`
  (`bcos-evm/opstack/OpForkSchedule.cpp:230`), wired at `bcos-evm/opstack/OpTransition.cpp:575`
  as `enforce_max_tx_gas = !cfg.deposit_exempt_from_max_tx_gas`, so an over-cap deposit
  executes normally (pinned by `OpOsakaSemanticsTest.cpp` `DepositExemptFromEip7825MaxGasLimit`,
  status 0 / gasUsed 21000). op-geth `core/state_transition.go:379-383` returns
  `ErrGasLimitTooHigh` when `isOsaka && msg.GasLimit > params.MaxTxGas` inside
  `if (!msg.SkipTransactionChecks)`; deposits do **not** set that flag — the only setter in
  the tree is `internal/ethapi/transaction_args.go:495` (eth_call) — so the cap applies to
  deposits. Note the check sits in `preCheck`, i.e. **before** execution: the
  deposit-tolerant path (`:489-496`, which keeps a failed deposit with `nonce+1`) covers
  errors from the execution phase only and does **not** cover this one, so op-geth's exact
  surface for an over-cap deposit (unprocessable block vs. recorded failure) still needs a
  differential confirmation (Plan D) rather than an assumption.
- **Why it matters / reachability:** FISCO's deposit gas ceiling is 20,000,000 while the
  7825 cap is 2^24 = 16,777,216, so deposits in **(2^24, 20M] are accepted by FISCO and
  rejected by op-geth** — a constructible input (L1-side senders choose deposit gas limits)
  on which the two nodes diverge at that block. No known chain config or corpus vector
  currently emits such a deposit.
- **Disposition (resolved 2026-09-13):** FISCO **keeps** the exemption — it now matches a
  second, independent implementation (op-revm, `handler.rs:81-100`), and the only end that
  caps deposits is op-geth. Do **not** align FISCO to op-geth on this point without evidence
  that op-geth's behaviour is the intended one. Two things had to be corrected on the way:
  (a) the earlier "spec says it is not enabled for deposits" claim rested on a citation that
  does not exist (killed above); (b) that fabricated claim had propagated into
  `tools/check-op-karst-release-gate.sh`, which asserts `deposit_exempt_from_max_tx_gas =
  true` — the assertion is kept, now with a comment citing the real basis, so the gate no
  longer rests on the non-existent line. WI-35 is therefore **closed as "matches op-revm;
  op-geth differs; spec silent"** (no production change).
  Resolve with authoritative evidence — a spec statement, an op-geth/op-node PR, or op-node
  code exempting deposits — or align FISCO with op-geth.
- **Review trigger:** any authoritative statement about EIP-7825 and deposits; op-geth
  changing `state_transition.go:379-383` to skip deposits; a Plan D differential run over a
  (2^24, 20M] deposit; or any chain config that lets L1 senders set deposit gas above 2^24.

### `receipt_derivation_tolerance` — a malformed (≠164B) Ecotone L1-attributes calldata still yields receipt fields on FISCO, while op-geth cannot derive them

- **Grid case(s):** none — and none is possible today: the corpus vectors are isthmus/jovian
  (Fjord+), so the Ecotone length rule is not exercised by any live gate. Reachable only from
  a non-conforming or hostile CL, because a conforming CL always emits exactly 164 bytes
  (`specs/protocol/ecotone/l1-attributes.md:41` — "Total calldata length MUST be exactly 164
  bytes"; selector `0x440a5e20` at `:30`).
- **Status:** registered, NOT triggered. This is an **observation-plane** difference, not a
  consensus/state divergence — and it must **not** be "fixed" by rejecting such blocks (see
  Disposition).
- **What happens (state):** both ends inherit the enforcement from the L1Block predeploy
  itself. op-geth's per-receipt L1 fees during processing come from **state slots**
  (`NewL1CostFunc` reads `L1FeeScalarsSlot`/`L1BlobBaseFeeSlot`/`L1BaseFeeSlot`, with the
  comment "deposit transactions from the block [must be] processed first by state
  transition. This behavior is consensus critical!"), i.e. the attributes enter state only by
  **executing** the predeploy. FISCO's C++ never writes those slots either — it only reads
  them (`bcos-evm/opstack/OpPredeploys.h:11`, `OpFeeParams.cpp:45-49`,
  `OpTransition.h:143`) — and the corpus pre-state carries the predeploy code
  (`tools/opstack-genesis/op-fork-base-allocs.json` address
  `0x42…15`, code at the entry near `:1045`). So a 165-byte calldata reverts the ABI decode
  on both ends and the attributes are not applied on either. **No state divergence.**
- **What happens (receipts):** op-geth's read path re-derives receipt L1 fields from the
  block's first transaction (`core/types/receipt_opstack.go:18` → `extractL1GasParams`), whose
  Ecotone branch hard-requires `len(data) == 164` (`core/types/rollup_cost.go:476-479`,
  "expected 164 L1 info bytes, got %d"). That error is **tolerated** by both real callers:
  `core/blockchain.go:2500` logs and continues (reorg/removal path), and
  `core/rawdb/accessors_chain.go:568` logs and returns **nil** (receipt lookup path), so a node
  importing such a block serves no receipts for it. FISCO's receipt-meta derivation is
  calldata-driven and reads the fields it needs by offset, so it still returns values. Two
  further narrowings on the upstream side: the derivation early-returns for deposit-only
  blocks (`receipt_opstack.go:13`) and only runs when the block has ≥2 transactions
  (`core/types/receipt.go:622`).
- **Why registered:** so that a future Plan D differential over an Ecotone block with a
  malformed attributes calldata is read correctly — the endpoints disagree on **receipt
  serving**, not on state, block validity, or `receiptsRoot` validation. It also records why
  FISCO has no 164-length check: our design inherits the rule from the predeploy's ABI instead
  of re-implementing it, which is the same enforcement op-geth relies on for state.
- **Disposition:** keep FISCO as is. Do **not** add a hard 164-byte rejection to the
  newPayload/validation path: op-geth *imports* such blocks (only its receipt lookup fails),
  so rejecting them would create a divergence in the opposite direction. If a stricter posture
  is ever wanted, it belongs in the receipt-derivation layer (mirror upstream's error/nil) and
  must be registered as its own deliberate divergence.
- **Review trigger:** a corpus Ecotone vector is added (Plan C) and the gate becomes visible;
  a CL that emits a non-164 Ecotone calldata; op-geth moving the length check into
  `state_transition`/block validation (making the block itself un-importable) — that would
  turn this into a consensus divergence and require a matching FISCO check; or FISCO switching
  its receipt derivation from calldata-driven to slot-driven.


### `da_footprint_overflow` — FISCO fails closed on a DA-footprint overflow; op-geth's uint64 accumulator wraps

- **Grid case(s):** none — no grid row can push Σ past uint64 (envelope sizes × a uint16 scalar
  cannot get there); a future row that can MUST carry
  `"known_divergence": "da_footprint_overflow"`.
- **Status:** registered, NOT triggered; deliberately stricter than op-geth (see Disposition).
- **What happens:** op-geth accumulates `daFootprint += EstimatedDASize × scalar` into a Go
  `uint64` with no guard (`core/types/rollup_cost.go:583-589`), so an overflowing Σ wraps
  silently. FISCO checks `sum > max - term` and fails closed
  (`opstack-executor/OpBlockExecute.h:281-285`; failure class `DaFootprintError::Overflow` at
  `:212-219`). Wrapping would let a crafted block clear the Jovian equality gate, so
  fail-closed is deliberate.
- **Disposition:** keep FISCO fail-closed; do not mirror the wraparound.
- **Review trigger:** op-geth adding an overflow guard, or a chain/envelope combination that
  can legitimately exceed uint64 (none known).

### `da_footprint_176b_activation_shape` — the "176B attributes ⇒ deposits-only" rule is enforced at different layers

- **Grid case(s):** none; a future row feeding a 176-byte attributes payload with a non-deposit
  transaction on a Jovian+ block MUST carry
  `"known_divergence": "da_footprint_176b_activation_shape"`.
- **Status:** registered, NOT triggered. **A judgment-point difference, not a strictness
  difference** — the same payload is rejected on both ends, at different layers. (An earlier
  note in this session called FISCO "stricter" here; that was wrong.)
- **What happens:** for `len(attributes) == 176` FISCO's `daFootprintOfEnvelopes` returns 0
  **without** inspecting the last transaction (`OpBlockExecute.h:261-265`); the
  last-tx-is-deposit rule lives in `validateJovianL1AttributesShape` (`:174-185`) on the
  block-execution path. op-geth performs the check inside the same computation and returns
  `unexpected non-deposit transactions in Jovian activation block`
  (`core/types/rollup_cost.go:571-577`).
- **Disposition:** keep both as is; only the layer (and the error surface) differs.
- **Review trigger:** any conformance comparison of rejection *surfaces* for this payload class.

## header-inline ABI note (NOT a divergence)

`flzCompressLen` / `estimatedDaSizeScaled` / `estimatedDaSizeFromFlz` were moved from
out-of-line definitions to `inline` in `bcos-evm/bcos-evm/opstack/RollupCost.h` (commit
`975c2ec58`, closing an unresolved-symbol gap in the exported static `engine` archive).
Consequence: a consumer of prebuilt artifacts sees an ABI/export change (the symbols are no
longer emitted from the archive); a full source rebuild sees no behavioural change. Code-side
motivation: `opstack-executor/OpBlockExecute.h:226-228`.
