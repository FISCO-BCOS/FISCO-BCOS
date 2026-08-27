#!/usr/bin/env bash
# =============================================================================
# FISCO-BCOS Engine API Integration Test
#
# 整合 Phase 1 (curl smoke) + Phase 2 (Python mock CL)。
#
# 用法:
#   ./tools/engine_integration_test.sh [BUILD_DIR] [RPC_PORT]
#
# 默认:
#   BUILD_DIR = ./build
#   RPC_PORT  = 8551
#
# CI 用法:
#   bash tools/engine_integration_test.sh build 8545
# =============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${1:-${ROOT_DIR}/build}"
RPC_PORT="${2:-8551}"
RPC_URL="http://127.0.0.1:${RPC_PORT}"
BINARY="${BUILD_DIR}/fisco-bcos-air/fisco-bcos"
WORK_DIR="${BUILD_DIR}/engine_integration_test"
# Resolve to absolute path (we cd into WORK_DIR later)
mkdir -p "${WORK_DIR}"
WORK_DIR="$(cd "${WORK_DIR}" && pwd)"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

PASSED=0
FAILED=0

# PID file for the node started by this script, used for precise cleanup
# without pkill'ing unrelated fisco-bcos processes on shared runners.
PID_FILE="${WORK_DIR}/node.pid"

# Kill any stale fisco-bcos processes from previous runs of this script only
if [ -f "${PID_FILE}" ]; then
    OLD_PID=$(cat "${PID_FILE}" 2>/dev/null || true)
    if [ -n "${OLD_PID}" ]; then
        kill "${OLD_PID}" 2>/dev/null || true
        pkill -P "${OLD_PID}" 2>/dev/null || true
    fi
    rm -f "${PID_FILE}"
fi
sleep 1

# ---- Cleanup handler ----
cleanup() {
    echo ""
    echo "=== Cleaning up ==="
    # Kill only the node started by this script (via PID file), not every
    # fisco-bcos process on the machine.
    if [ -n "${NODE_PID:-}" ]; then
        kill "${NODE_PID}" 2>/dev/null || true
        pkill -P "${NODE_PID}" 2>/dev/null || true
    fi
    rm -f "${PID_FILE}"
    sleep 1
    shopt -s nullglob 2>/dev/null || true
    for f in "${WORK_DIR}"/log_*.log; do
        echo "--- ${f} (last 30 lines) ---"
        tail -30 "${f}" 2>/dev/null || true
    done
    if [ -f "${WORK_DIR}/nohup.out" ]; then
        echo "--- nohup.out (last 30 lines) ---"
        tail -30 "${WORK_DIR}/nohup.out" 2>/dev/null || true
    fi
    if [ "${KEEP_WORK_DIR:-0}" != "1" ]; then
        rm -rf "${WORK_DIR}"
    else
        echo "KEEP_WORK_DIR=1: 保留 ${WORK_DIR} 便于诊断"
    fi
    echo "Cleanup done."
}
trap cleanup EXIT

# ---- Helpers ----
log_section() {
    echo ""
    echo -e "${GREEN}=== $1 ===${NC}"
}

log_test() {
    echo -e "  ${GREEN}[TEST]${NC} $1"
}

log_pass() {
    echo -e "    ${GREEN}✅ PASSED${NC}"
    PASSED=$((PASSED + 1))
}

log_fail() {
    echo -e "    ${RED}❌ FAILED: $1${NC}"
    FAILED=$((FAILED + 1))
}

log_info() {
    echo -e "    ${YELLOW}ℹ️  $1${NC}"
}

rpc_call() {
    local method="$1"
    local params="$2"
    curl -s -X POST "${RPC_URL}" \
        -H "Content-Type: application/json" \
        -H "Authorization: Bearer ${JWT_TOKEN}" \
        -d "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"${method}\",\"params\":${params}}" 2>/dev/null
}

# Extract a JSON value from response. If $2 is empty, extracts result as-is.
# Otherwise extracts d['result'][$2] for string results or nested dict keys.
json_val() {
    local json="$1"
    local key="$2"
    if [ -z "${key}" ]; then
        # Extract result value directly (for string results like eth_chainId)
        python3 -c "
import sys,json
try:
    d=json.loads(sys.argv[1])
    r=d.get('result','')
    if isinstance(r,str): print(r)
    elif isinstance(r,dict): print('')
except: pass
" "${json}" 2>/dev/null || true
    else
        # Extract nested dict key from result
        python3 -c "
import sys,json
try:
    d=json.loads(sys.argv[1])
    r=d.get('result',{})
    if isinstance(r,dict):
        v=r.get('${key}','')
        if isinstance(v,str): print(v)
except: pass
" "${json}" 2>/dev/null || true
    fi
}

