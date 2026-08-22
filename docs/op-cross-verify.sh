#!/bin/bash
# OP Stack 交叉验证脚本
# 用法: bash docs/op-cross-verify.sh [commit_sha]

set -euo pipefail

COMMIT="${1:-HEAD}"
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

echo "=== OP Stack 交叉验证 ==="
echo "Commit: $(git rev-parse --short "$COMMIT")"
echo "日期: $(date -I)"
echo ""

# 1. 代码存在性检查
echo "## 1. 关键函数存在性"
declare -A FUNCTIONS=(
  ["exchangeCapabilities"]="engine/bcos-engine/EngineServiceImpl.h"
  ["updateForkchoice"]="engine/bcos-engine/EngineServiceImpl.h"
  ["handleOpNewPayload"]="engine/bcos-engine/EngineServiceImpl.h"
  ["handleGetPayload"]="engine/bcos-engine/EngineServiceImpl.h"
  ["derivePayloadId"]="engine/bcos-engine/EngineServiceImpl.h"
  ["buildOpPayload"]="engine/bcos-engine/EngineServiceImpl.h"
  ["calcOpBaseFee"]="engine/bcos-engine/EngineServiceImpl.h"
  ["validateOpNewPayloadRequest"]="engine/bcos-engine/EngineServiceImpl.cpp"
  ["OpstackExecutor"]="opstack-executor/OpstackExecutor.h"
  ["decodeDepositEnvelope"]="opstack-executor/OpstackExecutor.h"
  ["preBlockOpSteps"]="opstack-executor/OpBlockExecute.h"
  ["sealOpBlock"]="opstack-executor/OpBlockExecute.h"
  ["finalizeOpBlockResult"]="opstack-executor/OpBlockExecute.h"
  ["finalizeOpBlock"]="opstack-executor/OpBlockExecute.cpp"
  ["mismatchedFieldOf"]="opstack-executor/OpCommitments.h"
  ["encodeReceiptForRoot"]="opstack-executor/OpBlockExecute.cpp"
  ["deriveOpReceiptMeta"]="opstack-executor/OpBlockExecute.cpp"
  ["computeOpTxRoot"]="opstack-executor/OpBlockExecute.h"
  ["opStorageRoot"]="opstack-executor/OpBlockExecute.cpp"
  ["computeL1Cost"]="bcos-evm/bcos-evm/opstack/RollupCost.cpp"
  ["computeL1CostFromFlz"]="bcos-evm/bcos-evm/opstack/RollupCost.cpp"
  ["computeOpCost"]="bcos-evm/bcos-evm/opstack/RollupCost.cpp"
  ["deriveOpL1Fee"]="bcos-evm/bcos-evm/opstack/RollupCost.cpp"
  ["deriveOpOperatorFee"]="bcos-evm/bcos-evm/opstack/RollupCost.cpp"
  ["estimatedDaFootprint"]="bcos-evm/bcos-evm/opstack/RollupCost.cpp"
  ["validateOpFeeParams"]="bcos-evm/bcos-evm/opstack/OpFeeParams.h"
  ["unpackOpFeeParams"]="bcos-evm/bcos-evm/opstack/OpFeeParams.h"
  ["opValidate"]="bcos-evm/bcos-evm/opstack/OpTransition.cpp"
  ["opTransition"]="bcos-evm/bcos-evm/opstack/OpTransition.cpp"
  ["runDeposit"]="bcos-evm/bcos-evm/opstack/OpTransition.cpp"
  ["OpReceiptMeta"]="bcos-evm/bcos-evm/opstack/OpTransition.h"
  ["stateRootOf"]="bcos-evm/bcos-evm/adapter/StateRootCompute.h"
  ["applyStateDiff"]="bcos-evm/bcos-evm/eth/state/state.hpp"
)

FOUND=0
MISSING=0
for fn in "${!FUNCTIONS[@]}"; do
  file="${FUNCTIONS[$fn]}"
  if [ -f "$file" ] && grep -q "$fn" "$file"; then
    FOUND=$((FOUND+1))
  else
    echo "  ❌ $fn — $file"
    MISSING=$((MISSING+1))
  fi
done
echo "  结果: $FOUND 存在 / $MISSING 缺失 / ${#FUNCTIONS[@]} 总计"
echo ""

# 2. OP 预部署地址检查
echo "## 2. OP 预部署地址"
ADDRESSES=(
  "OP_L1_BLOCK|0x4200000000000000000000000000000000000015"
  "OP_GAS_PRICE_ORACLE|0x420000000000000000000000000000000000000f"
  "OP_L2_TO_L1_MESSAGE_PASSER|0x4200000000000000000000000000000000000016"
  "OP_SEQUENCER_FEE_VAULT|0x4200000000000000000000000000000000000011"
  "OP_BASE_FEE_VAULT|0x4200000000000000000000000000000000000019"
  "OP_L1_FEE_VAULT|0x420000000000000000000000000000000000001a"
  "OP_OPERATOR_FEE_VAULT|0x420000000000000000000000000000000000001b"
  "OP_DEPOSITOR|0x4200000000000000000000000000000000000001"
)

