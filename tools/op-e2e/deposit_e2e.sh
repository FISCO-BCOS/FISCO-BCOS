#!/usr/bin/env bash
# deposit_e2e.sh — L1→L2 deposit e2e (functional-completeness gap: the missing
# half of value transfer; spec 2026-08-23-e2e-coverage-gaps §2, review-fixed).
#
# depositETH(1 ETH, DEV1) -> poll the DERIVED credit (delta vs a baseline
# snapshot; DEV1 carries a genesis balance so an absolute check is vacuous) ->
# assert the deposit lands in a block <= safe_l2 with the 0x7e/sourceHash/mint
# shape -> two depositTransaction calldata variants (data-to-empty-address
# status 1; unknown-selector-to-MessagePasser status 0) -> print the L1
# baseline the withdraw leg nets against.
# Env: C2_L1_RPC/C2_L2_WEB3/C2_OP_NODE/C2_STATE/C2_DEV_KEY (defaults = C2).
set -euo pipefail
cd "$(dirname "$0")"

# cast honors the macOS system proxy; bypass it for localhost RPC (a local
# proxy tool answering instead of the node yields "parser error" bodies).
export NO_PROXY="${NO_PROXY:-127.0.0.1,localhost}"
export no_proxy="${no_proxy:-127.0.0.1,localhost}"

L1="${C2_L1_RPC:-http://127.0.0.1:8549}"
L2="${C2_L2_WEB3:-http://127.0.0.1:8555}"
OPNODE="${C2_OP_NODE:-http://127.0.0.1:9545}"
STATE="${C2_STATE:-/tmp/c2/state.json}"
KEY="${C2_DEV_KEY:-0x59c6995e998f97a5a0044966f0945389dc9e86dae88c7a8412f4603b6b78690d}"  # DEV1
DEV1=0x70997970C51812dc3A010C7d01b50e0d17dc79C8
PASSER=0x4200000000000000000000000000000000000016
PORTAL=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["opChainDeployments"][0]["OptimismPortalProxy"])' "$STATE")

log() { echo "[dep] $*"; }
die() { echo "[dep] !! $*" >&2; exit 1; }

log "portal=$PORTAL l1=$L1 l2=$L2 opnode=$OPNODE"
L1_BEFORE=$(cast balance "$DEV1" --rpc-url "$L1")
L2_BEFORE=$(cast balance "$DEV1" --rpc-url "$L2")
TIP0=$(cast block-number --rpc-url "$L2")

# ── phase 1: depositETH + derived-credit poll + shape asserts ──────────────
# OptimismPortal2 has no depositETH() selector — a plain ETH transfer hits
# receive(), which deposits to msg.sender (RECEIVE_DEFAULT_GAS_LIMIT 100k).
cast send "$PORTAL" --value 1ether --private-key "$KEY" \
  --rpc-url "$L1" > /dev/null || die "depositETH (receive) send failed"
log "depositETH(1 ETH) sent; waiting for derivation (<=300s)"

python3 - "$L2" "$OPNODE" "$DEV1" "$L2_BEFORE" "$TIP0" <<'PY' || die "deposit phase-1 failed"
import json, sys, time, urllib.request
l2, opnode, dev1, before, tip0 = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4]), int(sys.argv[5])
def rpc(url, m, p):
    req = urllib.request.Request(url, data=json.dumps(
        {"jsonrpc": "2.0", "method": m, "params": p, "id": 1}).encode(),
        headers={"Content-Type": "application/json"})
    return json.load(urllib.request.urlopen(req, timeout=5))["result"]
