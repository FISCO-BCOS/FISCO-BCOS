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

### `karst_alias` — FISCO `karstConfig` aliases `jovianConfig` (switch_karst)

- **Grid case:** `switch_karst` (already carries `"known_divergence":
  "karst_alias"`).
- **Status:** confirmed consistent across all four ends — but consistent *by
  design*, not because Karst semantics have been verified anywhere.
- **What happens:** FISCO's `karstConfig()` is a placeholder alias of
  `jovianConfig()` (bcos-evm `OpForkSchedule.h`); op-geth selects the Jovian
  operator-fee-fix via `IsJovian` and never consults `KarstTime`; op-revm's
  `KARST` is `>= JOVIAN` so it takes the same `×100` operator path;
  GasPriceOracle has no Karst branch and reports the Jovian formula. All four
  therefore emit the jovian numbers for this row.
- **Why registered:** the agreement is a placeholder coincidence, not evidence
  that real Karst behaviour (Isthmus→Karst DA changes) is implemented. Real
  Karst adaptation is tracked separately (see the da-matrix plan's "单独立案").
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
