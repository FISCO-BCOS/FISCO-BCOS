# 池余额加 L1/operator worst-case —— 方案记录（待实施）

- **日期**：2026-08-20
- **状态**：调研完成，方案待实施（工作量 ~2 天）
- **分支**：feat-opstack-e2e

## 问题

OP Stack 交易的实际费用 = L2 gas 费 + L1 数据费 + operator 费。FISCO 交易池的余额检查只算 `value + gasLimit × gasPrice`（`bcos-txpool/bcos-txpool/txpool/validator/TxValidator.cpp:246-279`），导致：余额刚好够付 L2 gas 的交易被池子接受 → 出块执行时扣 L1 费 + operator 费后余额不足 → 交易失败被剔除 → 用户白等一轮。

op-geth / op-reth 在池子准入时就拦下，用户立即收到"余额不足"错误。**非共识问题**（执行期检查已兜底），属体验优化。

## op-geth 实现（已拉取源码核实）

### 核心：`core/txpool/rollup.go`（2025 年 OP 专用文件）

```go
type RollupCostFunc func(tx types.RollupTransaction) *uint256.Int

// TotalTxCost = 常规执行成本 + 汇总成本（L1 费 + operator 费）
func TotalTxCost(tx RollupTransaction, rollupCostFn RollupCostFunc) (*uint256.Int, bool) {
    cost, overflow := uint256.FromBig(tx.Cost())   // value + gasLimit*gasPrice
    if overflow { return nil, true }
    if rollupCostFn == nil { return cost, false }  // 非 OP 链：只有常规成本
    rollupCost := rollupCostFn(tx)
    if rollupCost == nil { return cost, false }
    return cost.AddOverflow(cost, rollupCost)
}
```

### 按区块缓存参数（性能关键）：`legacypool.go:1522`

```go
func (pool *LegacyPool) resetRollupCostFn(ts uint64, statedb *state.StateDB) {
    if costFn := types.NewTotalRollupCostFunc(pool.chainconfig, statedb); costFn != nil {
        pool.rollupCostFn = func(tx types.RollupTransaction) *uint256.Int {
            return costFn(tx, ts)
        }
    }
}
```

- `NewTotalRollupCostFunc`（`core/types/rollup_cost.go`）内部 `NewL1CostFunc` / `NewOperatorCostFunc` 各自维护 `forBlock` 计时器——**L1Block 存储槽（1/3/7/8）只在新区块时读一次**，同区块几十笔交易共享参数
- 每笔交易只算 `FlzCompressLen(raw)`（FastLZ 压缩长度）套公式
- `resetRollupCostFn` 在每次新区块头时调用（`legacypool.go:361,1500`）

### 使用点（三处）

1. `ValidateTransactionWithState`（`validation.go:313`）：准入时 `balance < TotalTxCost` → `ErrInsufficientFunds`
2. `list.go:372`（pending 列表维护）：nonce 连续性/替换时重算成本
3. `legacypool.go:673`（替换交易 bump 检查）：`ExistingCost` 回调

### 接入形态：`RollupCostFn` 是 `ValidationOptionsWithState` 的**可选扩展字段**——非 OP 链传 nil，走纯 geth 逻辑，零侵入。

## op-reth 实现（已调研核实）

### `OpTransactionValidator` 包装 `EthTransactionValidator`

```rust
struct OpTransactionValidator<Client, Tx> {
    inner: EthTransactionValidator<...>,   // 标准以太坊验证器
    block_info: Arc<OpL1BlockInfo>,        // 跟踪的 L1 块信息
    require_l1_data_gas_fee: bool,         // 开关
}
```

- **L1 信息跟踪**：`block_info` 由 `extract_l1_info(&block.body)` 更新——从 L2 块的 **L1 attributes 交易**提取 L1 baseFee/scalars，随块缓存（与 op-geth 按区块缓存异曲同工）
- **验证**：`validate_one` = 常规检查 + `require_l1_data_gas_fee` 时用 `block_info.tx_cost_with_tx(tx)` 检查余额
- **底层**（op-revm `OpTransaction`）：`max_balance_spending()` / `ensure_enough_balance()` / `L1BlockInfo::calculate_tx_l1_cost()`（按 Regolith/Fjord 分叉规则）
- **差异**：op-reth 只额外加 **L1 费**（operator 视 flag）；op-geth 把 L1 + operator **一起**算

## 对照表

| 维度 | op-geth | op-reth |
|---|---|---|
| 成本构成 | 常规 + L1 + operator 全部 | 常规 + L1（operator 视 flag） |
| 参数来源 | 新区块从 L1Block 合约存储槽读（statedb） | 从 L2 块 L1 attributes 交易提取 |
| 缓存 | `forBlock` 计时器，同区块共享 | `Arc<OpL1BlockInfo>`，随块更新 |
| 每笔计算 | FastLZ 压缩长度 + 公式 | `calculate_tx_l1_cost`（同公式） |
| 接入方式 | `RollupCostFn` 可选字段（nil = 纯 geth） | 包装验证器 + flag 开关 |
| 溢出处理 | `AddOverflow` → 拒绝 | `ensure_enough_balance` 返回错误 |

**共同点**：参数读取**按区块缓存**（每区块一次，非每笔一次）——FISCO 实现时最重要的参考。

## FISCO 实施要点

**推荐形态**（对齐 op-geth）：可选回调 + 按区块缓存。

1. **跨模块问题**：费用计算在 `bcos-evm/bcos-evm/opstack/`（RollupCost.cpp），交易池在 `bcos-txpool`，无链接。选项：
   - (a) 把 `flzCompressLen` / L1 成本公式提升到共享层（bcos-framework）
   - (b) 保守近似：calldata gas（zeroes×4 + ones×16）作 L1 费上界，不需 FastLZ 移植
   - (c) 可配置 worst-case 上限参数（最简单，精度差）
2. **参数读取**：`TxValidator::validateBalance`（协程）已有 `scheduler->getPendingStorageAt` 读状态的能力 → 按区块缓存 L1Block 槽 1/3/7/8 读一次
3. **接入**：`TxValidator::validateBalance` 的余额检查追加 `required += l1Cost + opCost`
4. **开关**：仅 OP 模式（executor_version >= 3 / feature_l2_ethereum_compat）启用，非 OP 链零影响

**工作量**：~2 天（跨模块设计 + 缓存 + 测试）。**优先级**：P2。
