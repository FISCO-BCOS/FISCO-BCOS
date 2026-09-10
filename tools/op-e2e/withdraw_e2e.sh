#!/bin/bash
# withdraw_e2e.sh — full withdrawal closed loop as one script (op-e2e parity).
#
#   record L1 balance -> initiateWithdrawal 1 ETH -> wait finalized head to pass
#   the inclusion block -> withdraw_claim.py (prove -> resolve -> finalize, ALL
#   real-time waits on the compressed dispute timelines, NO clock warps) ->
#   assert the L1 balance delta -> assert the sequencer still accepts txs.
#
# The post-claim tx check is the point of the compressed-timeline design: with
# clock warps the sequencer dies ~30min later (handoff 裁决 8); with real-time
# waits it must stay healthy. Run on a C2 devnet built by setup_c2.sh (the
# intent deploys game clock 60s / maturity 12s / finality delay 6s).
#
# Preconditions: a QUIET devnet window (a mid-flight FISCO data wipe makes the
# restarted op-node drop the L1-finalized-era batches as "past batches with old
# timestamps", pinning finalized_l2 at 0 forever — the claim can then never
# run; rebuild the devnet instead). Requires cast + the python deps of
# withdraw_claim.py. Takes ~20-25 min (finalization lag dominates).
set -euo pipefail
cd "$(dirname "$0")"

# Env overrides (defaults = the /tmp/c2 C2 layout; same names as withdraw_claim.py
# and setup_c2.sh): C2_L1_RPC / C2_L2_WEB3 / C2_L2_CHAIN_ID / C2_DEV_KEY.
DEV1=0x70997970C51812dc3A010C7d01b50e0d17dc79C8
KEY="${C2_DEV_KEY:-0x59c6995e998f97a5a0044966f0945389dc9e86dae88c7a8412f4603b6b78690d}"  # DEV1
DEV0_KEY="${C2_DEPLOYER_KEY:-0xac0974bec39a17e36ba4a6b4d238ff944bacb478cbed5efcae784d7bf4f2ff80}"
L1="${C2_L1_RPC:-http://127.0.0.1:8549}"
L2="${C2_L2_WEB3:-http://127.0.0.1:8555}"
CHAIN_ID="${C2_L2_CHAIN_ID:-914901}"
STATE="${C2_STATE:-/tmp/c2/state.json}"

# Fund the portal BEFORE withdrawing. On real chains deposits continuously fund
# it; a fresh devnet portal holds 0 ETH and finalize's relay does
# SafeCall.callWithMinGas(target, gasLimit, value, data) whose inner call{value}
# then FAILS SILENTLY — finalize succeeds, WithdrawalFinalized(wh, false) is
# emitted, and the 1 ETH never lands (observed 2026-08-24: delta -0.0811 ETH).
# Plain ETH from DEV0 stands in for a prior deposit's funding; DEV0's balance
# is not asserted here so it stays out of the delta math.
PORTAL=$(python3 -c "import json;print(json.load(open('$STATE'))['opChainDeployments'][0]['OptimismPortalProxy'])")
PORTAL_BAL=$(cast balance "$PORTAL" --rpc-url "$L1")
if [ "$PORTAL_BAL" -lt 2000000000000000000 ] 2>/dev/null; then
  cast send "$PORTAL" --value 2ether --private-key "$DEV0_KEY" --rpc-url "$L1" > /dev/null
  echo "portal funded with 2 ETH (was $PORTAL_BAL wei)"
fi

