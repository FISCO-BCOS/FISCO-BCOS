#!/usr/bin/env bash
# l2_gas.sh — padded gas limits + receipt checks for OP L2 contract calls.
#
# eth_estimateGas simulates with infinite sender balance; on FISCO OP paths the
# observed gasUsed can exceed the estimate slightly (L1/operator fee accounting).
# Source this file and use l2_padded_gas / assert_l2_receipt_ok.
l2_padded_gas() {
  python3 -c "print(int('$1') * 6 // 5 + 10000)"
}

assert_l2_receipt_ok() {
  local tx="$1" l2="$2"
  python3 - "$tx" "$l2" <<'PY'
import json, sys, urllib.request
tx, l2 = sys.argv[1], sys.argv[2]
def rpc(m, p):
    req = urllib.request.Request(l2, data=json.dumps(
        {"jsonrpc": "2.0", "method": m, "params": p, "id": 1}).encode(),
        headers={"Content-Type": "application/json"})
    return json.load(urllib.request.urlopen(req, timeout=30))["result"]
r = rpc("eth_getTransactionReceipt", [tx])
if r.get("status") != "0x1":
    raise SystemExit(
        f"L2 tx failed (status={r.get('status')}, gasUsed={r.get('gasUsed')})")
PY
}
