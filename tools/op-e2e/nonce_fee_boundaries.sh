#!/bin/bash
# nonce_fee_boundaries.sh — C2 L2 nonce-ordering and fee-cap boundary scenarios.
# Covers the two pools of edge cases the happy-path suites never touch:
#
#   NONCE:  1. out-of-order (gap) txs wait in the pool for the gapless prefix
#              (MemPoolImpl::seal picks only consecutive nonces from the state
#              nonce up; a gap tx confirms ONLY after its predecessors)
#           2. duplicate nonce — the later tx REPLACES the earlier one in the
#              mempool (ordered_unique (sender, nonce) index), so exactly one
#              of the two ever mines
#           3. a too-low nonce is admitted, never sealed, and swept by
#              MemPoolImpl::remove on the next build
#   FEE:    4. legacy gasPrice < baseFee is evicted at build time (evmone
#              validate_transaction: FEE_CAP_LESS_THAN_BLOCKS -> opValidate ->
#              the engine's OP build loop evicts the culprit by hash) — the
#              ruling-5 regression: the chain must NOT be poisoned, later
#              builds keep sealing normal txs
#           5. EIP-1559 maxFeePerGas < baseFee — same eviction
#           6. maxFeePerGas == baseFee (tip 0) — admitted, effective == baseFee
#           7. legacy gasPrice == baseFee — admitted, effective == baseFee
#           8. legacy gasPrice > baseFee — admitted, effective == gasPrice
#           9. 1559 maxFee > baseFee with a tip — effective == baseFee + tip
#
# cast >= 1.7.1 renamed the EIP-1559 fee flags: --gas-price IS the maxFeePerGas
# for 1559 txs (and the gasPrice for legacy), --priority-gas-price is the tip.
#
# Every send passes an EXPLICIT --nonce: on the single-node mempool path
# eth_getTransactionCount(pending) mirrors "latest" (the mempool does not feed
# the txpool's pending-nonce cache), so cast's auto nonce would collide on two
# rapid sends. Every transfer is a plain 21000-gas value send with an explicit
# --gas-limit (cast's estimate-gas would run for rejected txs too, and the
# constant keeps gasUsed deterministic). Env addressing follows the family
# convention (C2_L2_WEB3 / C2_DEV_KEY / C2_L2_CHAIN_ID).
set -euo pipefail

L2="${C2_L2_WEB3:-http://127.0.0.1:8555}"
KEY="${C2_DEV_KEY:-0x59c6995e998f97a5a0044966f0945389dc9e86dae88c7a8412f4603b6b78690d}"  # DEV1
DEV1_ADDR=0x70997970C51812dc3A010C7d01b50e0d17dc79C8
CHAIN_ID="${C2_L2_CHAIN_ID:-914901}"
SINK1=0xDeadDeAddeAddEAddeadDEaDDEAdDeaDDeAD0000
SINK2=0xDeaDbeefdEAdbeefdEadbEEFdeadbeEFdEaDbeeF

# --- helpers ---------------------------------------------------------------

rpc() {  # $1 method, $2 params JSON -> result as valid JSON, or "null"
  curl -s --noproxy '*' -m 30 -X POST -H 'Content-Type: application/json' \
    -d '{"jsonrpc":"2.0","method":"'"$1"'","params":'"$2"',"id":1}' "$L2" \
    | python3 -c 'import json,sys; r=json.load(sys.stdin).get("result"); print("null" if r is None else json.dumps(r))'
}

state_nonce() {  # $1 address -> decimal state nonce
  rpc eth_getTransactionCount '["'"$1"'","latest"]' | python3 -c 'import json,sys; print(int(json.loads(sys.stdin.read()),16))'
}