# deposit_e2e credits exactly 1 ETH on L2; value=1ether also needs gas headroom on
# chains where DEV1 has no genesis L2 prefund. Use python for uint256 compares.
WITHDRAW_WEI=1000000000000000000
GAS_HEADROOM_WEI=100000000000000000
L2_BAL=$(cast balance "$DEV1" --rpc-url "$L2")
if ! python3 - "$L2_BAL" "$WITHDRAW_WEI" "$GAS_HEADROOM_WEI" <<'PY'
import sys
bal, need, head = map(int, sys.argv[1:4])
raise SystemExit(0 if bal >= need + head else 1)
PY
then
  echo "L2 gas headroom: depositing 0.1 ETH to DEV1 (balance $L2_BAL wei)"
  cast send "$PORTAL" --value 0.1ether --private-key "$KEY" --rpc-url "$L1" > /dev/null
  python3 - "$L2" "$DEV1" "$L2_BAL" <<'PY'
import json, sys, time, urllib.request
l2, dev1, before = sys.argv[1], sys.argv[2], int(sys.argv[3])
def rpc(m, p):
    req = urllib.request.Request(l2, data=json.dumps(
        {"jsonrpc": "2.0", "method": m, "params": p, "id": 1}).encode(),
        headers={"Content-Type": "application/json"})
    return json.load(urllib.request.urlopen(req, timeout=5))["result"]
deadline = time.time() + 300
while time.time() < deadline:
    bal = int(rpc("eth_getBalance", [dev1, "latest"]), 16)
    if bal >= before + 10**17:
        print(f"L2 headroom credited: {before} -> {bal} wei")
        sys.exit(0)
    time.sleep(3)
raise SystemExit("L2 headroom deposit not credited within 300s")
PY
fi

L1_BEFORE=$(cast balance "$DEV1" --rpc-url "$L1")
echo "DEV1 L1 before: $L1_BEFORE"
GAS_EST=$(cast estimate 0x4200000000000000000000000000000000000016 \
  "initiateWithdrawal(address,uint256,bytes)" "$DEV1" 100000 0x --value 1ether \
  --from "$DEV1" --rpc-url "$L2")
# shellcheck source=l2_gas.sh
source "$(dirname "$0")/l2_gas.sh"
GAS_LIMIT=$(l2_padded_gas "$GAS_EST")
TX=$(cast send 0x4200000000000000000000000000000000000016 \
  "initiateWithdrawal(address,uint256,bytes)" "$DEV1" 100000 0x --value 1ether \
  --private-key "$KEY" --rpc-url "$L2" --chain-id "$CHAIN_ID" --gas-limit "$GAS_LIMIT" --json \
  | python3 -c 'import json,sys; print(json.load(sys.stdin)["transactionHash"])')
echo "withdrawal tx: $TX (gas limit $GAS_LIMIT, estimate $GAS_EST)"
assert_l2_receipt_ok "$TX" "$L2"
echo "withdrawal receipt OK"

python3 withdraw_claim.py "$TX" --wait-finalized 2400

L1_AFTER=$(cast balance "$DEV1" --rpc-url "$L1")
echo "DEV1 L1 after:  $L1_AFTER"
python3 - "$L1_BEFORE" "$L1_AFTER" <<'PY'
import sys
before, after = int(sys.argv[1]), int(sys.argv[2])
delta = after - before
# The full round now includes bond recovery (withdraw_claim.py's two-step
# claimCredit after finalize), so the 0.08 create-bond comes BACK: the delta is
# 1 ETH minus only the gas of ~7 L1 transactions. Before recovery existed the
# window had to admit the held bond (~0.92); any credit-accounting bug now
# shows up as a delta outside [1 - 0.01, 1].
assert 10**18 - 10**16 <= delta <= 10**18, \
    f"unexpected L1 delta {delta} (want ~1 ETH minus gas; bond must be recovered)"
print(f"BALANCE ASSERT OK: delta={delta} wei (~1 ETH minus gas, bond recovered)")
PY

# No clock warps were used, so the sequencer must still accept txs after the
# full claim — this is the regression guard for the 裁决 8 failure mode.
cast send "$DEV1" --value 0.001ether --private-key "$KEY" \
  --rpc-url "$L2" --chain-id "$CHAIN_ID" > /dev/null
echo "POST-CLAIM TX OK — sequencer healthy after claim (no clock warps used)"
echo "E2E ALL GREEN"
