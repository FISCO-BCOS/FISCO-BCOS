#!/usr/bin/env bash
# =============================================================================
# FISCO-BCOS + op-node Integration Helper
#
# 用途：一键准备 op-node 运行环境并启动对接 FISCO-BCOS 节点
# 前置：FISCO-BCOS 节点已启动 (Engine API + JSON-RPC)
# 用法：./tests/run_opnode_dev.sh [ENGINE_RPC_URL]
#
# op-node 是 Optimism Stack 的 rollup 节点，负责：
#   1. 从 L1 读取交易批次和状态根
#   2. 通过 Engine API 驱动 L2 执行引擎（此处为 FISCO-BCOS）
#   3. 将 L2 交易结果提交回 L1
#
# 本测试将 FISCO-BCOS 同时作为 L1 和 L2 引擎，验证：
#   - engine_exchangeCapabilities
#   - engine_forkchoiceUpdatedV2/V3
#   - engine_getPayloadV2/V3
#   - engine_newPayloadV2/V3
# =============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
# FISCO-BCOS JSON-RPC URL (also used as L1 endpoint for op-node)
RPC_URL="${1:-http://127.0.0.1:8645}"
# Engine API uses same URL (FISCO-BCOS does not use separate authrpc port)
ENGINE_URL="${RPC_URL}"
JWT_FILE="${SCRIPT_DIR}/jwt.hex"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo "============================================"
echo "  op-node Integration Setup"
echo "============================================"
echo ""

# ---- Step 1: Build op-node binary (via go install) ----
echo "[1/5] Preparing op-node binary..."

if ! command -v go &>/dev/null; then
    echo "  ERROR: Go not found. Install: https://go.dev/doc/install"
    exit 1
fi
GO_VERSION=$(go version)
echo "  ${GO_VERSION}"

# Check Go version >= 1.24 (required by op-node)
GO_VER_NUM=$(go version | grep -oP 'go\K[0-9]+\.[0-9]+' | head -1)
if [ "$(printf '%s\n' "1.24" "${GO_VER_NUM}" | sort -V | head -1)" != "1.24" ]; then
    echo "  ERROR: Go >= 1.24 required, found ${GO_VER_NUM}"
    echo "  Install: https://go.dev/doc/install"
    exit 1
fi

OP_NODE_BINARY="${SCRIPT_DIR}/op-node"
OP_NODE_VERSION="${OP_NODE_VERSION:-op-node/v1.19.0}"
if [ -f "${OP_NODE_BINARY}" ] && [ -x "${OP_NODE_BINARY}" ]; then
    echo "  Using existing op-node binary: ${OP_NODE_BINARY}"
else
    echo "  Building op-node from source (${OP_NODE_VERSION}, may take a few minutes)..."
    BUILD_DIR="${SCRIPT_DIR}/op_node_build"
    rm -rf "${BUILD_DIR}"
    mkdir -p "${BUILD_DIR}"
    # Shallow clone + build (go install fails with monorepo replace directives)
    git clone --depth 1 --branch "${OP_NODE_VERSION}" \
        https://github.com/ethereum-optimism/optimism.git "${BUILD_DIR}" 2>&1 || {
        echo "  ERROR: Failed to clone optimism repo. Check network."
        rm -rf "${BUILD_DIR}"
        exit 1
    }
    cd "${BUILD_DIR}/op-node"
    GOTOOLCHAIN=local go build -o "${OP_NODE_BINARY}" ./cmd 2>&1 || {
        echo "  ERROR: Failed to build op-node."
        cd "${SCRIPT_DIR}"
        rm -rf "${BUILD_DIR}"
        exit 1
    }
    cd "${SCRIPT_DIR}"
    rm -rf "${BUILD_DIR}"
    chmod +x "${OP_NODE_BINARY}"
    echo "  op-node binary built: ${OP_NODE_BINARY}"
fi

OP_NODE_VERSION_OUT=$("${OP_NODE_BINARY}" --version 2>/dev/null || echo "unknown")
echo "  op-node version: ${OP_NODE_VERSION_OUT}"

