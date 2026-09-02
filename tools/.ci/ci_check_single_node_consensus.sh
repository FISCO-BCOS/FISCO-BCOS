#!/usr/bin/env bash
# Copyright (c) FISCO-BCOS, Apache-2.0
#
# Smoke test for the built-in single-node consensus mode:
#   1. build a single-node AIR chain via build_chain.sh
#   2. patch config.genesis for executor_version=2 + evmc revision + L2 alloc funding
#   3. patch config.ini: web3_rpc + [consensus] enable_single_node_consensus=true
#   4. start the node (no pbft/sealer/txpool — the single-node driver produces blocks
#      from the mempool via the scheduler commit path)
#   5. drive eth_sendRawTransaction -> eth_getTransactionReceipt / eth_getBlockByNumber
#
# Usage: bash tools/.ci/ci_check_single_node_consensus.sh [REPO_ROOT]
set -euo pipefail

REPO_ROOT="${1:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}"
BINARY="${REPO_ROOT}/build/fisco-bcos-air/fisco-bcos"
BUILDER="${REPO_ROOT}/tools/BcosAirBuilder/build_chain.sh"
WORK_DIR="${REPO_ROOT}/tools/nodes/127.0.0.1/node0"
RPC_URL="http://127.0.0.1:8545"

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'
log()  { echo -e "${GREEN}[single-node-ci]${NC} $*"; }
fail() {
    echo -e "${RED}[single-node-ci] FAIL: $*${NC}" >&2
    echo -e "${YELLOW}[single-node-ci] last node log lines:${NC}" >&2
    tail -n 120 "${WORK_DIR}"/log/*.log 2>/dev/null || true
    exit 1
}

NODE_PID=""
stop_node() {
    if [ -n "${NODE_PID}" ] && kill -0 "${NODE_PID}" 2>/dev/null; then
        kill -9 "${NODE_PID}" 2>/dev/null || true
    fi
    if [ -f "${WORK_DIR}/stop.sh" ]; then
        (cd "$(dirname "${WORK_DIR}")" && bash stop_all.sh 2>/dev/null) || true
    fi
    sleep 1
}
trap stop_node EXIT

[ -f "${BINARY}" ] || fail "binary not found: ${BINARY}"

# ---- 1. build the single-node chain ----
log "building single-node chain via build_chain.sh"
cd "${REPO_ROOT}/tools"
rm -rf nodes config
bash "${BUILDER}" -l "127.0.0.1:1" -v 3.18.0 -e "${BINARY}" "" >/dev/null 2>&1 \
    || fail "build_chain.sh failed"
[ -f "${WORK_DIR}/config.genesis" ] || fail "config.genesis not generated"

# ---- 2. patch genesis for executor_version=2 + L2 alloc ----
log "patching config.genesis for executor_version=2 + L2 alloc"
cd "${WORK_DIR}"
perl -p -i -e 's/version=1/version=2/' config.genesis
perl -p -i -e 's/^(\s*is_serial_execute=true)/$1\n    evm_revision=cancun/' config.genesis

cat >> config.genesis <<'GENESIS_EOF'

[features]
    feature_l2_ethereum_compat=1
GENESIS_EOF

# Pre-fund a sender (secretKey below) via genesis alloc.
cat >> config.genesis <<'GENESIS_EOF'
[alloc.0]
    address=0x9015bca99e8d49107c33b2cac14013a8dfd2c1b0
    balance=1000000000000000000000
    nonce=0
    code=
GENESIS_EOF

# L2 mode (feature_l2_ethereum_compat) now REQUIRES an [eth_genesis_header]
# section (NodeConfig::validateL2Invariants, upstream #5420): an L2 chain
# without it would mint a Tars-hashed B0 that no op-node/op-reth can match.
# Identical alloc to ci_check_eth_executor.sh, so the same precomputed
# state_root / hash apply (see that script for the regeneration procedure).
cat >> config.genesis <<'GENESIS_EOF'

[eth_genesis_header]
parent_hash=0x0000000000000000000000000000000000000000000000000000000000000000
sha3_uncles=0x1dcc4de8dec75d7aab85b567b6ccd41ad312451b948a7413f0a142fd40d49347
miner=0x4200000000000000000000000000000000000011
state_root=0x6ea0c8bc0a9b451c2bab98df63d4be058d3b82b9b3a2e5e02cbf0122ef95e5a5
transactions_root=0x56e81f171bcc55a6ff8345e692c0f86e5b48e01b996cadc001622fb5e363b421
receipts_root=0x56e81f171bcc55a6ff8345e692c0f86e5b48e01b996cadc001622fb5e363b421
logs_bloom=0x00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
difficulty=0x0
number=0x0
gas_limit=0x1c9c380
gas_used=0x0
timestamp=0x689d5c00
extra_data=0x01000000fa000000060000000000000000
mix_hash=0x0000000000000000000000000000000000000000000000000000000000000000
nonce=0x0000000000000000
base_fee_per_gas=0x3b9aca00
withdrawals_root=0x56e81f171bcc55a6ff8345e692c0f86e5b48e01b996cadc001622fb5e363b421
blob_gas_used=0x0
excess_blob_gas=0x0
parent_beacon_block_root=0x0000000000000000000000000000000000000000000000000000000000000000
requests_hash=0xe3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
hash=0xf66beef79256c0b3f1fd2852040413c5dffa1ff44f0f78421ae17cf8f397a1a5
GENESIS_EOF

# ---- 3. patch config.ini: web3_rpc + single-node consensus ----
log "patching config.ini for web3_rpc + single-node consensus"
perl -p -i -e 'if (/\[web3_rpc\]/) { $f=1 } elsif ($f && s/enable\s*=\s*false/enable=true/) { $f=0 }' config.ini

# Enable the built-in single-node consensus: timed block production from the mempool via
# the scheduler commit path. produce_empty_blocks=true so the smoke test also sees the
# timer driving empty blocks. The keys are inserted into the existing [consensus] section
# (a second [consensus] section would be rejected by the ini parser).
python3 - "config.ini" <<'PYEOF'
import sys
path = sys.argv[1]
content = open(path).read()
idx = content.find('\n[consensus]\n    enable_single_node_consensus')
if idx != -1:
    content = content[:idx]
if 'enable_single_node_consensus' not in content:
    content = content.replace('min_seal_time=500\n',
        'min_seal_time=500\n    enable_single_node_consensus=true\n    block_interval=1000\n    produce_empty_blocks=true\n')
open(path, 'w').write(content)
print("patched [consensus]")
PYEOF

# ---- 4. start node and wait for RPC ----
log "starting node"
bash start.sh >/dev/null 2>&1 || fail "start.sh failed"
NODE_PID=$(pgrep -f "fisco-bcos.*-c config.ini" || true)
for i in $(seq 1 40); do
    sleep 2
    if curl -s -X POST "${RPC_URL}" -H 'Content-Type: application/json' \
        -d '{"jsonrpc":"2.0","id":1,"method":"eth_blockNumber","params":[]}' 2>/dev/null | grep -q '"result"'; then
        log "RPC up after ~$((i*2))s"
        break
    fi
    [ "$i" -eq 40 ] && fail "node RPC did not come up"
done

# The single-node driver should already be producing empty blocks every 1s.
sleep 4
BLOCK_NUM=$(curl -s -X POST "${RPC_URL}" -H 'Content-Type: application/json' \
    -d '{"jsonrpc":"2.0","id":1,"method":"eth_blockNumber","params":[]}' | python3 -c 'import sys,json; print(int(json.load(sys.stdin)["result"],16))')
log "blockNumber after boot: ${BLOCK_NUM}"
[ "${BLOCK_NUM}" -ge 1 ] || fail "single-node driver did not produce any block (blockNumber=${BLOCK_NUM})"

# ---- 5. drive a value transfer via RPC ----
log "submitting a value transfer"
SECRET="0xa4e7ea6dc542e6de38bf1229c3ea744aa6b9f501386aa167ad42f49e3787066f"
SENDER="0x9015bca99e8d49107c33b2cac14013a8dfd2c1b0"
RECIPIENT="0xc3ea744aa6b9f501386aa167ad42f49e3787066f"

python3 - "${RPC_URL}" "${SECRET}" "${SENDER}" "${RECIPIENT}" <<'PYEOF'
import json, sys, time, urllib.request
import eth_utils
from eth_account import Account

rpc_url, secret, sender, recipient = sys.argv[1:5]
def rpc(method, params):
    req = urllib.request.Request(rpc_url, json.dumps({"jsonrpc":"2.0","id":1,"method":method,"params":params}).encode(),
        {"Content-Type":"application/json"})
    with urllib.request.urlopen(req) as resp:
        body = json.load(resp)
    if "error" in body:
        raise RuntimeError(f"{method}: {body['error']}")
    return body["result"]

chain_id = int(rpc("eth_chainId", []), 16)
nonce = int(rpc("eth_getTransactionCount", [sender, "latest"]), 16)
tx = {"chainId": chain_id, "nonce": nonce, "gasPrice": 1, "gas": 21000,
      "to": eth_utils.to_checksum_address(recipient), "value": 1, "data": b""}
signed = Account.sign_transaction(tx, secret)
tx_hash = rpc("eth_sendRawTransaction", [signed.raw_transaction.hex()])
print(f"tx_hash={tx_hash}")

receipt = None
for _ in range(30):
    time.sleep(1)
    receipt = rpc("eth_getTransactionReceipt", [tx_hash])
    if receipt:
        break
if not receipt:
    raise RuntimeError("receipt not found after 30s — block not produced")
print(f"receipt status={receipt['status']} block={receipt['blockNumber']} gasUsed={receipt['gasUsed']}")
assert receipt["status"] == "0x1", f"tx failed: {receipt}"

bal = int(rpc("eth_getBalance", [recipient, "latest"]), 16)
print(f"recipient balance={bal}")
assert bal >= 1, "recipient was not credited"

block = rpc("eth_getBlockByNumber", [receipt["blockNumber"], False])
print(f"block {receipt['blockNumber']}: txs={len(block['transactions'])} "
      f"stateRoot={block['stateRoot']} gasUsed={block['gasUsed']}")
assert len(block["transactions"]) >= 1, "block has no transactions"
# Wall-clock mode (this script uses no fixed_timestamp): the produced block's
# timestamp (seconds, Ethereum RPC semantics — the header stores ms and the RPC
# divides by 1000) must sit within a sane window of the test's own wall clock.
# This guards the unit regression where utcTime() — which already returns
# milliseconds — was multiplied by 1000, turning block.timestamp into
# ~1.786e15 (year 58577 in the EVM).
block_ts = int(block["timestamp"], 16)
now_sec = int(time.time())
print(f"block timestamp={block_ts} wall-clock-now={now_sec}")
assert abs(block_ts - now_sec) < 120, (
    f"block timestamp {block_ts} not within 120s of wall clock {now_sec}")
print("PASS: single-node consensus produced a block containing the tx")
PYEOF
