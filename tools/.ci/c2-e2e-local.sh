#!/usr/bin/env bash
# c2-e2e-local.sh — one-command local C2 full-stack e2e runner (CONTEST=0 smoke
# by default; CONTEST=1 for the adversarial leg, same as nightly CI).
#
# Recipe provenance: first green local run on this machine 2026-10-10
# (fix/5615-t01, 528s e2e budget, exit 0). The pieces below are what that run
# needed; keep this file in sync when the CI pin or the harness layout moves.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

# --- paths (all overridable) -------------------------------------------------
# Harness: a CLEAN worktree at the CI pin (the sibling clone
# ~/octo/code/op-stack-e2e-tests carries local WIP — do not point this at it
# without a `git status` check; refresh via `git -C "$HARNESS_DIR" fetch origin`
# then checkout the ref c2-e2e.yml's `ref:` line carries).
HARNESS_DIR="${HARNESS_DIR:-/tmp/op-e2e-harness}"
# Monorepo: reuse the existing development clone (saves the 1–2 GB re-clone the
# runner's default would do). Its dirty contracts-bedrock patches (isContext
# try/catch for the op-deployer Go script host) are LIVE and must survive the
# checkout — stash+pop around it.
OP_MONOREPO="${OP_MONOREPO:-$HOME/octo/code/blockchain-impl/optimism}"
FISCO_BIN="${FISCO_BIN:-${REPO_ROOT}/build/fisco-bcos-air/fisco-bcos}"
CONTEST="${CONTEST:-0}"

# --- preflight ---------------------------------------------------------------
[ -x "$FISCO_BIN" ] || { echo "missing fisco binary: $FISCO_BIN (build first)"; exit 1; }
command -v yq >/dev/null || { echo "yq missing (brew install yq)"; exit 1; }
command -v forge >/dev/null || { echo "forge missing (foundryup)"; exit 1; }
command -v just >/dev/null || { echo "just missing (brew/cargo install just)"; exit 1; }
python3 -c "import yaml, eth_hash, trie, rlp" 2>/dev/null \
  || { echo "python deps missing (pip3 install -r tools/.ci/c2-e2e-requirements.txt)"; exit 1; }
# Port 8549/8749/8755/9745 must be free; leftover processes: `pkill -f anvil` etc.

# Monorepo must sit on the harness's versions.json pin — the CI gate enforces
# the same pairing, and the deployed contract bytecode rides the monorepo commit.
PIN="$(python3 -c "import json; print(json.load(open('$HARNESS_DIR/tools/op-e2e/versions.json'))['op_monorepo']['commit'])")"
HEAD="$(git -C "$OP_MONOREPO" rev-parse HEAD)"
[ "$HEAD" = "$PIN" ] || echo "WARN: monorepo HEAD ${HEAD:0:12} != harness pin ${PIN:0:12} — setup_c2's enforced version check will abort"

# L2 contract deps: on this network a plain github clone dies with HTTP/2
# stream CANCEL; clone with the DNS-pinned resolver instead when missing:
#   git -c http.version=HTTP/1.1 -c 'http.curloptResolve=github.com:443:140.82.112.4' \
#     clone --filter=blob:none <repo>
# (fetch-l2-contract-deps.sh does the rest once the three lib/ dirs exist.)

# SKIP_OP_BUILD=1: the OP trio is already built under .ci-c2-bins after the
# first run; drop it to rebuild (e.g. after a monorepo checkout change).
exec env REPO_ROOT="$REPO_ROOT" OP_E2E_DIR="$HARNESS_DIR" OP_MONOREPO="$OP_MONOREPO" \
  FISCO_BIN="$FISCO_BIN" CONTEST="$CONTEST" SKIP_OP_BUILD="${SKIP_OP_BUILD:-1}" \
  bash tools/.ci/c2-e2e.sh "$@"
