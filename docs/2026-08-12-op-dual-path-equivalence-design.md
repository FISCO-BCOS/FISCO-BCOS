# OP 双路径执行等价性 Harness 设计

日期：2026-08-12
分支：feat-op-eest-baseline
状态：已设计（brainstorming 审查后定稿）；**修订 v2（3-agent 审查后，2026-08-12）**

> **修订 v2**：吸收三份审查（事实/架构/可执行性）发现。**核心变更（A4/D7，用户裁定）**：harness **建在 OpBlockScheduler 分支（Task 4 buildOpBlockInfo 修复）合流后**的基线——§6.2 `deposit_basefee_observer` 从分叉触发向量**降级为绿守卫**（验证 deposit baseFee 读 header，不再触发分叉），P1 soft 分叉计数 **6→4**（gaslimit×2 + empty_account_cleanup×2），§8 只留 gasLimit 修复（baseFee 部分 Task 4 已完成）。其余：A1 P1 分叉断言改 ALLOWLIST 模式；A2 §9 幽灵测试套件（OpCallSchedulerTest/OpTwoPhaseTest 不存在）→ OpBlockSchedulerSuite；A3 §7(a) 单模板参数；B1-B8 include/管线/别名/tripwire/导出/引用/清单/log 内容；G5 运行时实测 0.66s 删拆分；P0 dry-run 已实测通过。

## 1. Context

目标是让 `BaselineScheduler` 编排 OP 块执行。两条内层路线：

- **路线 A**：OP 分支内层直调 `executeOpBlock`（块级 `processOpBlock`）——与 op-geth 对齐过的唯一生产路径。
- **路线 B**：逐笔注入循环，直接使用 `OpstackExecutor`（`executeTransaction` / `executeDeposit` / `finalizeBlock`）——符合 ethereum-executor 式编排，但模块属"v2 独立参照、未装配"，**与 processOpBlock 的字节级等价性从未被证明过**。

本任务 = 路线 B 的前置 gate。交付 `harness + 定向向量 + 修复 + 全绿 gate`；**不含 BaselineScheduler 接线**（后续任务）。

## 2. 已确认事实

- **语料**：`opstack-executor/tests/t8n/vectors/` 81 个 valid 向量（127 文件排除 invalid_）+ `golden/engine/*.golden.json`（**6 键：blockHash/transactionsRoot/extraData/excessBlobGas/rawTransactions/encodedHeaderHex，无 stateRoot**——stateRoot 在向量文件 `_op_expected.header.stateRoot`，v2 修正事实）。chain 向量：`isthmus_chain_3` / `jovian_chain_3`（无 golden，但每块有 `_op_expected.header.stateRoot` 锚）。op-geth pin `e8800cffe53d459cde8a07c8e8f1de9d86e79e07`（检出已确认可用）。**多交易 77/81**（非 75；4 个单笔：deposit_only×2 / upgrade_jovian_activation / jovian_first_block，v2 修正计数）。
- **路径 A 用 `Storage2State` 桥**（毒标记、全 `/apps/`）；**路径 B**（OpstackExecutor）硬接 `eth::StorageStateView` + `eth::applyStateDiff`。两者读写同一 storage2 键空间 → 同一 MLS 驱动两路径。
- **写回收敛（审查 H1 修正）**：OP 向量触达地址（L1Block / MessagePasser / fee vaults / coinbase / users）全非 FISCO 系统合约 → 全 `/apps/` 无 split-brain；零槽对 `stateRootOf` 不可见（Storage2State 读端过滤）。**但"语料无自毁向量 → deleted_accounts 恒空"是错误前提**：语料有 `empty_account_cleanup`（×2 fork）触发 **EIP-161 touch-delete**（非 SELFDESTRUCT），`deleted_accounts` 非空，且两路径写回不对称（Storage2State 彻底删账户行 + SYS_TABLES marker；eth::applyStateDiff 只清零字段、留 marker）→ **path B stateRoot 多一个空账户叶子**。这是路线 B 的真实写回语义 bug，**必须纳入 Phase 2 修复**（修 eth::applyStateDiff 的 deleted_accounts 分支，与 Storage2State 对齐；该函数与 EthereumExecutor 共享 → 需回归验证非 OP 路径）。
- **对比面**：`OpExecuteBlockResult{receipts, seal, stateRoot, gasUsed, txRoot}`（**v2：合流后落点 `OpSchedulerImpl.h:66`，ns `bcos::evm::engine`；主仓库旧位置 OpErrors.h:39 合流后失效**）；seal = `sealOpBlock(result, cfg, messagePasserStorage)`（**吃 `OpBlockResult`（带 txTypes），非 OpExecuteBlockResult**——injector 内部持 OpBlockResult 中间态，G4 v2）；stateRoot = `stateRootOf(bridge)`；txRoot = `computeOpTxRoot(rawTxBytes)`（OpEngineSeam.h:137）。
- **路径 B 编排配方**（复刻 `processOpBlock`，OpBlockExecute.cpp:93-207）：块前 `system_call_block_start`（evmone 直调，executor 无入口）→ deposit-first（`isL1AttributesTx` 未导出，重实现 3 行）→ 懒 `loadOpFeeParams` + Jovian D-1 calldata [176:178] 覆盖 → 逐笔 blockGasLeft 递减 + `setCumulativeGasUsed` → `finalizeBlock`。
- **tx 构造**：`opEnvelopeToTars(env, hash)`（EngineServiceImpl.h:168，`bcos::engine::detail`）decode 成 bcostars::Transaction。普通交易 `takeToTarsTransaction` 设的 extraTransactionBytes 是 **signing preimage**——必须**覆盖为完整 envelope** + pre-flight `bcosTransactionToEvmone(重建tx) == decodeOneRawTx(env)` 字段级校验；**deposit 分支例外**：`takeToTarsTransaction` 对 deposit 已直接填完整 `encode()` envelope（Web3Transaction.cpp:113-127），无需覆盖（v2 修正事实）。
- **逐笔 threading 模板**：**v2（B6）**：本 worktree 无 `OpCallScheduler`（那是 op-alignment 分支的类）——用 **`OpBlockScheduler::coCallLatest`（OpBlockScheduler.h:259-306）** 作逐笔注入式 executeTransaction 的 threading 模板。
- **OpstackExecutorTest 头仅 setNumber(1)** → `gasLimit()==0` → Phase 2 修复用 fallback 保留行为。

