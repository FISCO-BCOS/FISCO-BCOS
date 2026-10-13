# da-matrix — FISCO opstack DA / operator-fee four-source matrix

Parameterized DA-gas / operator-fee matrix for the FISCO opstack
**Jovian DA gas / operator fee** feature. A single JSON grid
(`da_matrix.json`, the *only* input) drives four thin runners, each producing a
per-case `{id, l1_cost, operator_cost}` snapshot; the snapshots are compared
bit-for-bit and committed under `golden/`.

This directory lives in the TESTS domain (CI `TESTS=ON`); the FISCO runner is
built as the `opstack-da-matrix-runner` executable and the differential gate
executes in ctest (`OpDaMatrixCheck`: `run_fisco --check fisco --golden golden/`,
known_divergence rows still computed and compared — the fisco golden carries
them, so the exemption scopes only the four-source comparator). `compare_snapshots.py` additionally
cross-compares the committed snapshots and FAILS on a missing end.

## Provenance

The four ends of the committed `golden/` snapshots are pinned as follows (review
F19):

| End | Source | Pinned to |
|---|---|---|
| fisco | this tree | the FISCO runner (`run_fisco.cpp` over `opstack-executor/OpRollupCost.h`); recomputed by the `OpDaMatrixCheck` ctest |
| opgeth | op-geth | tag **v1.101702.2** (commit `e8800cffe53d459cde8a07c8e8f1de9d86e79e07`, recorded in `opstack-executor/tests/support/GoldenSample.h`) |
| oprevm | op-revm 20.0.0 | the `op-revm` git rev in `run_oprevm/Cargo.toml` (`5f90f749caea14398554afb75062f7111b1fc554`; see its F12 fetchability warning) |
| solidity | contracts-bedrock | `da197e45ed44b9fca258b3b0d0709e8dfca1c7cd` (same optimism monorepo checkout as op-revm — see `PINNED REFERENCES` below) |

`golden/SHA256SUMS` carries the sha256 of the four `out_*.json` snapshots.
Regenerate it in the SAME change as any snapshot edit
(`cd golden && shasum -a 256 */out_*.json > SHA256SUMS`), and verify with
`cd golden && shasum -a 256 -c SHA256SUMS`: `tools/.ci/check-ci-pins.sh`'s
op-geth axis fails on a mismatch, so an edited-but-unmanifested golden cannot
silently redefine the gate's expected values.

## Layout

```
da-matrix/
  da_matrix.json        grid: envelopes registry + 16 cases (slots 1/3/7/8, gas, fork)
  run_fisco.cpp         FISCO C++ runner (computeL1Cost / computeOperatorCost, caller-gated)
  run_oprevm/           op-revm 20.0.0 Rust runner (cargo +1.94 build --release)
  solidity/             Solidity authoritative end (GasPriceOracle forge test)
  golden/
    fisco/              FISCO snapshot    (out_fisco.json)
    opgeth/             op-geth snapshot  (out_opgeth.json)
    oprevm/             op-revm snapshot  (out_oprevm.json)
    solidity/           Solidity snapshot (out_solidity.json)
    SHA256SUMS          sha256 of the four snapshots (provenance pin)
  known_divergences.json  canonical known_divergence tag list (single source)
  test_compare_snapshots.py  pytest probes for the comparator's mismatch path
  DIVERGENCES.md        known_divergence registry (prose companion)
  README.md
```

## Running the four ends

```bash
# 1. FISCO (built target; produced by Task 3)
cd <build>/opstack-executor/tests && ./opstack-da-matrix-runner \
  --grid <this>/da_matrix.json --out out_fisco.json

# 2. op-geth (Task 4; authoritative reference v1.101702.2, commit
#    e8800cffe53d459cde8a07c8e8f1de9d86e79e07 — see Provenance)
#    run_opgeth/ carries main.go ONLY (no go.mod/go.sum): drop the directory into a
#    checkout of op-geth at the pinned tag — the HOST checkout's go.mod/go.sum are
#    this runner's dependency source (review F45: a committed go.sum without a
#    go.mod was never read by any build) — then:
cd run_opgeth && go run . --grid <this>/da_matrix.json --out out_opgeth.json

# 3. op-revm (Task 5; op-revm 20.0.0 checkout)
cd run_oprevm && cargo +1.94 build --release
./target/release/run_oprevm --grid <this>/da_matrix.json --out out_oprevm.json

# 4. Solidity (Task 5; in the external contracts-bedrock checkout)
#    test/OperatorFeeCheck.t.sol → writes .testdata/solidity_out.json
cd <contracts-bedrock> && forge test --match-contract OperatorFeeCheck --json
```

Outputs are unified lowercase `"0x"` hex (op-geth `hexutil.Big` convention).

> **Solidity end is deliberately NOT in ctest.** The `solidity/` end
> (`OperatorFeeCheck.t.sol`) requires the external `contracts-bedrock` checkout
> (see `PINNED REFERENCES`) and a `forge` toolchain, neither of which CI can
> reach — so only the FISCO, op-geth and op-revm runners are wired into
> `CMakeLists.txt`; the Solidity end is run manually against the pinned
> checkout (step 4). Its `operator_cost` is the authoritative operator fee; its
> `l1_cost` is a **cross-reference only** (`GasPriceOracle.getL1Fee` uses the
> unsigned-tx **+68** convention, see `DIVERGENCES.md` and
> `solidity_l1_uint32_overflow`).

