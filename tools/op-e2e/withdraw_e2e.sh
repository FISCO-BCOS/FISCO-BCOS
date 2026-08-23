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

DEV1=0x70997970C51812dc3A010C7d01b50e0d17dc79C8
KEY=0x59c6995e998f97a5a0044966f0945389dc9e86dae88c7a8412f4603b6b78690d  # DEV1
L1=http://127.0.0.1:8549
L2=http://127.0.0.1:8555

L1_BEFORE=$(cast balance "$DEV1" --rpc-url "$L1")
echo "DEV1 L1 before: $L1_BEFORE"
TX=$(cast send 0x4200000000000000000000000000000000000016 \
  "initiateWithdrawal(address,uint256,bytes)" "$DEV1" 100000 0x --value 1ether \
  --private-key "$KEY" --rpc-url "$L2" --chain-id 914901 --json \
  | python3 -c 'import json,sys; print(json.load(sys.stdin)["transactionHash"])')
echo "withdrawal tx: $TX"

python3 withdraw_claim.py "$TX" --wait-finalized 2400

L1_AFTER=$(cast balance "$DEV1" --rpc-url "$L1")
echo "DEV1 L1 after:  $L1_AFTER"
python3 - "$L1_BEFORE" "$L1_AFTER" <<'PY'
import sys
before, after = int(sys.argv[1]), int(sys.argv[2])
delta = after - before
assert 10**18 - 5 * 10**16 <= delta <= 10**18, f"unexpected L1 delta {delta}"
print(f"BALANCE ASSERT OK: delta={delta} wei (~1 ETH minus gas)")
PY

# No clock warps were used, so the sequencer must still accept txs after the
# full claim — this is the regression guard for the 裁决 8 failure mode.
cast send "$DEV1" --value 0.001ether --private-key "$KEY" \
  --rpc-url "$L2" --chain-id 914901 > /dev/null
echo "POST-CLAIM TX OK — sequencer healthy after claim (no clock warps used)"
echo "E2E ALL GREEN"