## 3. 审查发现（brainstorming 结论）

1. **已知分叉在现有语料上不触发（本设计的最关键修正）**。语料唯一环境观察者合约 `fee_env_observer`（`0x3a5f55486001554760025500` = GASPRICE/BASEFEE/SELFBALANCE → SSTORE）**不读 GASLIMIT(0x45)**；全语料无合约执行 `0x45`，也无 deposit 调用读 BASEFEE 的合约。故：
   - harness 对现有 81 向量**大概率零分叉直接绿**（证明"语料上 A==B + 写回收敛"，这本身就是 route B 的核心价值）；
   - **分叉机制由新增定向向量专门触发并验证**（与 corpus 覆盖解耦）。
2. **语料多交易覆盖强**：77/81 向量多交易（`isthmus_big_block_130tx` 131 笔、chain 3 块×2 笔）→ 块循环（blockGasLeft / cumulative）充分锻炼。
3. **`deleted_accounts.empty()` 守卫不可实现**（path B 经 OpstackExecutor 内部 applyDiff，拿不到每笔 diff）→ 删除，改由 stateRoot 对比 + 分叉归因兜底。
4. **pre-Isthmus 盲区**（用户裁定接受）：A、B 两路径在 pre-Isthmus 向量上跑 isthmusConfig（configAt 下限解析），等价仍成立；fork 语义归 OpT8nReplayTest。路线 B 若将来用于 pre-Isthmus 链，届时再补。

## 4. 决策记录

