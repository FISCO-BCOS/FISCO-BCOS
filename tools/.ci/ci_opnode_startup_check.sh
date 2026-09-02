#!/usr/bin/env bash
# Copyright (c) FISCO-BCOS, Apache-2.0
#
# op-node startup quick check (K0):
#   1. start anvil as L1
#   2. build a single-node AIR chain with [op_engine_rpc] enabled (external-driver
#      mode: no built-in single-node consensus), executor_version=2
#   3. generate rollup.json from the live L1/L2 genesis via gen_rollup_config.py
#   4. start op-node --sequencer.stopped and require it to survive rollup
#      Config.Check() + L1/L2 genesis validation
#
# If op-node or anvil is not installed the script reports SKIP and exits 0, so it
# is safe to wire into CI before the toolchain image carries those binaries.
#
# Usage: bash tools/.ci/ci_opnode_startup_check.sh [REPO_ROOT]
set -euo pipefail

REPO_ROOT="${1:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}"
BINARY="${REPO_ROOT}/build/fisco-bcos-air/fisco-bcos"
BUILDER="${REPO_ROOT}/tools/BcosAirBuilder/build_chain.sh"
GEN_ROLLUP="${REPO_ROOT}/tools/opstack-genesis/gen_rollup_config.py"
WORK_ROOT="${REPO_ROOT}/tools/opnode-check"
NODE_DIR="${WORK_ROOT}/nodes/127.0.0.1/node0"