# ---- Step 2: Generate JWT secret ----
echo "[2/5] Generating JWT secret..."
openssl rand -hex 32 > "${JWT_FILE}" 2>/dev/null || \
    python3 -c "import secrets; print(secrets.token_hex(32))" > "${JWT_FILE}" 2>/dev/null || {
    echo "  ERROR: Cannot generate JWT secret"
    exit 1
}
echo "  JWT secret written to ${JWT_FILE}"

# ---- Step 3: Check RPC connectivity ----
echo "[3/5] Checking RPC connectivity to ${RPC_URL}..."
if curl -s -X POST "${RPC_URL}" \
    -H "Content-Type: application/json" \
    -d '{"jsonrpc":"2.0","id":1,"method":"eth_chainId","params":[]}' \
    | grep -q '"result"'; then
    echo "  RPC reachable ✓"
else
    echo "  ERROR: Cannot reach ${RPC_URL}"
    echo "  Make sure the FISCO-BCOS node is running with web3_rpc enabled."
    exit 1
fi

# Get chain ID for rollup config
CHAIN_ID_HEX=$(curl -s -X POST "${RPC_URL}" \
    -H "Content-Type: application/json" \
    -d '{"jsonrpc":"2.0","id":1,"method":"eth_chainId","params":[]}' \
    | python3 -c "import sys,json; d=json.load(sys.stdin); print(d.get('result','0x0'))" 2>/dev/null || echo "0x0")
CHAIN_ID_DEC=$(python3 -c "h='${CHAIN_ID_HEX}'; print(int(h,16) if h.startswith('0x') else 0)")
echo "  Chain ID: ${CHAIN_ID_HEX} (${CHAIN_ID_DEC})"

# Get L1 genesis block hash (required by op-node v1.19.0+; empty/zero hash rejected)
GENESIS_RESP=$(curl -s -X POST "${RPC_URL}" \
    -H "Content-Type: application/json" \
    -d '{"jsonrpc":"2.0","id":1,"method":"eth_getBlockByNumber","params":["0x0",false]}')
