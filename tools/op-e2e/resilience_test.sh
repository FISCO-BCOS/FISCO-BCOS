#!/bin/bash
# resilience_test.sh — operational resilience triad (op-e2e actions/ parity).
#
# Three scenarios on a LIVE devnet: hard-kill a component mid-operation,
# restart it correctly, assert the chain self-heals — heads resume advancing
# and nothing regresses. This converts our hardest-won operational knowledge
# into assertions:
#   1. batcher:  kill -9 + restart MUST use </dev/null + disown (a plain
#                background job gets SIGHUP-killed ~60s later) and safe head
#                must resume advancing.
#   2. FISCO:    restart MUST keep the same data dir — the process comes back,
#                op-node reconnects, the head advances and finalized never
#                regresses. (Wiping the data dir of a running chain is the
#                documented anti-pattern: the restarted op-node drops the
#                finalized-era batches and finalized pins at 0 forever.)
#   3. op-node:  restart re-derives from L1; head resumes, safe/finalized do
#                not regress below their pre-kill values.
#
# Env addressing matches the tool family: C2_L1_RPC / C2_L2_WEB3 / C2_OP_NODE
# for observation, plus C2 (workspace with the binaries + configs, default
# /tmp/c2), FISCO_BIN, and the setup_c2.sh port set (defaults = shared C2;
# point at the c2b range for the isolated instance).
set -uo pipefail   # NOT -e: scenarios must run their own asserts and report

C2="${C2:-/tmp/c2}"
FISCO_BIN="${FISCO_BIN:-/Users/octopus/octo/code/FISCO-BCOS/build/fisco-bcos-air/fisco-bcos}"
ANVIL_PORT="${ANVIL_PORT:-8549}"
FISCO_WEB3="${FISCO_WEB3:-8555}"
FISCO_ENGINE="${FISCO_ENGINE:-8566}"
OP_NODE_PORT="${OP_NODE_PORT:-9545}"
OP_BATCHER_PORT="${OP_BATCHER_PORT:-8547}"
OP_NODE_EXTRA_FLAGS="${OP_NODE_EXTRA_FLAGS:-}"
OP_NODE="${C2_OP_NODE:-http://127.0.0.1:$OP_NODE_PORT}"
L2="${C2_L2_WEB3:-http://127.0.0.1:$FISCO_WEB3}"

PASS=0; FAIL=0
DIR="$(cd "$(dirname "$0")" && pwd)"
ok()   { echo "  [ok] $*"; PASS=$((PASS+1)); }
bad()  { echo "  [FAIL] $*"; FAIL=$((FAIL+1)); }
section() { echo; echo "=== $* ==="; }

# ---- observation helpers (c2lib CLI: one home for the family's polling idioms) ----
heads() {  # prints "<unsafe> <safe> <finalized>" from op-node syncStatus
  python3 "$DIR/c2lib.py" heads --url "$OP_NODE"
}
field() { heads | awk -v f="$1" "{print \$(f+1)}"; }

wait_advance() {  # $1 field name (unsafe/safe), $2 baseline, $3 delta, $4 timeout_s
  python3 "$DIR/c2lib.py" wait-advance --url "$OP_NODE" --field "$1" \
    --baseline "$2" --delta "$3" --timeout "$4"
}

# ---- restart functions (mirror setup_c2.sh's invocations exactly) ----
start_batcher() {
  local key; key=$(cast wallet private-key --mnemonic \
    "test test test test test test test test test test test junk" --mnemonic-index 2)
  nohup "$C2/op-batcher" \
    --l1-eth-rpc http://127.0.0.1:$ANVIL_PORT \
    --l2-eth-rpc http://127.0.0.1:$FISCO_WEB3 \
    --rollup-rpc http://127.0.0.1:$OP_NODE_PORT \
    --private-key "$key" \
    --max-channel-duration "${BATCHER_MAX_CHANNEL:-1}" \
    --rpc.port "$OP_BATCHER_PORT" \
    --log.level debug \
    < /dev/null > "$C2/op-batcher.log" 2>&1 &
  echo $! > "$C2/op-batcher.pid"; disown
}
start_op_node() {
  nohup "$C2/op-node" \
    --rollup.config "$C2/rollup.json" \
    --rollup.l1-chain-config "$C2/l1_chain_config.json" \
    --l1 http://127.0.0.1:$ANVIL_PORT \
    --l2 http://127.0.0.1:$FISCO_ENGINE \
    --l2.jwt-secret "$C2/fisco/jwt.hex" \
    --l2.enginekind geth \
    --l1.beacon.ignore \
    --sequencer.enabled \
    --sequencer.l1-confs 1 \
    --p2p.sequencer.key "$(cat "$C2/sequencer.key")" \
    --rpc.port "$OP_NODE_PORT" \
    $OP_NODE_EXTRA_FLAGS \
    --log.level info \
    --log.format json \
    < /dev/null > "$C2/op-node.log" 2>&1 &
  echo $! > "$C2/op-node.pid"; disown
}
start_fisco() {
  ulimit -s 65520
  ( cd "$C2/fisco" && nohup "$FISCO_BIN" -c config.genesis -g config.genesis \
      < /dev/null > nohup.out 2>&1 & echo $! > "$C2/fisco/node.pid" )
}