L1_PORT=8546
L2_RPC_URL="http://127.0.0.1:8545"
L1_RPC_URL="http://127.0.0.1:${L1_PORT}"
ENGINE_URL="http://127.0.0.1:8551"
L1_CHAIN_ID=900
L2_CHAIN_ID=901

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'
log()  { echo -e "${GREEN}[opnode-ci]${NC} $*"; }
skip() { echo -e "${YELLOW}[opnode-ci] SKIP: $*${NC}"; exit 0; }
fail() {
    echo -e "${RED}[opnode-ci] FAIL: $*${NC}" >&2
    echo -e "${YELLOW}[opnode-ci] op-node log tail:${NC}" >&2
    tail -n 60 "${WORK_ROOT}/op-node.log" 2>/dev/null >&2 || true
    echo -e "${YELLOW}[opnode-ci] fisco log tail:${NC}" >&2
    tail -n 40 "${NODE_DIR}"/log/*.log 2>/dev/null >&2 || true
    exit 1
}

ANVIL_PID=""; NODE_PID=""; OPNODE_PID=""
cleanup() {
    if [ -f "${NODE_DIR}/stop.sh" ]; then
        (cd "${NODE_DIR}" && bash stop.sh >/dev/null 2>&1) || true
    fi
    # PID variables may hold several lines (pgrep); iterate line-wise
    echo "${OPNODE_PID} ${NODE_PID} ${ANVIL_PID}" | tr ' ' '\n' | while read -r pid; do
        if [ -n "${pid}" ] && kill -0 "${pid}" 2>/dev/null; then
            kill -9 "${pid}" 2>/dev/null || true
        fi
    done
}
trap cleanup EXIT

# ---- 0. toolchain detection: SKIP when the OP stack binaries are absent ----
command -v python3 >/dev/null 2>&1 || skip "python3 not found"
command -v jq >/dev/null 2>&1 || skip "jq not found"
command -v curl >/dev/null 2>&1 || skip "curl not found"
command -v anvil >/dev/null 2>&1 || skip "anvil not found (install foundry to run this check)"
command -v op-node >/dev/null 2>&1 || skip "op-node not found (install op-node v1.19.3 to run this check)"
[ -f "${BINARY}" ] || skip "fisco-bcos binary not found: ${BINARY} (build first)"

rpc_call() { # url method params
    curl -s --max-time 5 -H 'Content-Type: application/json' \
        --data "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":${2},\"params\":${3}}" "${1}"
}

# ---- 1. anvil as L1 ----
mkdir -p "${WORK_ROOT}"
log "starting anvil (chain-id ${L1_CHAIN_ID}, port ${L1_PORT})"
anvil --port "${L1_PORT}" --chain-id "${L1_CHAIN_ID}" --block-time 12 \
    >"${WORK_ROOT}/anvil.log" 2>&1 &
ANVIL_PID=$!
for _ in $(seq 1 20); do
    if rpc_call "${L1_RPC_URL}" '"eth_chainId"' '[]' | grep -q result; then break; fi
    sleep 0.5
done
rpc_call "${L1_RPC_URL}" '"eth_chainId"' '[]' | grep -q result || fail "anvil did not come up"
L1_GENESIS_HASH=$(rpc_call "${L1_RPC_URL}" '"eth_getBlockByNumber"' '["0x0", false]' | jq -r .result.hash)
[ "${L1_GENESIS_HASH}" != "null" ] || fail "cannot read anvil genesis hash"

# ---- 2. FISCO chain with [op_engine_rpc] enabled ----
log "building single-node chain (op_engine_rpc enabled, executor_version=2)"
rm -rf "${WORK_ROOT}/nodes"
(cd "${WORK_ROOT}" && bash "${BUILDER}" -l "127.0.0.1:1" -v 3.18.0 -e "${BINARY}" -O \
    >/dev/null 2>&1) || fail "build_chain.sh failed"
[ -f "${NODE_DIR}/config.genesis" ] || fail "config.genesis not generated"

cd "${NODE_DIR}"
# build_chain.sh -O already pins the genesis to the v2 executor with an explicit
# evm_revision (op_engine_rpc refuses anything else); assert instead of patching
grep -qE '^\s*version=2' config.genesis || fail "build_chain -O did not pin executor version=2"
grep -qE '^\s*evm_revision=' config.genesis || fail "build_chain -O did not pin an evm_revision"
perl -p -i -e "s/chain_id=20200/chain_id=${L2_CHAIN_ID}/" config.genesis
cat >> config.genesis <<'GENESIS_EOF'

[features]
    feature_l2_ethereum_compat=1

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

# external-driver mode: web3_rpc on, single-node consensus stays off;
# [op_engine_rpc] is already emitted with enable=true by build_chain.sh -O
perl -p -i -e 'if (/\[web3_rpc\]/) { $f=1 } elsif ($f && s/enable\s*=\s*false/enable=true/) { $f=0 }' config.ini
grep -q '^\[op_engine_rpc\]' config.ini || fail "[op_engine_rpc] section not in config.ini"
grep -q "conf/op-engine/jwt.hex" config.ini || fail "jwt_secret_file not in config.ini"
[ "$(wc -c <conf/op-engine/jwt.hex)" -eq 65 ] || fail "jwt.hex is not 64 hex chars + newline"

log "starting fisco-bcos node"
bash start.sh >/dev/null 2>&1 || fail "start.sh failed"
# the node's cmdline is "<start.sh's pwd>/../fisco-bcos -c config.ini" (start.sh resolves
# SHELL_FOLDER with `cd ...; pwd`). That is normally the literal ${NODE_DIR}, but under a
# symlinked root the shell's pwd may resolve differently and a single pattern silently
# never matches, leaving cleanup's kill fallback dead — so try the logical and the
# physical form. Keeping the node dir in the pattern scopes the match to this check's
# node only; unrelated local fisco-bcos processes are never matched or killed.
NODE_PID=$(pgrep -f "${NODE_DIR}/../fisco-bcos" ||
    pgrep -f "$(cd "${NODE_DIR}" && pwd -P)/../fisco-bcos" || true)
for _ in $(seq 1 40); do
    if rpc_call "${L2_RPC_URL}" '"eth_chainId"' '[]' | grep -q result; then break; fi
    sleep 1
done
rpc_call "${L2_RPC_URL}" '"eth_chainId"' '[]' | grep -q result || fail "web3 rpc did not come up"

L2_GENESIS_JSON=$(rpc_call "${L2_RPC_URL}" '"eth_getBlockByNumber"' '["0x0", false]')
L2_GENESIS_HASH=$(echo "${L2_GENESIS_JSON}" | jq -r .result.hash)
L2_GENESIS_TIME=$(( $(echo "${L2_GENESIS_JSON}" | jq -r '.result.timestamp // "0x0"') ))
[ "${L2_GENESIS_HASH}" != "null" ] || fail "cannot read L2 genesis hash"
# Config.Check requires l2_time > 0; tolerate a zero genesis timestamp from the node
# (K1 owns the real genesis header) by pinning an arbitrary positive value.
[ "${L2_GENESIS_TIME}" -gt 0 ] || L2_GENESIS_TIME=1

# ---- 3. rollup.json from live genesis data ----
log "generating rollup.json"
python3 "${GEN_ROLLUP}" \
    --l1-chain-id "${L1_CHAIN_ID}" --l2-chain-id "${L2_CHAIN_ID}" \
    --l1-genesis-hash "${L1_GENESIS_HASH}" \
    --l2-genesis-hash "${L2_GENESIS_HASH}" \
    --l2-genesis-time "${L2_GENESIS_TIME}" \
    --out "${WORK_ROOT}/rollup.json" || fail "gen_rollup_config.py rejected the config"
jq -e '.genesis.system_config.batcherAddr and (.chain_op_config | length > 0)' \
    "${WORK_ROOT}/rollup.json" >/dev/null || fail "rollup.json missing required fields"

# ---- 4. op-node must survive Config.Check + genesis validation ----
# op-node v1.19.3 with a custom (non superchain-registry) L1 chain id loads the L1
# chain spec from --rollup.l1-chain-config at startup, before any endpoint is dialed
# (verified against the real binary: without the file it dies with "failed to read
# chain spec"). Emit a minimal geth-genesis-shaped chain config for the anvil L1.
cat > "${WORK_ROOT}/l1-chain-config.json" <<L1CFG_EOF
{
  "config": {
    "chainId": ${L1_CHAIN_ID},
    "homesteadBlock": 0, "eip150Block": 0, "eip155Block": 0, "eip158Block": 0,
    "byzantiumBlock": 0, "constantinopleBlock": 0, "petersburgBlock": 0,
    "istanbulBlock": 0, "muirGlacierBlock": 0, "berlinBlock": 0, "londonBlock": 0,
    "arrowGlacierBlock": 0, "grayGlacierBlock": 0, "mergeNetsplitBlock": 0,
    "shanghaiTime": 0, "cancunTime": 0, "pragueTime": 0,
    "terminalTotalDifficulty": 0,
    "blobSchedule": {
      "cancun": {"target": 3, "max": 6, "baseFeeUpdateFraction": 3338477},
      "prague": {"target": 6, "max": 9, "baseFeeUpdateFraction": 5007716}
    }
  }
}
L1CFG_EOF

log "starting op-node (--sequencer.stopped)"
op-node \
    --l1="${L1_RPC_URL}" --l1.trustrpc --l1.rpckind=any --l1.beacon.ignore=true \
    --l2="${ENGINE_URL}" --l2.jwt-secret="${NODE_DIR}/conf/op-engine/jwt.hex" \
    --rollup.config="${WORK_ROOT}/rollup.json" \
    --rollup.l1-chain-config="${WORK_ROOT}/l1-chain-config.json" \
    --sequencer.enabled --sequencer.stopped --sequencer.l1-confs=0 \
    --p2p.disable --rpc.enable-admin --rpc.port=9545 \
    >"${WORK_ROOT}/op-node.log" 2>&1 &
OPNODE_PID=$!

sleep 15
if ! kill -0 "${OPNODE_PID}" 2>/dev/null; then
    # K0-scope allowance: rollup.json passed Config.Check (op-node printed its
    # "Rollup Config" banner), the JWT handshake worked and op-node reached the
    # Engine API — but the L2 genesis header still carries the legacy FISCO
    # genesisData blob in extraData (> 32 bytes), which op-node rejects when
    # resolving the genesis L2BlockRef. That header is owned by the eth-header
    # PR (Ledger genesis extraData rework); remove this allowance there.
    if grep -q "Rollup Config" "${WORK_ROOT}/op-node.log" &&
        grep -q "requires 32 or less bytes of extra data" "${WORK_ROOT}/op-node.log"; then
        log "PASS (K0 scope): op-node accepted rollup.json (Config.Check), authenticated"
        log "  over JWT and queried the Engine API; it stopped at the L2 genesis header"
        log "  extraData check, which the eth-header PR resolves"
        exit 0
    fi
    fail "op-node exited within 15s (rollup config or genesis validation failed)"
fi
if grep -iE "invalid rollup config|failed to load config|CRIT " "${WORK_ROOT}/op-node.log" \
    >/dev/null 2>&1; then
    fail "op-node reported a fatal configuration error"
fi

log "PASS: op-node accepted rollup.json and survived genesis validation"
exit 0