# Extract payloadStatus.status from forkchoiceUpdated response
json_fcu_status() {
    local json="$1"
    python3 -c "
import sys,json
try:
    d=json.loads(sys.argv[1])
    r=d.get('result',{})
    ps=r.get('payloadStatus',{})
    s=ps.get('status','')
    if isinstance(s,str): print(s)
except: pass
" "${json}" 2>/dev/null || true
}

# ---- Step 1: Prepare node workspace ----
log_section "Step 1: Prepare node workspace"

if [ ! -f "${BINARY}" ]; then
    log_fail "Binary not found: ${BINARY}"
    exit 1
fi
# Resolve absolute path before cd
ABS_BINARY="$(cd "$(dirname "${BINARY}")" && pwd)/$(basename "${BINARY}")"
log_info "Binary: ${ABS_BINARY}"
log_info "Work dir: ${WORK_DIR}"

rm -rf "${WORK_DIR}"
mkdir -p "${WORK_DIR}"

# Generate genesis config
cat > "${WORK_DIR}/config.genesis" << GENESIS_EOF
[consensus]
consensus_type=pbft
block_tx_count_limit=1000
leader_period=1
node.0=__NODE_ID_PLACEHOLDER__:1
[rpc]
listen_ip=0.0.0.0
listen_port=20210
disable_ssl=true
sm_ssl=false

[web3_rpc]
enable=true
listen_ip=0.0.0.0
listen_port=$((RPC_PORT + 1))

[op_engine_rpc]
enable=true
listen_ip=0.0.0.0
listen_port=${RPC_PORT}
jwt_secret_file=${WORK_DIR}/jwt.hex
clock_skew_secs=300
; this harness drives the v1 EngineService over the endpoint (no [executor] version=2
; here); production chains must never set this test-only escape hatch
unsafe_allow_v1_executor=true

[p2p]
listen_ip=0.0.0.0
listen_port=31300
sm_ssl=false
nodes_path=./
nodes_file=nodes.json

[cert]
ca_path=./
ca_cert=ca.crt
node_key=ssl.key
node_cert=ssl.crt
multi_ca_path=multiCaPath

[certificate_blacklist]
[certificate_whitelist]

[executor]
is_auth_check=false
auth_admin_account=0x3443d6866e757893e6862f451f5d1b7976c54594
is_serial_execute=true
epoch_sealer_num=4
epoch_block_num=1000
notify_rotate_flag=false

[tx]
gas_limit=3000000000

[storage]
type=RocksDB

[log]
enable=true
log_path=./
level=debug
GENESIS_EOF

# Generate minimal certs (use existing from repo if available, otherwise generate self-signed)
mkdir -p "${WORK_DIR}/conf"
CERT_FOUND=0

# First try to copy all cert files from existing node config
for src_dir in "${ROOT_DIR}/tests/nodes/node0/conf" "${ROOT_DIR}/tests/nodes/ca"; do
    if [ -d "${src_dir}" ]; then
        cp "${src_dir}/"* "${WORK_DIR}/conf/" 2>/dev/null || true
        CERT_FOUND=1
        break
    fi
done

