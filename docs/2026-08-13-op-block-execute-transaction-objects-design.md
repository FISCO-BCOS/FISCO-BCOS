# OP 块执行消费 Transaction 对象（消除普通 tx 解析）— design

> 状态：2026-08-13 草稿（brainstorming 定稿，方案 A）。分支 `feat-op-block-scheduler`。
> 前置：`docs/2026-08-12-op-block-scheduler-design.md`（门面化 OpScheduler + route B 注入循环）、`docs/2026-08-12-op-dual-path-equivalence-design.md`（A/B 双路径等价 + DIVERGE=0 gate）。

## 0. 结论摘要

route B 的 execute hook 对每个 raw envelope 做**两次重复推导**：`decodeOneRawTx` → evmone `Transaction`（普通 tx 只用到 `tx.type`），以及 `opEnvelopeToTars` → 全新的 tars `Transaction`（与引擎 `buildOpBlock` 已写进 `block.transactions()` 的对象**逐字节相同**）。本设计消除这两次推导：execute hook 直接消费块的 `Transaction` 对象，仅 deposit（0x7e）走 `decodeDepositTx`；`decodeOneRawTx` 及四个 typed/legacy decoder 从生产**彻底删除**（连同 `OpTxDecode.h`），测试侧 route-A 的 `OpBlockTx` 改由 `decodeDepositTx` + `bcosTransactionToEvmone(tars)` 构建（`buildFiscoTx` 的 pre-flight 已证明二者等价，见 §4）；三项共识检查（chain-id / EIP-2 low-s / yParity>1）从 decode 层搬进 `OpstackExecutor::m_prepare`（opValidate 之前，`call==false` 才强制）。canonical 往返 + 定宽检查对普通 tx 放弃，由六路承诺比较兜底（共识语义不变，仅报错粗一层）。

> **`OpBlockTx` 保留**：`processOpBlock`（route A 内核，**无生产调用者**，仅 t8n replay / dual-path 测试当黄金参照）与 `validateJovianBlockShape` 仍消费它；只有 `runOpBlockInjection` 停止消费。它留在 `OpBlockExecute.h`，不做删除。

## 1. 背景与问题

### 1.1 现状：route B 的三重重复推导

route B（`runOpBlockInjection`）的 execute hook（`OpScheduler::execute`，OpScheduler.h:122-240）对每个 raw envelope：

1. `detail::decodeOneRawTx(raw, m_chainId)`（OpScheduler.h:150）→ `OpBlockTx`（`variant<DepositTx, evmone::state::Transaction>`）。对普通 tx，执行只读它的 `tx.type`（回执 EncodeIndex 的 EIP-2718 前缀，OpBlockExecute.h:274）；sender / 字段全部没用——执行里 `bcosTransactionToEvmone` 重新恢复。
2. `m_envelopeToTars(raw, txHash)`（OpScheduler.h:158-175）→ `normalTxs`（全新 tars `Transaction`）。而引擎 `buildOpBlock`（EngineServiceImpl.h:1161-1194）已对**同一个** envelope 跑过**同一个** `opEnvelopeToTars`，把结果存进 `block.transactions()` 且 `extraTransactionBytes` 覆盖为全量 envelope。execute hook 拿到 `transactions` 参数（= 块的 Transaction 对象）后**丢弃**，从 raw 又转了一遍。

3. （执行内）`bcosTransactionToEvmone(tars tx)` → evmone `Transaction` → `opValidate`/`opTransition`。

第 1、2 步对普通 tx 都是纯冗余。

### 1.2 共识约束：`decodeOneRawTx` 不只是"构建 evmone Transaction"

它还是目前**唯一**跑以下三项检查的地方。`opEnvelopeToTars` 用的是 lenient 的 `Web3Transaction` 解码（EngineServiceImpl.cpp:33-59，`bcos::codec::rlp::decode`），evmone `validate_transaction` 也不查它们；引擎 step-2（`validateOpNewPayloadRequest`）不解码 envelope（EngineServiceImpl.h:1177 注释）。

| 检查 | 六路承诺比较兜底？ | 直接删的后果 |
|---|---|---|
| EIP-2 low-s（`requireLowSSignature`） | ❌（篡改签名内部自洽，比较全过） | 高-s 块被接受 → 与 op-geth 分叉 |
| chain-id 匹配 | ❌（跨链重放自洽） | 接受 op-geth 拒的块 → 分叉 |
| yParity>1 | ❌（截断后自洽） | 接受 op-geth 拒的编码 → 分叉 |
| canonical 往返（`assertCanonicalRoundTrip`） | ✅ txRoot 不匹配 → INVALID | 防御纵深（报错精度） |
| 定宽字段（`readFixedWidth`） | ✅ 改地址 → stateRoot 不匹配 | 防御纵深（报错精度） |

因此"执行消费 Transaction 对象"必须**把三项检查搬进 Transaction 对象路径**，而不是删掉。

