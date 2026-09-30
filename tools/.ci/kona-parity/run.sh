#!/usr/bin/env bash
# Copyright (c) FISCO-BCOS, Apache-2.0
#
# kona-client block parity gate (ADR 0007): the FISCO sequencer produces N blocks carrying a
# user deposit, an EIP-7702 tx, a call into the overlay SystemConfig and a reverting call; then
# for every block b kona-host --native derives b from L1 on top of FISCO's block b-1 and must
# reach op-node's optimism_outputAtBlock(b). The first difference stops the gate.
#
# Usage:
#   bash tools/.ci/kona-parity/run.sh [--blocks N] [--attach] [--mutate fee-vault]
#
#   --blocks N          minimum gated blocks (default 12); the range grows to cover the mix
#   --attach            use a running harness devnet (C2 workspace + setup_c2.sh port vars)
#                       instead of starting a throwaway one; refused (exit 2) when the
#                       sequencer's L1 origin trails the L1 head too far for a deposit to land
#   --mutate fee-vault  negative control: FISCO_BIN was built after mutate-fee-vault.sh; the
#                       gate must go red (exit 1) at the first fee-paying block
#
# Env: OVERLAY=on|off (default on; honoured by the harness once ticket 12 lands, verified here
#      against the chain either way), FISCO_BIN, BIN_DIR, OP_E2E_DIR, OP_MONOREPO, REPO_ROOT,
#      ANVIL_BIN, KONA_HOST_BIN (native binary) or KONA_HOST_IMAGE (default: pins.json),
#      KONA_L2_RPC (default: FISCO web3), KONA_TIMEOUT (seconds per block, default 900),
#      KONA_TRACE=1 (kona-host -vvvvv: counts debug_dbGet code hints, much larger logs),
#      DEPOSIT_BUDGET (seconds a deposit may take to reach L2, default 300), WORK.
#
# Exit: 0 every block matches, 1 first divergence (both sides' roots printed),
#       2 the gate could not produce evidence ("missing: ..." says what). Never 0 by skipping.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="${REPO_ROOT:-$(cd "${HERE}/../../.." && pwd)}"
CMP="${HERE}/compare.py"
BLOCKS=12 ATTACH=0 MUTATE=""
# L1 block time. It equals rollup.json block_time (2 on the harness devnet): the sequencer
# advances its L1 origin by at most one L1 block per L2 block, so a faster L1 makes the origin
# lag, and with it the deposit latency, grow for the whole life of the devnet.
ANVIL_BLOCK_TIME="${ANVIL_BLOCK_TIME:-2}"
while [ $# -gt 0 ]; do
  case "$1" in
    --blocks) BLOCKS="$2"; shift 2 ;;
    --attach) ATTACH=1; shift ;;
    --mutate) MUTATE="$2"; shift 2 ;;
    -h|--help) awk 'NR > 2 && /^set -euo/ { exit } NR > 2' "$0"; exit 0 ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done
OVERLAY="${OVERLAY:-on}"
[[ "$OVERLAY" =~ ^(on|off)$ ]] || { echo "OVERLAY must be on or off" >&2; exit 2; }
[[ -z "$MUTATE" || "$MUTATE" == "fee-vault" ]] || { echo "--mutate supports fee-vault" >&2; exit 2; }

log() { echo "[kona-parity] $*"; }
missing() { echo "[kona-parity] missing: $*" >&2; exit 2; }
die() { echo "[kona-parity] ERROR: $*" >&2; exit 2; }

export NO_PROXY="${NO_PROXY:-127.0.0.1,localhost}" no_proxy="${no_proxy:-127.0.0.1,localhost}"

# ── tooling ──────────────────────────────────────────────────────────────────
python3 -c 'from eth_hash.auto import keccak; keccak(b"")' 2>/dev/null \
  || missing "python eth-hash[pycryptodome] (pip install -r tools/.ci/c2-e2e-requirements.txt)"
pin() { python3 -c 'import json,sys; d=json.load(open(sys.argv[1])); print(d[sys.argv[2]][sys.argv[3]])' \
  "${HERE}/pins.json" "$1" "$2"; }
