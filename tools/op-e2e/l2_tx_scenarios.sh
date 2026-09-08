#!/bin/bash
# l2_tx_scenarios.sh — C2 L2 transaction scenarios: contract CREATION and
# FAILED-tx inclusion (the two coverage gaps every other suite leaves open:
# b3_contracts only runs on the B3 single-node bed, and the withdrawal loop
# only ever asserts happy paths).
#
#   1. deploy a counter contract (creation tx, EIP-1559 default fees)
#        -> contractAddress resolves, eth_getCode non-empty
#   2. inc() twice -> counter() reads back 1 then 2 (state change through
#      the full sequencer path)
#   3. boom() (always reverts) -> the tx IS included with receipt status 0
#      and gasUsed > 0 — a failed tx must land in a block, not vanish
#   4. counter() still 2 — the revert left no state behind
#
# The counter bytecode is precompiled and embedded (solc 0.8.20):
#   uint256 public n; inc(){n+=1;} boom(){require(false,"boom");}
# — no foundry dependency at runtime. Env addressing follows the family
# convention (C2_L2_WEB3 / C2_DEV_KEY / C2_L2_CHAIN_ID).
set -euo pipefail

L2="${C2_L2_WEB3:-http://127.0.0.1:8555}"
KEY="${C2_DEV_KEY:-0x59c6995e998f97a5a0044966f0945389dc9e86dae88c7a8412f4603b6b78690d}"  # DEV1
CHAIN_ID="${C2_L2_CHAIN_ID:-914901}"

BYTECODE=0x6080604052348015600e575f5ffd5b506102158061001c5f395ff3fe608060405234801561000f575f5ffd5b506004361061003f575f3560e01c80632e52d60614610043578063371303c014610061578063a169ce091461006b575b5f5ffd5b61004b610075565b60405161005891906100ee565b60405180910390f35b61006961007a565b005b610073610094565b005b5f5481565b60015f5f82825461008b9190610134565b92505081905550565b5f6100d4576040517f08c379a00000000000000000000000000000000000000000000000000000000081526004016100cb906101c1565b60405180910390fd5b565b5f819050919050565b6100e8816100d6565b82525050565b5f6020820190506101015f8301846100df565b92915050565b7f4e487b71000000000000000000000000000000000000000000000000000000005f52601160045260245ffd5b5f61013e826100d6565b9150610149836100d6565b925082820190508082111561016157610160610107565b5b92915050565b5f82825260208201905092915050565b7f626f6f6d000000000000000000000000000000000000000000000000000000005f82015250565b5f6101ab600483610167565b91506101b682610177565b602082019050919050565b5f6020820190508181035f8301526101d88161019f565b905091905056fea2646970667358221220ba7e3e35ede08cd1edc60597958e25c65fa75444702899bab85496a98b12f93a64736f6c63430008220033

receipt() {  # $1 tx hash -> "status gasUsed" via one RPC ("null null" if pending)
  curl -s --noproxy '*' -m 30 -X POST -H 'Content-Type: application/json' \
    -d '{"jsonrpc":"2.0","method":"eth_getTransactionReceipt","params":["'"$1"'"],"id":1}' \
    "$L2" | python3 -c '
import json,sys
r = json.load(sys.stdin).get("result")
if r is None: print("null null")
else: print(r["status"], int(r["gasUsed"],16))'
}
wait_receipt() {
  local out i
  for i in $(seq 1 30); do
    out=$(receipt "$1")
    [ "$out" != "null null" ] && { echo "$out"; return 0; }
    sleep 2
  done
  echo "RECEIPT_TIMEOUT for $1" >&2; return 1
}
counter() { cast call "$1" "n()(uint256)" --rpc-url "$L2"; }

echo "== 1. contract deployment (creation tx, 1559) =="
# cast send --create <code> --json: ALL flags must precede --create. cast >= 1.7.1
# re-parses "--create" as a nested subcommand whose option set contains NO tx flags
# (--private-key/--rpc-url after it are rejected as unknown arguments), so the
# incantation below is the only ordering that works on current cast.
ADDR=$(cast send --private-key "$KEY" --rpc-url "$L2" --chain-id "$CHAIN_ID" \
  --json --create "$BYTECODE" \
  | python3 -c 'import json,sys; print(json.load(sys.stdin)["contractAddress"])')
echo "  deployed at $ADDR"
CODE=$(cast code "$ADDR" --rpc-url "$L2")
[ "${#CODE}" -gt 10 ] && echo "  [ok] eth_getCode non-empty (${#CODE} chars)" \
  || { echo "  [FAIL] code empty after deploy"; exit 1; }

echo "== 2. inc() x2 -> state change through the sequencer path =="
cast send "$ADDR" "inc()" --private-key "$KEY" --rpc-url "$L2" --chain-id "$CHAIN_ID" > /dev/null
N1=$(counter "$ADDR")
cast send "$ADDR" "inc()" --private-key "$KEY" --rpc-url "$L2" --chain-id "$CHAIN_ID" > /dev/null
N2=$(counter "$ADDR")
[ "$N1" = "1" ] && [ "$N2" = "2" ] \
  && echo "  [ok] counter 0 -> $N1 -> $N2" \
  || { echo "  [FAIL] counter expected 1 then 2, got $N1 then $N2"; exit 1; }

echo "== 3. boom() always reverts -> tx INCLUDED with status 0 =="
# cast send estimates gas first and ABORTS before broadcasting when the estimate
# reverts — an explicit --gas-limit skips the estimate so the tx actually lands.
# cast send then exits nonzero on the reverted receipt, so capture with || true and
# assert on the receipt itself — the WHOLE POINT is that the chain includes it.
TX=$( { cast send --gas-limit 200000 --private-key "$KEY" --rpc-url "$L2" \
    --chain-id "$CHAIN_ID" --json "$ADDR" "boom()" || true; } 2>/dev/null \
  | python3 -c 'import sys,re; m=re.search(r"\"transactionHash\":\"(0x[0-9a-fA-F]{64})\"", sys.stdin.read()); print(m.group(1) if m else "")')
[ -n "$TX" ] || { echo "  [FAIL] could not extract boom() tx hash"; exit 1; }
read -r STATUS GAS < <(wait_receipt "$TX")
[ "$STATUS" = "0x0" ] && [ "$GAS" -gt 0 ] 2>/dev/null \
  && echo "  [ok] failed tx included: status 0x0, gasUsed $GAS" \
  || { echo "  [FAIL] expected included status-0 receipt, got status=$STATUS gas=$GAS"; exit 1; }

echo "== 4. revert left no state behind =="
N3=$(counter "$ADDR")
[ "$N3" = "2" ] && echo "  [ok] counter still 2" \
  || { echo "  [FAIL] counter changed after revert: $N3"; exit 1; }

echo "L2 TX SCENARIOS ALL GREEN"
