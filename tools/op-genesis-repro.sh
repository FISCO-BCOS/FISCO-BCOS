#!/usr/bin/env bash
# Copyright (c) FISCO-BCOS, Apache-2.0
#
# One command from a clean checkout to the OP genesis fragment ([alloc.*] +
# [eth_genesis_header] + [op_fork_timestamps]) of the pinned op-deployer genesis:
#
#   bash tools/op-genesis-repro.sh                                  # op-deployer accounts only
#   bash tools/op-genesis-repro.sh --chain-config my-chain.yaml     # plus the chain's predeploys
#   bash tools/op-genesis-repro.sh -o my.ini                        # default ./op-genesis.ini
#
# tools/.ci/op-e2e-harness.pin pins the op-stack-e2e-tests commit (the same one the c2-e2e
# CI jobs check out) and the sha256 of the op-deployer genesis the harness commits at that
# commit (tools/opstack-genesis/op-fork-base-allocs.json, chain
# 914901, every OP fork active at genesis). The harness is checked out into
# .ci-op-e2e-tests, the CI path; set OP_E2E_DIR to reuse another checkout of the pin.
#
# Chain-specific predeploys (bcos-l2-contracts) enter the genesis the OP way, as alloc
# entries; the harness's tools/opstack-genesis/build-allocs.py merges whatever its chain
# config lists. So:
#   * without --chain-config the in-repo converter runs on the raw op-deployer genesis
#     (build-allocs.py is skipped);
#   * with --chain-config (harness format, see its chain-config.template.yaml; the
#     template's 0x...dEaD owner and feature_flags must be replaced) build-allocs.py
#     merges the predeploys onto that genesis and the converter re-roots and re-hashes the
#     merged accounts. The converter aborts unless the merge keeps every genesis account
#     unchanged. The chain config and the local forge build are NOT pinned.
# That needs the forge build of bcos-l2-contracts
# (bash tools/.ci/fetch-l2-contract-deps.sh && (cd bcos-l2-contracts && forge build)).
# Python deps: tools/.ci/c2-e2e-requirements.txt.
#
# What this does NOT reproduce: the genesis a c2-e2e RUN boots. That one is minted per run
# (`op-deployer apply` against a fresh anvil L1, header timestamp taken from anvil), so it
# is not a pinned artifact and has no fixed hash to compare against.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PIN="${REPO_ROOT}/tools/.ci/op-e2e-harness.pin"
OP_E2E_DIR="${OP_E2E_DIR:-${REPO_ROOT}/.ci-op-e2e-tests}"
OPGEN="${OP_E2E_DIR}/tools/opstack-genesis"

die() { echo "[op-genesis-repro] ERROR: $*" >&2; exit 1; }

CHAIN_CONFIG=""
OUT="${PWD}/op-genesis.ini"
while [ $# -gt 0 ]; do
  case "$1" in --chain-config|-o) [ $# -ge 2 ] || die "$1 needs a value" ;; esac
  case "$1" in
    --chain-config) CHAIN_CONFIG="$(cd "$(dirname "$2")" && pwd)/$(basename "$2")"; shift 2 ;;
    -o) OUT="$2"; shift 2 ;;
    *) die "unknown argument: $1" ;;
  esac
done

COMMIT="$(sed -n 's/^commit=//p' "$PIN")"
EXPECTED_SHA="$(sed -n 's/^base_genesis_sha256=//p' "$PIN")"
[[ "$COMMIT" =~ ^[0-9a-f]{40}$ ]] || die "no commit=<40 hex> in $PIN"
[[ "$EXPECTED_SHA" =~ ^[0-9a-f]{64}$ ]] || die "no base_genesis_sha256=<64 hex> in $PIN"

if [ "$(git -C "$OP_E2E_DIR" rev-parse HEAD 2>/dev/null || true)" != "$COMMIT" ]; then
  echo "[op-genesis-repro] checking out op-stack-e2e-tests@${COMMIT:0:12} into $OP_E2E_DIR"
  mkdir -p "$OP_E2E_DIR"
  git -C "$OP_E2E_DIR" init -q
  git -C "$OP_E2E_DIR" fetch -q --depth 1 \
    https://github.com/FISCO-BCOS/op-stack-e2e-tests "$COMMIT"
  git -C "$OP_E2E_DIR" checkout -q FETCH_HEAD
fi
git -C "$OP_E2E_DIR" diff --quiet HEAD || die "$OP_E2E_DIR has local changes; the pin would not hold"

BASE_GENESIS="${OPGEN}/op-fork-base-allocs.json"
ACTUAL_SHA="$(python3 -c 'import hashlib,sys; print(hashlib.sha256(open(sys.argv[1],"rb").read()).hexdigest())' "$BASE_GENESIS")"
[ "$ACTUAL_SHA" = "$EXPECTED_SHA" ] || die "$BASE_GENESIS sha256 $ACTUAL_SHA != pinned $EXPECTED_SHA"

ALLOC_ARGS=()
if [ -n "$CHAIN_CONFIG" ]; then
  MERGED="$(mktemp -t op-genesis-allocs.XXXXXX)"
  trap 'rm -f "$MERGED"' EXIT
  python3 "${OPGEN}/build-allocs.py" --config "$CHAIN_CONFIG" \
    --contracts "${REPO_ROOT}/bcos-l2-contracts" --base-allocs "$BASE_GENESIS" \
    --out /dev/null --out-json "$MERGED"
  ALLOC_ARGS=(--alloc "$MERGED")
fi

python3 "${REPO_ROOT}/tools/op-genesis-to-allocs.py" --genesis "$BASE_GENESIS" \
  "${ALLOC_ARGS[@]+"${ALLOC_ARGS[@]}"}" --name "op-e2e@${COMMIT:0:12}" -o "$OUT"
grep -E '^    (state_root|hash)=' "$OUT"