| # | 决策 | 选择 |
|---|---|---|
| D1 | 已知分叉验证方式 | **加定向向量**（generator + regen，推荐） |
| D2 | pre-Isthmus 盲区 | **接受限制**（fork 语义归 OpT8nReplayTest） |
| D3 | Phase 2 修复验证 | 定向向量 harness 验证 + 独立单测双保险 |
| D4 | golden 三方对比（G1 修订） | **P1 软（REPORT）→ P3 硬**。P1 只诊断（防未覆盖向量上 Storage2State-vs-golden 差异大面积击穿 P1），P3 翻硬（防"两路径一起错"） |
| G2 | 定向向量在 OpT8nReplayTest 上红 | **停止 harness 阶段，按 route-A 共识问题独立调查**（processOpBlock ≠ op-geth 是比 route B 更大的缺陷；route B 修复建立在"route A 正确"上） |
| G3 | P0 regen 可复现性 | **dry-run 门禁前置**：加 case 前先全量 regen 现有语料并确认 diff 为空（**v2：已实测通过**——worktree 跑 regen.sh 未改 cases，exit 0、字节等同、工作区零漂移；主/worktree `t8n/` 一致）；正式 regen 后 diff **只含** cases.go + **4 新向量 + 4 新 `.golden.json`** + manifest 5 行（v2 修正：A3 清单漏 golden 文件），任何现有向量改动 = 停止调查 |
| G4 | 路径 B 循环形态 | **提取为生产模块 `opstack-executor/OpBlockInjector.h`**（`runOpBlockInjection(...)`），harness 调用它、BaselineScheduler 将来也调用它——消除移植漂移，harness 变持久回归门禁。**v2 修正（架构 B2）**：`sealOpBlock` 吃 `OpBlockResult`（**带** txTypes），返回面 `OpExecuteBlockResult`（不带）——injector 内部必须持 `OpBlockResult` 中间态走 `sealOpBlock`，再窄化返回 `OpExecuteBlockResult` |
| G5 | 运行时成本 | **v2：已实测，删拆分**。全量 `opstack-executor-block-tests` 实测 **0.66s**（663847us），单 `OpT8nReplay` 0.38s；双路径 harness 全量加入 ≈ +1-2s，**远超 <60s 上限两个数量级** → 全量进默认 gate，无「拆默认子集」分支。**补充（架构）**：P1 的红探针向量（gaslimit×2 + cleanup×2 + basefee×2 回归守卫）须无条件在全量——CI 默认跑不触发任何红则 P1 gate 是盲的 |
| D5 | **写回分叉（三 agent 审查 H1）** | Phase 2 **扩展为「BlockInfo 修复 + 写回修复」**：修 `eth::applyStateDiff` 的 deleted_accounts 分支（彻底删账户行 + SYS_TABLES marker，与 Storage2State 对齐）；与 EthereumExecutor 共享 → 回归验证非 OP 路径 |
| D6 | **tx 构造边界（审查 H2）** | `buildFiscoTx` **不放入** `OpBlockInjector.h`（opEnvelopeToTars 在 engine lib，会成链接循环）→ 成为**调用方职责**：injector 收 `Transaction::Ptr` 或 builder 回调 |
| **D7** | **harness 基线（v2 用户裁定）** | **建在 OpBlockScheduler 分支（Task 4 buildOpBlockInfo 修复）合流后**。故 §6.2 `deposit_basefee_observer` 从分叉触发向量**降级为绿守卫**（deposit baseFee 已读 header → 不再分叉，仅验证语义）；P1 soft 分叉计数 **6→4**（gaslimit×2 + empty_account_cleanup×2）；§8 只留 gasLimit 修复（baseFee 部分 Task 4 已完成，删除）。§5/§9 相应同步 |
| **D8** | **P1 分叉断言机制（v2 架构 A1）** | 弃「全局布尔 `kAllowKnownDivergences`」——P1=true 放行任意未列出 soft 分叉，「恰 N」只能人眼 REPORT，与 §10「新分叉不豁免」矛盾。**改 DIVERGENCES.md ALLOWLIST 模式**（沿 OpT8nReplayTest 既有范式）：按 {向量, 字段, expected} 枚举已知分叉，未列出的 soft 分叉 P1 即红 |
| R1 | deposit case `To` 指针（审查 H3） | generator 里 `inputDeposit.To` 是 `*common.Address` → `To: &basefeeObsAddr`；观察者地址换 `0xc0de...0007/0008`（避开 delegateAddr/aclAddr） |
| R2 | P0 dry-run 顺序（审查 MED） | dry-run **在加 case 前**跑（当前 Task 2 前置）；regen 清孤儿向量 + dry-run 含 `golden/`；manifest 行数按 `append_if_absent` 实际（1 注释 + 4 文件名 = 5 行，非 2） |
| R3 | harness 加固（审查 MED） | 逐向量 try/catch 继续；`_op_*` 从 opStackMeta **集合驱动**（isthmus 9/jovian 11/deposit 2，不硬编码 13）；chain 向量加 **per-block golden 锚**（`blocks[i]._op_expected.header.stateRoot` 存在）；/sys 路由 tripwire（断言语料地址 ∉ c_systemTxsAddress）；导出 narrowGasUsed/hexCumulative/isL1AttributesTx（免复制漂移） |

## 5. 总体结构

```
P0  regen：dry-run 可复现性门禁 → generator/cases.go +2 定向 case（each bothForks）
    → opt8n-ref 构建（op-geth 仓库 Go 工具，非 CMake target）→ regen.sh 重生成
    → 验收：diff 只含 cases.go + 4 新向量 + 4 新 .golden.json + manifest（1 注释 + 4 文件名 = 5 行）
P1  红 harness：OpBlockInjector.h（路径 B 生产模块）+ OpDualPathEquivalenceTest + CMake
    预期（v2，Task 4 合流后）：恰 4 个 soft 分叉（gaslimit×2 + empty_account_cleanup×2），
    其余向量零分叉；deposit_basefee 2 向量为绿守卫（验证语义）；mechanics 字段全 hard 绿
    + log 内容比对；ALLOWLIST 驱动（D8），未列出分叉 P1 即红
P2  修复 OpstackExecutor.h（仅 GASLIMIT——BASEfee 已由 Task 4 完成）+ BCOS2Evmone.h（写回）+ 单测 → 重跑 harness 全绿
P3  golden 三方翻硬 + 强制清单 + OP_RECEIPT_FIELDMAP.md 更新
```

## 6. 定向向量（generator cases）

### 6.1 `gaslimit_observer`（bothForks）—— 触发 GASLIMIT 分叉

- 观察者地址：`0xc0de...0007`（避开现有 delegateAddr 0x...04 / aclAddr 0x...05，审查 R1）。
- `pre` 部署合约 `0x4560005500` = `GASLIMIT → PUSH1 0 → SSTORE → STOP`（slot0 存 gaslimit()）。
- 块：attributes deposit（tx0）+ EIP-1559 调观察者（tx1，data 空）。
- 分叉机制：path B（修复前）`buildOpBlockInfo` gasLimit = **blockGasLeft**（deposit 扣 gas 后 < header gasLimit，实测 deposit 消耗 ~23k gas → 必然 <）；path A / op-geth = **header gasLimit** → slot0 不同 → stateRoot 不同。
- **OpT8nReplayTest 必须绿**：processOpBlock 用 header gasLimit == op-geth golden（有效触发向量的前提）。

