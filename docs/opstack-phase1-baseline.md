# Phase 1 EVMOne Baseline — EEST v5.4.0 (first full run)

**Run date:** 2026-08-10
**EEST version:** `v5.4.0` (execution-spec-tests release; fixtures filled `--until=BPO4`,
commit `4f68564f47c7e577ad6cbb570858316f5ff0e7bb`, timestamp 2025-12-06)
**Runner:** `ethereum-executor/tests/eest-runner` on branch `feat-op-eest-baseline`
**Fixture root:** `${EVM_REF_EEST_ROOT}/fixtures` (symlinks to `evm_ref_eest_fixtures-src`)
**Command:**
`./eest-runner --fixture-dir "$EEST_FIXTURE_DIR" --json-failures /tmp/baseline-fails.json --quiet`

---

## Summary

| Metric      | Value    |
|-------------|----------|
| Total tests | 132,789  |
| Passed      | 132,789  |
| Failed      | 0        |
| Pass rate   | 100%     |
| Files       | 5,583    |
| Skipped     | 35       |
| Load failures | 0     |
| Elapsed     | 114 s    |

`baseline-fails-v540.json` (committed alongside this doc) is an **empty JSON array `[]`**.

> **All-pass finding (contradicts the plan's expectation).** The plan assumed a first
> full baseline would surface a natural failure set to triage. It does not: every
> fixture the runner executes passes. This is a *finding for the controller*, not an
> assumption — it was verified on a real full run (see below). The committed empty
> fails-JSON is the machine-comparable regression baseline: any future change that
> turns a currently-passing fixture red will appear as a new element when diffed.

---

## Scope (what "full" means)

The run covered the tree reachable from `${EVM_REF_EEST_ROOT}/fixtures`:

- `state_tests/` — 2,723 files, state-test format (default parse path)
- `blockchain_tests/` — 2,848 files, blockchain-test format (incl. 2,446 legacy
  `static/state_tests/` files that EEST emits as blockchain tests)
- `transaction_tests/` — 12 files, **skipped** (transaction format not executed)

**Engine-format口径 (Step 3).** `detectFormatFromPath` marks any path containing
`/blockchain_tests_engine/` or `/blockchain_tests_engine_x/` as `unsupported` and
`processFixtureFile` skips those files (`EESTRunner.cpp:1949-1955`, `:2039-2041`).
The `evm_ref_eest_fixtures-src` source tree *does* contain `blockchain_tests_engine/`
(2,844 files) and `blockchain_tests_engine_x/` (38,577 files), but those are **not**
symlinked into the `fixtures/` root consumed by the runner, so the full baseline
genuinely contains **no engine-format fixtures** — the "全量 BlockchainTests 不含
engine 格式" claim holds for the executed set.

---

## Skipped (35)

Two kinds, both intentional:

1. **23 unknown-fork fixtures** (blockchain tests referencing the post-Osaka
   "BPO" / Blob-Price-Ops forks), skipped by the new fork-skip logic:
   - `blockchain_tests/osaka/eip7918_blob_reserve_price/test_reserve_price_at_transition.json`
     — 22 fixtures: BPO1ToBPO2 ×5, BPO2ToBPO3 ×7, BPO3ToBPO4 ×5, OsakaToBPO1 ×5
   - `blockchain_tests/osaka/eip7918_blob_reserve_price/test_blob_base_fee_with_bpo_transition.json`
     — 1 fixture: OsakaToBPO1 ×1
   These were previously **silently executed under `EVMC_OSAKA`** (undefined
   behavior); they are now skipped with a `WARNING` and counted, never run.
2. **12 transaction_tests files** (file-level skip; transaction format unsupported).

Each skipped case prints `WARNING: skipping … unknown fork (not in
forkNameToRevision)` and is recorded in `Skipped files`; it is **not** counted in
Total/Passed/Failed (`g_totalTests` is decremented back at the skip site).

---

## By fork

All executed tests passed, so every fork row is 100%. Counts below are fixture
test-cases discovered in the tree (before skip); the 23 BPO-network rows are the
skipped set.

### state_tests (post-section fork keys)

| Fork              | Tests |
|-------------------|-------|
| Frontier          |   363 |
| Homestead         |   373 |
| Byzantium         |   454 |
| ConstantinopleFix |   463 |
| Istanbul          |   608 |
| Berlin            | 1,249 |
| London            | 1,504 |
| Paris             | 1,564 |
| Shanghai          | 1,745 |
| Cancun            |16,847 |
| Prague            |18,869 |
| Osaka             |19,517 |
| **state total**   |**63,556** |

### blockchain_tests (per-network)

| Network                    | Tests |
|----------------------------|-------|
| Frontier                   |   414 |
| Homestead                  |   424 |
| Byzantium                  |   503 |
| ConstantinopleFix          |   515 |
| Istanbul                   |   660 |
| Berlin                     | 1,301 |
| London                     | 1,557 |
| Paris                      | 1,624 |
| Shanghai                   | 1,849 |
| CancunToPragueAtTime15k    |    98 |
| ParisToShanghaiAtTime15k   |    40 |
| PragueToOsakaAtTime15k     |    86 |
| ShanghaiToCancunAtTime15k  |    60 |
| Cancun                     |17,685 |
| Prague                     |20,859 |
| Osaka                      |21,558 |
| BPO1ToBPO2AtTime15k        |     5 |  ← skipped
| BPO2ToBPO3AtTime15k        |     7 |  ← skipped
| BPO3ToBPO4AtTime15k        |     5 |  ← skipped
| OsakaToBPO1AtTime15k       |     6 |  ← skipped
| **blockchain total**       |**69,256** |

**Reconciliation:** 63,556 + 69,256 = 132,812 discovered test-cases; minus the 23
skipped = **132,789 executed = Passed**. The counters match exactly.

---

## By category

No failures → every category is 0 (balance/storage/nonce/code mismatch,
expected_exception, unexpected). `baseline-fails-v540.json` is `[]`.

---

## Triage conclusions (Step 6)

Failure set is **empty**. Classification per the plan's three categories:

- **Runner bug:** none surfaced (the runner was already validated to catch and
  categorize mismatches; this run produced zero).
- **Fixture-format gap:** the only gap is the 23 BPO transition fixtures, which are
  **not** format gaps — they are *future/unknown forks* correctly **skipped**, not
  failed. No fixture in the executed set was dropped for format reasons.
- **evmone version semantics:** no divergence observed. The executed set (all 12
  pre-Osaka forks + Osaka + Prague→Osaka transitions, plus the static legacy
  blockchain set) matches evmone state-transition expectations at 100%.

No genuine failures to attribute. The empty baseline is the regression anchor.

---

## Code changes in this task (fork map + engine口径 + baseline)

- `EESTRunner.h:214` — `forkNameToRevision` now returns
  `std::optional<evmc_revision>` (doc comment updated; was "Returns EVMC_OSAKA").
- `EESTRunner.cpp:566-612` — unknown fork → `std::nullopt` (was `EVMC_OSAKA`).
- `EESTRunner.cpp:804-848` — `configureFork` → `bool`; returns `false` when the
  fork, or either endpoint of a `XToYAtTimeT` transition, is unknown (transition
  never falls back to executing under `fromRev`).
- `EESTRunner.cpp:1324-1342`, `:1622-1642` — `runFixture` / `runBlockchainFixture`
  take a `bool* skipped` out-param; on unknown fork they print a `WARNING` and
  signal skip instead of running.
- `EESTRunner.cpp:2012-2023`, `:2065-2077` — `processFixtureFile` skip accounting:
  undo the pre-incremented `g_totalTests`, `++g_skippedFiles`, do not count as
  passed/failed.
- `EESTRunner.cpp:2167-2177` — **symlink fix**: the directory walk now uses
  `fs::recursive_directory_iterator(dir, fs::directory_options::follow_directory_symlink)`.
  The fetched EEST tree exposes `state_tests/`, `blockchain_tests/`,
  `transaction_tests/` as symlinks; the default iterator descends into none of
  them, so `--fixture-dir "$EEST_FIXTURE_DIR"` previously found **0 files** and
  the earlier "blockchain_tests is empty" report was an artifact of that. With the
  fix, the full baseline discovers 5,583 files / 132,789 tests.

### Verified commands

```bash
export EEST_FIXTURE_DIR="$(grep '^EVM_REF_EEST_ROOT' build/CMakeCache.txt | cut -d= -f2)/fixtures"
ls "$EEST_FIXTURE_DIR/state_tests"          # berlin byzantium cancun ... osaka prague shanghai static
./eest-runner --fixture-dir "$EEST_FIXTURE_DIR" --json-failures /tmp/baseline-fails.json --quiet
```

Smoke (pre-full): cancun subset `state_tests/cancun` → 4,425 tests 100% pass,
`--json-failures /tmp/smoke-cancun.json` = `[]`; BPO file → 22 skipped + 6 passed.
