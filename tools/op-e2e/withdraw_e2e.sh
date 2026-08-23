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
L1="${C2_L1_RPC:-http://127.0.0.1:8549}"
L2="${C2_L2_WEB3:-http://127.0.0.1:8555}"
CHAIN_ID="${C2_L2_CHAIN_ID:-914901}"

L1_BEFORE=$(cast balance "$DEV1" --rpc-url "$L1")
echo "DEV1 L1 before: $L1_BEFORE"
TX=$(cast send 0x4200000000000000000000000000000000000016 \
  "initiateWithdrawal(address,uint256,bytes)" "$DEV1" 100000 0x --value 1ether \
  --private-key "$KEY" --rpc-url "$L2" --chain-id "$CHAIN_ID" --json \
  | python3 -c 'import json,sys; print(json.load(sys.stdin)["transactionHash"])')
echo "withdrawal tx: $TX"

python3 withdraw_claim.py "$TX" --wait-finalized 2400

L1_AFTER=$(cast balance "$DEV1" --rpc-url "$L1")
echo "DEV1 L1 after:  $L1_AFTER"
python3 - "$L1_BEFORE" "$L1_AFTER" <<'PY'
import sys
before, after = int(sys.argv[1]), int(sys.argv[2])
delta = after - before
# 1 ETH out MINUS the 0.08 create bond, which stays in the game's credit
# ledger until claimCredit (DelayedWETH holds it 3.5 days — outside this
# window), minus L1 gas. This window was never actually reachable before
# 2026-08-24: a five-field finalize signature in the claim tool's wait loop
# had been failing every run upstream of this assert.
assert 10**18 - 9 * 10**16 <= delta <= 10**18 - 7 * 10**16, \
    f"unexpected L1 delta {delta} (want ~0.92: 1 ETH minus the held 0.08 bond minus gas)"
print(f"BALANCE ASSERT OK: delta={delta} wei (~1 ETH minus 0.08 held bond minus gas)")
PY

# No clock warps were used, so the sequencer must still accept txs after the
# full claim — this is the regression guard for the 裁决 8 failure mode.
cast send "$DEV1" --value 0.001ether --private-key "$KEY" \
  --rpc-url "$L2" --chain-id "$CHAIN_ID" > /dev/null
echo "POST-CLAIM TX OK — sequencer healthy after claim (no clock warps used)"
echo "E2E ALL GREEN"
