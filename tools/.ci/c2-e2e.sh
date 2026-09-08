#!/usr/bin/env bash
# Copyright (c) FISCO-BCOS, Apache-2.0
#
# CI entry for the C2 full-stack OP e2e (deposit + withdraw + optional contest).
# Wraps tools/op-e2e/withdraw_e2e_ephemeral.sh with pinned OP monorepo binaries.
#
# Env (all optional):
#   REPO_ROOT          — default: parent of tools/
#   OP_MONOREPO        — optimism clone dir (default: $REPO_ROOT/.ci-op-monorepo)
#   BIN_DIR            — staging dir for op-deployer/op-node/op-batcher
#   FISCO_BIN          — path to fisco-bcos-air/fisco-bcos (must exist)
#   CONTEST            — 1 (default) full adversarial leg; 0 for faster smoke
#   XDM                — 1 to run xdm_e2e after the main loop (off by default)
#   SKIP_OP_BUILD      — 1 if BIN_DIR already populated (e.g. cache restore)
#   SKIP_FISCO_BUILD   — 1 if FISCO_BIN already built by an earlier CI step
#
# Usage (from repo root after building fisco-bcos):
#   bash tools/.ci/c2-e2e.sh
set -euo pipefail

REPO_ROOT="${REPO_ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}"
VERSIONS="${REPO_ROOT}/tools/op-e2e/versions.json"
OP_MONOREPO="${OP_MONOREPO:-${REPO_ROOT}/.ci-op-monorepo}"
BIN_DIR="${BIN_DIR:-${REPO_ROOT}/.ci-c2-bins}"
FISCO_BIN="${FISCO_BIN:-${REPO_ROOT}/build/fisco-bcos-air/fisco-bcos}"
CONTEST="${CONTEST:-1}"
XDM="${XDM:-0}"

log() { echo "[c2-e2e] $*"; }
die() { echo "[c2-e2e] ERROR: $*" >&2; exit 1; }

[ -f "$VERSIONS" ] || die "missing $VERSIONS (commit tools/op-e2e first)"

read_op_pin() {
  python3 - "$VERSIONS" <<'PY'
import json, sys
v = json.load(open(sys.argv[1]))
print(v["op_monorepo"]["commit"])
PY
}

OP_COMMIT="$(read_op_pin)"
[[ "$OP_COMMIT" =~ ^[0-9a-f]{40}$ ]] || die "invalid op_monorepo.commit in versions.json"

prepare_op_monorepo_for_go_build() {
  git -C "$OP_MONOREPO" submodule update --init superchain-registry
  if ! command -v yq >/dev/null; then
    die "yq not on PATH (required to build optimism superchain-configs.zip)"
  fi
  bash "$OP_MONOREPO/op-core/superchain/sync-superchain.sh"
}

mkdir -p "$BIN_DIR"

if [[ "${SKIP_OP_BUILD:-0}" != "1" ]]; then
  if [[ ! -d "$OP_MONOREPO/.git" ]]; then
    log "cloning optimism @ ${OP_COMMIT:0:12}…"
    git clone --filter=blob:none https://github.com/ethereum-optimism/optimism "$OP_MONOREPO"
  fi
  git -C "$OP_MONOREPO" fetch --depth=1 origin "$OP_COMMIT" 2>/dev/null \
    || git -C "$OP_MONOREPO" fetch origin
  git -C "$OP_MONOREPO" checkout -q "$OP_COMMIT"
  prepare_op_monorepo_for_go_build

  log "building op-deployer, op-node, op-batcher…"
  (cd "$OP_MONOREPO" && go build -o "$BIN_DIR/op-deployer" ./op-deployer/cmd/op-deployer)
  (cd "$OP_MONOREPO" && go build -o "$BIN_DIR/op-node" ./op-node/cmd)
  (cd "$OP_MONOREPO" && go build -o "$BIN_DIR/op-batcher" ./op-batcher/cmd)
fi

for b in op-deployer op-node op-batcher; do
  [ -x "$BIN_DIR/$b" ] || die "missing $BIN_DIR/$b"
done

if [[ "${SKIP_FISCO_BUILD:-0}" != "1" ]]; then
  [ -x "$FISCO_BIN" ] || die "FISCO_BIN not found: $FISCO_BIN (build WITH_L2_CONTRACTS=ON first)"
fi

# setup_c2 / build-allocs need forge artifacts from bcos-l2-contracts.
if ! command -v forge >/dev/null; then
  die "forge not on PATH (install Foundry before running c2-e2e)"
fi
if [[ ! -d "${REPO_ROOT}/bcos-l2-contracts/out" ]]; then
  log "forge build bcos-l2-contracts (first run)…"
  (cd "${REPO_ROOT}/bcos-l2-contracts" && forge build)
fi

python3 -m pip install --quiet -r "${REPO_ROOT}/tools/.ci/c2-e2e-requirements.txt"

export NO_PROXY="${NO_PROXY:-127.0.0.1,localhost}"
export no_proxy="${no_proxy:-127.0.0.1,localhost}"

log "running withdraw_e2e_ephemeral (CONTEST=${CONTEST} XDM=${XDM})…"
BIN_DIR="$BIN_DIR" \
FISCO_BIN="$FISCO_BIN" \
OPGEN="${REPO_ROOT}/tools/opstack-genesis" \
OP_NODE_EXTRA_FLAGS="--p2p.disable" \
CONTEST="$CONTEST" \
XDM="$XDM" \
bash "${REPO_ROOT}/tools/op-e2e/withdraw_e2e_ephemeral.sh"