### 6.2 `deposit_basefee_observer`（bothForks）—— deposit 内 BASEFEE 语义绿守卫（v2 降级，D7）

> **v2 变更（D7，用户裁定）**：原设计用它触发「path B `executeDeposit` baseFee=0」分叉。Task 4 合流后 `executeDeposit` 经 `buildOpBlockInfo` 读 `header.baseFee().value_or(0)`（OpstackExecutor.h:207 → :93-94）→ 该分叉**不再存在**。向量保留为**绿守卫**：验证 deposit 内读到的 BASEFEE == header baseFee（回归保护 Task 4 修复不被回退）。

- 观察者地址：`0xc0de...0008`。
- `pre` 部署合约 `0x4860005500` = `BASEFEE → PUSH1 0 → SSTORE → STOP`。
- 块：attributes deposit（tx0）+ **第二个 deposit** 调观察者（tx1）——deposit 全在 non-deposit 前。
- **守卫语义（v2）**：path A / path B / op-geth 三方 BASEFEE 读数**必须一致**（slot0 == header baseFee），作为 Task 4 deposit-baseFee 修复的回归守卫；不再计入 soft 分叉。
- **generator 写法（审查 R1）**：`inputDeposit.To` 是 `*common.Address` → 必须 `To: &to`（`to := basefeeObsAddr`），否则 Go 编译失败。
- OpT8nReplayTest 同样必须绿（runDeposit 用真实 baseFee == op-geth）。

### 6.3 生成

`cases.go` 加 2 case（参照 `fee_env_observer` case 结构：`c.Pre[addr] = types.Account{Code: ...}` + `c.Transactions = append(...)`）→ `regen.sh` 重生成 → 新向量 + golden 落库 + manifest 自动更新。全链路门禁一致。

## 7. harness 设计（`opstack-executor/tests/OpDualPathEquivalenceTest.cpp`）

自含 Boost.Test 套件 `OpDualPathEquivalence`。**v2（D8）：弃全局 `kAllowKnownDivergences` 布尔开关**——P1=true 会放行任意未列出 soft 分叉（「恰 N」只能人眼 REPORT，与 §10「新分叉不豁免」矛盾）。改 **DIVERGENCES.md ALLOWLIST**（沿 OpT8nReplayTest 既有范式）：按 {向量, 字段, expected} 枚举已知分叉（gaslimit×2 + cleanup×2），未列出的 soft 分叉 P1 即红；P3 清空 ALLOWLIST 翻严格。

### 7.0 NEW `opstack-executor/OpBlockInjector.h`（生产模块，G4 提取 + 审查 D6/R3 修订）

路径 B 逐笔注入循环提取为生产 header，harness 与将来 BaselineScheduler 共用同一实现：

```cpp
namespace bcos::evm::engine
// 把 processOpBlock 的编排配方（system_call_block_start → deposit-first →
// 懒 loadOpFeeParams + D-1 覆盖 → 逐笔 blockGasLeft 递减 + setCumulativeGasUsed
// → finalizeBlock）搬进 OpstackExecutor 注入式入口之上的可复用函数。
// 审查 D6：普通交易的 FISCO Transaction 由**调用方**预构建（buildFiscoTx/opEnvelopeToTars
// 在 harness 侧——opEnvelopeToTars 在 engine lib，injector 若内建会成链接循环）。
// 错误分类（审查 R3）：poison/hashErr → OpStorageError，块形状/校验 → OpConsensusError，
//   gas-pool overrun（processOpBlock 抛 "gas pool overrun"）也归 OpConsensusError（块级拒绝）。
// v2（架构 B2）：内部持 OpBlockResult 中间态走 sealOpBlock（吃带 txTypes 的 OpBlockResult），
//   再窄化返回 OpExecuteBlockResult（丢弃 txTypes）。v2（架构）：txs ≡ decodeOneRawTx(rawTxBytes)
//   逐元素同源对应，normalTxs[k] = 块内第 k 个非 deposit 交易（次序契约，deposit 不占位）。
// v2（架构）：injector 注入的 cfg 必须 == 路径 A executeOpBlock 的 configAt 解析结果（fork 平价）。
template <class Storage>
OpExecuteBlockResult runOpBlockInjection(OpstackExecutor& executor, Storage& view,
    BlockHeader const& header, std::span<OpBlockTx const> txs, OpForkConfig const& cfg,
    uint64_t chainId, LedgerConfig const& ledgerConfig,
    std::span<bcos::protocol::Transaction::Ptr const> normalTxs,  // 第 k 个非 deposit 交易（调用方构建）
    std::vector<bcos::bytes> const& rawTxBytes, crypto::Hash::Ptr const& hashImpl);
```