# The harness's l2_tx_scenarios.sh deploys its counter with `cast send --create`, whose flag
# order only parses on cast >= 1.7.1 (its comment at :48-50), so the forge that op-deployer
# pins (1.2.3) cannot supply the cast this gate runs: the CI jobs put a newer cast first on PATH.
command -v cast >/dev/null || missing "cast (Foundry) on PATH"
CAST_MIN="$(pin cast min_version)"
CAST_VER="$(cast --version 2>/dev/null | sed -nE 's/.*[^0-9.]([0-9]+\.[0-9]+\.[0-9]+).*/\1/p;' | sed -n 1p)"
python3 -c 'import sys; v, m = (tuple(int(x) for x in a.split(".")) for a in sys.argv[1:]); sys.exit(v < m)' \
  "${CAST_VER:-0.0.0}" "$CAST_MIN" \
  || missing "cast >= $CAST_MIN on PATH (found $(command -v cast): ${CAST_VER:-unparseable version});" \
             "the harness l2_tx_scenarios.sh needs it for cast send --create (pins.json cast)"
KONA_HOST_IMAGE="${KONA_HOST_IMAGE:-$(pin kona_host image)}"
if [ -n "${KONA_HOST_BIN:-}" ]; then
  [ -x "$KONA_HOST_BIN" ] || missing "kona-host binary at KONA_HOST_BIN=$KONA_HOST_BIN"
elif [ "$(uname)" != "Linux" ]; then
  missing "KONA_HOST_BIN (the docker fallback uses --network host, supported here on Linux only)"
else
  command -v docker >/dev/null || missing "kona-host: set KONA_HOST_BIN or install docker for $KONA_HOST_IMAGE"
  docker image inspect "$KONA_HOST_IMAGE" >/dev/null 2>&1 || docker pull -q "$KONA_HOST_IMAGE" >/dev/null \
    || missing "kona-host image $KONA_HOST_IMAGE (docker pull failed)"
fi

WORK="${WORK:-$(mktemp -d /tmp/kona-parity.XXXXXX)}"
mkdir -p "$WORK/kona"
WORK="$(cd "$WORK" && pwd -P)"
BEACON_PORT="${BEACON_PORT:-8848}"
BG_PIDS=()

teardown() {
  local rc=$?
  for p in "${BG_PIDS[@]:-}"; do [ -n "$p" ] && kill "$p" 2>/dev/null || true; done
  if [ "$ATTACH" = 0 ] && [ -n "${C2:-}" ]; then
    for f in "$C2"/op-batcher.pid "$C2"/op-node.pid "$C2"/fisco/node.pid "$C2"/anvil.pid; do
      [ -f "$f" ] && kill "$(cat "$f")" 2>/dev/null || true
    done
    pgrep -f "$C2" | xargs kill 2>/dev/null || true
  fi
  log "evidence kept in $WORK (rc=$rc)"
  exit "$rc"
}
trap teardown EXIT INT TERM

# ── devnet: throwaway (default) or attached ──────────────────────────────────
if [ "$ATTACH" = 1 ]; then
  : "${C2:?--attach needs C2=<harness workspace holding rollup.json, l1_chain_config.json, state.json>}"
  ANVIL_PORT="${ANVIL_PORT:-8549}" FISCO_WEB3="${FISCO_WEB3:-8555}" OP_NODE_PORT="${OP_NODE_PORT:-9545}"
