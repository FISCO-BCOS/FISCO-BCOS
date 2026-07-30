#!/usr/bin/env bash
# MPT Lazy-Build acceptance gate runner (M10.5).
#
# Maps every SURVIVING item of the MPT snapshot-storage design spec's §11 acceptance list
# (internal design doc "mpt-snapshot-storage-design3", 2026-04-24; the preheat items were
# replaced by its 2026-07-09 slot-level revision, the half-commit item retired 2026-07-30
# in favor of single-WriteBatch UT tripwires) to a concrete executable check, runs them
# one by one, prints a spec-vs-check table and exits 0/1.
#
# The build dir must be configured with -DMPT_ACCEPTANCE_GATES=ON — otherwise the armed
# full-scale latency gates are not registered and their row fails via --no-tests=error.
#
# Two release-readiness entry points (both required before a release):
#   1. ctest -L acceptance --output-on-failure     # full-scale latency gates, fine-grained
#   2. tools/mpt_acceptance_gate.sh <build-dir>    # this script: §11 cross-reference + exit code
#
# The script is a thin ctest/git-grep orchestrator on purpose: a compiled runner would
# duplicate ctest's test selection and add a build target for zero extra checking power.
#
# Usage: tools/mpt_acceptance_gate.sh [build-dir]   (default: ./build)
#   --strict     count PENDING items (spec items whose implementing PR is still in flight)
#                as failures — use for the actual release decision.
set -u

BUILD_DIR="build"
STRICT=0
for arg in "$@"; do
    case "$arg" in
        --strict) STRICT=1 ;;
        *) BUILD_DIR="$arg" ;;
    esac
done
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT" || exit 2
if [ ! -f "$BUILD_DIR/CTestTestfile.cmake" ]; then
    echo "error: '$BUILD_DIR' is not a CTest build directory (run from repo root or pass the build dir)" >&2
    exit 2
fi

LOG_DIR="$(mktemp -d "${TMPDIR:-/tmp}/mpt-acceptance-gate.XXXXXX")"
PASS=0; FAIL=0; PENDING=0; SKIP=0
declare -a TABLE

# run_ctest <spec-ref> <description> <ctest-regex>
# --no-tests=error turns "0 tests matched" into FAIL — a renamed suite can never
# silently turn its §11 row green.
run_ctest() {
    local ref="$1" desc="$2" regex="$3"
    local log="$LOG_DIR/$(echo "$ref" | tr ' #/' '__-').log"
    echo "--- [$ref] ctest -R '$regex'"
    if ctest --test-dir "$BUILD_DIR" -R "$regex" --no-tests=error --output-on-failure \
        >"$log" 2>&1; then
        PASS=$((PASS + 1)); TABLE+=("$ref|GATE|$desc|PASS")
    else
        FAIL=$((FAIL + 1)); TABLE+=("$ref|GATE|$desc|FAIL")
        echo "    FAILED — log: $log"; tail -20 "$log" | sed 's/^/    /'
    fi
}

# mark <spec-ref> <mode: PENDING|SKIP> <description> <note>
mark() {
    local ref="$1" mode="$2" desc="$3"
    case "$mode" in
        PENDING) PENDING=$((PENDING + 1)) ;;
        SKIP) SKIP=$((SKIP + 1)) ;;
    esac
    TABLE+=("$ref|$mode|$desc|$mode")
}

# ---- §11 items, in spec order --------------------------------------------------------------

# #1 scenario B 100-block replay. Degraded form: synthetic deterministic 100-block replay
# with per-block from-scratch oracle (MPTSyntheticReplayTest.cpp documents why real mainnet
# blocks are not replayable offline). Real-vector fidelity: op-geth-anchored genesis tests.
run_ctest "§11 #1" "scenario B 100-block replay, per-block root == from-scratch oracle (synthetic)" \
    "MPTSyntheticReplaySuite"

# #2 scenario B eth_getProof independently verified.
run_ctest "§11 #2" "scenario B eth_getProof verifies against independent verifier" \
    "EthGetProofIntegrationSuite/ScenarioB_AllAllocAccountsProve"

# #3 scenario A activation: stateRoot vs independent oracle, first-touch transition.
# (Single-process oracle equivalence; a true multi-node consistency run is a deployment test.)
run_ctest "§11 #3" "scenario A activation transition + oracle-equal stateRoot" \
    "BaselineSchedulerMPTL1UpgradeSuite"

# #4 dormant account -> AccountNotInMPT (-32004). Two independent rows on purpose: an
# alternation regex would stay green if one of the two tests were renamed away.
run_ctest "§11 #4a" "eth_getProof(dormant) -32004 — integration (real ledger + verifier)" \
    "EthGetProofIntegrationSuite/ScenarioA_ActiveProofVerifies_DormantReturns32004"
run_ctest "§11 #4b" "eth_getProof(dormant) -32004 — RPC endpoint unit" \
    "EthGetProofRpcTest/DormantAccountReturns32004"

# #5 cold slot of a touched account -> SlotNotInMPT with flat-KV value.
mark "§11 #5" "PENDING" "eth_getProof(touched account, cold slot) -> SlotNotInMPT (in-flight branch feat/mpt-proof-slot-not-in-mpt; current code returns a 0x0 exclusion-style value)"

