#!/usr/bin/env bash
# =============================================================================
# FISCO-BCOS + Lodestar Integration Helper (Phase 3)
#
# 用途：一键准备 Lodestar 运行环境并启动 dev 模式对接节点
# 前置：Node.js >= 18, FISCO-BCOS 节点已启动
# 用法：./tests/run_lodestar_dev.sh [RPC_URL]
#
# 注意：
#   - 当前 FISCO-BCOS 节点的 web3_rpc 不做 JWT 鉴权，
#     Lodestar 发送的 Authorization header 会被忽略。
#   - 本脚本仍会生成 jwt.hex 供 Lodestar 侧使用（Lodestar 要求此参数）。
# =============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
RPC_URL="${1:-http://127.0.0.1:8545}"
JWT_FILE="${SCRIPT_DIR}/jwt.hex"

echo "============================================"
echo "  Lodestar Dev Mode Setup"
echo "============================================"
echo ""

# ---- Step 1: Check Node.js ----
echo "[1/4] Checking Node.js..."
if ! command -v node &>/dev/null; then
    echo "  ERROR: Node.js not found. Install with:"
    echo "    curl -fsSL https://deb.nodesource.com/setup_20.x | sudo -E bash -"
    echo "    sudo apt install -y nodejs"
    exit 1
fi

NODE_VERSION=$(node --version)
echo "  Node.js ${NODE_VERSION} ✓"

# ---- Step 2: Generate JWT secret (for Lodestar internal use) ----
echo "[2/4] Generating JWT secret..."
openssl rand -hex 32 > "${JWT_FILE}"
echo "  JWT secret written to ${JWT_FILE}"
echo "  ℹ️  FISCO-BCOS currently does NOT validate JWT — this is for Lodestar only."

# ---- Step 3: Check RPC connectivity ----
echo "[3/4] Checking RPC connectivity to ${RPC_URL}..."
if curl -s -X POST "${RPC_URL}" \
    -H "Content-Type: application/json" \
    -d '{"jsonrpc":"2.0","id":1,"method":"eth_chainId","params":[]}' \
    | grep -q '"result"'; then
    echo "  RPC reachable ✓"
else
    echo "  WARNING: Cannot reach ${RPC_URL}"
    echo "  Make sure the FISCO-BCOS node is running with web3_rpc enabled."
    exit 1
fi

# ---- Step 4: Run Lodestar ----
echo "[4/4] Starting Lodestar in dev mode..."
echo ""
echo "  Execution URL: ${RPC_URL}"
echo "  JWT Secret:    ${JWT_FILE}"
echo ""
echo "  Lodestar will:"
echo "    1. Call engine_exchangeCapabilities"
echo "    2. Call engine_forkchoiceUpdatedV3 repeatedly"
echo "    3. Call engine_getPayloadV3 for block building"
echo "    4. Call engine_newPayloadV3 for block validation"
echo ""
echo "  Watch for:"
echo "    ✓ 'Connected to execution client' messages"
echo "    ✓ No persistent 'Execution Layer Syncing' errors"
echo "    ✓ Block height increasing on FISCO-BCOS node"
echo ""
echo "  Press Ctrl+C to stop."
echo "============================================"
echo ""

# Use pnpm dlx (like npx) with the correct Lodestar v1.43+ command syntax:
# - 'dev' is a top-level command (not 'beacon dev')
# - --execution.engineMock false is required to connect to real EL
# - --reset for clean start each time
exec pnpm dlx @chainsafe/lodestar dev \
    --execution.urls "${RPC_URL}" \
    --execution.engineMock false \
    --jwtSecret "${JWT_FILE}" \
    --genesisValidators 4 \
    --startValidators 0..3 \
    --reset \
    --rest \
    --rest.port 9596