else
  OP_E2E_DIR="${OP_E2E_DIR:-${REPO_ROOT}/.ci-op-e2e-tests}"
  OP_MONOREPO="${OP_MONOREPO:-${REPO_ROOT}/.ci-op-monorepo}"
  BIN_DIR="${BIN_DIR:-${REPO_ROOT}/.ci-c2-bins}"
  FISCO_BIN="${FISCO_BIN:-${REPO_ROOT}/build/fisco-bcos-air/fisco-bcos}"
  ANVIL_BIN="${ANVIL_BIN:-anvil}"
  [ -f "${OP_E2E_DIR}/tools/op-e2e/setup_c2.sh" ] \
    || missing "op-stack-e2e-tests checkout at $OP_E2E_DIR (commit: pins.json op_stack_e2e_tests)"
  # Until ticket 12 lands in the harness, setup_c2.sh does not read OVERLAY and every devnet
  # carries the overlay: refuse OVERLAY=off before spending minutes on a devnet that the code
  # check below would reject anyway. workflow.yml runs the same test to skip the leg outright.
  if [ "$OVERLAY" = off ] && ! grep -qw OVERLAY "${OP_E2E_DIR}/tools/op-e2e/setup_c2.sh"; then
    missing "harness overlay switch (ticket 12): setup_c2.sh at $OP_E2E_DIR does not read OVERLAY"
  fi
  for b in op-deployer op-node op-batcher; do
    [ -x "$BIN_DIR/$b" ] || missing "$BIN_DIR/$b (build it as tools/.ci/c2-e2e.sh does)"
  done
  [ -x "$FISCO_BIN" ] || missing "FISCO_BIN=$FISCO_BIN (build with -DWITH_L2_CONTRACTS=ON)"
  [ -d "${REPO_ROOT}/bcos-l2-contracts/out" ] || missing "bcos-l2-contracts/out (forge build)"
  command -v "$ANVIL_BIN" >/dev/null || missing "anvil (foundry >= v1.8.0, pins.json anvil)"
  C2="$WORK/c2"
  mkdir -p "$C2"
  for b in op-deployer op-node op-batcher; do cp "$BIN_DIR/$b" "$C2/$b"; chmod +x "$C2/$b"; done
  ANVIL_PORT=8849 FISCO_WEB3=8855 OP_NODE_PORT=9845
  # Started here, not by setup_c2.sh, so the gate controls the anvil version: setup_c2.sh
  # skips its own anvil when the port already answers. Flags copy its anvil command line.
  "$ANVIL_BIN" --port "$ANVIL_PORT" --chain-id 900900 \
    --mnemonic "test test test test test test test test test test test junk" \
    --block-time "$ANVIL_BLOCK_TIME" --slots-in-an-epoch 1 > "$C2/anvil.log" 2>&1 &
  echo $! > "$C2/anvil.pid"
  for _ in $(seq 1 30); do cast chain-id --rpc-url "http://127.0.0.1:$ANVIL_PORT" >/dev/null 2>&1 && break; sleep 1; done
  log "starting the harness devnet in $C2 (OVERLAY=$OVERLAY)"
  C2="$C2" ANVIL_PORT=$ANVIL_PORT FISCO_WEB3=$FISCO_WEB3 FISCO_ENGINE=8866 FISCO_RPC=22313 \
  FISCO_P2P=33500 OP_NODE_PORT=$OP_NODE_PORT OP_BATCHER_PORT=8847 \
  OP_NODE_EXTRA_FLAGS="--p2p.disable" BATCHER_MAX_CHANNEL=2 OVERLAY="$OVERLAY" \
  FISCO_BIN="$FISCO_BIN" OPGEN="${OP_E2E_DIR}/tools/opstack-genesis" MONOREPO="$OP_MONOREPO" \
  L2CONTRACTS="${REPO_ROOT}/bcos-l2-contracts" FISCO_REPO="$REPO_ROOT" \
    bash "${OP_E2E_DIR}/tools/op-e2e/setup_c2.sh" > "$WORK/setup_c2.log" 2>&1 \
    || { tail -40 "$WORK/setup_c2.log" >&2; die "harness setup_c2.sh failed (log: $WORK/setup_c2.log)"; }
fi
L1="http://127.0.0.1:$ANVIL_PORT"
L2="http://127.0.0.1:$FISCO_WEB3"
OPN="http://127.0.0.1:$OP_NODE_PORT"
KONA_L2="${KONA_L2_RPC:-$L2}"
for f in rollup.json l1_chain_config.json state.json; do [ -f "$C2/$f" ] || missing "$C2/$f"; done
cp "$C2/rollup.json" "$WORK/rollup.json"
# kona reads a bare alloy ChainConfig (kona_genesis::L1ChainConfig = alloy_genesis::ChainConfig);
# the harness file wraps it in {"config": ...} for op-node.
python3 -c 'import json,sys; d=json.load(open(sys.argv[1])); json.dump(d.get("config", d), open(sys.argv[2], "w"))' \
  "$C2/l1_chain_config.json" "$WORK/l1-config.json"

