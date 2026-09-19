#!/usr/bin/env bash
# FISCO BCOS — Karst atomic release gate: production OP surfaces must not keep
# OpForkFlags / isJovianActive, and karstConfig must be Osaka + EIP-7825.
#
# Search with git grep -E (CI images may not have ripgrep). Fail-closed:
# match → exit 1; tool error → exit 2; no match → OK.
#
# K1 legacy fallback is intentional: opJovianActive / feature_op_jovian via
# resolveOpForkScheduleCanonical (when metadata and genesis section are absent)
# stay allowed. This gate forbids isJovianActive / OpForkFlags /
# configAt(OpForkFlags) only.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

if ! command -v git >/dev/null 2>&1; then
  echo "check-op-karst-release-gate: git is required" >&2
  exit 2
fi

FORBIDDEN='isJovianActive|OpForkFlags|configAt\(OpForkFlags\)'
SCAN_ROOTS=(
  engine
  libinitializer
  opstack-executor
  bcos-rpc
  bcos-evm
  bcos-framework
  bcos-ledger
)

# git grep -E over production sources. Pathspecs keep the same globs/exclusions
# as the former rg scan (*.h/hpp/cpp/cc/inl, skip test/tests/unittests).
git_grep_src() {
  local pattern="$1"
  local dir="$2"
  git grep -nE -e "$pattern" -- \
    ":(glob)$dir/**/*.h" \
    ":(glob)$dir/**/*.hpp" \
    ":(glob)$dir/**/*.cpp" \
    ":(glob)$dir/**/*.cc" \
    ":(glob)$dir/**/*.inl" \
    ":(exclude,glob)$dir/**/test/**" \
    ":(exclude,glob)$dir/**/tests/**" \
    ":(exclude,glob)$dir/**/unittests/**"
}