- 依赖 OpstackExecutor（executeTransaction/executeDeposit/finalizeBlock）+ `RecentBlockHashes` + `sealOpBlock`/`stateRootOf`/`computeOpTxRoot` + `OpEngineSeam.h`（`computeOpTxRoot`/`toBcosH256` 声明所在，**必须 include**）。
- **v2 include 清单（可执行性 B1）**：仅 OpEngineSeam.h + OpstackExecutor.h + RecentBlockHashes.h **不够**，还必须显式 include：`bcos-evm/bcos-evm/adapter/StateRootCompute.h`（stateRootOf）、`bcos-evm/bcos-evm/adapter/StateDiffSanitize.h`、`bcos-evm/bcos-evm/eth/state/system_contracts.hpp`、`bcos-evm/bcos-evm/opstack/OpFeeParams.h`、`bcos-evm/bcos-evm/opstack/OpPredeploys.h`。（`OpBlockTx`/`validateJovianBlockShape` 经 OpEngineSeam.h→OpBlockSeal.h→OpBlockExecute.h 传递可达。）**全部已在 target 链接面内**（bcosevm::opstack / bcos-evm-eth），纯 include 修复。
- harness 路径 B 与单测调用它；BaselineScheduler 接线时直接消费它（消除移植漂移，harness 变持久回归门禁）。
- **/sys 路由 tripwire（审查 R3，v2 升级建议）**：断言每个向量的 pre/postState/tx to/from/coinbase 地址 ∉ `c_systemTxsAddress`（`bcos::precompiled`，**11 项非 8**，PrecompiledTypeDef.h:143）；**v2 建议升级**为派生表前缀断言 `accountTableName(addr)` 前缀 == `apps/`（比枚举地址更健壮，不依赖集合完整性）。
- **v2 保留语义（架构）**：逐笔「任何一笔校验失败 = 整块拒绝」必须原样保留 processOpBlock 语义，不能降级为逐笔软失败。
- **v2 非协程**：`runOpBlockInjection` 非协程 → 内部每步用 `bcos::task::syncWait(...)`（executeDeposit/executeTransaction/finalizeBlock/applyStateDiff/storage2 全 awaitable；先例 OpSchedulerImplSmokeTest.cpp:160）。

**(a) Fixture**：镜像 OpNewPayloadRpcE2eTest.cpp:70-160 —— MLS + **单桶** CONCURRENT 后端（否则 visitAccounts 扫错）；`receiptFactory`；chainId=0x2105；`forkTimestampsFor(bool jovian)`；**v2（A3）`OpSchedulerImpl<ViewType>`（单模板参数，非 `<ViewType, MLS>`——两分支皆单参）**（路径 A）+ `bcostars::BlockFactory`；`ledgerConfig` 设 evmcRevision。

**(b) 语料枚举**：遍历 `OP_T8N_VECTORS_DIR`，跳过 `invalid_` 前缀与 `_op_expected.reject`；chain 向量走 `blocks[]` 分支；每向量对比数 > 0。

**(c) 帮助函数**：
- `buildHeaderFromEnv`（chain 向量无 golden）：设 number / timestamp×1000 / gasLimit / baseFee / coinbase / prevRandao / parentInfo{parentHash} / parentBeaconBlockRoot / extraData{} / blobGasUsed{0} / excessBlobGas{0}——**v2 事实修正**：`toBlockInfo` 读 number/timestamp/gasLimit/baseFee/coinbase/prevRandao/parentBeaconBlockRoot/extraData/blobGasUsed，**不读 parentInfo 与 excessBlobGas**（这两者服务 RecentBlockHashes/blockHash/seal，字段清单本身合理仅归因语过宽）；单块向量用 `w6test::decodeGoldenHeader`（GoldenSample.h:17，ns w6test）。
- `buildRawTxBytes`：deposit → `canonicalEnvelopeBytes(OpBlockTx)`（OpTxDecode.h:307，ns bcos::evm::opstack）；normal → `_op_raw` 原样；单块向量直接用 golden `rawTransactions`。**v2（数据）**：`_op_raw` 是 per-tx 字段（在 `block.transactions[]._op_raw`，非顶层）；chain 向量 deposit 无 raw bytes，需从 `_op_deposit` dict 重建 OpBlockTx 再 canonicalEnvelopeBytes——**该模式已被 OpT8nReplayTest.cpp:581-583/703/731 证明，直接复用**。
- `parseOpBlockTxs`：`decodeOneRawTx`（OpTxDecode.h:381，与 executeOpBlock 内部同源）。
- `buildFiscoTx(env)`：`opEnvelopeToTars(env, hash)`（EngineServiceImpl.h:168，`bcos::engine::detail`，返回 `std::optional<bcostars::Transaction>`）+ **覆盖 extraTransactionBytes=env**（仅普通交易；deposit 已完整）+ pre-flight `bcosTransactionToEvmone(重建tx) == decodeOneRawTx(env)` 字段级校验（chain_id/nonce/fee/gas/to/value/data/access_list/r/s/v）。`TransactionImpl` copy-deleted → 全程持 `Transaction::Ptr`（make_shared）。
- `narrowGasUsed` / `hexCumulative` / `isL1AttributesTxOf`：**v2（B5）**——前两者在 OpBlockExecute.cpp:32-48 **匿名命名空间未导出**，R3 的「导出」必须实际落到 `OpBlockExecute.h`（否则 harness 复制漂移护栏不成立）；`isL1AttributesTxOf` 重实现 3 行（或与导出一并处理）。

