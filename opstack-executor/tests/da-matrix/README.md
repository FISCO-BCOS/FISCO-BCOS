# da-matrix — FISCO opstack DA / operator-fee four-source matrix

Parameterized DA-gas / operator-fee matrix for the FISCO opstack
**Jovian DA gas / operator fee** feature. A single JSON grid
(`da_matrix.json`, the *only* input) drives four thin runners, each producing a
per-case `{id, l1_cost, operator_cost}` snapshot; the snapshots are compared
bit-for-bit and committed under `golden/`.

This directory lives in the TESTS domain (CI `TESTS=ON`); the runners are wired
into `opstack-executor/tests/CMakeLists.txt`.

## Layout

```
da-matrix/
  da_matrix.json        grid: envelopes registry + 16 cases (slots 1/3/7/8, gas, fork)
  run_fisco.cpp         FISCO C++ runner (computeL1Cost / computeChargedOperatorCost)
  run_oprevm/           op-revm 20.0.0 Rust runner (cargo +1.94 build --release)
  solidity/             Solidity authoritative end (GasPriceOracle forge test)
  golden/
    fisco/              FISCO snapshot    (out_fisco.json)
    opgeth/             op-geth snapshot  (out_opgeth.json)
    oprevm/             op-revm snapshot  (out_oprevm.json)
    solidity/           Solidity snapshot (out_solidity.json)
  DIVERGENCES.md        known_divergence registry
  README.md
```

## Running the four ends

```bash
# 1. FISCO (built target; produced by Task 3)
cd <build>/opstack-executor/tests && ./opstack-da-matrix-runner \
  --grid <this>/da_matrix.json --out out_fisco.json

# 2. op-geth (Task 4; authoritative reference v1.101702.2)
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
| op-revm | `/Users/octopus/octo/code/blockchain-impl/optimism/rust/op-revm` | `da197e45ed44b9fca258b3b0d0709e8dfca1c7cd` (version 20.0.0) |
| contracts-bedrock | `/Users/octopus/octo/code/blockchain-impl/optimism/packages/contracts-bedrock` | `da197e45ed44b9fca258b3b0d0709e8dfca1c7cd` (same optimism monorepo) |
| op-geth | — (build-tagged) | v1.101702.2 (already recorded above) |

op-revm and contracts-bedrock both live in the same optimism monorepo
(`/Users/octopus/octo/code/blockchain-impl/optimism`), so they share one pinned
commit. Verify with:

```bash
cd /Users/octopus/octo/code/blockchain-impl/optimism/rust/op-revm && git rev-parse HEAD
cd /Users/octopus/octo/code/blockchain-impl/optimism/packages/contracts-bedrock && git rev-parse HEAD
```

Both must print `da197e45ed44b9fca258b3b0d0709e8dfca1c7cd`.

## known_divergence

See `DIVERGENCES.md`. Only `switch_karst` (karst_alias) is marked in the grid;
`l1_fee_saturation` and `flz_zero_clamp` are registered but not triggered by
any current row. The Solidity-side `solidity_l1_uint32_overflow` is a new
finding from the four-source comparison (GasPriceOracle.getL1Fee uint32
overflow), affecting only the Solidity L1 cross-reference, not the operator fee.