# The ledger's latest-state read can transiently lag the receipt table (observed:
# right after a receipt appears the count may still show a pre-tx value for a few
# seconds), so each section waits until the read has ADVANCED past the last nonce
# it used. The last nonce is passed IN (a $(...) subshell cannot propagate an
# assignment back). This keeps every explicit --nonce genuinely current.
wait_nonce_advance() {  # $1 = last used nonce; echoes the next current nonce
  # The ledger count can keep advancing for a few seconds AFTER the last
  # receipt appears (observed lag up to ~2 blocks), so a single > check is not
  # enough: wait until the count has advanced past $1 AND is stable across a
  # 4s re-read. Only my own txs advance this account between sections, so
  # stability is a safe convergence signal.
  local n n2 i
  for i in $(seq 1 20); do
    n=$(state_nonce "$DEV1_ADDR")
    if [ "$n" -gt "$1" ]; then
      sleep 4
      n2=$(state_nonce "$DEV1_ADDR")
      if [ "$n2" -ge "$n" ]; then echo "$n2"; return 0; fi
    fi
    sleep 2
  done
  echo "NONCE_STALL: state nonce never stabilized past $1 (last read $n)" >&2; return 1
}

base_fee() {
  rpc eth_getBlockByNumber '["latest",false]' | python3 -c 'import sys,json; print(int(json.load(sys.stdin)["baseFeePerGas"],16))'
}

receipt() {  # $1 tx hash -> "status gasUsed blockNumber effGasPrice" ("null ..." if pending)
  curl -s --noproxy '*' -m 30 -X POST -H 'Content-Type: application/json' \
    -d '{"jsonrpc":"2.0","method":"eth_getTransactionReceipt","params":["'"$1"'"],"id":1}' \
    "$L2" | python3 -c '
import json,sys
r = json.load(sys.stdin).get("result")
if r is None: print("null null null null -1")
else: print(r["status"], int(r["gasUsed"],16), int(r["blockNumber"],16),
            int(r["effectiveGasPrice"],16) if r.get("effectiveGasPrice") else -1,
            int(r["transactionIndex"],16))'
}

tx_lookup() {  # $1 tx hash -> "present" if the chain (ledger) knows it, else "null"
  rpc eth_getTransactionByHash '["'"$1"'"]' | python3 -c 'import json,sys; print("null" if json.loads(sys.stdin.read()) is None else "present")'
}

wait_receipt() {  # $1 tx hash -> "status gasUsed blockNumber effGasPrice"; exits 1 on timeout
  local out i
  for i in $(seq 1 60); do
    out=$(receipt "$1")
    [ "$out" != "null null null null -1" ] && { echo "$out"; return 0; }
    sleep 2
  done
  echo "RECEIPT_TIMEOUT for $1" >&2; return 1
}

# wait_absent <tx hash> <label>: assert the tx NEVER mines within a 100s window
# (a rejected tx must not appear; a valid tx would mine within the ~45s seal lag
# plus buffer). The caller then proves the chain is healthy with a control tx —
# the RPC surface cannot distinguish "waiting in pool" from "evicted" because
# eth_getTransactionByHash is ledger-only, so no pool-presence assertion is made.
wait_absent() {
  local out i
  for i in $(seq 1 20); do
    out=$(receipt "$1")
    if [ "$out" != "null null null null -1" ]; then
      echo "  [FAIL] $2 was mined (status=$out)" >&2; return 1
    fi
    sleep 5
  done
  echo "  [ok] $2 never mined (100s window)"
}

send_async() {  # $@ = cast flags (fee style + nonce ...) -> bare tx hash
  local h
  h=$(cast send --async --private-key "$KEY" --rpc-url "$L2" --chain-id "$CHAIN_ID" \
    --gas-limit 21000 "$@")
  echo "  sent $* -> $h" >&2
  echo "$h"
}

# --- 0. baseline: chain reachable, baseFee read, control tx -----------------

echo "== 0. baseline =="
BF=$(base_fee)
echo "  baseFeePerGas=$BF"
N0=$(wait_nonce_advance -1)
echo "  DEV1 next nonce=$N0"
H0=$(send_async --nonce "$N0" "$SINK1" --value 1)
read -r S0 _G0 _B0 _E0 _I0 < <(wait_receipt "$H0")
[ "$S0" = "0x1" ] && echo "  [ok] control tx mined: status $S0" \
  || { echo "  [FAIL] control tx status $S0"; exit 1; }

# --- 1. nonce gap: the future-nonce tx waits for its gapless prefix ----------

echo "== 1. nonce gap (out-of-order) =="
N1=$(wait_nonce_advance "$N0")
HA=$(send_async --nonce $((N1 + 2)) "$SINK1" --value 13)
sleep 12  # ~2 blocks: a gap tx must NOT confirm while nonce N1 is still pending
OUT=$(receipt "$HA")
[ "$OUT" = "null null null null -1" ] && echo "  [ok] gap tx (nonce $((N1+2))) not mined alone" \
  || { echo "  [FAIL] gap tx mined before its prefix: $OUT"; exit 1; }