**(d) 路径 A 驱动**：`scheduler.executeOpBlock(viewA, header, rawTxBytes)` → `OpExecuteBlockResult`（**v2：落点在 `OpSchedulerImpl.h:66`，ns `bcos::evm::engine`；主仓库旧位置 OpErrors.h:39 合流后失效**）（fork + newMutable）。

**(e) 路径 B 驱动**：调用 §7.0 的 `runOpBlockInjection`（逻辑落于 OpBlockInjector.h，不在测试内联）——`toBlockInfo(header)` → `system_call_block_start` + `sanitizeStateDiff` + applyStateDiff → deposit-first 检查 + `validateJovianBlockShape`（已导出 OpBlockExecute.h:76）→ 逐笔循环（executeDeposit / buildFiscoTx+executeTransaction，thread {fee, blockGasLeft, chainId, &hashes}，递减 + setCumulativeGasUsed）→ `finalizeBlock` → `Storage2State` 抓 MessagePasser + `sealOpBlock`（**吃 OpBlockResult，非 OpExecuteBlockResult**，见 G4 v2）+ `stateRootOf` + `computeOpTxRoot` → 窄化组装 `OpExecuteBlockResult`。

**(f) 对比 `assertEquivalent`**：
- **hard（mechanics，任何分叉即失败）**：gasUsed / txRoot / receipt 数 / 每笔 status / gasUsed / cumulativeGasUsed（精确 hex 串）/ effectiveGasPrice / logsCount **+ v2（架构 B8）log 内容（topics/data/address）**——同 count 异内容日志只在 logsBloom（soft，P1 掩掉）露头，P1 就该抓，成本极低。
- **type 判别（F1 修正）**：`OpExecuteBlockResult` **无 txTypes 字段**（v2：OpSchedulerImpl.h:66）——per-receipt 的 deposit/normal 判别**从 `rawTxBytes` 推导**（每个 envelope 首字节：0x7E=deposit，否则 EIP-2718 type byte），两路径输入相同故天然一致；"type" 不参与 A-vs-B 对比（同义反复），仅用于正确提取 opStackMeta 字段（deposit 只带 nonce/version）。`OpBlockInjector` 内部的 `result.txTypes` 只服务 `sealOpBlock` 的 receiptsRoot，不进对比面。
- **soft（v2：ALLOWLIST 驱动，D8）**：stateRoot / seal 五字段 / 每笔 output / `_op_*` 字段（**从 receipt->opStackMeta() 集合驱动**，审查 R3：isthmus normal 9 字段 / jovian normal 11 / deposit 2，不硬编码数量）。**弃全局布尔 `kAllowKnownDivergences`**——改 DIVERGENCES.md ALLOWLIST（{向量, 字段, expected} 枚举已知分叉：gaslimit×2 + cleanup×2），未列出的 soft 分叉 **P1 即红**；P3 清空 ALLOWLIST 翻严格。
- **golden 三方（G1：P1 软 REPORT → P3 硬）**：path A.stateRoot == `_op_expected.header.stateRoot`（**注意：在向量文件，不在 golden 文件**，v2 事实核查：golden 文件 6 键无 stateRoot）。P1 只 REPORT（防未覆盖向量上 Storage2State-vs-golden 差异大面积击穿 P1），P3 翻硬。**v2 残留风险显式化（架构）**：P0 的 OpT8nReplay 0 DIVERGE 已在 P1 前把 route A 执行对 golden 卡死，G1 软化是第二道冗余；但 OpT8nReplayTest 用 TestStateLedger+stateRootOf，**不是 Storage2State 桥**——Storage2State-only 的读写 bug 在 P1 期间（A-vs-B 同桥同偏 + golden 软化）全掩，由 P3 翻硬兜底，须在 §9 P1 行写明。
- **chain 向量锚（审查 R3 修订）**：chain 向量虽无 `.golden.json`，但每个 `blocks[i]._op_expected.header.stateRoot` **是 op-geth 根**（实测每块都有）→ 逐块加 `path A.stateRoot == blocks[i]._op_expected.header.stateRoot`（soft → P3 硬，与单块同）。
- **effectiveGasPrice 边界（v2 数据）**：`_op_expected.receipts` **无 effectiveGasPrice 字段**（仅 type/status/gasUsed/cumulativeGasUsed/logsCount/output/_op_deposit_*）——effectiveGasPrice 只能从 FISCO receipt 顶层字段做 **A-vs-B** 对比，不可与 expected 对。
- 分叉归因：stateRoot 不一致时 `dumpAccountDiff`（双视图 visitAccounts，地址键控 balance/nonce/codeHash/storage，上限 20 条）。
- 驱动 trace：`(i, blockGasLeft-after, fee-snapshot)`。