# ===================== scenario 1: batcher =====================
section "S1 batcher: kill -9, restart with disown, safe resumes"
SAFE0=$(field 1); echo "  baseline safe=$SAFE0"
kill -9 "$(cat "$C2/op-batcher.pid" 2>/dev/null)" 2>/dev/null
sleep 2
pgrep -f "$C2/op-batcher" > /dev/null && bad "batcher still alive after kill -9" \
  || ok "batcher killed"
start_batcher
sleep 3
pgrep -f "$C2/op-batcher" > /dev/null && ok "batcher restarted (pid $(cat "$C2/op-batcher.pid"))" \
  || { bad "batcher failed to restart"; echo "RESULT: $PASS passed, $FAIL failed"; exit 1; }
# SIGHUP trap guard: the historical bug killed a plain background batcher ~60s
# after launch; the disown restart must outlive that window.
sleep 70
pgrep -f "$C2/op-batcher" > /dev/null && ok "batcher survived 70s (SIGHUP trap guard)" \
  || bad "batcher died within 70s — SIGHUP trap regression"
if wait_advance safe "$SAFE0" 3 240; then ok "safe advanced +3 after batcher restart"
else bad "safe did not advance after batcher restart (baseline $SAFE0, now $(field 1))"; fi

# ===================== scenario 2: FISCO =====================
section "S2 FISCO: restart on the SAME data dir, head advances, finalized never regresses"
read -r U0 S0 F0 <<< "$(heads)"; echo "  baseline unsafe=$U0 safe=$S0 finalized=$F0"
kill "$(cat "$C2/fisco/node.pid" 2>/dev/null)" 2>/dev/null
# Liveness must be scoped by PID file, not a name pattern: the cmdline has no
# workspace path, and sibling devnets run identically-named processes.
for i in $(seq 1 20); do kill -0 "$(cat "$C2/fisco/node.pid" 2>/dev/null)" 2>/dev/null || break; sleep 1; done
kill -0 "$(cat "$C2/fisco/node.pid" 2>/dev/null)" 2>/dev/null \
  && bad "FISCO still alive after kill" || ok "FISCO stopped"
start_fisco
sleep 5
curl -s --noproxy '*' -m 5 -X POST -H 'Content-Type: application/json' \
  -d '{"jsonrpc":"2.0","method":"eth_chainId","params":[],"id":1}' "$L2" | grep -q result \
  && ok "FISCO web3 back up" || bad "FISCO web3 not answering after restart"
if wait_advance unsafe "$U0" 3 300; then ok "L2 head advanced +3 after FISCO restart (op-node reconnected)"
else bad "head did not advance after FISCO restart (baseline $U0, now $(field 0))"; fi
F1=$(field 2)
[ "${F1:--1}" -ge "$F0" ] 2>/dev/null && ok "finalized did not regress ($F0 -> $F1)" \
  || bad "finalized regressed ($F0 -> $F1)"

# ===================== scenario 3: op-node =====================
section "S3 op-node: restart re-derivation, heads resume, finalized never regresses"
read -r U0 S0 F0 <<< "$(heads)"; echo "  baseline unsafe=$U0 safe=$S0 finalized=$F0"
kill "$(cat "$C2/op-node.pid" 2>/dev/null)" 2>/dev/null
# Scope liveness to THIS workspace's rollup config — sibling devnets run
# identically-named op-node processes with their own rollup.json paths.
for i in $(seq 1 20); do pgrep -f "op-node --rollup.config $C2/rollup.json" > /dev/null || break; sleep 1; done
pgrep -f "op-node --rollup.config $C2/rollup.json" > /dev/null \
  && bad "op-node still alive after kill" || ok "op-node stopped"
start_op_node
for i in $(seq 1 24); do  # RPC back up within ~2min
  curl -s --noproxy '*' -m 3 -X POST -H 'Content-Type: application/json' \
    -d '{"jsonrpc":"2.0","method":"optimism_syncStatus","params":[],"id":1}' "$OP_NODE" | grep -q '"result"' && break
  sleep 5
done
curl -s --noproxy '*' -m 3 -X POST -H 'Content-Type: application/json' \
  -d '{"jsonrpc":"2.0","method":"optimism_syncStatus","params":[],"id":1}' "$OP_NODE" | grep -q '"result"' \
  && ok "op-node RPC back up" || bad "op-node RPC not answering after restart"
if wait_advance unsafe "$U0" 3 300; then ok "L2 head advanced +3 after op-node restart (sequencer resumed)"
else bad "head did not advance after op-node restart (baseline $U0, now $(field 0))"; fi
read -r _ S1 F1 <<< "$(heads)"
[ "${F1:--1}" -ge "$F0" ] 2>/dev/null && ok "finalized did not regress ($F0 -> $F1)" \
  || bad "finalized regressed ($F0 -> $F1)"
[ "${S1:--1}" -ge "$S0" ] 2>/dev/null && ok "safe did not regress ($S0 -> $S1)" \
  || bad "safe regressed ($S0 -> $S1)"

# ===================== summary =====================
section "RESULT: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ] && echo "RESILIENCE ALL GREEN" || { echo "RESILIENCE FAILED"; exit 1; }