# Derivation lag varies with batcher warm-up (observed: seconds to ~4min on
# the compressed devnet) — the budget covers the variance.
deadline = time.time() + 300
while time.time() < deadline:
    try:
        bal = int(rpc(l2, "eth_getBalance", [dev1, "latest"]), 16)
        if bal - before == 10**18:  # exact-credit invariant (spec §2.2-1)
            tip = int(rpc(l2, "eth_blockNumber", []), 16)
            # multi-index scan: deposits may share a block with others
            for n in range(max(tip0, tip - 40), tip + 1):
                for i in range(8):
                    t = rpc(l2, "eth_getTransactionByBlockNumberAndIndex",
                            [hex(n), hex(i)])
                    if not t:
                        break
                    if t.get("type") == "0x7e" and (t.get("to") or "").lower() == dev1.lower() \
                            and int(t.get("value", "0x0"), 16) == 10**18:
                        assert "sourceHash" in t, "deposit missing sourceHash"
                        assert int(t.get("mint", "0x0"), 16) == 10**18, "mint != value"
                        # derivation-path proof: bounded wait for safe to reach n
                        sdead = time.time() + 120
                        while time.time() < sdead:
                            safe = rpc(opnode, "optimism_syncStatus",
                                      [])["safe_l2"]["number"]
                            if safe >= n:
                                print(f"[dep] derived deposit: block {n} "
                                      f"(safe {safe}), type 0x7e, sourceHash "
                                      f"present, mint == 1 ETH, credit exact")
                                sys.exit(0)
                            time.sleep(2)
                        raise AssertionError(f"deposit at {n} never reached safe "
                                             f"(safe stuck < {n} for 120s)")
        if bal - before > 10**18:
            raise AssertionError(f"credit delta {bal-before} > 1 ETH — unexpected")
    except Exception as e:
        if isinstance(e, (AssertionError,)) or "unexpected" in str(e) or "never reached" in str(e):
            print(f"[dep] !! {e}", file=sys.stderr)
            sys.exit(1)
    time.sleep(3)
print("[dep] !! deposit not credited within 180s", file=sys.stderr)
sys.exit(1)
PY

# ── phase 2: depositTransaction calldata variants (XDM L1→L2 pre-coverage) ──
cast send "$PORTAL" "depositTransaction(address,uint256,uint64,bool,bytes)" \
  0x1111111111111111111111111111111111111111 0 100000 false 0xdeadbeef \
  --private-key "$KEY" --rpc-url "$L1" > /dev/null || die "variant-a send failed"
cast send "$PORTAL" "depositTransaction(address,uint256,uint64,bool,bytes)" \
  "$PASSER" 0 100000 false 0xffffffff \
  --private-key "$KEY" --rpc-url "$L1" > /dev/null || die "variant-b send failed"
log "calldata variants sent; waiting for derivation + execution receipts"

python3 - "$L2" <<'PY' || die "calldata-variant execution asserts failed"
import json, sys, time, urllib.request
l2 = sys.argv[1]
def rpc(m, p):
    req = urllib.request.Request(l2, data=json.dumps(
        {"jsonrpc": "2.0", "method": m, "params": p, "id": 1}).encode(),
        headers={"Content-Type": "application/json"})
    return json.load(urllib.request.urlopen(req, timeout=5))["result"]
def find_deposit(to, data_prefix):
    # Variant deposits derive at a DETERMINISTIC L2 height (~206 on the
    # compressed devnet) that can be ~5 min of wall time past the send —
    # the budget must cover the height, not just the batch latency.
    deadline = time.time() + 600
    seen_dump = []
    while time.time() < deadline:
        tip = int(rpc("eth_blockNumber", []), 16)
        for n in range(max(1, tip - 80), tip + 1):
            for i in range(8):
                t = rpc("eth_getTransactionByBlockNumberAndIndex", [hex(n), hex(i)])
                if not t:
                    break
                if t.get("type") == "0x7e" and (t.get("to") or "").lower() == to:
                    if (t.get("input") or t.get("data") or "").startswith(data_prefix):
                        return t, rpc("eth_getTransactionReceipt", [t["hash"]])
                    if len(seen_dump) < 3:
                        seen_dump.append(
                            f"block {n}: to={t.get('to')} input={(t.get('input') or t.get('data'))!r}")
        time.sleep(3)
    print(f"[dep] DEBUG 0x7e txs to {to} seen: {seen_dump or 'NONE'}",
          file=sys.stderr)
    return None, None
ta, ra = find_deposit("0x1111111111111111111111111111111111111111", "0xdeadbeef")
assert ta and ra, "variant-a deposit not derived within 180s"
assert ra["status"] == "0x1", f"variant-a expected status 1, got {ra['status']}"
print(f"[dep] variant-a (data->empty addr): status 1 at block {int(ra['blockNumber'],16)}")
tb, rb = find_deposit("0x4200000000000000000000000000000000000016", "0xffffffff")
assert tb and rb, "variant-b deposit not derived within 180s"
assert rb["status"] == "0x0", f"variant-b expected status 0, got {rb['status']}"
print(f"[dep] variant-b (unknown selector->MessagePasser): status 0 at block {int(rb['blockNumber'],16)}")
PY

# ── phase 3: roundtrip baseline (withdraw_e2e closes the loop) ─────────────
L1_AFTER=$(cast balance "$DEV1" --rpc-url "$L1")
log "DEPOSIT E2E GREEN — L1 $L1_BEFORE -> $L1_AFTER (baseline for the withdraw leg)"