log "genesis hash: $(cast block 0 --field hash --rpc-url "$L2")"
log "rollup.json forks: $(python3 -c 'import json,sys; r=json.load(open(sys.argv[1])); print({k: v for k, v in r.items() if k.endswith("_time")})' "$WORK/rollup.json")"
CODE=$(cast code 0x4200000000000000000000000000000000001000 --rpc-url "$L2")
if [ "$OVERLAY" = on ] && [ "$CODE" = 0x ]; then
  die "OVERLAY=on but the overlay SystemConfig 0x4200…1000 has no code"
elif [ "$OVERLAY" = off ] && [ "$CODE" != 0x ]; then
  missing "harness overlay switch (ticket 12): OVERLAY=off requested but 0x4200…1000 has code"
fi
L2_BLOCK_TIME=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["block_time"])' "$WORK/rollup.json")
python3 "$CMP" origin-lag --op-node "$OPN" --l2-block-time "$L2_BLOCK_TIME" \
  --budget "${DEPOSIT_BUDGET:-300}" || exit 2
[ -n "$MUTATE" ] && log "negative control: FISCO_BIN must carry mutate-fee-vault.sh; expecting red"

python3 "$CMP" preflight --l1 "$L1" --l2 "$KONA_L2" || exit 2

GENESIS_TS=$(cast block 0 --field timestamp --rpc-url "$L1")
python3 "$CMP" beacon-stub --port "$BEACON_PORT" --genesis-time "$GENESIS_TS" \
  --seconds-per-slot "$ANVIL_BLOCK_TIME" 2> "$WORK/beacon-stub.log" &
BG_PIDS+=($!)
for _ in $(seq 1 20); do curl -sf "http://127.0.0.1:$BEACON_PORT/eth/v1/config/spec" >/dev/null && break; sleep 0.5; done

# ── drive the transaction mix ────────────────────────────────────────────────
DEV1_KEY=0x59c6995e998f97a5a0044966f0945389dc9e86dae88c7a8412f4603b6b78690d  # owns the overlay SystemConfig
# 7702 authority: a fresh key per run. A fixed account keeps the previous run's delegation
# (code 0xef0100<counter>) on an --attach devnet, and the funding transfer then executes the
# counter's code and reverts.
read -r K9 K9_KEY < <(cast wallet new --json \
  | python3 -c 'import json,sys; w=json.load(sys.stdin)[0]; print(w["address"], w["private_key"])') \
  || die "cast wallet new failed"
CHAIN_ID=$(cast chain-id --rpc-url "$L2")
START=$(( $(cast block-number --rpc-url "$L2") + 1 ))
PORTAL=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["opChainDeployments"][0]["OptimismPortalProxy"])' "$C2/state.json")
log "driving the mix from L2 block $START"
OP_E2E_DIR="${OP_E2E_DIR:-${REPO_ROOT}/.ci-op-e2e-tests}"
SCEN=$(C2_L2_WEB3="$L2" C2_DEV_KEY="$DEV1_KEY" C2_L2_CHAIN_ID="$CHAIN_ID" \
  bash "${OP_E2E_DIR}/tools/op-e2e/l2_tx_scenarios.sh") || { echo "$SCEN" >&2; die "l2_tx_scenarios.sh failed"; }
COUNTER=$(echo "$SCEN" | sed -n 's/^  deployed at //p')
[ -n "$COUNTER" ] || die "no counter address in l2_tx_scenarios.sh output"
cast send "$K9" --value 1ether --private-key "$DEV1_KEY" --legacy --rpc-url "$L2" \
  --chain-id "$CHAIN_ID" > /dev/null || die "funding the 7702 authority failed"
# Explicit fees and gas: cast then skips eth_feeHistory and eth_estimateGas (foundry
# crates/cast/src/tx.rs:375-392); --auth <addr> self-signs the authorization at nonce+1 (:425-438).
cast send "$K9" "inc()" --auth "$COUNTER" --private-key "$K9_KEY" --gas-limit 200000 \
  --gas-price 10gwei --priority-gas-price 1gwei --rpc-url "$L2" --chain-id "$CHAIN_ID" > /dev/null \
  || die "EIP-7702 transaction was not included with status 1"
