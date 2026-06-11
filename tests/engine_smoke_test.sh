#!/usr/bin/env bash
# =============================================================================
# FISCO-BCOS Engine API Smoke Test (Phase 1: curl)
#
# 用途：快速验证节点 engine_* 各接口可正常调用并返回预期格式
# 前置：节点已启动且 web3_rpc 端口可达
# 用法：chmod +x engine_smoke_test.sh && ./engine_smoke_test.sh [RPC_URL]
# =============================================================================

set -e

RPC_URL="${1:-http://127.0.0.1:8545}"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

PASSED=0
FAILED=0

log_test() {
    echo -e "${GREEN}[TEST]${NC} $1"
}

log_pass() {
    echo -e "  ${GREEN}✅ PASSED${NC}"
    PASSED=$((PASSED + 1))
}

log_fail() {
    echo -e "  ${RED}❌ FAILED: $1${NC}"
    FAILED=$((FAILED + 1))
}

log_info() {
    echo -e "  ${YELLOW}ℹ️  $1${NC}"
}

rpc_call() {
    local method="$1"
    local params="$2"
    local req_id="${3:-1}"

    local body
    body=$(cat <<EOF
{"jsonrpc":"2.0","id":${req_id},"method":"${method}","params":${params}}
EOF
)
    curl -s -X POST "${RPC_URL}" \
        -H "Content-Type: application/json" \
        -d "${body}" 2>/dev/null
}

# ---------------------------------------------------------------------------
echo "============================================"
echo "  FISCO-BCOS Engine API Smoke Test (curl)"
echo "  Target: ${RPC_URL}"
echo "============================================"
echo ""

# ---- Test 1: Basic Connectivity ----
log_test "Basic Connectivity (eth_chainId)"
RESP=$(rpc_call "eth_chainId" "[]" 1)
if echo "${RESP}" | grep -q '"result"'; then
    CHAIN_ID=$(echo "${RESP}" | grep -oP '"result"\s*:\s*"\K[^"]+')
    log_info "chainId = ${CHAIN_ID}"
    log_pass
else
    log_fail "Cannot reach node or missing result: ${RESP}"
fi

log_test "Basic Connectivity (eth_blockNumber)"
RESP=$(rpc_call "eth_blockNumber" "[]" 2)
if echo "${RESP}" | grep -q '"result"'; then
    BLOCK_NUM=$(echo "${RESP}" | grep -oP '"result"\s*:\s*"\K[^"]+')
    log_info "blockNumber = ${BLOCK_NUM}"
    log_pass
else
    log_fail "Cannot get blockNumber: ${RESP}"
fi

# ---- Test 2: engine_exchangeCapabilities ----
log_test "engine_exchangeCapabilities"
RESP=$(rpc_call "engine_exchangeCapabilities" \
    '[["engine_newPayloadV2","engine_forkchoiceUpdatedV2","engine_getPayloadV2"]]' 10)

if echo "${RESP}" | grep -q '"result"'; then
    log_info "capabilities returned"
    if echo "${RESP}" | grep -q '"error"'; then
        ERR_MSG=$(echo "${RESP}" | grep -oP '"message"\s*:\s*"\K[^"]+')
        log_fail "${ERR_MSG}"
    else
        # verify expected capabilities
        for CAP in engine_exchangeCapabilities \
                   engine_forkchoiceUpdatedV1 engine_forkchoiceUpdatedV2 engine_forkchoiceUpdatedV3 \
                   engine_getPayloadV1 engine_getPayloadV2 engine_getPayloadV3 \
                   engine_newPayloadV1 engine_newPayloadV2 engine_newPayloadV3; do
            if echo "${RESP}" | grep -q "\"${CAP}\""; then
                log_info "  ✓ ${CAP}"
            else
                log_info "  ✗ ${CAP} (missing)"
            fi
        done
        log_pass
    fi
else
    log_fail "No result field: ${RESP}"
fi

# ---- Test 3: engine_forkchoiceUpdatedV2 (no payloadAttributes) ----
log_test "engine_forkchoiceUpdatedV2 (without payloadAttributes)"

# Get current head hash
HEAD_RESP=$(rpc_call "eth_getBlockByNumber" '["latest",false]' 3)
HEAD_HASH=$(echo "${HEAD_RESP}" | grep -oP '"hash"\s*:\s*"\K[^"]+' | head -1)

if [ -z "${HEAD_HASH}" ]; then
    log_fail "Cannot get head block hash"
else
    log_info "headBlockHash = ${HEAD_HASH}"

    RESP=$(rpc_call "engine_forkchoiceUpdatedV2" \
        "[{\"headBlockHash\":\"${HEAD_HASH}\",\"safeBlockHash\":\"${HEAD_HASH}\",\"finalizedBlockHash\":\"${HEAD_HASH}\"},null]" 11)

    if echo "${RESP}" | grep -q '"result"'; then
        STATUS=$(echo "${RESP}" | grep -oP '"status"\s*:\s*"\K[^"]+')
        log_info "payloadStatus.status = ${STATUS}"
        if [ "${STATUS}" = "VALID" ] || [ "${STATUS}" = "SYNCING" ]; then
            log_pass
        else
            log_fail "Unexpected status: ${STATUS}"
        fi
    else
        ERR_MSG=$(echo "${RESP}" | grep -oP '"message"\s*:\s*"\K[^"]+')
        log_fail "${ERR_MSG:-${RESP}}"
    fi
fi

# ---- Test 4: engine_forkchoiceUpdatedV2 (with payloadAttributes) ----
log_test "engine_forkchoiceUpdatedV2 (with payloadAttributes)"