**(g) 驱动**：`runSingleVector`（seedPreState → **双 fork（A/B 各持独立 viewA/viewB，同一 MLS 两次 fork()）** → A vs B → assertEquivalent）；`runChainVector`（**每块双 fork、A/B 对比后各自 mergeView 继承状态**——v2 拓扑显式化，避免误读为共享单 view）。**逐向量 try/catch（审查 R3）**：path A 抛 `OpConsensusError`/`OpStorageError`、path B 抛 `std::runtime_error`——逐向量 catch → BOOST_ERROR + 继续（否则单向量异常中断整个套件，动态零分叉计数无法度量）。

**(h) CMake**：加入 `opstack-executor-block-tests` 源列表（tests/CMakeLists.txt:26-40），无新链接/编译定义。

## 8. Phase 2 修复（`opstack-executor/OpstackExecutor.h`，无签名变更）

> **v2 范围缩减（D7，用户裁定「建在 Task 4 合流后」）**：baseFee 部分 **Task 4 已完成**（`buildOpBlockInfo` 读 `header.baseFee().value_or(0)`，三调用点全部去恒 0），**§8 只做 gasLimit 修复**。原「修 executeDeposit baseFee=0 硬编码」的文本删除。

```cpp
    // v2（可执行性 B3）：detail:: 需在函数体/文件内 alias（照 OpstackExecutor.h:88 既有写法），
    // bcos::executor_v1::opstack 内 detail::narrowU256ToU64 不可见。
    namespace detail = bcos::evm::engine::detail;
    static uint64_t opBlockGasLimit(protocol::BlockHeader const& header, uint64_t fallback)
    {
        auto const gl = header.gasLimit();  // 非 optional 的 u256（BlockHeader.h:156）
        return (gl == 0) ? fallback : detail::narrowU256ToU64(gl, "BlockInfo::gasLimit");
    }
```

- **v2 行号**（Task 4 合流后）：`m_prepare`(:270) / `m_execute`(:300) / `executeDeposit`(:207)——三调用点的 gasLimit 参数改为 `opBlockGasLimit(header, blockGasLeft)`（baseFee 已是真实值，不碰）。
- **独立单测**（不依赖 harness）：断言修复后 `buildOpBlockInfo` gasLimit == header.gasLimit（非 blockGasLeft）、`executeDeposit` 用真实 header baseFee（v2：后者已是 Task 4 行为，单测变为回归固化）。

**非回归论据**：eth_call（blockGasLeft == header.gasLimit → 相同 BlockInfo）不变；OpstackExecutorTest（header gasLimit()==0 → fallback）不变；concept 6 参调用不受影响（生产 OP 不用 concept 路径）；attributes deposit 不读 BASEFEE（修复对 tx0 无行为影响）。

### 8.1 写回修复（审查 D5：`ethereum-executor/BCOS2Evmone.h`）

修 `eth::applyStateDiff` 的 `deleted_accounts` 分支：彻底删除账户（字段行 + **SYS_TABLES marker**，与 `Storage2State::applyDeletedEntry` 对齐），消除 EIP-161 touch-delete 后 path B 的幽灵空账户（`empty_account_cleanup` ×2 分叉的根因）。**该函数与 EthereumExecutor 共享** → 回归验证非 OP 路径（EthereumExecutor 相关测试 + `empty_account_cleanup` 定向确认）。

> **v2 tripwire 语义裁定（可执行性 B4）**：**不要照搬 `applyDeletedEntry` 的严格 ghost-delete tripwire**（Storage2State.h:396-401 账户缺失即抛异常）。共享的 `eth::applyStateDiff` 应**保留现有 `if (acc.exists())` 守卫**（BCOS2Evmone.h:157），只在账户存在时补「删字段行 + SYS_TABLES marker」；缺失时静默跳过。否则可能把非 OP 路径打成硬错。两语义下状态一致、仅异常面不同，stateRoot 无影响。
>
> **v2 回归面（可执行性实测）**：`EESTRunner`（ethereum-executor/tests/）**从不比对 stateRoot**（只赋值 post.stateRoot，无消费点），且 `verifyPostState` 只遍历 expected 账户、不扫全账本 → 幽灵空账户对 EEST 结果不可见 → **搞红风险低**。相关套件：EESTRunner + TestEthereumExecutorScheduler（transaction-scheduler/tests/）+ EthTransitionTest。

> 注意：`eth::applyStateDiff` 的零槽残留对 `stateRootOf` 不可见（读端过滤），无 stateRoot 影响；但 `has_storage` 读端不对称（审查 R3，StorageStateView 把零槽行算 has_storage）是**潜在** CREATE/CREATE2 碰撞判定风险——P1 加语料扫描（同块零写 + CREATE2 同地址）提前 triage，不预先修（语料大概率不触发）。

## 9. 阶段与验证