if [ "$OVERLAY" = on ]; then
  cast send 0x4200000000000000000000000000000000001000 "setValueByKey(string,uint192,uint64)" \
    block_tx_count_limit 1000 $(( START + 1000 )) --private-key "$DEV1_KEY" --legacy \
    --gas-limit 300000 --rpc-url "$L2" --chain-id "$CHAIN_ID" > /dev/null \
    || die "overlay SystemConfig.setValueByKey was not included with status 1"
fi
# The L1 deposit goes last: a deposit from DEV1 increments DEV1's L2 nonce when it lands, so
# sent earlier it can consume the nonce an L2 transaction above was already signed with, and
# that transaction then never lands (first standalone run with a 2s L1: the deposit took nonce
# 5 in block 35 and setValueByKey, signed with nonce 5, timed out).
cast send "$PORTAL" --value 1ether --private-key "$DEV1_KEY" --rpc-url "$L1" > /dev/null \
  || die "L1 deposit to OptimismPortal failed"
LAST_MIX=$(python3 "$CMP" wait-mix --l2 "$L2" --first "$START" --overlay "$OVERLAY") || exit 2
LAST=$(( START + BLOCKS - 1 ))
[ "$LAST_MIX" -gt "$LAST" ] && LAST=$LAST_MIX
log "gated range: [$START, $LAST]; waiting for op-node to mark it safe"
python3 "$CMP" collect --l2 "$L2" --op-node "$OPN" --first "$START" --last "$LAST" --out "$WORK" || exit 2
python3 "$CMP" coverage --workdir "$WORK" --overlay "$OVERLAY" || exit 2

# ── replay every block with kona-host --native ──────────────────────────────
L1_HEAD=$(cat "$WORK/l1_head.txt")
# kona-host retries a hint the L2 node cannot answer forever, so every run needs a deadline.
# macOS ships no coreutils timeout; perl's alarm (SIGALRM, exit 142) is the fallback.
if command -v timeout >/dev/null; then
  TIMEOUT=(timeout "${KONA_TIMEOUT:-900}")
elif command -v gtimeout >/dev/null; then
  TIMEOUT=(gtimeout "${KONA_TIMEOUT:-900}")
else
  TIMEOUT=(perl -e 'alarm shift; exec @ARGV or die "exec: $!"' "${KONA_TIMEOUT:-900}")
fi
kona() {
  if [ -n "${KONA_HOST_BIN:-}" ]; then
    ${TIMEOUT[@]+"${TIMEOUT[@]}"} "$KONA_HOST_BIN" "$@"
  else
    ${TIMEOUT[@]+"${TIMEOUT[@]}"} docker run --rm --network host --user "$(id -u):$(id -g)" \
      -v "$WORK:$WORK" "$KONA_HOST_IMAGE" "$@"
  fi
}
set +e
while IFS=$'\t' read -r B AGREED_HASH AGREED_ROOT CLAIMED; do
  kona --logs.stdout.format json ${KONA_TRACE:+-vvvvv} single --native \
    --l1-head "$L1_HEAD" \
    --agreed-l2-head-hash "$AGREED_HASH" --agreed-l2-output-root "$AGREED_ROOT" \
    --claimed-l2-output-root "$CLAIMED" --claimed-l2-block-number "$B" \
    --l1-node-address "$L1" --l2-node-address "$KONA_L2" \
    --l1-beacon-address "http://127.0.0.1:$BEACON_PORT" \
    --rollup-config-path "$WORK/rollup.json" --l1-config-path "$WORK/l1-config.json" \
    --data-dir "$WORK/kona/$B.kv" < /dev/null > "$WORK/kona/$B.log" 2>&1
  RC=$?
  python3 "$CMP" verdict --workdir "$WORK" --block "$B" --rc "$RC" \
    --log "$WORK/kona/$B.log" --claimed "$CLAIMED" < /dev/null || break
done < "$WORK/pairs.tsv"
set -e
EXPECT=()
[ -n "$MUTATE" ] && EXPECT=(--expect-fee-vault)
python3 "$CMP" summary --workdir "$WORK" --overlay "$OVERLAY" ${EXPECT[@]+"${EXPECT[@]}"}
