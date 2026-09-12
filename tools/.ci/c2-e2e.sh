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
# The e2e harness lives in FISCO-BCOS/op-stack-e2e-tests (migrated out of
# tools/op-e2e in PR #5593's follow-up); the workflow checks it out at a pinned
# ref into OP_E2E_DIR. Point OP_E2E_DIR at any harness checkout for local runs.
OP_E2E_DIR="${OP_E2E_DIR:-${REPO_ROOT}/.ci-op-e2e-tests}"
VERSIONS="${OP_E2E_DIR}/tools/op-e2e/versions.json"
OP_MONOREPO="${OP_MONOREPO:-${REPO_ROOT}/.ci-op-monorepo}"
BIN_DIR="${BIN_DIR:-${REPO_ROOT}/.ci-c2-bins}"
FISCO_BIN="${FISCO_BIN:-${REPO_ROOT}/build/fisco-bcos-air/fisco-bcos}"
CONTEST="${CONTEST:-1}"
XDM="${XDM:-0}"

log() { echo "[c2-e2e] $*"; }
die() { echo "[c2-e2e] ERROR: $*" >&2; exit 1; }

[ -f "$VERSIONS" ] || die "missing $VERSIONS (check out FISCO-BCOS/op-stack-e2e-tests at the pinned ref into $OP_E2E_DIR first)"

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

# optimism pins forge in mise.toml; newer forge runs forge-lint during build and fails.
# Scope only forge (not cast) so e2e can keep a newer cast with `send --data`.
ensure_op_forge() {
  local pin forge_dir
  pin="$(yq -r '.tools.forge' "$OP_MONOREPO/mise.toml" 2>/dev/null || true)"
  # An unreadable pin must be loud, not silently fall back to a hardcoded version:
  # the pin exists precisely because a wrong-version forge fails the op-deployer
  # build (forge-lint), and a stale fallback produces that failure far from its
  # cause. Only a missing mise.toml KEY falls back (older monorepo pins).
  if ! [[ "$pin" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    if [[ -f "$OP_MONOREPO/mise.toml" ]] && ! grep -q 'forge' "$OP_MONOREPO/mise.toml"; then
      pin="1.2.3"
      log "mise.toml has no forge pin; using fallback ${pin}"
    else
      die "cannot read the forge pin from $OP_MONOREPO/mise.toml (got '${pin}') — fix yq or the pin file"
    fi
  fi
  forge_dir="${HOME}/.foundry/versions/v${pin}"
  if [[ ! -x "${forge_dir}/forge" ]]; then
    if command -v foundryup >/dev/null; then
      log "installing forge ${pin} for op-deployer build…"
      foundryup -i "v${pin}"
    else
      die "forge ${pin} required for op-deployer build; install foundryup"
    fi
  fi
  OP_FORGE_BIN_DIR="$(cd "${forge_dir}" && pwd)"
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

  log "building op-deployer (with embedded L1 artifacts), op-node, op-batcher…"
  if command -v just >/dev/null && [ -f "$OP_MONOREPO/op-deployer/justfile" ]; then
    # Plain `go build` omits artifacts.tzst; op-deployer apply then fails at runtime.
    ensure_op_forge
    ( PATH="${OP_FORGE_BIN_DIR}:$PATH" && cd "$OP_MONOREPO/op-deployer" && just build )
    cp "$OP_MONOREPO/op-deployer/bin/op-deployer" "$BIN_DIR/op-deployer"
  else
    die "just is required to build op-deployer with embedded artifacts (install: cargo install just)"
  fi
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
  log "fetching bcos-l2-contracts Solidity deps…"
  bash "${REPO_ROOT}/tools/.ci/fetch-l2-contract-deps.sh" "$REPO_ROOT"
  log "forge build bcos-l2-contracts (first run)…"
  (cd "${REPO_ROOT}/bcos-l2-contracts" && forge build)
fi

# Python deps for the op-e2e helpers (pyyaml / eth-hash / trie / rlp). The runner's
# system python3 is PEP-668 externally-managed, so a plain `python3 -m pip install`
# fails there; mirror workflow.yml's fallback chain. A real failure still aborts.
REQS="${REPO_ROOT}/tools/.ci/c2-e2e-requirements.txt"
python3 -m pip install --quiet -r "$REQS" 2>/dev/null \
  || pip3 install --quiet -r "$REQS" 2>/dev/null \
  || pip3 install --break-system-packages --quiet -r "$REQS"
python3 -c "import yaml, eth_hash, trie, rlp" 2>/dev/null \
  || die "C2 python deps unavailable after install (pyyaml/eth-hash/trie/rlp)"

export NO_PROXY="${NO_PROXY:-127.0.0.1,localhost}"
export no_proxy="${no_proxy:-127.0.0.1,localhost}"

log "running withdraw_e2e_ephemeral (CONTEST=${CONTEST} XDM=${XDM})…"
# MONOREPO/L2CONTRACTS/FISCO_REPO are what setup_c2.sh (reached through
# withdraw_e2e_ephemeral.sh) reads; its own defaults are repo-relative, but the
# monorepo clone and the contracts dir are CI-layout specific, so pin them here.
BIN_DIR="$BIN_DIR" \
FISCO_BIN="$FISCO_BIN" \
OPGEN="${REPO_ROOT}/tools/opstack-genesis" \
MONOREPO="$OP_MONOREPO" \
L2CONTRACTS="${REPO_ROOT}/bcos-l2-contracts" \
FISCO_REPO="$REPO_ROOT" \
OP_NODE_EXTRA_FLAGS="--p2p.disable" \
CONTEST="$CONTEST" \
XDM="$XDM" \
bash "${OP_E2E_DIR}/tools/op-e2e/withdraw_e2e_ephemeral.sh"
