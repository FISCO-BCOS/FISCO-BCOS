#!/usr/bin/env bash
# withdraw_e2e_ephemeral.sh — one-shot time-compressed cross-domain e2e.
#
# Throwaway devnet in a mktemp workspace (real op-deployer contracts, clocks
# compressed ONLY via launch parameters — no warps, so the 裁决-8 sequencer
# poisoning mode cannot occur): mode preflight (OP-stack vs eth/PBFT evidence)
# -> L1->L2 deposit roundtrip leg -> L2->L1 withdraw closed loop -> adversarial
# dispute scenarios (CONTEST=1 default) -> unconditional teardown.
#
# Budget: 5-8 min with CONTEST=1 (deploy ~2 min dominates; the L2 finalized
# lag adds ~10 min — observed 1305s total); >30 min = investigate.
# Ports 8749/8755/8766/22213/33400/9745/8747 stay clear of the shared C2
# (85xx/95xx) and the C2b range (86xx/96xx) — never point this at /tmp/c2.
# BIN_DIR: where to stage op-deployer/op-node/op-batcher from (defaults to
# /tmp/c2 locally; CI points it at its own build staging). MATRIX=1 additionally
# runs the rpc_matrix tier-1 suite against the instance before teardown
# (funds the matrix sender first). CONTEST=0 skips the adversarial scenarios;
# XDM=1 adds the L2->L1 cross-domain-messaging relay leg.
set -euo pipefail

BIN_DIR="${BIN_DIR:-/tmp/c2}"

# cast honors the macOS system proxy; bypass it for all the localhost RPC this
# runner performs (proxy-garbage responses surface as cast "parser error").
export NO_PROXY="${NO_PROXY:-127.0.0.1,localhost}"
export no_proxy="${no_proxy:-127.0.0.1,localhost}"

HERE="$(cd "$(dirname "$0")" && pwd)"
WORKSPACE="$(mktemp -d /tmp/c2eph.XXXXXX)"
# Physicalize: macOS resolves /tmp -> /private/tmp and lsof reports the
# PHYSICAL cwd — comparing against the symlinked form never matches.
WORKSPACE="$(cd "$WORKSPACE" && pwd -P)"
START_TS=$(date +%s)
EPH_ANVIL=8749 EPH_WEB3=8755 EPH_ENGINE=8766 EPH_RPC=22213 EPH_P2P=33400
EPH_OPNODE=9745 EPH_BATCHER=8747
KEY=0x59c6995e998f97a5a0044966f0945389dc9e86dae88c7a8412f4603b6b78690d  # DEV1
DEV1=0x70997970C51812dc3A010C7d01b50e0d17dc79C8
CHAIN_ID="${C2_L2_CHAIN_ID:-914901}"
export C2_L1_RPC="http://127.0.0.1:$EPH_ANVIL"
export C2_L2_WEB3="http://127.0.0.1:$EPH_WEB3"
export C2_OP_NODE="http://127.0.0.1:$EPH_OPNODE"
export C2_STATE="$WORKSPACE/state.json"
export C2_ROCKSDB="$WORKSPACE/fisco/data/1/latest"

log() { echo "[eph] $*"; }

teardown() {
    local rc=$?
    for f in "$WORKSPACE"/op-node.pid "$WORKSPACE"/op-batcher.pid \
             "$WORKSPACE"/anvil.pid "$WORKSPACE"/fisco/node.pid; do
        if [ -f "$f" ]; then kill "$(cat "$f")" 2>/dev/null || true; fi
    done
    for pid in $(pgrep -f "fisco-bcos" 2>/dev/null || true); do
        if [ "$(lsof -p "$pid" 2>/dev/null | awk '$4=="cwd" {print $NF}')" \
             = "$WORKSPACE/fisco" ]; then kill "$pid" 2>/dev/null || true; fi
    done
    pgrep -f "$WORKSPACE" | xargs kill 2>/dev/null || true
    sleep 1
    rm -rf "$WORKSPACE"
    log "teardown done (rc=$rc, total $(( $(date +%s) - START_TS ))s)"
    exit "$rc"
}
trap teardown EXIT INT TERM

log "workspace: $WORKSPACE"
# Stage prebuilt binaries (setup only auto-builds op-batcher — a fresh go
# build per run wastes ~1min). chmod: a leftover 644 dest makes cp keep the
# un-executable mode and the -x staging check fail.
for bin in op-deployer op-node op-batcher; do
    if [ -f "$BIN_DIR/$bin" ]; then
        cp "$BIN_DIR/$bin" "$WORKSPACE/$bin"
        chmod +x "$WORKSPACE/$bin"
    fi
done
for bin in op-deployer op-node; do
    [ -x "$WORKSPACE/$bin" ] || { log "missing /tmp/c2/$bin (prebuild first)"; exit 1; }
done