### 1.3 目标文件现状

- `OpTxDecode.h`（412 行）：`decodeOneRawTx` + 5 个类型 decoder + `canonicalEnvelopeBytes` + `assertCanonicalRoundTrip`。
- `OpRlpDecode.h`（432 行）：RLP 标量基元 + `toBlockInfo`/`narrowU256ToU64` + ecrecover 基元。被 `OpBlockExecute.h` 与 `OpstackExecutor.h` 共同消费（叶子）。
- `OpBlockExecute.h`：`OpBlockTx`/`OpBlockResult` + `runOpBlockInjection` + seal + `announcedCommitmentsOf`/`computeOpTxRoot`。

## 2. 目标与范围

### 2.1 目标
- execute hook 直接消费块的 `Transaction` 对象（`transactions` 参数），消除普通 tx 的 raw-envelope 解析与 tars 重转。
- 三项共识检查（chain-id / low-s / yParity）在 executor validate 路径保持强制，共识边界不变。
- 删除 `decodeOneRawTx` + 4 个 typed/legacy decoder + `canonicalEnvelopeBytes` + `assertCanonicalRoundTrip`；删除 `OpTxDecode.h`。
- 保留 `decodeDepositTx`（并入 `OpBlockExecute.h`）+ RLP 基元（`OpRlpDecode.h` 叶子）+ 新增 `validateEnvelopeSignature`。
- 双路径等价测试 DIVERGE=0 保持，语料（125 向量）不变绿。

### 2.2 不在范围
- 引擎 `buildOpBlock` / `opEnvelopeToTars` 不动（块的 Transaction 对象来源保持现状）。
- deposit 表示不变：`executeDeposit` 仍吃 `DepositTx`（tars Transaction 无 deposit 字段，改接口改动过大）。
- `processOpBlock`（route A 的 execute 内核，**无生产调用者**）与 route A 残余不动 —— 保留作 t8n replay / dual-path 测试的黄金参照；`OpBlockTx` 因它保留（§0）。
- eth_call 语义不变（三项检查对 `call==true` 跳过，与现状一致）。

## 3. 设计

### 3.1 数据流

**引擎侧（不变）**：`buildOpBlock` 把每个 envelope 经 `opEnvelopeToTars` 转成 tars `Transaction` 写进 `block.transactions()`，`extraTransactionBytes` = 全量 envelope；转换失败的落 minimal fallback tx（只有 hash + wire bytes）。

**`OpScheduler::execute`（重构）**：
- 直接用 `transactions` 参数（块的 Transaction 对象）。**删除** normalTxs 重转循环（OpScheduler.h:158-175）与 `m_envelopeToTars` 成员/ctor 参数；`Initializer.cpp` 的注入点同步删。
- 一遍循环按类型字节分类 `rawTxBytes[i][0]`：
  - `0x7e` → deposit，`decodeDepositTx(raw)` 收集进 `deposits`（块序）；
  - `>= 0xc0`（legacy）/ `0x01` / `0x02` / `0x04` → 普通，**不解析**；
  - 其它（`0x03` blob 等）→ 直接 `OpConsensusError`（保持精确 INVALID；单字节检查，不算解析）。
- 把 `transactions` + `deposits` + `rawTxBytes` 传给 `runOpBlockInjection`。

**`runOpBlockInjection`（签名改）**：`txs(span<OpBlockTx>) + normalTxs(span<Transaction::Ptr>)` → `transactions(span<Transaction::ConstPtr>, 对齐 rawTxBytes) + deposits(span<DepositTx>, 块序)`。循环按 `rawTxBytes[i][0]` 分派：
- deposit 分支：`executeDeposit(view, header, deposits[depIdx++], ...)`；
- 普通分支：`executeTransaction(view, header, *transactions[i], ...)`。
- `txTypes[i]`（回执 EncodeIndex 前缀）从 `rawTxBytes[i][0]` 推：deposit→`kDepositTxType(0x7e)`、typed→原字节、legacy→0。
- deposit-first 不变量：`rawTxBytes[0][0]==0x7e && isL1AttributesTx(deposits[0])`；"deposit after non-deposit" 检查改吃分类。
- Jovian 形状检查：`runOpBlockInjection` 内联重表达为吃 `deposits[0].data` + 末交易类型字节；`validateJovianBlockShape(span<OpBlockTx>, cfg)` **保留不动**（route-A 的 `processOpBlock` 仍调用它）。

`OpBlockTx` **保留**在 `OpBlockExecute.h`（`processOpBlock` 与 `validateJovianBlockShape` 仍消费，见 §2.2/§4）；只有 `runOpBlockInjection` 停止消费它。

### 3.2 三项共识检查 → `OpstackExecutor::m_prepare`