if [ "${CERT_FOUND}" -eq 0 ]; then
    log_info "No existing certs found, generating self-signed certs..."
    # Generate CA
    openssl req -x509 -newkey rsa:2048 -nodes -keyout "${WORK_DIR}/conf/ca.key" \
        -out "${WORK_DIR}/conf/ca.crt" -days 365 \
        -subj "/C=CN/ST=GD/L=SZ/O=FISCO-BCOS/OU=CI/CN=CA" 2>/dev/null
    # Generate node key + cert
    openssl req -newkey rsa:2048 -nodes -keyout "${WORK_DIR}/conf/ssl.key" \
        -out "${WORK_DIR}/conf/node.csr" \
        -subj "/C=CN/ST=GD/L=SZ/O=FISCO-BCOS/OU=CI/CN=node0" 2>/dev/null
    openssl x509 -req -in "${WORK_DIR}/conf/node.csr" \
        -CA "${WORK_DIR}/conf/ca.crt" -CAkey "${WORK_DIR}/conf/ca.key" \
        -CAcreateserial -out "${WORK_DIR}/conf/ssl.crt" -days 365 2>/dev/null
    rm -f "${WORK_DIR}/conf/node.csr"
    # Generate node.pem (P-256 keypair for consensus signing)
    openssl ecparam -name prime256v1 -genkey -noout -out "${WORK_DIR}/conf/node.pem" 2>/dev/null

    # Extract the node ID from node.pem (keccak256 of uncompressed pubkey, without 04 prefix)
    NODE_ID=$(python3 -c "
import sys, subprocess, hashlib
pem = open('${WORK_DIR}/conf/node.pem', 'rb').read()
# use openssl to get DER-encoded pubkey
der = subprocess.check_output(['openssl', 'pkey', '-pubout', '-outform', 'DER'], input=pem)
# Skip the 04 prefix byte (uncompressed point indicator) and take last 64 bytes
pub = der.decode('latin-1')[-64:]
# FISCO-BCOS node ID is hex encoding of the 64-byte uncompressed pubkey
print(pub.encode('latin-1').hex())
" 2>/dev/null)
    if [ -z "${NODE_ID}" ]; then
        log_fail "Failed to extract node ID from node.pem"
        exit 1
    fi
    log_info "Node ID: ${NODE_ID}"

    # Verify all required cert files were created
    CERT_MISSING=0
    for f in ca.crt ca.key ssl.crt ssl.key node.pem; do
        if [ ! -f "${WORK_DIR}/conf/${f}" ]; then
            log_info "ERROR: Missing cert file: ${f}"
            CERT_MISSING=1
        fi
    done
    if [ "${CERT_MISSING}" -eq 1 ]; then
        log_fail "Certificate generation failed (macOS may use LibreSSL; check openssl compatibility)"
        exit 1
    fi
    log_info "Self-signed certs generated"
fi

# Replace node.0 placeholder with the actual node ID from node.pem
if [ "$(uname)" = "Darwin" ]; then
    sed -i '' "s/__NODE_ID_PLACEHOLDER__/${NODE_ID}/g" "${WORK_DIR}/config.genesis"
else
    sed -i "s/__NODE_ID_PLACEHOLDER__/${NODE_ID}/g" "${WORK_DIR}/config.genesis"
fi

# Copy certs to work dir root (config expects them there too)
cp "${WORK_DIR}/conf/"* "${WORK_DIR}/" 2>/dev/null || true

# Generate nodes.json - it's just a list of IP:port peers for this node
cat > "${WORK_DIR}/nodes.json" << 'NODES_EOF'
{"nodes":["127.0.0.1:31300"]}
NODES_EOF

log_info "Workspace prepared"
log_pass

# ---- Generate JWT for OP Engine RPC ----
log_section "Step 1b: Generate JWT secret"
openssl rand -hex 32 > "${WORK_DIR}/jwt.hex" 2>/dev/null || \
    python3 -c "import secrets; print(secrets.token_hex(32))" > "${WORK_DIR}/jwt.hex" 2>/dev/null || {
        log_fail "Failed to generate JWT secret"
        exit 1
    }
JWT_TOKEN=$(python3 -c "
import json, time, hmac, hashlib, base64
with open('${WORK_DIR}/jwt.hex','r') as f: secret = bytes.fromhex(f.read().strip())
h = base64.urlsafe_b64encode(json.dumps({'alg':'HS256','typ':'JWT'}).encode()).rstrip(b'=').decode()
p = base64.urlsafe_b64encode(json.dumps({'iat':int(time.time()),'id':'12345678','clv':'FISCO-BCOS'}).encode()).rstrip(b'=').decode()
sig = base64.urlsafe_b64encode(hmac.new(secret, f'{h}.{p}'.encode(), hashlib.sha256).digest()).rstrip(b'=').decode()
print(f'{h}.{p}.{sig}')" 2>/dev/null)
if [ -z "${JWT_TOKEN}" ]; then
    log_fail "Failed to generate JWT token"
    exit 1
fi
log_info "JWT secret + token generated"
log_pass

# ---- Step 2: Start FISCO-BCOS node ----
log_section "Step 2: Start FISCO-BCOS node"

# Enable core dumps for crash diagnostics
ulimit -c unlimited 2>/dev/null || true

cd "${WORK_DIR}"
# Binary daemonizes: nohup only captures parent's brief version output.
# The child writes to a timestamped log file — we must watch that instead.
rm -f log_*.log 2>/dev/null
nohup "${ABS_BINARY}" -c config.genesis -g config.genesis > nohup.out 2>&1 &
NODE_PID=$!
sleep 1

# After daemonization, find the real child PID via pgrep
CHILD_PID=$(pgrep -f "$(basename "${ABS_BINARY}")" | head -1)
if [ -n "${CHILD_PID}" ]; then
    NODE_PID="${CHILD_PID}"
fi
log_info "Node PID: ${NODE_PID}"
echo "${NODE_PID}" > "${PID_FILE}"

# Wait for the log file to appear and the RPC HTTP server to be up
STARTUP_LOG=""
for i in $(seq 1 30); do
    sleep 2
    # Binaries daemonize — if the process is gone, find it again
    if ! kill -0 "${NODE_PID}" 2>/dev/null; then
        NODE_PID=$(pgrep -f "$(basename "${ABS_BINARY}")" | head -1)
    fi
    if [ -z "${NODE_PID}" ]; then
        log_fail "Node process died"
        # Dump any available logs
        for f in log_*.log; do tail -30 "$f"; done
        exit 1
    fi

    # Find the daemonized child's log file
    STARTUP_LOG=$(ls -t log_*.log 2>/dev/null | head -1)
    if [ -n "${STARTUP_LOG}" ]; then
        # Check if the OP Engine HTTP server has started listening
        if grep -q "startListen.*${RPC_PORT}" "${STARTUP_LOG}" 2>/dev/null; then
            # Refresh JWT token now that the node is up
            JWT_TOKEN=$(python3 -c "
import json, time, hmac, hashlib, base64
with open('${WORK_DIR}/jwt.hex','r') as f: secret = bytes.fromhex(f.read().strip())
h = base64.urlsafe_b64encode(json.dumps({'alg':'HS256','typ':'JWT'}).encode()).rstrip(b'=').decode()
p = base64.urlsafe_b64encode(json.dumps({'iat':int(time.time()),'id':'12345678','clv':'FISCO-BCOS'}).encode()).rstrip(b'=').decode()
sig = base64.urlsafe_b64encode(hmac.new(secret, f'{h}.{p}'.encode(), hashlib.sha256).digest()).rstrip(b'=').decode()
print(f'{h}.{p}.{sig}')
" 2>/dev/null)
            RESP=$(rpc_call "eth_chainId" "[]" 2>/dev/null || echo "")
            if echo "${RESP}" | grep -q '"result"'; then
                READY=1
                CHAIN_ID=$(json_val "${RESP}" "")
                log_info "Node ready, PID=${NODE_PID}, chainId=${CHAIN_ID}"
                break
            fi
        fi
    fi
    echo -n "."
done
echo ""

if [ "${READY:-0}" -ne 1 ]; then
    log_fail "Node failed to start within 60s"
    log_info "--- nohup.out ---"
    cat nohup.out 2>/dev/null | tail -30
    if [ -n "${STARTUP_LOG}" ] && [ -f "${STARTUP_LOG}" ]; then
        log_info "--- ${STARTUP_LOG} (last 30 lines) ---"
        tail -30 "${STARTUP_LOG}"
    fi
    exit 1
fi
log_pass

# ---- Step 3: curl smoke tests ----
log_section "Step 3: curl smoke tests"

# 3.1 eth_blockNumber
log_test "eth_blockNumber"
RESP=$(rpc_call "eth_blockNumber" "[]")
if echo "${RESP}" | grep -q '"result"'; then
    BLOCK_NUM=$(json_val "${RESP}" "")
    log_info "blockNumber = ${BLOCK_NUM}"
    log_pass
else
    log_fail "No result: ${RESP}"
fi

# 3.2 engine_exchangeCapabilities — everything implemented, pre-Karst included (B4)
log_test "engine_exchangeCapabilities (implemented surface)"
RESP=$(rpc_call "engine_exchangeCapabilities" '[["engine_newPayloadV4"]]')
if echo "${RESP}" | grep -q '"result"'; then
    MISSING=""
    for CAP in engine_exchangeCapabilities \
        engine_forkchoiceUpdatedV1 engine_forkchoiceUpdatedV2 engine_forkchoiceUpdatedV3 \
        engine_getPayloadV1 engine_getPayloadV2 engine_getPayloadV3 \
        engine_getPayloadV4 engine_getPayloadV5 \
        engine_newPayloadV1 engine_newPayloadV2 engine_newPayloadV3 engine_newPayloadV4; do
        echo "${RESP}" | grep -q "\"${CAP}\"" || MISSING="${MISSING} ${CAP}"
    done
    # forkchoiceUpdatedV4 is not implemented, so must not be advertised.
    EXTRA=""
    for CAP in engine_forkchoiceUpdatedV4; do
        echo "${RESP}" | grep -q "\"${CAP}\"" && EXTRA="${EXTRA} ${CAP}"
    done
    if [ -z "${MISSING}" ] && [ -z "${EXTRA}" ]; then
        log_pass
    else
        log_fail "capabilities missing:${MISSING:- none} unexpected:${EXTRA:- none} — ${RESP}"
    fi
else
    log_fail "No result: ${RESP}"
fi

# Zero beacon root: this mock CL has no beacon chain.
ZERO_ROOT="0x0000000000000000000000000000000000000000000000000000000000000000"
# Engine wire timestamps are Unix seconds.
TS_SECS=$(date +%s)
TS_HEX=$(printf '0x%x' "${TS_SECS}")

# 3.3 engine_forkchoiceUpdatedV3 (without payloadAttributes)
log_test "engine_forkchoiceUpdatedV3 (no payload)"
HEAD_RESP=$(rpc_call "eth_getBlockByNumber" '["latest",false]')
HEAD_HASH=$(json_val "${HEAD_RESP}" "hash")

if [ -n "${HEAD_HASH}" ]; then
    RESP=$(rpc_call "engine_forkchoiceUpdatedV3" \
        "[{\"headBlockHash\":\"${HEAD_HASH}\",\"safeBlockHash\":\"${HEAD_HASH}\",\"finalizedBlockHash\":\"${HEAD_HASH}\"},null]")
    if echo "${RESP}" | grep -q '"result"'; then
        STATUS=$(json_fcu_status "${RESP}")
        log_info "status = ${STATUS}"
        if [ "${STATUS}" = "VALID" ] || [ "${STATUS}" = "SYNCING" ]; then
            log_pass
        else
            log_fail "Unexpected status: ${STATUS}"
        fi
    else
        log_fail "No result: ${RESP}"
    fi
else
    log_fail "Cannot get head hash"
fi

# 3.4 engine_forkchoiceUpdatedV3 (with V3 payloadAttributes: withdrawals + beacon root)
log_test "engine_forkchoiceUpdatedV3 (with payload)"
PAYLOAD_ID=""
if [ -n "${HEAD_HASH}" ]; then
    RESP=$(rpc_call "engine_forkchoiceUpdatedV3" \
        "[{\"headBlockHash\":\"${HEAD_HASH}\",\"safeBlockHash\":\"${HEAD_HASH}\",\"finalizedBlockHash\":\"${HEAD_HASH}\"},{\"timestamp\":\"${TS_HEX}\",\"prevRandao\":\"0x0000000000000000000000000000000000000000000000000000000000000001\",\"suggestedFeeRecipient\":\"0x0000000000000000000000000000000000000001\",\"withdrawals\":[],\"parentBeaconBlockRoot\":\"${ZERO_ROOT}\"}]")
    if echo "${RESP}" | grep -q '"result"'; then
        STATUS=$(json_fcu_status "${RESP}")
        PAYLOAD_ID=$(json_val "${RESP}" "payloadId")
        log_info "status=${STATUS}, payloadId=${PAYLOAD_ID:-none}"
        if [ "${STATUS}" = "VALID" ] || [ "${STATUS}" = "SYNCING" ]; then
            log_pass
        else
            log_fail "Unexpected status: ${STATUS}"
        fi
    else
        log_fail "No result: ${RESP}"
    fi
else
    log_fail "No head hash available"
fi

# 3.5 engine_getPayloadV5 + engine_newPayloadV4
log_test "engine_getPayloadV5 + engine_newPayloadV4"
if [ -n "${PAYLOAD_ID:-}" ]; then
    # getPayload
    GET_RESP=$(rpc_call "engine_getPayloadV5" "[\"${PAYLOAD_ID}\"]")
    if echo "${GET_RESP}" | grep -q '"blockHash"'; then
        # V5 response shape: executionRequests must be present.
        if ! echo "${GET_RESP}" | grep -q '"executionRequests"'; then
            log_fail "getPayloadV5 response missing executionRequests: ${GET_RESP}"
        fi
        # Extract payload and feed to newPayloadV4 with the required
        # [payload, [], beaconRoot, []] parameter shape. Use a temp file to
        # avoid shell quoting issues with the multi-KB JSON.
        NEW_REQ_FILE="${WORK_DIR}/newPayload_req.json"
        echo "${GET_RESP}" | python3 -c "
import sys,json
d=json.load(sys.stdin)['result']
p=d['executionPayload'] if 'executionPayload' in d else d
root=d.get('parentBeaconBlockRoot','${ZERO_ROOT}')
print(json.dumps({'jsonrpc':'2.0','id':1,'method':'engine_newPayloadV4','params':[p,[],root,[]]}))
" 2>/dev/null > "${NEW_REQ_FILE}"
        NEW_RESP=$(curl -s -X POST "${RPC_URL}" \
            -H "Content-Type: application/json" \
            -H "Authorization: Bearer ${JWT_TOKEN}" \
            -d "@${NEW_REQ_FILE}" 2>/dev/null || echo '{}')
        NEW_STATUS=$(json_val "${NEW_RESP}" "status")
        # VALID only: ACCEPTED means the node acknowledged the block without validating or
        # storing it, the escape M9 removed. Accepting it here would hide its return.
        log_info "newPayloadV4 status = ${NEW_STATUS}"
        if [ "${NEW_STATUS}" = "VALID" ]; then
            log_pass
        else
            log_fail "newPayloadV4 did not answer VALID: ${NEW_RESP}"
        fi

        # The Engine boundary speaks Unix seconds; block headers store milliseconds. The
        # committed block is what proves the conversion happened: a missing conversion is
        # symmetric on the wire (parse and serialize cancel out) but lands the seconds in
        # the header as milliseconds, so eth_getBlockByNumber — which divides the header
        # timestamp by 1000 — reports a 1970 timestamp.
        log_test "committed block carries the Unix-seconds timestamp we sent"
        BLOCK_NUM_HEX=$(echo "${GET_RESP}" | python3 -c "
import sys,json
d=json.load(sys.stdin)['result']
p=d['executionPayload'] if 'executionPayload' in d else d
print(p['blockNumber'])
" 2>/dev/null || echo "")
        BLOCK_TS=""
        if [ -n "${BLOCK_NUM_HEX}" ]; then
            BLOCK_RESP=$(rpc_call "eth_getBlockByNumber" "[\"${BLOCK_NUM_HEX}\",false]")
            BLOCK_TS=$(echo "${BLOCK_RESP}" | python3 -c "
import sys,json
print(int(json.load(sys.stdin)['result']['timestamp'],16))
" 2>/dev/null || echo "")
        fi
        if [ -n "${BLOCK_TS}" ] && \
           [ "$((BLOCK_TS > TS_SECS ? BLOCK_TS - TS_SECS : TS_SECS - BLOCK_TS))" -le 1 ]; then
            log_info "block ${BLOCK_NUM_HEX} timestamp = ${BLOCK_TS} (sent ${TS_SECS})"
            log_pass
        else
            log_fail "block timestamp ${BLOCK_TS:-<none>} is not the ${TS_SECS} seconds we sent"
        fi
    else
        log_fail "getPayload failed: ${GET_RESP}"
    fi
else
    log_info "Skipping (no payloadId from forkchoiceUpdated)"
    log_pass
fi

# 3.6 pre-Karst surface still works: the V2 build/fetch/submit loop end to end.
log_test "engine_forkchoiceUpdatedV2 + getPayloadV2 + newPayloadV2 (pre-Karst)"
V2_HEAD=$(json_val "$(rpc_call "eth_getBlockByNumber" '["latest",false]')" "hash")
V2_TS_SECS=$((TS_SECS + 12))
V2_TS_HEX=$(printf '0x%x' "${V2_TS_SECS}")
if [ -n "${V2_HEAD}" ]; then
    RESP=$(rpc_call "engine_forkchoiceUpdatedV2" \
        "[{\"headBlockHash\":\"${V2_HEAD}\",\"safeBlockHash\":\"${V2_HEAD}\",\"finalizedBlockHash\":\"${V2_HEAD}\"},{\"timestamp\":\"${V2_TS_HEX}\",\"prevRandao\":\"0x0000000000000000000000000000000000000000000000000000000000000001\",\"suggestedFeeRecipient\":\"0x0000000000000000000000000000000000000001\",\"withdrawals\":[]}]")
    V2_PAYLOAD_ID=$(json_val "${RESP}" "payloadId")
    if echo "${RESP}" | grep -q '\-38005'; then
        log_fail "forkchoiceUpdatedV2 was rejected as an unsupported fork: ${RESP}"
    elif [ -z "${V2_PAYLOAD_ID}" ]; then
        log_fail "forkchoiceUpdatedV2 built no payload: ${RESP}"
    else
        GET_RESP=$(rpc_call "engine_getPayloadV2" "[\"${V2_PAYLOAD_ID}\"]")
        # V2 response shape: executionPayload + blockValue, no blobsBundle.
        if ! echo "${GET_RESP}" | grep -q '"blockValue"' \
            || echo "${GET_RESP}" | grep -q '"blobsBundle"'; then
            log_fail "getPayloadV2 did not answer in the V2 shape: ${GET_RESP}"
        else
            V2_REQ_FILE="${WORK_DIR}/newPayloadV2_req.json"
            echo "${GET_RESP}" | python3 -c "
import sys,json
d=json.load(sys.stdin)['result']
p=d['executionPayload'] if 'executionPayload' in d else d
print(json.dumps({'jsonrpc':'2.0','id':1,'method':'engine_newPayloadV2','params':[p]}))
" 2>/dev/null > "${V2_REQ_FILE}"
            V2_RESP=$(curl -s -X POST "${RPC_URL}" \
                -H "Content-Type: application/json" \
                -H "Authorization: Bearer ${JWT_TOKEN}" \
                -d "@${V2_REQ_FILE}" 2>/dev/null || echo '{}')
            V2_STATUS=$(json_val "${V2_RESP}" "status")
            log_info "newPayloadV2 status = ${V2_STATUS}"
            if [ "${V2_STATUS}" = "VALID" ]; then
                log_pass
            else
                log_fail "Unexpected newPayloadV2 status: ${V2_RESP}"
            fi
        fi
    fi
else
    log_fail "Cannot get head hash"
fi

# 3.7 the one unimplemented method version answers -38005 (not method-not-found)
log_test "engine_forkchoiceUpdatedV4 answers -38005 (not implemented)"
RESP=$(rpc_call "engine_forkchoiceUpdatedV4" \
    "[{\"headBlockHash\":\"${HEAD_HASH}\",\"safeBlockHash\":\"${HEAD_HASH}\",\"finalizedBlockHash\":\"${HEAD_HASH}\"},null]")
if echo "${RESP}" | grep -q '\-38005'; then
    log_pass
else
    log_fail "Expected -38005 Unsupported fork, got: ${RESP}"
fi

# ---- Step 4: Python mock consensus client ----
log_section "Step 4: Python mock CL test"

PYTHON_SCRIPT="${ROOT_DIR}/tests/mock_consensus_client.py"
if [ -f "${PYTHON_SCRIPT}" ]; then
    # Ensure 'requests' is installed
    python3 -c "import requests" 2>/dev/null || \
        pip3 install --quiet requests 2>/dev/null || \
        pip3 install --break-system-packages --quiet requests 2>/dev/null || true
    if python3 "${PYTHON_SCRIPT}" "${RPC_URL}" "${WORK_DIR}/jwt.hex" 2>&1; then
        log_info "Python tests passed"
        log_pass
    else
        log_fail "Python tests failed (see output above)"
    fi
else
    log_info "Python script not found at ${PYTHON_SCRIPT}, skipping"
    log_pass
fi

# ---- Summary ----
log_section "Results"
echo -e "  ${GREEN}Passed: ${PASSED}${NC}"
echo -e "  ${RED}Failed: ${FAILED}${NC}"

if [ "${FAILED}" -gt 0 ]; then
    echo ""
    echo -e "${RED}ENGINE INTEGRATION TEST FAILED${NC}"
    exit 1
fi

echo ""
echo -e "${GREEN}ENGINE INTEGRATION TEST PASSED${NC}"
exit 0