# #6 first-touch storageRoot commits only the touched slots.
run_ctest "§11 #6" "first-touch storageRoot covers ONLY slots written this block" \
    "BaselineSchedulerMPTL1UpgradeSuite/DormantAccountFirstTouchCommitsTouchedRowsOnly"

# #7 + #8 latency gates: big-account first-touch within subsequent-touch magnitude, and
# subsequent-touch p99 < 200ms at the 1000-account scale. One armed acceptance run covers both
# (MPTSubsequentTouchP99Gate.cpp).
run_ctest "§11 #7+#8" "latency gates: subsequent-touch p99 < 200ms; big first-touch <= magnitude" \
    "acceptance/MPTSubsequentTouchP99Gate"

# #9 legacy chain (flags off) unaffected. Representative tripwires here; the FULL suite runs
# in every per-PR CI (plain ctest), which is the real coverage for this item.
run_ctest "§11 #9" "legacy XOR chain unaffected (representative; full suite = per-PR CI)" \
    "FullChainFixtureSmokeSuite/XorChainOneBlockRoundTrip|TestExecuteStateRootRegression"

# #10 commitTrie purity: 1000 repeats byte-identical.
run_ctest "§11 #10" "commitTrie 1000x repeat byte-identical" "HashBuilderPuritySuite"

# #11 zero AccountDelta materialization ("git grep AccountDelta" — comment mentions that
# describe the DELETED design are tolerated; any code identifier is a failure).
echo "--- [§11 #11] git grep AccountDelta (non-comment hits)"
HITS="$(git grep -n "AccountDelta" -- '*.cpp' '*.h' | grep -vE ':[0-9]+: *(//|\*|/\*)' || true)"
if [ -z "$HITS" ]; then
    PASS=$((PASS + 1)); TABLE+=("§11 #11|GATE|no AccountDelta materialization in code|PASS")
else
    FAIL=$((FAIL + 1)); TABLE+=("§11 #11|GATE|no AccountDelta materialization in code|FAIL")
    echo "$HITS" | sed 's/^/    /'
fi

# #12 sizeof(TrieNode) <= 64 — compile-time static_assert; verify the assert exists (it fails
# the build itself if violated, so presence == enforcement).
echo "--- [§11 #12] static_assert(sizeof(TrieNode) <= 64) present"
if git grep -q "static_assert(sizeof(TrieNode) <= 64" -- '*.cpp' '*.h'; then
    PASS=$((PASS + 1)); TABLE+=("§11 #12|GATE|static_assert(sizeof(TrieNode) <= 64) enforced at build|PASS")
else
    FAIL=$((FAIL + 1)); TABLE+=("§11 #12|GATE|static_assert(sizeof(TrieNode) <= 64) enforced at build|FAIL")
fi

# #13 scenario B historical eth_call vs MPTReadView oracle.
mark "§11 #13" "PENDING" "eth_call with historical block tag (in-flight branch feat/mpt-eth-call-blocktag; MPTAccount M13.1 is merged, the RPC->scheduler path is not)"

# #14 half-commit detection — RETIRED 2026-07-30: the single-WriteBatch commit made the
# crash window structurally impossible (see the durability section of
# docs/mpt/mpt-deploy-runbook.md). The surviving enforcement is the tripwire suite below.
run_ctest "§11 #14" "RETIRED half-commit item -> single-WriteBatch tripwires" \
    "TestRocksDBStorage2/mergeAppliesSourcesInArgumentOrder|TestMPTSchedulerWiring/commitAtomicityAndObserverTiming|TestExecuteStateRootRegression/executeWritesStayInViewUntilCommit"

# #15 PR file/line constraints — enforced by the pre-commit hook on every PR, not runnable here.
mark "§11 #15" "SKIP" "PR file/line constraints (pre-commit hook enforces per PR)"

# ---- report --------------------------------------------------------------------------------
LINE="$(printf '%.0s=' {1..118})"
echo
echo "$LINE"
printf '%-10s %-8s %-85s %s\n' "spec" "mode" "check" "result"
printf '%.0s-' {1..118}; echo
for row in "${TABLE[@]}"; do
    IFS='|' read -r ref mode desc result <<<"$row"
    printf '%-10s %-8s %-85.85s %s\n' "$ref" "$mode" "$desc" "$result"
done
echo "$LINE"
echo "Acceptance summary: $PASS passed, $FAIL failed, $PENDING pending, $SKIP skipped (manual)"
echo "Logs: $LOG_DIR"

EXIT=0
if [ "$FAIL" -gt 0 ]; then
    echo "*** GATE FAILED — DO NOT RELEASE ***"
    EXIT=1
elif [ "$PENDING" -gt 0 ]; then
    if [ "$STRICT" -eq 1 ]; then
        echo "*** GATE INCOMPLETE (--strict): $PENDING spec item(s) still pending — DO NOT RELEASE ***"
        EXIT=1
    else
        echo "*** GATES PASSED, but $PENDING spec item(s) pending in-flight PRs (use --strict for the release decision) ***"
    fi
else
    echo "*** GATE PASSED — release readiness confirmed ***"
fi
exit $EXIT
