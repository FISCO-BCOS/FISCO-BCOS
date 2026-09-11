#!/usr/bin/env bash
# xdm_e2e.sh — L2→L1 cross-domain messaging e2e: the last XDM half.
#
# Modern OP-stack XDM has NO separate proof path: the L1 messenger's
# relayMessage is authorized by msg.sender == PORTAL, i.e. an L2→L1 message
# IS a withdrawal whose target is the L1CrossDomainMessenger — finalize
# executes the withdrawal and the relay happens inside it. So this leg
# composes the already-proven claim machinery:
#
#   L2 sendMessage{value}(DEV0, 0x, gas)  (internally initiates the withdrawal
#     to the L1 messenger carrying the relayMessage calldata)
#   -> withdraw_claim.py happy path (game -> prove -> resolve -> finalize;
#      finalize's target call IS the relay)
#   -> assert DEV0's L1 balance gained the value, the L2 messenger nonce
#      advanced, and finalize actually succeeded.
#
# Env: C2_L2_WEB3/C2_L1_RPC/C2_DEV_KEY (defaults = C2).
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

export NO_PROXY="${NO_PROXY:-127.0.0.1,localhost}"
export no_proxy="${no_proxy:-127.0.0.1,localhost}"

L1="${C2_L1_RPC:-http://127.0.0.1:8549}"
L2="${C2_L2_WEB3:-http://127.0.0.1:8555}"
KEY="${C2_DEV_KEY:-0x59c6995e998f97a5a0044966f0945389dc9e86dae88c7a8412f4603b6b78690d}"  # DEV1
DEV1=0x70997970C51812dc3A010C7d01b50e0d17dc79C8
CHAIN_ID="${C2_L2_CHAIN_ID:-914901}"
# Recipient of the relayed value (the XDM message target on L1):
RECIPIENT=0xf39Fd6e51aad88F6F4ce6aB8827279cffFb92266  # DEV0
L2_XDM=0x4200000000000000000000000000000000000007
AMT=0.5ether
STATE="${C2_STATE:-/tmp/c2/state.json}"
# DEV0 (== RECIPIENT) is the L1 dev account used to fund the portal.
DEV0_KEY="${C2_DEPLOYER_KEY:-0xac0974bec39a17e36ba4a6b4d238ff944bacb478cbed5efcae784d7bf4f2ff80}"

log() { echo "[xdm] $*"; }
die() { echo "[xdm] !! $*" >&2; exit 1; }

# shellcheck source=l2_gas.sh
source "$SCRIPT_DIR/l2_gas.sh"

# Fund the portal BEFORE withdrawing. On a fresh devnet the portal holds 0 ETH;
# finalize's relay is an OP SafeCall.callWithMinGas(target, gasLimit, value,
# data) whose inner call{value} then FAILS SILENTLY — finalize still succeeds
# and emits WithdrawalFinalized(wh, false), so the messenger relay never runs
# and the recipient's L1 balance does not move (delta 0). Mirrors
# withdraw_e2e.sh; funded from DEV0 BEFORE the balance baseline so the relayed
# value remains the sole delta.
PORTAL=$(python3 -c "import json;print(json.load(open('$STATE'))['opChainDeployments'][0]['OptimismPortalProxy'])")
PORTAL_BAL=$(cast balance "$PORTAL" --rpc-url "$L1")
if [ "$PORTAL_BAL" -lt 2000000000000000000 ] 2>/dev/null; then
  cast send "$PORTAL" --value 2ether --private-key "$DEV0_KEY" --rpc-url "$L1" > /dev/null
  log "portal funded with 2 ETH (was $PORTAL_BAL wei)"
fi

log "L2 sendMessage($RECIPIENT, 0x, 100000) value=$AMT via the L2 messenger"
NONCE_BEFORE=$(cast call "$L2_XDM" "messageNonce()(uint256)" --rpc-url "$L2")
BAL_BEFORE=$(cast balance "$RECIPIENT" --rpc-url "$L1")
GAS_EST=$(cast estimate "$L2_XDM" "sendMessage(address,bytes,uint32)" \
  "$RECIPIENT" 0x 100000 --value "$AMT" --from "$DEV1" --rpc-url "$L2")
GAS_LIMIT=$(l2_padded_gas "$GAS_EST")
TX=$(cast send "$L2_XDM" "sendMessage(address,bytes,uint32)" \
  "$RECIPIENT" 0x 100000 --value "$AMT" \
  --private-key "$KEY" --rpc-url "$L2" --chain-id "$CHAIN_ID" \
  --gas-limit "$GAS_LIMIT" --json \
  | python3 -c 'import json,sys; print(json.load(sys.stdin)["transactionHash"])')
log "sendMessage tx: $TX (gas limit $GAS_LIMIT, estimate $GAS_EST)"
assert_l2_receipt_ok "$TX" "$L2"

# The messenger's withdrawal flows through the standard claim machinery;
# finalize's target call IS the relay.
python3 "$SCRIPT_DIR/withdraw_claim.py" "$TX" --wait-finalized 600

NONCE_AFTER=$(cast call "$L2_XDM" "messageNonce()(uint256)" --rpc-url "$L2")
BAL_AFTER=$(cast balance "$RECIPIENT" --rpc-url "$L1")
python3 - "$NONCE_BEFORE" "$NONCE_AFTER" "$BAL_BEFORE" "$BAL_AFTER" <<'PY' || die "xdm asserts failed"
import sys
nb, na, bb, ba = sys.argv[1:4+1]
def num(x):
    x = x.strip().split()[0]
    return int(x, 16) if x.lower().startswith("0x") else int(x)
assert num(na) > num(nb), f"L2 messenger nonce did not advance: {nb} -> {na}"
delta = num(ba) - num(bb)
assert delta >= num("500000000000000000"), f"relayed value did not land: delta {delta}"
print(f"[xdm] RELAYED: messenger nonce {num(nb)}->{num(na)}, "
      f"recipient L1 +{delta/1e18:.3f} ETH")
PY
log "XDM L2->L1 E2E GREEN"