L1_GENESIS_HASH=$(echo "${GENESIS_RESP}" | python3 -c "
import sys,json
d=json.load(sys.stdin)
b=d.get('result',{})
h=b.get('hash','')
if not h or h == '0x' + '0'*64:
    h='0x' + 'ab'*32
print(h)
" 2>/dev/null || echo "0x$(printf 'ab%.0s' $(seq 1 32))")
L1_GENESIS_NUMBER=$(echo "${GENESIS_RESP}" | python3 -c "
import sys,json
d=json.load(sys.stdin)
b=d.get('result',{})
n=b.get('number','0x0')
print(int(n,16) if isinstance(n,str) and n.startswith('0x') else 0)
" 2>/dev/null || echo "0")
echo "  L1 Genesis Hash: ${L1_GENESIS_HASH}"
echo "  L1 Genesis Number: ${L1_GENESIS_NUMBER}"

# ---- Step 4: Generate rollup config ----
echo "[4/5] Generating rollup config..."

ROLLUP_CONFIG="${SCRIPT_DIR}/rollup.json"
# Generate via python3 to use actual genesis hash and chain IDs
python3 -c "
import json
cfg = {
  'genesis': {
    'l1': {
      'hash': '${L1_GENESIS_HASH}',
      'number': ${L1_GENESIS_NUMBER}
    },
    'l2': {
      'hash': '${L1_GENESIS_HASH}',
      'number': 0
    },
    'l2_time': 0,
    'system_config': {
      'batcherAddr': '0x0000000000000000000000000000000000000001',
      'overhead': '0x0000000000000000000000000000000000000000000000000000000000000000',
      'scalar': '0x0000000000000000000000000000000000000000000000000000000000000000',
      'gasLimit': 30000000,
      'operatorFeeScalar': 0,
      'operatorFeeConstant': 0
    }
  },
  'block_time': 2,
  'max_sequencer_drift': 600,
  'seq_window_size': 3600,
  'channel_timeout': 300,
  'l1_chain_id': ${CHAIN_ID_DEC},
  'l2_chain_id': ${CHAIN_ID_DEC},
  'regolith_time': 0,
  'canyon_time': 0,
  'delta_time': 0,
  'ecotone_time': 0,
  'fjord_time': 0,
  'granite_time': 0,
  'holocene_time': 0,
  'batch_inbox_address': '0x0000000000000000000000000000000000000000',
  'deposit_contract_address': '0x0000000000000000000000000000000000000000',
  'l1_system_config_address': '0x0000000000000000000000000000000000000000',
  'protocol_versions_address': '0x0000000000000000000000000000000000000000'
}
with open('${ROLLUP_CONFIG}', 'w') as f:
    json.dump(cfg, f, indent=2)
"
echo "  Rollup config written to ${ROLLUP_CONFIG}"

# ---- Step 4b: Generate L1 chain config ----
echo "[4b/5] Generating L1 chain config..."

L1_CHAIN_CONFIG="${SCRIPT_DIR}/l1_chain_config.json"
python3 -c "
import json
cfg = {
  'config': {
    'chainId': ${CHAIN_ID_DEC},
    'homesteadBlock': 0,
    'eip150Block': 0,
    'eip155Block': 0,
    'eip158Block': 0,
    'byzantiumBlock': 0,
    'constantinopleBlock': 0,
    'petersburgBlock': 0,
    'istanbulBlock': 0,
    'berlinBlock': 0,
    'londonBlock': 0,
    'mergeNetsplitBlock': 0,
    'shanghaiTime': 0,
    'cancunTime': 0,
    'pragueTime': 0,
    'terminalTotalDifficulty': 0,
    'optimism': {
      'eip1559Elasticity': 6,
      'eip1559Denominator': 50
    }
  }
}
with open('${L1_CHAIN_CONFIG}', 'w') as f:
    json.dump(cfg, f, indent=2)
"
echo "  L1 chain config written to ${L1_CHAIN_CONFIG}"

# ---- Step 5: Run op-node ----
echo "[5/5] Starting op-node..."
echo ""
echo "  Engine URL:    ${ENGINE_URL}"
echo "  L1 RPC URL:    ${RPC_URL}"
echo "  JWT Secret:    ${JWT_FILE}"
echo "  Rollup Config: ${ROLLUP_CONFIG}"
echo ""
echo "  op-node will:"
echo "    1. Connect to L1 (${RPC_URL}) for deposit feeds"
echo "    2. Call engine_exchangeCapabilities on L2 engine"
echo "    3. Call engine_forkchoiceUpdatedV3 to sync L2 chain"
echo "    4. Build and submit L2 blocks via engine_getPayloadV3"
echo "    5. Validate blocks via engine_newPayloadV3"
echo ""
echo "  Watch for:"
echo "    ✓ 'Starting op-node' version info"
echo "    ✓ 'Connected to L1' or 'EL sync in progress'"
echo "    ✓ Engine API method calls (exchangeCapabilities, forkchoiceUpdated)"
echo "    ✓ No persistent connection errors"
echo ""
echo "  Press Ctrl+C to stop."
echo "============================================"
echo ""

# Run op-node with:
#   --l1             : FISCO-BCOS as L1 endpoint
#   --l2             : FISCO-BCOS Engine API (same as JSON-RPC since no separate authrpc)
#   --l2.jwt-secret  : JWT secret file
#   --rollup.config  : Rollup configuration
#   --sequencer.enabled : Enable sequencing (for block production testing)
#   --p2p.disable    : Disable P2P (not needed for single-node test)
#   --rpc.addr       : Bind address for op-node's own RPC
#   --rpc.port       : Port for op-node's admin RPC
exec "${OP_NODE_BINARY}" \
    --l1="${RPC_URL}" \
    --l1.beacon="${RPC_URL}" \
    --l2="${ENGINE_URL}" \
    --l2.jwt-secret="${JWT_FILE}" \
    --rollup.config="${ROLLUP_CONFIG}" \
    --rollup.l1-chain-config="${L1_CHAIN_CONFIG}" \
    --syncmode=execution-layer \
    --sequencer.enabled \
    --p2p.disable \
    --rpc.addr=0.0.0.0 \
    --rpc.port=19545