新增 `bcos::evm::engine::detail::validateEnvelopeSignature(bcos::bytes const& env, uint64_t chainId)`（放 `OpRlpDecode.h`，复用 RLP 标量基元 + evmmax secp256k1）：按类型字节分派做 chain-id 匹配 / EIP-2 low-s / yParity>1，违规抛 `OpConsensusError`。它**只校验不构建**——跳过 `decodeOneRawTx` 里最贵的 ecrecover（执行侧 `opValidate` 自己恢复 sender），也不做 canonical 往返。

`OpstackExecutor::m_prepare` 在 `opValidate` 之前调用它，门控：`call==false` 才强制（块执行普通 tx）；`call==true`（eth_call）跳过——eth_call 现状本就不跑这些检查，行为不变。`m_prepare` 新增 `bool call` 参数（默认 `false`），由 `executeTransaction` 透传（该签名已有 `call` 参数）；concept 路径的 `ExecuteContext::prepare`（OP 不走）保持默认值。

### 3.3 错误分类

删除 converter 重转后，畸形/不支持 envelope 的 INVALID 由两个出口保证：
1. 类型字节拒绝（`0x03`/未知字节 → `OpConsensusError`）；
2. `executeTransaction` 对 minimal fallback tx 的 `opValidate` 失败 → `OpTxValidationFailed` → 包成 `OpConsensusError`。

两者都经 execute hook 的既有 catch 结构归到 INVALID，不再有 `OpExecutionInternalError`(-32603) 的错路。

### 3.4 文件收尾

- `OpTxDecode.h`：**删除**（`decodeOneRawTx`/`decodeEip1559Tx`/`decodeSetCodeTx`/`decodeAccessListTx`/`decodeLegacyTx`/`canonicalEnvelopeBytes`/`assertCanonicalRoundTrip` 全部移除）。
- `decodeDepositTx`：移入 `OpBlockExecute.h`（`bcos::evm::engine::detail`，与 `runOpBlockInjection` 同文件，RLP 基元经已 include 的 `OpRlpDecode.h` 复用）。
- deposit 编码侧（`canonicalEnvelopeBytes` 的 deposit 分支，测试构造 deposit envelope 用）：移入 `opstack-executor/tests/support/`（测试专用；typed/legacy 编码分支随 `assertCanonicalRoundTrip` 一并删除）。
- `OpRlpDecode.h`：保留为 decode 基元叶子，新增 `validateEnvelopeSignature`。
- include 修正：`OpScheduler.h` 去掉 `OpTxDecode.h` include；`OpSchedulerImpl.h` 的 `OpRlpDecode.h` include 属残留，顺带清理。

## 4. 测试

- `OpDualPathEquivalenceTest`：
  - route-B leg 改走新 `runOpBlockInjection` 签名（块 transactions + deposits + rawTxBytes）；DIVERGE=0 必须保持——执行语义未变，仅删冗余解析。
  - `buildFiscoTx` 的 pre-flight（`bcosTransactionToEvmone(tars) == decodeOneRawTx(env)`，:597-621）**删除**——重构后 route B 只剩 tars→evmone 单一路径，无漂移可测；该等价成为设计前提。
  - route-A leg 的 `OpBlockTx` 构建改为：deposit 用 `decodeDepositTx(env)`，普通 tx 用 `bcosTransactionToEvmone(tars)` + raw envelope 作 `signedEnvelope`（pre-flight 已证明与直接解码等价）。
- `OpSchedulerTest` / `OpT8nReplayTest`：弃用 `decodeOneRawTx`/`makeConverter`；deposit envelope 构造改走测试 support 的 deposit 编码；blob（0x03）拒绝断言改为"类型字节 OpConsensusError"。
- 新增回归钉：low-s / chain-id / yParity 经 executor validate 路径（`validateEnvelopeSignature`）仍被拒——证明检查从 decode 迁到 validate 后无漏网。
- 构建：opstack-executor lib + libinitializer + 三个测试目标；`OpEnvelopeToTarsTest` 不动（opEnvelopeToTars 未改）。

## 5. 决策记录

| 决定 | 依据 |
|---|---|
| 方案 A（普通 tx 零解析）而非 B（保留 canonical 守卫） | 最低解析量；canonical/定宽有六路比较兜底，共识不变；报错粗一层可接受 |
| 三项检查进 `m_prepare` 而非 `opEnvelopeToTars` | 后者被两道墙卡死：engine 不依赖 bcos-evm（canonical RlpEncodeTuple / low-s evmmax 放不进）+ `opEnvelopeToTars` 拿不到 chainId |
| `m_envelopeToTars` 从 OpScheduler 移除 | 与 `buildOpBlock` 产出逐字节相同，纯冗余 |
| blob/未知类型字节保留单字节拒绝 | 保持 blob 精确 INVALID，成本一个字节比较 |
| eth_call 跳过三项检查 | 现状本就不跑（decodeOneRawTx 不进 eth_call 路径），行为不变 |