# Clock invariant (InvalidClockExtension): max(ext*2, ext+preimage) <= maxClock
# -> with ext=1, preimage=2: 3 <= 30 OK.
C2="$WORKSPACE" \
ANVIL_PORT=$EPH_ANVIL FISCO_WEB3=$EPH_WEB3 FISCO_ENGINE=$EPH_ENGINE \
FISCO_RPC=$EPH_RPC FISCO_P2P=$EPH_P2P \
OP_NODE_PORT=$EPH_OPNODE OP_BATCHER_PORT=$EPH_BATCHER \
OP_NODE_EXTRA_FLAGS="--p2p.disable" \
ANVIL_BLOCK_TIME=1 PROOF_MATURITY_SECONDS=6 DISPUTE_FINALITY_SECONDS=2 \
FAULT_GAME_MAX_CLOCK=30 PREIMAGE_CHALLENGE_SECONDS=2 BATCHER_MAX_CHANNEL=2 \
    bash "$HERE/setup_c2.sh" || { log "setup failed"; exit 1; }

# Warm-up: unsafe AND safe past genesis before anything else.
python3 - "$EPH_OPNODE" <<'PY'
import json, sys, time, urllib.request
url = f"http://127.0.0.1:{sys.argv[1]}"
deadline = time.time() + 180
while time.time() < deadline:
    try:
        req = urllib.request.Request(url, data=json.dumps(
            {"jsonrpc": "2.0", "method": "optimism_syncStatus", "params": [],
             "id": 1}).encode(), headers={"Content-Type": "application/json"})
        s = json.load(urllib.request.urlopen(req, timeout=5))["result"]
        if s["unsafe_l2"]["number"] > 0 and s["safe_l2"]["number"] > 0:
            print(f"[eph] warm: unsafe={s['unsafe_l2']['number']} "
                  f"safe={s['safe_l2']['number']}")
            sys.exit(0)
    except Exception:
        pass
    time.sleep(3)
print("[eph] !! devnet did not reach unsafe+safe > 0 within 180s")
sys.exit(1)
PY

# ── mode preflight: prove the instance under test is FISCO OP-stack ────────
python3 - "$EPH_WEB3" "$EPH_OPNODE" "$WORKSPACE/rollup.json" <<'PY'
import json, sys, time, urllib.request
web3_url = f"http://127.0.0.1:{sys.argv[1]}"
opnode_url = f"http://127.0.0.1:{sys.argv[2]}"
rollup = json.load(open(sys.argv[3]))
def rpc(url, method, params):
    req = urllib.request.Request(url, data=json.dumps(
        {"jsonrpc": "2.0", "method": method, "params": params,
         "id": 1}).encode(), headers={"Content-Type": "application/json"})
    return json.load(urllib.request.urlopen(req, timeout=5))["result"]
fails = []
def check(name, ok, detail):
    print(f"[eph] mode: {'PASS' if ok else 'FAIL'} {name} — {detail}")
    if not ok:
        fails.append(name)
chain_id = int(rpc(web3_url, "eth_chainId", []), 16)
check("chainId", chain_id == rollup["l2_chain_id"],
      f"{chain_id} == rollup {rollup['l2_chain_id']}")
b0 = rpc(web3_url, "eth_getBlockByNumber", ["0x0", False])
check("genesis-hash", b0["hash"] == rollup["genesis"]["l2"]["hash"],
      f"block0 {b0['hash'][:18]}… == rollup genesis")
check("rollup-fork-schedule", "jovian_time" in rollup and "isthmus_time" in rollup,
      "isthmus/jovian activation fields present (an OP rollup config)")
head = rpc(web3_url, "eth_getBlockByNumber", ["latest", False])
wr = head.get("withdrawalsRoot")
check("withdrawalsRoot", isinstance(wr, str) and len(wr) == 66,
      "header carries it (OP/Isthmus invariant; PBFT headers have none)")
bf = head.get("baseFeePerGas")
check("baseFeePerGas", isinstance(bf, str) and int(bf, 16) > 0,
      f"{bf} (OP headers always carry baseFee; PBFT never writes it)")
ed = head.get("extraData", "0x")
check("extraData-1559-params", len(ed) == 2 + 17 * 2 and ed[2:4] in ("00", "01"),
      f"{ed[:24]}… ({(len(ed)-2)//2}B, version byte {ed[2:4]} — OP EIP-1559/DA-params "
      "encoding, not a PBFT extraData)")
for back in range(6):
    n = int(head["number"], 16) - back
    if n < 1:
        break
    t = rpc(web3_url, "eth_getTransactionByBlockNumberAndIndex", [hex(n), "0x0"])
    if t:
        # mint is omitted when zero (FISCO's serializer) — sourceHash is the
        # hard deposit signature.
        ok = t.get("type") == "0x7e" and "sourceHash" in t
        check("deposit-tx[0]", ok,
              f"block {n}: type {t.get('type')}, sourceHash "
              f"{'present' if 'sourceHash' in t else 'MISSING'}")
        break
