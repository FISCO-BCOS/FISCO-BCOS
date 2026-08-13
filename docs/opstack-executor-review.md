# OP Stack 交易分流审查说明

## 1. 背景

当前的 OP Stack 执行器在 [opstack-executor/OpstackExecutor.h](../opstack-executor/OpstackExecutor.h) 中提供了两类入口：

- `executeTransaction(...)`：用于普通交易
- `executeDeposit(...)`：用于 0x7E deposit 交易

这两条路径的语义不同：

- 普通交易走 `m_prepare -> m_execute -> m_finish` 的通用 pipeline
- deposit 交易走 `runDeposit`，并直接应用 `StateDiff`

这意味着：deposit 不能被误走普通交易的 validate / transition 流程，否则会破坏 OP Stack 的执行语义。

---

## 2. 审查目标

目标不是“改接口”，而是“在内部实现中安全分流”，且对外保持不感知：

- 外部调用方无需传 `isDeposit` 或 `txType`
- 交易类型判断放在内部执行器中
- 执行器内部对 deposit 和 normal tx 做分发
- 不能让 deposit 进入普通交易验证/转移路径

---

## 3. 正确判定标准

协议层已经给出明确的判定接口：

- [bcos-framework/bcos-framework/protocol/Transaction.h](../bcos-framework/bcos-framework/protocol/Transaction.h#L90-L98) 中定义 `isDepositTx()`
- [bcos-tars-protocol/bcos-tars-protocol/protocol/TransactionImpl.cpp](../bcos-tars-protocol/bcos-tars-protocol/protocol/TransactionImpl.cpp#L456-L466) 中明确上实现：
  - `return web3TypedTxKind() == 0x7e;`

这说明 deposit 的判定应基于事务协议层的显式语义，而不是靠裸 `tx.type` 或 magic number 猜测。

---

## 4. 设计方案

### 4.1 外部接口不变

保留现有公开入口：

- `executeTransaction(...)`
- `executeDeposit(...)`

不新增参数，也不要求调用方显式传 `bool isDeposit`。

### 4.2 内部分流在 `executeTransaction` 中完成

在 [opstack-executor/OpstackExecutor.h](../opstack-executor/OpstackExecutor.h#L120-L155) 的 `executeTransaction(...)` 开头增加一层内部 guard：

1. 若 `transaction.isDepositTx()` 为真，内部触发 deposit 分支
2. 否则走普通交易 pipeline

伪代码：

```cpp
if (transaction.isDepositTx())
{
    // decode DepositTx
    // call executeDeposit(...)
    // return receipt
}

// normal tx path
auto props = co_await m_prepare(...);
...
```

### 4.3 语义要求

deposit 分支必须使用 `runDeposit`，不能经过：

- `opValidate`
- `opTransition`
- 普通 tx 的签名 envelope 校验

普通交易分支仍然走原来的三阶段：

- validate
- execute
- finish

---

## 5. 关键代码位置

以下文件需要一起审查：

- [opstack-executor/OpstackExecutor.h](../opstack-executor/OpstackExecutor.h)
- [opstack-executor/OpBlockExecute.h](../opstack-executor/OpBlockExecute.h#L180-L217)
- [bcos-framework/bcos-framework/protocol/Transaction.h](../bcos-framework/bcos-framework/protocol/Transaction.h#L90-L98)
- [bcos-tars-protocol/bcos-tars-protocol/protocol/TransactionImpl.cpp](../bcos-tars-protocol/bcos-tars-protocol/protocol/TransactionImpl.cpp#L456-L466)

其中，block 级分发逻辑已在 [opstack-executor/OpBlockExecute.h](../opstack-executor/OpBlockExecute.h#L196-L217) 中体现：

- deposit 先被直接识别并执行
- 普通交易在后续进入 fee / normal tx pipeline

这与单 tx 层面的内部 dispatch 是一致的。

---

## 6. 审查重点

AI reviewer 应重点确认以下几点：

### 6.1 是否真实实现了内部分流

检查：

- `executeTransaction(...)` 是否在入口处判断 `transaction.isDepositTx()`
- 是否在 deposit 分支直接调用 `executeDeposit(...)`
- 是否避免继续执行 `m_prepare` / `m_execute` / `m_finish`

### 6.2 是否避免误判

检查：

- 判断不依赖裸 `type() == 0x7e`
- 使用协议层 `isDepositTx()` 语义
- 不让 `isSystemTransaction` 误导 deposit 判定

### 6.3 是否遵守 OP Stack 语义

deposit 交易：

- 不应走普通 tx 对应的 validate 与 gas 逻辑
- 应直接走 `runDeposit`
- state diff 仍应写回 storage

普通交易：

- 仍应走 `opValidate + opTransition`
- 仍应采用 fee / gas / block hash / envelope 校验

### 6.4 是否保持接口稳定

检查：

- 没有向外部暴露额外参数
- 不破坏上层 block injection / scheduler 调用模式
- 兼容已有 `executeTransaction(...)` 入口

---

## 7. 风险点

### 7.1 deposit 误走普通路径

这是最严重问题。若 deposit 进入 `opValidate` 或 `opTransition`，将导致：

- 错误的 gas 语义
- 错误的 envelope 校验
- 错误的 state diff 语义

### 7.2 依赖 raw type byte 误判

如果直接用 `tx.type() == 0x7e` 之类的硬编码，容易混淆协议层语义，尤其在 OP Stack 的 typed tx / deposit 表征下，判定逻辑必须基于 `isDepositTx()`。

### 7.3 接口扩张导致依赖污染

如果把 `isDeposit` 作为签名参数强行塞进调用链，会造成：

- 调用方重复判断
- 多条路径拥有分叉参数
- 容易出现“调用方不一致，执行器内部又分两套逻辑”的问题

---

## 8. 审查验收标准

该方案应被视为合格，前提是满足以下条件：

- [ ] `executeTransaction(...)` 内部实现能识别 deposit
- [ ] deposit 进入 `executeDeposit(...)`，不进入普通 tx pipeline
- [ ] 普通 tx 仍走原有 validate / execute / finish 流程
- [ ] 不新增对外 API 参数
- [ ] 判定逻辑遵循 `transaction.isDepositTx()` / `web3TypedTxKind() == 0x7e`
- [ ] block-level execution 和 single-tx execution 语义一致

---

## 9. 结论

这个方案的核心价值在于：在不改变外部调用方式的前提下，把“交易分类”收敛到内部执行器中，从而实现清晰、可验证、可审计的 dispatch 逻辑。

如果另一个 AI 参与 review，建议它优先检查：

1. deposit 是否全部被内部拦截；
2. 普通 tx 是否仍完整走原有 path；
3. 判定是否基于协议语义而非裸 byte；
4. API 是否保持稳定。

这四项可以直接作为 review 的最小验收标准。

---

## 10. 差异本质与抽象层次（补充结论）

> 2026-08-13 记录。回答「op 中普通交易和 deposit 交易差异这么大么，能否在 bcos-evm 层抽象、屏蔽这种差异」。

### 10.1 结论

执行核心**已经共享**（`runTxMessage`），差异集中在 **校验 + 结算 + 失败语义** 三块；其中失败语义是不可抹平的本质分歧。抽象的正确目标不是"让调用方以为只有一种交易"，而是"把 `OpBlockTx` 的分发下沉，同时保留两种失败出口"。

### 10.2 差异清单

| 维度 | 普通交易 `opTransition` | deposit `runDeposit` | 能否抹平 |
|---|---|---|---|
| 输入类型 | `evmone::state::Transaction`（带签名、type 0–4、fee 字段） | `DepositTx`（`source_hash`/`mint`/`is_system_tx`，无签名） | ❌ 本质不同 |
| 校验 | `opValidate`：全量 `validate_transaction`（nonce/余额/手续费/EIP-3607）+ L1/operator cost + 512-bit cap | 仅算 intrinsic + EIP-7623 floor；masked 视图 + base_fee=0 跳过余额/手续费 | ❌ 本质不同 |
| 结算 | buy-gas：`balance -= tx_max_cost + l1 + opCost`，后 refund + coinbase + 3 个 vault | `balance += mint`，无任何手续费扣减 | ❌ 本质不同 |
| 失败语义 | 非法 tx → `throw` 整块作废 | 失败是**合法回执**：mint 保留、nonce 强制 +1、gasUsed=gasLimit、status=FAILURE；仅 `is_system_tx`/超块 gas 为块级错误 | ❌ **最大分歧点** |
| 回执元数据 | L1/operator/DA footprint + effectiveGasPrice | `deposit_nonce` + `deposit_receipt_version` | ❌ 不同 |
| 执行核心 | `runTxMessage` | **同一个 `runTxMessage`** | ✅ 已共享 |

证据：[OpTransition.h:78-79](../bcos-evm/bcos-evm/opstack/OpTransition.h#L78-L79) 与 [runDeposit 内部](../bcos-evm/bcos-evm/opstack/OpTransition.cpp#L530-L532)：deposit 内部构造 `Transaction` 壳（type=legacy、max_gas_price=0）后走**同一个 `runTxMessage`**。

### 10.3 为什么不能完全抹平

对应 op-geth 的 `TransitionDb`（普通）vs `ApplyDepositTx`（deposit）两条独立路径：

1. **deposit 没有签名经济**：无 sender recovery → 无 gas 费 → 无 balance cap；`mint` 是凭空铸币方向。
2. **失败语义相反**：deposit 失败必须保留 mint 并落 FAILURE 回执（否则 L2 资产凭空消失）；普通交易非法必须 void 整块（否则区块无效）。强行塞进单一 `runTx` 会让返回类型变成 `variant<receipt, block_error>`，等于把 `if/else` 从调用点挪进返回契约，调用方仍须分别处理——屏蔽了个寂寞。

### 10.4 建议的抽象层次

差异当前被劈在两层：

- **bcos-evm 层**（`bcos-evm/bcos-evm/opstack/`）：`opValidate`/`opTransition`/`runDeposit` + 共享的 `runTxMessage`。**这层已抽象得当，不动。**
- **opstack-executor 层**（`opstack-executor/OpBlockExecute.{h,cpp}`）：`OpBlockTx` variant（[OpBlockExecute.h:52](../opstack-executor/OpBlockExecute.h#L52)）+ `processOpBlock` 里的 `if (DepositTx) … else …` 循环。

若目标是让调度方不再手写分支，正确做法是把 `OpBlockTx` 的 visit 封装成单入口下沉到 bcos-evm：

```cpp
// bcos-evm 提供（当前在 opstack-executor）：
TxOutcome runOpBlockTx(view, block, hashes, const OpBlockTx&, cfg, vm, chainId, rf);
// 内部 visit variant；失败语义通过「异常 vs 回执」区分，不可藏
```

但 `TxOutcome` 必须显式保留"失败回执 vs 块级错误"分叉。彻底"屏蔽到调用方以为只有一种交易"是错误目标——会逼出 `isDeposit` 标志位，比现有 variant 更糟。