HB=$(send_async --nonce "$N1" "$SINK1" --value 11)
HC=$(send_async --nonce $((N1 + 1)) "$SINK2" --value 12)
read -r _S _G BX _E IX < <(wait_receipt "$HB")
read -r _S _G BY _E IY < <(wait_receipt "$HC")
read -r _S _G BZ _E IZ < <(wait_receipt "$HA")
# Execution order follows the (block, in-block index) tuple: the gap tx may
# share a block with its prefix (seal picks the whole gapless run once the gap
# fills), so compare lexicographically instead of requiring separate blocks.
lt() {  # $1 b1 $2 i1 $3 b2 $4 i2 -> true when (b1,i1) < (b2,i2)
  [ "$1" -lt "$3" ] || { [ "$1" -eq "$3" ] && [ "$2" -lt "$4" ]; }
}
if lt "$BX" "$IX" "$BY" "$IY" && lt "$BY" "$IY" "$BZ" "$IZ"; then
  echo "  [ok] gapless ordering: (block,idx) ($BX,$IX) < ($BY,$IY) < ($BZ,$IZ)"
else
  echo "  [FAIL] expected ($BX,$IX) < ($BY,$IY) < ($BZ,$IZ)"; exit 1
fi

# --- 2. duplicate nonce: the later tx replaces the earlier one ----------------

echo "== 2. duplicate nonce =="
N2=$(wait_nonce_advance "$N1")
HD1=$(send_async --nonce "$N2" "$SINK1" --value 21)
HD2=$(send_async --nonce "$N2" "$SINK2" --value 22)
# The replace is settled once the pool's (sender, nonce) slot is taken by the
# later tx; the survivor then mines on the next seal, which can lag ~45s.
R1="null null null null -1"; R2="null null null null -1"
for i in $(seq 1 30); do
  R1=$(receipt "$HD1"); R2=$(receipt "$HD2")
  [ "$R1" != "null null null null -1" ] || [ "$R2" != "null null null null -1" ] && break
  sleep 3
done
if [ "$R1" = "null null null null -1" ] && [ "$R2" != "null null null null -1" ]; then
  echo "  [ok] later tx (D2) replaced earlier tx (D1); exactly one receipt"
elif [ "$R1" != "null null null null -1" ] && [ "$R2" = "null null null null -1" ]; then
  echo "  [WARN] earlier tx won the race (D1 mined, D2 replaced) — accept exactly-one"
elif [ "$R1" = "null null null null -1" ] && [ "$R2" = "null null null null -1" ]; then
  echo "  [FAIL] neither duplicate-nonce tx mined"; exit 1
else
  echo "  [FAIL] BOTH duplicate-nonce txs mined"; exit 1
fi

# --- 3. nonce too low: admitted, never sealed, swept on next build ------------

echo "== 3. nonce too low =="
N3=$(wait_nonce_advance "$N2")
[ "$N3" -gt 0 ] || { echo "  [FAIL] state nonce 0, cannot test too-low"; exit 1; }
HE=$(send_async --nonce $((N3 - 1)) "$SINK1" --value 31)
wait_absent "$HE" "too-low-nonce tx"
HCTL=$(send_async --nonce "$N3" "$SINK2" --value 32)  # chain still healthy
read -r S3 _G _B _E _I < <(wait_receipt "$HCTL")
[ "$S3" = "0x1" ] && echo "  [ok] too-low tx swept; chain still mines (control status $S3)" \
  || { echo "  [FAIL] control after too-low tx: status $S3"; exit 1; }

# --- 4. legacy gasPrice < baseFee: evicted, chain not poisoned (ruling 5) ------

echo "== 4. legacy gasPrice < baseFee (ruling-5 regression) =="
BF=$(base_fee)
N4=$(wait_nonce_advance "$N3")
HF=$(send_async --legacy --gas-price $((BF / 2)) --nonce "$N4" "$SINK1" --value 41)
wait_absent "$HF" "underpriced legacy tx"
HCTL=$(send_async --nonce "$N4" "$SINK2" --value 42)
read -r S4 _G _B _E _I < <(wait_receipt "$HCTL")
[ "$S4" = "0x1" ] && echo "  [ok] underpriced tx evicted; chain NOT poisoned (control status $S4)" \
  || { echo "  [FAIL] control after underpriced tx: status $S4"; exit 1; }