else:
    print("[eph] mode: WARN deposit-tx[0] — no non-empty block in the last 6 "
          "to inspect (skipped, non-fatal)")
def snap():
    s = rpc(opnode_url, "optimism_syncStatus", [])
    return s["unsafe_l2"]["number"], s["head_l1"]["number"]
a = snap(); time.sleep(5); b = snap()
check("op-node-driven", b[0] > a[0] and b[1] >= a[1],
      f"unsafe {a[0]}->{b[0]}, head_l1 {a[1]}->{b[1]} "
      "(L2 advances only while op-node drives the engine)")
if fails:
    print(f"[eph] mode: NOT CONFIRMED as FISCO OP-stack — failed: {fails}")
    sys.exit(1)
print(f"[eph] mode: FISCO OP-STACK CONFIRMED "
      f"(chainId {chain_id}, withdrawalsRoot/baseFee/1559-extraData/deposit/"
      "op-node-driven all green)")
PY

# ── phase 1: deposit (cross-domain roundtrip, leg 1) ───────────────────────
log "running deposit phase"
bash "$HERE/deposit_e2e.sh"

# ── phase 2: withdraw closed loop (leg 2) ──────────────────────────────────
log "running withdraw_e2e"
bash "$HERE/withdraw_e2e.sh"

# ── phase 3: adversarial dispute scenarios ─────────────────────────────────
if [ "${CONTEST:-1}" = "1" ]; then
  log "running adversarial dispute scenarios"
  TX=$(cast send 0x4200000000000000000000000000000000000016 \
    "initiateWithdrawal(address,uint256,bytes)" "$DEV1" 100000 0x --value 1ether \
    --private-key "$KEY" --rpc-url "$C2_L2_WEB3" --chain-id "$CHAIN_ID" --json \
    | python3 -c 'import json,sys; print(json.load(sys.stdin)["transactionHash"])')
  python3 "$HERE/withdraw_claim.py" "$TX" --wait-finalized 2400 --contest abandoned
  # dishonest reuses the same withdrawal: its precheck never proves (the fake
  # root provably rejects the real proof), so no interference with the
  # finalized state.
  python3 "$HERE/withdraw_claim.py" "$TX" --wait-finalized 0 --contest dishonest
fi

# ── phase 3.5 (optional): XDM L2->L1 relay leg ──────────────────────────────
if [ "${XDM:-0}" = "1" ]; then
  log "running the XDM L2->L1 relay leg"
  bash "$HERE/xdm_e2e.sh"
fi

# ── phase 4 (optional): rpc_matrix tier-1 against the live instance ───────
if [ "${MATRIX:-0}" = "1" ]; then
  log "running rpc_matrix tier-1 against the instance"
  cast send 0x6afa9580383E6627dA926B6f6ed9Ab2B9c8cC693 --value 1ether \
    --private-key "$KEY" --rpc-url "$C2_L2_WEB3" --chain-id "$CHAIN_ID" > /dev/null
  B3_ETH_PORT=$EPH_WEB3 B3_ENGINE_PORT=$EPH_ENGINE \
  B3A_JWT="$WORKSPACE/fisco/jwt.hex" \
  B3_GENESIS="$WORKSPACE/fisco/config.genesis" \
      python3 "$HERE/rpc_matrix.py" || \
  { log "matrix failed once (known pending/latest block race) — retrying"; sleep 8; \
    B3_ETH_PORT=$EPH_WEB3 B3_ENGINE_PORT=$EPH_ENGINE \
    B3A_JWT="$WORKSPACE/fisco/jwt.hex" \
    B3_GENESIS="$WORKSPACE/fisco/config.genesis" \
        python3 "$HERE/rpc_matrix.py"; }
fi

# Post-claim advancement snapshot: sequencer and derivation both alive.
python3 - "$EPH_OPNODE" <<'PY'
import json, sys, time, urllib.request
url = f"http://127.0.0.1:{sys.argv[1]}"
def snap():
    req = urllib.request.Request(url, data=json.dumps(
        {"jsonrpc": "2.0", "method": "optimism_syncStatus", "params": [],
         "id": 1}).encode(), headers={"Content-Type": "application/json"})
    return json.load(urllib.request.urlopen(req, timeout=5))["result"]
a = snap(); time.sleep(6); b = snap()
assert b["unsafe_l2"]["number"] > a["unsafe_l2"]["number"], "sequencer stalled after claim"
print(f"[eph] post-claim: unsafe {a['unsafe_l2']['number']}->{b['unsafe_l2']['number']}, "
      f"safe {a['safe_l2']['number']}->{b['safe_l2']['number']}")
PY

log "e2e green in $(( $(date +%s) - START_TS ))s (budget: <=600s)"