## Grid input contract: `block_time` and slots 5/6

Every grid case pins `block_time: 0`, and the comparability of the four ends
rests on that. Op-geth is the only end that CONSUMES the field: both of its cost
functions take the block time and use it to gate fork activation (Ecotone →
Fjord → Isthmus → Jovian) and the Regolith L1-fee path
(`core/types/rollup_cost.go`). The other three ends take no timestamp at all —
FISCO selects an `OpForkSpec` from the grid's `fork` tag (`run_fisco.cpp`),
op-revm selects an `OpSpecId` the same way (`run_oprevm/src/main.rs`), and the
Solidity end sets GasPriceOracle's fork flags from the tag
(`gen_solidity_grid.py` → `OperatorFeeCheck.t.sol`); each of those runners
carries a comment naming the input subset it consumes. With `block_time: 0`
against a config that activates every listed fork at time 0, the time-based and
tag-based selections agree, which is why the snapshots compare. A future row
with a NON-ZERO `block_time` is therefore only fully defined for the op-geth
end: either keep new rows at 0, or give the other ends a time input first.

Slots 5/6 (Bedrock-era overhead/scalar) are single-ended in the same way:
`run_fisco.cpp` reads them (zero-fill when absent, promotable if a future row
carries them), but no case does, and the other ends' readers take only slots
1/3/7/8 (op-geth warns "unexpected slot ... (ignored)"). A row that starts
carrying 5/6 would therefore be FISCO-only until the other ends grow the same
inputs — the schema test requires 1/3/7/8 and does not forbid extras.

## Current four-source status

All 16 grid cases agree **exactly** on FISCO, op-geth and op-revm (verified
value-level; JSON byte layout differs by serializer: jsoncpp puts a space
before `:` in the FISCO file). The Solidity end's `operator_cost` is the
authoritative operator fee and matches the same 16 values; its `l1_cost` is a
cross-reference only (see `DIVERGENCES.md` and
`solidity/OperatorFeeCheck.t.sol` — `getL1Fee` uses the unsigned-tx +68
convention and panics for scalars ≥ 2^28, see `solidity_l1_uint32_overflow`).

## PINNED REFERENCES

The reference sources are pinned to specific local checkouts so the committed
`golden/` snapshots stay reproducible. **Before regenerating any snapshot,
verify each local checkout has not drifted from the pinned commit** — a changed
checkout silently changes the expected output.

| Source | Local checkout | Pinned commit / version |
|---|---|---|
| op-revm | `<optimism monorepo>/rust/op-revm` | goldens generated at `da197e45ed44b9fca258b3b0d0709e8dfca1c7cd`; the enforced pin (`tools/.ci/check-ci-pins.sh` Axis 2, `extract.sh`'s `OP_REVM_PIN`) is `5f90f749caea14398554afb75062f7111b1fc554`. **Reconciled:** `rust/` is byte-identical between the two revisions (`git diff da197e45 5f90f749 -- rust/` is empty), so the goldens stand unchanged at the enforced pin. **Cargo.lock caveat (review F12, re-verified 2026-10-12):** the lock was generated under the `[patch]` path override, so its op-revm entry carries no `source` and `cargo build --locked` does NOT hold; and `5f90f749` is no longer fetchable from `ethereum-optimism/optimism` (upload-pack: "not our ref" — the ref it lived on was deleted/rewritten upstream), so the lock cannot be regenerated against the enforced pin as written. The fetchable successor is `da197e45` (byte-identical `rust/`); regenerating requires cloning it, pointing `Cargo.toml`'s `rev` there in the same change as the pin chain (`check-ci-pins.sh` + `extract.sh` + the oracle json), and network access to crates.io — until then treat the lock as a record of the resolved tree, not an enforceable exact pin |
| contracts-bedrock | `<optimism monorepo>/packages/contracts-bedrock` | `da197e45ed44b9fca258b3b0d0709e8dfca1c7cd` (same optimism monorepo) |
| op-geth | — (build-tagged) | v1.101702.2 (already recorded above) |

op-revm and contracts-bedrock both live in the same optimism monorepo, so they share one
pinned commit. Verify with:

```bash
git -C <optimism monorepo>/rust/op-revm rev-parse HEAD
git -C <optimism monorepo>/packages/contracts-bedrock rev-parse HEAD
```

Both must print `da197e45ed44b9fca258b3b0d0709e8dfca1c7cd`.

> **Pin reconciliation (closed):** the goldens were generated at `da197e45…` while the
> enforced op-revm oracle pin is `5f90f749…`; `rust/` is byte-identical at the two
> revisions, so no regeneration is pending — the enforced pin IS the goldens' tree.

## known_divergence

The canonical machine-readable tag list is `known_divergences.json`: it is loaded
by `compare_snapshots.py`, read at runtime by `run_opgeth`, and mirrored by
`OpDaMatrixSchemaTest.cpp`'s `c_divergences` (review F17). `DIVERGENCES.md` is its
prose companion. Only `switch_karst` (karst_alias) is marked in the grid;
`l1_fee_saturation` and `flz_zero_clamp` are registered but not triggered by
any current row. The Solidity-side `solidity_l1_uint32_overflow` is a new
finding from the four-source comparison (GasPriceOracle.getL1Fee uint32
overflow), affecting only the Solidity L1 cross-reference, not the operator fee.