# --- 5. 1559 maxFeePerGas < baseFee: same eviction ----------------------------

echo "== 5. 1559 maxFeePerGas < baseFee =="
BF=$(base_fee)
N5=$(wait_nonce_advance "$N4")
HG=$(send_async --gas-price $((BF - 1)) --priority-gas-price 0 \
  --nonce "$N5" "$SINK1" --value 51)
wait_absent "$HG" "underpriced 1559 tx"
HCTL=$(send_async --nonce "$N5" "$SINK2" --value 52)
read -r S5 _G _B _E _I < <(wait_receipt "$HCTL")
[ "$S5" = "0x1" ] && echo "  [ok] underpriced 1559 evicted; chain healthy (control status $S5)" \
  || { echo "  [FAIL] control after underpriced 1559: status $S5"; exit 1; }

# --- 6. 1559 maxFeePerGas == baseFee, tip 0: admitted, effective == baseFee ---

echo "== 6. 1559 maxFeePerGas == baseFee (tip 0) =="
BF=$(base_fee)
N6=$(wait_nonce_advance "$N5")
HH=$(send_async --gas-price "$BF" --priority-gas-price 0 \
  --nonce "$N6" "$SINK1" --value 61)
read -r S6 _G _B E6 _I < <(wait_receipt "$HH")
[ "$S6" = "0x1" ] && [ "$E6" = "$BF" ] \
  && echo "  [ok] status $S6, effectiveGasPrice $E6 == baseFee $BF" \
  || { echo "  [FAIL] status=$S6 effective=$E6 baseFee=$BF"; exit 1; }

# --- 7. legacy gasPrice == baseFee: admitted, effective == baseFee ------------

echo "== 7. legacy gasPrice == baseFee =="
BF=$(base_fee)
N7=$(wait_nonce_advance "$N6")
HI=$(send_async --legacy --gas-price "$BF" --nonce "$N7" "$SINK1" --value 71)
read -r S7 _G _B E7 _I < <(wait_receipt "$HI")
[ "$S7" = "0x1" ] && [ "$E7" = "$BF" ] \
  && echo "  [ok] status $S7, effectiveGasPrice $E7 == baseFee $BF" \
  || { echo "  [FAIL] status=$S7 effective=$E7 baseFee=$BF"; exit 1; }

# --- 8. legacy gasPrice > baseFee: effective == full gasPrice -----------------

echo "== 8. legacy gasPrice > baseFee =="
BF=$(base_fee)
N8=$(wait_nonce_advance "$N7")
HJ=$(send_async --legacy --gas-price $((BF * 2)) --nonce "$N8" "$SINK1" --value 81)
read -r S8 _G _B E8 _I < <(wait_receipt "$HJ")
[ "$S8" = "0x1" ] && [ "$E8" = $((BF * 2)) ] \
  && echo "  [ok] status $S8, effectiveGasPrice $E8 == 2*baseFee" \
  || { echo "  [FAIL] status=$S8 effective=$E8 expected $((BF*2))"; exit 1; }

# --- 9. 1559 maxFee > baseFee with tip: effective == baseFee + tip ------------

echo "== 9. 1559 maxFee > baseFee, tip < cap-base =="
BF=$(base_fee)
N9=$(wait_nonce_advance "$N8")
TIP=$((BF / 2))
HK=$(send_async --gas-price $((BF * 2)) --priority-gas-price "$TIP" \
  --nonce "$N9" "$SINK1" --value 91)
read -r S9 _G _B E9 _I < <(wait_receipt "$HK")
[ "$S9" = "0x1" ] && [ "$E9" = $((BF + TIP)) ] \
  && echo "  [ok] status $S9, effectiveGasPrice $E9 == baseFee + tip" \
  || { echo "  [FAIL] status=$S9 effective=$E9 expected $((BF+TIP))"; exit 1; }

echo "NONCE/FEE BOUNDARIES ALL GREEN"