| 阶段 | 内容 | 验证 |
|---|---|---|
| **P0** | **dry-run 门禁**（G3 + 审查 R2）：**先**用现状 generator 全量 regen 确认 `vectors/` + `golden/` diff 为空（**v2：已实测通过**——regen.sh 未改 cases exit 0、字节等同、零漂移；regen.sh 本身有 git-diff 门禁，dry-run = 未改 cases 前跑一次）→ 加 2 case → regen | manifest set-equality（双向：`ls *.json` == manifest，审查 R2 补孤儿清理）+ OpT8nReplay 0 DIVERGE；正式 regen 后 diff **只含** cases.go + 4 新向量 + **4 新 `.golden.json`** + manifest（行数按 `append_if_absent` 实际 = 1 注释 + 4 文件名 = 5 行，非 2）；**定向向量红 → 停止，route-A 共识问题 triage**（G2） |
| **P1** | OpBlockInjector.h + harness + CMake（红） | **恰 4 个 soft 分叉（v2，D7/D8）**：2 `gaslimit_observer`（isthmus/jovian）+ 2 `empty_account_cleanup`（写回分叉，P2 修复）；**deposit_basefee ×2 为绿守卫**（验证语义，不计分叉）；其余向量零分叉（**动态计数，F2**）；**soft 由 DIVERGENCES.md ALLOWLIST 驱动**（未列出分叉 P1 即红，弃全局布尔）；mechanics 全 hard 绿（**含 log 内容比对，B8**）；**golden 三方 P1 软（REPORT）**，且 §9 明示 Storage2State-only bug 在 P1 期间掩蔽、P3 翻硬兜底；**/sys tripwire + has_storage 扫描（R3）**；**运行时（G5 v2）**：已实测全套件 0.66s → 全量进默认 gate，红探针向量（gaslimit×2 + cleanup×2 + basefee×2）无条件在全量 |
| **P2** | **gasLimit 修复（OpstackExecutor.h，baseFee 已由 Task 4 完成）+ 写回修复（BCOS2Evmone.h）** + 单测 | 重跑 harness 全绿（4 分叉全消失 + 绿守卫保持）；OpstackExecutorTest / eth_call / **EthereumExecutor 非 OP 路径**不退步；残留分叉 = 真发现（triaged 不豁免） |
| **P3** | **清空 ALLOWLIST（D8）** + golden 三方翻硬 + 强制清单 + 文档 | 全绿 gate；OP_RECEIPT_FIELDMAP.md §6 记录新 gate |

回归面：`opstack-executor-block-tests --run_test=OpT8nReplay`（0 DIVERGE）+ **OpstackExecutorTest（opstack-executor-tests GTest）** + **OpBlockSchedulerSuite** + OpNewPayloadRpcE2eSuite 全绿（黄金约束）。**v2（A2）**：`OpCallSchedulerTest`/`OpTwoPhaseTest` 全仓 grep=0 不存在（前者是 op-alignment 分支的类），以 OpBlockSchedulerSuite 替代。

## 10. 风险与护栏

| 风险 | 缓解 |
|---|---|
| FISCO tx round-trip 不匹配（extraTransactionBytes=preimage） | 覆盖为完整 envelope + pre-flight 字段级校验（v2：仅普通交易；deposit 已完整） |
| 写回不对称伪装成执行分叉 | stateRoot 对比 + dumpAccountDiff 归因（替代已删的 deleted_accounts 守卫） |
| Phase 2 修复回归 eth_call / OpstackExecutorTest | opBlockGasLimit fallback + 无签名变更 + 独立单测 |
| 路径 B 驱动偏离 processOpBlock | 复用导出函数 + mirror OpBlockExecute.cpp:93-207 + 驱动 trace |
| 已知分叉之外的**新**分叉 | **v2（D8）：ALLOWLIST 驱动**——未列出 soft 分叉 P1 即红；归因日志；阻断 P2/P3，triaged 而非豁免（弃全局布尔） |
| pre-Isthmus 向量跑 isthmusConfig | **v2（架构）：现状盲区非将来**——81 valid 语料已含 pre-Isthmus 向量（ecotone_contract_create / fjord_* / granite_* 等），现被 configAt 下限解析 flatten；D2 改写为「现状接受限制 + fork 语义归 OpT8nReplayTest（须验证 replay 用真实 fork config 解析，不同样 flatten）+ 等价性（非 fork 语义）是 gate 契约」 |
| chain 向量无 golden | buildHeaderFromEnv + canonicalEnvelopeBytes 构造 + assertCanonicalRoundTrip 兜底（v2：每块有 `_op_expected.header.stateRoot` 锚） |
| regen 后 golden 漂移 | regen.sh git-diff 门禁 + manifest set-equality |
| regen 可复现性被破坏（现有向量漂移） | P0 dry-run 门禁前置（G3，v2 已实测通过）；正式 regen 后任何现有向量改动 = 停止调查 |
| 定向向量在 OpT8nReplay 上红（processOpBlock≠op-geth） | **停止 harness 阶段，按 route-A 共识问题独立调查**（G2）——比 route B 更大的缺陷，非 harness 问题 |
| 运行时超标（>60s） | **v2（G5）：已实测 0.66s，删拆分**——全量进默认 gate，红探针向量无条件在全量 |
| 手写合约字节码错误 | golden stateRoot 不匹配即暴露 |