# Untracked (but not gitignored) production sources. `git grep` only sees the
# index, so a new file that was never `git add`ed would otherwise bypass the gate.
grep_untracked_src() {
  local pattern="$1"
  local dir="$2"
  local hit=1
  local f
  while IFS= read -r f; do
    [[ -z "$f" ]] && continue
    case "$f" in
      */test/*|*/tests/*|*/unittests/*) continue ;;
      *.h|*.hpp|*.cpp|*.cc|*.inl) ;;
      *) continue ;;
    esac
    if grep -nE -e "$pattern" -- "$f"; then
      hit=0
    fi
  done < <(git ls-files --others --exclude-standard -- "$dir")
  return "$hit"
}

fail=0
for dir in "${SCAN_ROOTS[@]}"; do
  if [[ ! -d "$dir" ]]; then
    echo "check-op-karst-release-gate: missing scan root: $dir" >&2
    fail=1
    continue
  fi
  # Capture status explicitly: `if git grep` would treat missing/error as "no match".
  set +e
  git_grep_src "$FORBIDDEN" "$dir"
  status=$?
  grep_untracked_src "$FORBIDDEN" "$dir"
  ut_status=$?
  set -e
  case "$status" in
    0)
      echo "check-op-karst-release-gate: forbidden identifier in $dir" >&2
      fail=1
      ;;
    1)
      ;; # no match
    *)
      echo "check-op-karst-release-gate: git grep failed in $dir (exit $status)" >&2
      exit 2
      ;;
  esac
  case "$ut_status" in
    0)
      echo "check-op-karst-release-gate: forbidden identifier in untracked $dir" >&2
      fail=1
      ;;
    1)
      ;;
    *)
      echo "check-op-karst-release-gate: untracked scan failed in $dir (exit $ut_status)" >&2
      exit 2
      ;;
  esac
done

# Production karstConfig must ship Osaka + deposit exemption (not a Jovian alias).
KARST_CFG='bcos-evm/bcos-evm/opstack/OpForkSchedule.cpp'
if [[ ! -f "$KARST_CFG" ]]; then
  echo "check-op-karst-release-gate: missing $KARST_CFG" >&2
  fail=1
else
  set +e
  karst_body="$(awk '/const OpForkConfig& karstConfig\(\) noexcept/,/^}/' "$KARST_CFG")"
  karst_status=$?
  set -e
  if [[ "$karst_status" -ne 0 ]]; then
    echo "check-op-karst-release-gate: awk failed on $KARST_CFG (exit $karst_status)" >&2
    exit 2
  fi
  if [[ -z "$karst_body" ]]; then
    echo "check-op-karst-release-gate: karstConfig() not found in $KARST_CFG" >&2
    fail=1
  else
    set +e
    printf '%s\n' "$karst_body" | grep -qE 'EVMC_OSAKA'
    osaka_status=$?
    # Basis for requiring the exemption (do NOT cite specs/protocol/karst/overview.md:20 —
    # that line does not exist): op-revm's validate_env returns Ok() for deposits before
    # reaching the baseline check that enforces revm's TxGasLimitCap
    # (op-revm/src/handler.rs:81-100; revm-handler-*/src/validation.rs:150-159) — the OP Rust
    # stack exempts deposits as FISCO does, while op-geth applies the cap to deposits
    # (core/state_transition.go:379-383). Specs are silent. See da-matrix/DIVERGENCES.md
    # `eip7825_deposit_exemption` (WI-35, closed 2026-09-13).
    printf '%s\n' "$karst_body" | grep -qE 'deposit_exempt_from_max_tx_gas = true'
    exempt_status=$?
    set -e
    case "$osaka_status" in
      0) ;;
      1)
        echo "check-op-karst-release-gate: karstConfig() must set EVMC_OSAKA" >&2
        fail=1
        ;;
      *)
        echo "check-op-karst-release-gate: grep failed checking EVMC_OSAKA (exit $osaka_status)" >&2
        exit 2
        ;;
    esac
    case "$exempt_status" in
      0) ;;
      1)
        echo "check-op-karst-release-gate: karstConfig() must set deposit_exempt_from_max_tx_gas = true" >&2
        fail=1
        ;;
      *)
        echo "check-op-karst-release-gate: grep failed checking deposit exemption (exit $exempt_status)" >&2
        exit 2
        ;;
    esac
  fi
fi

# Production OP getPayload must not serve the Eth V1-V5 window via EngineTracker.
OP_GETPAYLOAD_FILES=(
  engine/bcos-engine/OpEngineService.inl
  engine/bcos-engine/OpEngineService.h
  engine/bcos-engine/OpEngineService.cpp
)
for f in "${OP_GETPAYLOAD_FILES[@]}"; do
  if [[ ! -f "$f" ]]; then
    echo "check-op-karst-release-gate: missing $f" >&2
    fail=1
    continue
  fi
  set +e
  grep -nE 'm_tracker\.getPayload' "$f"
  tracker_status=$?
  grep -nE 'isGetPayloadVersionSupported' "$f"
  window_status=$?
  set -e
  case "$tracker_status" in
    0)
      echo "check-op-karst-release-gate: OP getPayload must not call m_tracker.getPayload ($f)" >&2
      fail=1
      ;;
    1) ;; # no match
    *)
      echo "check-op-karst-release-gate: grep failed in $f (exit $tracker_status)" >&2
      exit 2
      ;;
  esac
  case "$window_status" in
    0)
      echo "check-op-karst-release-gate: OP getPayload must not use isGetPayloadVersionSupported ($f)" >&2
      fail=1
      ;;
    1) ;; # no match
    *)
      echo "check-op-karst-release-gate: grep failed in $f (exit $window_status)" >&2
      exit 2
      ;;
  esac
done

if [[ "$fail" -ne 0 ]]; then
  echo "check-op-karst-release-gate: FAILED" >&2
  exit 1
fi

echo "check-op-karst-release-gate: OK"