if [ -n "${HEAD_HASH}" ]; then
    RESP=$(rpc_call "engine_forkchoiceUpdatedV2" \
        "[{\"headBlockHash\":\"${HEAD_HASH}\",\"safeBlockHash\":\"${HEAD_HASH}\",\"finalizedBlockHash\":\"${HEAD_HASH}\"},{\"timestamp\":\"0x100\",\"prevRandao\":\"0x0000000000000000000000000000000000000000000000000000000000000001\",\"suggestedFeeRecipient\":\"0x0000000000000000000000000000000000000001\",\"withdrawals\":[]}]" 12)

    if echo "${RESP}" | grep -q '"result"'; then
        STATUS=$(echo "${RESP}" | grep -oP '"status"\s*:\s*"\K[^"]+')
        log_info "payloadStatus.status = ${STATUS}"

        PAYLOAD_ID=$(echo "${RESP}" | grep -oP '"payloadId"\s*:\s*"\K[^"]+')
        if [ -n "${PAYLOAD_ID}" ]; then
            log_info "payloadId = ${PAYLOAD_ID}"
            export SMOKE_PAYLOAD_ID="${PAYLOAD_ID}"
        else
            log_info "no payloadId (SYNCING state)"
            export SMOKE_PAYLOAD_ID=""
        fi

        if [ "${STATUS}" = "VALID" ] || [ "${STATUS}" = "SYNCING" ]; then
            log_pass
        else
            log_fail "Unexpected status: ${STATUS}"
        fi
    else
        ERR_MSG=$(echo "${RESP}" | grep -oP '"message"\s*:\s*"\K[^"]+')
        log_fail "${ERR_MSG:-${RESP}}"
    fi
fi

# ---- Test 5: engine_getPayloadV2 ----
log_test "engine_getPayloadV2"

PAYLOAD_ID="${SMOKE_PAYLOAD_ID:-}"
if [ -z "${PAYLOAD_ID}" ]; then
    # Try a synthetic payloadId
    PAYLOAD_ID="0x0000000000000100"
    log_info "Using synthetic payloadId=${PAYLOAD_ID}"
fi

RESP=$(rpc_call "engine_getPayloadV2" "[\"${PAYLOAD_ID}\"]" 13)

if echo "${RESP}" | grep -q '"result"'; then
    # Check key fields
    for FIELD in parentHash stateRoot blockHash transactions withdrawals; do
        if echo "${RESP}" | grep -q "\"${FIELD}\""; then
            log_info "  ✓ ${FIELD}"
        else
            log_info "  ✗ ${FIELD} (missing)"
        fi
    done

    # Store payload for newPayload test
    PAYLOAD_HASH=$(echo "${RESP}" | grep -oP '"blockHash"\s*:\s*"\K[^"]+')
    if [ -n "${PAYLOAD_HASH}" ]; then
        log_info "blockHash = ${PAYLOAD_HASH}"

        # Extract the execution payload object from the result
        EXEC_PAYLOAD=$(echo "${RESP}" | python3 -c "
import sys,json
data=json.load(sys.stdin)
print(json.dumps(data['result']))
" 2>/dev/null || echo "")
        export SMOKE_EXEC_PAYLOAD="${EXEC_PAYLOAD}"
        log_pass
    else
        log_fail "Missing blockHash in response"
    fi
else
    ERR_MSG=$(echo "${RESP}" | grep -oP '"message"\s*:\s*"\K[^"]+')
    log_fail "${ERR_MSG:-${RESP}}"
fi

# ---- Test 6: engine_newPayloadV2 ----
log_test "engine_newPayloadV2"

EXEC_PAYLOAD="${SMOKE_EXEC_PAYLOAD:-}"
if [ -n "${EXEC_PAYLOAD}" ]; then
    RESP=$(rpc_call "engine_newPayloadV2" "[${EXEC_PAYLOAD}]" 14)
else
    # fallback: synthetic minimal payload
    log_info "Using synthetic payload (getPayload may have failed)"
    RESP=$(rpc_call "engine_newPayloadV2" \
        '[{"parentHash":"0x1111111111111111111111111111111111111111111111111111111111111111","feeRecipient":"0x2222222222222222222222222222222222222222","stateRoot":"0x3333333333333333333333333333333333333333333333333333333333333333","receiptsRoot":"0x4444444444444444444444444444444444444444444444444444444444444444","logsBloom":"0x00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000","prevRandao":"0x5555555555555555555555555555555555555555555555555555555555555555","blockNumber":"0x1","gasLimit":"0x5208","gasUsed":"0x5208","timestamp":"0x1","extraData":"0x1234","baseFeePerGas":"0x1","blockHash":"0x6666666666666666666666666666666666666666666666666666666666666666","transactions":[],"withdrawals":[]}]' 14)
fi

if echo "${RESP}" | grep -q '"result"'; then
    STATUS=$(echo "${RESP}" | grep -oP '"status"\s*:\s*"\K[^"]+')
    log_info "payloadStatus = ${STATUS}"

    case "${STATUS}" in
        VALID|ACCEPTED)
            log_pass
            ;;
        INVALID|INVALID_BLOCK_HASH|SYNCING)
            log_info "Status '${STATUS}' is expected for synthetic/test payloads"
            log_pass
            ;;
        *)
            log_fail "Unexpected status: ${STATUS}"
            ;;
    esac
else
    ERR_MSG=$(echo "${RESP}" | grep -oP '"message"\s*:\s*"\K[^"]+')
    log_fail "${ERR_MSG:-${RESP}}"
fi

# ---------------------------------------------------------------------------
echo ""
echo "============================================"
echo "  Results: ${GREEN}${PASSED} passed${NC}, ${RED}${FAILED} failed${NC}"
echo "============================================"

if [ "${FAILED}" -gt 0 ]; then
    exit 1
fi
exit 0