ADDR_FOUND=0
ADDR_MISSING=0
for entry in "${ADDRESSES[@]}"; do
  name="${entry%%|*}"
  addr="${entry#*|}"
  if grep -rq "$name" bcos-evm/bcos-evm/opstack/OpPredeploys.h; then
    ADDR_FOUND=$((ADDR_FOUND+1))
  else
    echo "  ❌ $name ($addr)"
    ADDR_MISSING=$((ADDR_MISSING+1))
  fi
done
echo "  结果: $ADDR_FOUND 存在 / $ADDR_MISSING 缺失 / ${#ADDRESSES[@]} 总计"
echo ""

# 3. 存储槽映射检查
echo "## 3. 存储槽映射"
SLOTS=(
  "l1_base_fee|slot 1|whole slot"
  "base_fee_scalar|slot 3|bytes[16,20)"
  "blob_base_fee_scalar|slot 3|bytes[20,24)"
  "blob_base_fee|slot 7|whole slot"
  "operator_fee_scalar|slot 8|bytes[20,24)"
  "operator_fee_constant|slot 8|bytes[24,32)"
  "da_footprint_gas_scalar|slot 8|bytes[18,20)"
)

SLOT_FOUND=0
for entry in "${SLOTS[@]}"; do
  field="${entry%%|*}"
  if grep -q "$field" bcos-evm/bcos-evm/opstack/OpFeeParams.h; then
    SLOT_FOUND=$((SLOT_FOUND+1))
  else
    echo "  ❌ $field"
  fi
done
echo "  结果: $SLOT_FOUND / ${#SLOTS[@]} 总计"
echo ""

# 4. OpReceiptMeta 字段检查
echo "## 4. OpReceiptMeta 字段"
META_FIELDS=(
  "l1_gas_price"
  "l1_blob_base_fee"
  "l1_base_fee_scalar"
  "l1_blob_base_fee_scalar"
  "l1_fee"
  "l1_gas_used"
  "operator_fee_scalar"
  "operator_fee_constant"
  "da_footprint_gas_scalar"
  "da_footprint"
  "effective_gas_price"
)

META_FOUND=0
for field in "${META_FIELDS[@]}"; do
  if grep -q "std::optional.*$field" bcos-evm/bcos-evm/opstack/OpTransition.h; then
    META_FOUND=$((META_FOUND+1))
  else
    echo "  ❌ $field"
  fi
done
echo "  结果: $META_FOUND / ${#META_FIELDS[@]} 总计"
echo ""

# 5. 分叉映射检查
echo "## 5. 分叉→EVMC 映射"
FORKS=(
  "Ecotone|EVMC_CANCUN"
  "Fjord|EVMC_CANCUN"
  "Granite|EVMC_CANCUN"
  "Holocene|EVMC_CANCUN"
  "Isthmus|EVMC_PRAGUE"
  "Jovian|EVMC_PRAGUE"
)

FORK_FOUND=0
for entry in "${FORKS[@]}"; do
  fork="${entry%%|*}"
  evmc="${entry#*|}"
  if grep -q "$fork" bcos-evm/bcos-evm/opstack/OpForkSchedule.h && \
     grep -q "$evmc" bcos-evm/bcos-evm/opstack/OpForkSchedule.h; then
    FORK_FOUND=$((FORK_FOUND+1))
  else
    echo "  ❌ $fork → $evmc"
  fi
done
echo "  结果: $FORK_FOUND / ${#FORKS[@]} 总计"
echo ""

# 6. 测试覆盖检查
echo "## 6. 测试文件存在性"
TEST_FILES=(
  "engine/test/unittests/engine/EngineServiceTest.cpp"
  "opstack-executor/tests/OpstackExecutorTest.cpp"
)

TEST_FOUND=0
for f in "${TEST_FILES[@]}"; do
  if [ -f "$f" ]; then
    count=$(grep -c "TEST_F\|TEST(" "$f" 2>/dev/null || echo 0)
    echo "  ✅ $f ($count test cases)"
    TEST_FOUND=$((TEST_FOUND+1))
  else
    echo "  ❌ $f"
  fi
done
echo "  结果: $TEST_FOUND / ${#TEST_FILES[@]} 存在"
echo ""

# 汇总
echo "=== 验证汇总 ==="
echo "commit: $(git rev-parse --short "$COMMIT")"
echo "函数: $FOUND/$((FOUND+MISSING))"
echo "地址: $ADDR_FOUND/$((ADDR_FOUND+ADDR_MISSING))"
echo "槽位: $SLOT_FOUND/${#SLOTS[@]}"
echo "Meta: $META_FOUND/${#META_FIELDS[@]}"
echo "分叉: $FORK_FOUND/${#FORKS[@]}"
echo "测试: $TEST_FOUND/${#TEST_FILES[@]}"
