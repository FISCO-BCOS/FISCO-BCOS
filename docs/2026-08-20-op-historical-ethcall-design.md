# OP 模式历史 `eth_call` 设计文档

- **日期**：2026-08-20
- **状态**：Draft（含并行 Review 结论；B1 定性已修正——OP 与 BaselineScheduler 共享同一 trie，见 §5.5；历史数据源方向见 §6）
- **分支**：`feat-op-historical-ethcall`
- **关联**：PR #5429（OpScheduler standalone）；`HistoricalStateBackend`（M13.2 / spec §5.13）

---

## 1. 背景与动机

OP 模式（`executor_version >= 3`）下，`OpScheduler` 实现了 `SchedulerInterface`，但其 `call()` 仅走 `coCallLatest`（fork 最新已提交状态 dry-run）。`OpScheduler` **未 override `callAtBlock`**，基类默认（`SchedulerInterface.h:67`）直接转发 `call()` —— 历史 blockTag 在 `callAtBlock` 被静默忽略，一律以最新状态执行（注：`safe`/`finalized` 在 RPC 层 `getBlockNumberByTag` 先经 FCU 解析，**未追踪时直接抛错**，仅解析成功后才走到 `callAtBlock` 的静默降级，`EthEndpoint.cpp:981-982`）。

目标：让 OP 模式的历史 `eth_call` 在 block N 的历史状态上执行，并保持 **OP 执行语义**（L1 fee、OP receipt 字段、OP fork 规则）。

## 2. 现状链路

```
EthEndpoint::call (EthEndpoint.cpp:570)
  ├─ getBlockNumberByTag → [blockNumber, isLatest]
  │    safe/finalized → engineService FCU 数字（未追踪则抛错）
  │    latest/pending → [latest, true]；earliest → [0, false]；hex → [num, false]
  ├─ isLatest → scheduler->call(tx, cb)
  └─ 非 latest → scheduler->callAtBlock(tx, N, cb)
       └─ m_scheduler = MultiVersionScheduler（无 callAtBlock override）
            └─ 基类默认 → call() → getScheduler().call() → OpScheduler::call()（最新态）   ← 历史被丢弃
```

`OpScheduler::call()` → `coCallLatest`（`OpScheduler.h:921`）：fork 最新存储 → 手建 `LedgerConfig` → `Storage2State` → `OpstackExecutor::executeTransaction(call=true)` → 丢弃 fork。

## 3. 目标与非目标

### 目标

- 历史 `eth_call`（非 latest tag）在 block N 状态上执行，返回与 OP 链一致的结果。
- 保持 OP 执行语义（与 `coCallLatest` 同一执行器）。
- 错误语义对齐 op-geth（越界 → JSON-RPC error，非 null）。

### 非目标

- 历史 `eth_estimateGas`、`eth_getProof` 的历史化（不在本设计范围）。
- 历史区块数据的读写分离/归档。
- 修复 `coCallLatest` 既有语义分歧（baseFee/`value` 校验，见 §5 M2/M3）——如做，走共享 call 路径，不限于历史路径。

## 4. 方案设计（路线 B）

**核心思路**：`OpScheduler` override `callAtBlock`，复用 `HistoricalStateBackend`（block N 历史 MPT 读透）+ `OpstackExecutor`（OP 执行）。

### 4.1 数据流

```
OpScheduler::callAtBlock(tx, N) ── task::wait + 双 catch
  └─ coCallAtBlock(tx, N)
       1. latestView = m_multiLayerStorage->fork()
       2. latest = getCurrentBlockNumber(latestView, fromStorage)
       3. N > latest        → 抛 InvalidBlockNumber
       4. N == latest       → 复用 coCallLatest（快路径）
       5. !feature_l2_ethereum_compat → 抛 InvalidStatus（OQ6 门控）
       6. block = getBlockData(latestView, N, HEADER, *blockFactory)
          stateRoot = block->blockHeader()->stateRoot()；空则抛 InvalidStatus
          auto blockHeader = block->blockHeader(); auto const& header = *blockHeader;  // 防悬垂
       7. HistoricalStateBackend<ViewType> backend(latestView, stateRoot);   // ViewType = MultiLayerStorage::ViewType
          storage2::View<MutableStorage, void, decltype(backend)> histView(&backend);
          histView.newMutable();
       8. 手建 OP LedgerConfig（N / header.timestamp() / features / evmcRevision=cfg.rev）
       9. Storage2State<decltype(histView)> stateView(histView);
          fee = op::loadOpFeeParams(stateView)                    // 历史 L1Block fee 参数
          blockGasLeft = static_cast<int64_t>(narrowU256ToU64(header.gasLimit(),
                            "OpScheduler blockGasLeft"))   // 双参签名（OpCommon.h:158，无默认值）
      10. RecentBlockHashes hashes(histView, header.number(), toEvmcBytes32(header.parentInfo().blockHash), &hashErr)
      11. OpstackExecutor executor(m_receiptFactory, m_hashImpl, cfg, sharedError);
          receipt = co_await executor.executeTransaction(histView, header, *tx, 0,
                      *ledgerConfig, /*call=*/true, fee, blockGasLeft, m_chainId, &hashes)
      12. hashErr 有值则抛；否则返回 receipt（视图随协程结束丢弃）
```

### 4.2 改动文件

| 文件 | 改动 |
| --- | --- |
| `opstack-executor/OpScheduler.h` | ① `#include <bcos-transaction-scheduler/HistoricalCallStorage.h>`；② override `callAtBlock`（紧跟 `call()`）；③ 新增 `coCallAtBlock` |
| `libinitializer/MultiVersionScheduler.h/.cpp` | 新增 `callAtBlock` override 透传 `getScheduler().callAtBlock(...)`（**必须**，否则功能静默降级，见 §5 B3） |
| `opstack-executor/tests/OpSchedulerTest.cpp` | 历史 eth_call 测试（仿 `TestEthCallHistory` 范式） |

### 4.3 错误处理

> 表头"参照 BaselineScheduler"含**增强**：`BaselineScheduler::callAtBlock`（`BaselineScheduler.h:941-1011`）现状对"MPT 节点缺失/执行期异常"均经通用 catch 映射为 `UnknownError`（-70000），无专门探测；下表两处 `InvalidStatus`/`classifyException` 是本设计的增强，非既有对齐。

| 场景 | 行为（参照 BaselineScheduler，含增强） |
| --- | --- |
| `N > latest` | `InvalidBlockNumber`：`"eth_call: block N does not exist (latest: M)"`（带 `eth_call: ` 前缀，`BaselineScheduler.h:954-956`） |
| 非 `feature_l2_ethereum_compat` | `InvalidStatus`（响亮拒绝） |
| `stateRoot` 为空 / 该 root 无 MPT 落盘 | `InvalidStatus`（**增强**：需可探测能力，见 §5 M5） |
| 执行期异常 | 双 catch → Error 走回调（**增强**：复用 `classifyException` 提升精度，BaselineScheduler 现为通用 `UnknownError`） |

## 5. 并行 Review 结论（4 agent，2026-08-20）

> 交叉验证：Agent A（C++ 正确性）与 Agent C（MPT/存储兼容性）**独立发现**同一 Blocker B1。

### 🔴 Blocker

| # | 问题 | 来源 |
| --- | --- | --- |
| **B1** | **OP 链从不持久化 `/mpt/` trie 节点**。`stateRootOf`（`bcos-evm/.../StateRootCompute.h:45`）无状态全量重建 root，`computeTrieRoot` 节点被丢弃（`HashBuilder.h:112-114` 警告）。写 `/mpt/` 行仅两处：创世导入（`Ledger.cpp:2391`，`l2EthereumCompat` 门控）与 `BaselineScheduler` 路径。`HistoricalStateBackend` 读历史 root 缺节点时抛 `MPTInvariantViolation`（`Trie.h:47`；`MPTMissingNode` 为注释幽灵名，`Errors.h` 无此类型）→ block N>0 历史 eth_call 整体不可用（响亮失败，仅 block 0 可能成功）。⚠️ **定性已修正（§5.5）**：OP 与 BaselineScheduler 共享同一棵 trie，可复用构建/落盘/读取路径，非"无解" | A + C（独立）+ D(Major2)，修正于 2026-08-20（R2：幽灵类型名） |
| **B2** | `std::addressof(&backend)` 双重指针编译错误 → 应 `std::addressof(backend)` | A |
| **B3** | `MultiVersionScheduler` 无 `callAtBlock` override → RPC 历史分支静默降级为最新态（错误答案，非报错） | D |

### 🟠 Major

| # | 问题 | 建议 |
| --- | --- | --- |
| M1 | `probeHasStorage`（EIP-7610 CREATE 碰撞）经 `range()` 透传 latest → 历史 `has_storage` 判错（成功/失败反转）。MPT 无法枚举 slot key，结构性 | 历史模式 `computeHasStorage=false` 降级 + 文档化；或建 slot-key 索引 |
| M2 | baseFee/fee-cap：历史高 baseFee 区块被拒（缺省 gasPrice=2gwei），op-geth 以 `baseFee=0` 运行（`NoBaseFee`） | 共享 call 路径统一修：`call=true` 且 gasPrice 未指定时清零 baseFee |
| M3 | `skipBalanceCheck` 连 `value` 一起跳过：`value>0` 无余额时 FISCO 返回成功，op-geth 报 `ErrInsufficientFunds` | `opValidate` 保留 `balance >= value` 校验（共享路径） |
| M4 | `header` 必须绑定局部 `shared_ptr`（`auto blockHeader = block->blockHeader(); auto const& header = *blockHeader;`）否则悬垂 UB | 仿 `coCallLatest`（`OpScheduler.h:934-936` 防悬垂注释） |
| M5 | 空 stateRoot 门控无判别力（OP 块 stateRoot 永不为空） | 改为可探测"该 root MPT 已落盘"（如 `readOne(mptNodeStateKey(stateRoot))`） |
| M6 | 边界检测两层一致性：RPC 层 FCU 检测 vs `callAtBlock` 内部检测 | `callAtBlock` 完整复刻 BaselineScheduler 边界序列，MPT 异常翻译为语义化错误 |
| M7 | 错误码粒度丢失：`bcos::Error` 硬编码映射 `-32603`，`SchedulerError` 码丢失 | Error 通道携带原始码 / `error.data` 透传 |

### 🟡 已确认正确（非问题）

- 点读层逐字段兼容（Balance 十进制 / Nonce / CodeHash 32B / Slot 32B ↔ `Storage2State::fetchAccount`）。
- L1 fee 参数走历史 MPT **正确**（对齐 op-geth `NewL1CostFunc(config, statedb@blockN)`）。
- BLOCKHASH 窗口 `[N-256,N-1]` + 父哈希种子对齐 op-geth `GetHashFn`。
- 越界 → JSON-RPC error（非 null）对齐 op-geth "header not found"。
- `SYS_CODE_BINARY` / `SYS_NUMBER_2_HASH` 透传 latest 在 canonical 链上安全。
- 协程生命周期、模板类型（除 B2）、并发安全通过。
- `pending` 标签语义、`estimateGas` TOCTOU 为既有已知偏差（文档化即可）。

## 5.5 关键修正：OP 与 BaselineScheduler 共享同一棵 trie（2026-08-20 复核）

> 深挖 `StateRootCompute` / `MPTBuilder` 后**修正了 §5 B1 的"无解"定性**：OP 需要的 evmone stateRoot 与 BaselineScheduler 构建的 MPT 根是**同一棵 trie**，只是 OP 从未把节点落盘。适配路径由此明确，工作量大降。

### 5.5.1 同 trie 证据链（逐字段核对）

| 组件 | OP `stateRootOf`（StateRootCompute.h/.cpp） | BaselineScheduler `buildMPTStateRoot`（MPTBuilder.h） | 一致 |
| --- | --- | --- | --- |
| trie 核心 | `bcos-ledger/mpt/computeTrieRoot`（注释明说替代 evmone mpt_hash，33-vector gate） | `commitTrie`（同核心） | ✓ |
| account key | `keccak256(addr)`（:100） | `accountKeyHash(addr)` = keccak256（MPTReadView.cpp:29） | ✓ |
| storage key | `keccak256(slot)`（StateRootCompute.cpp:29） | `slotKeyHash(slot)` = keccak256（StorageValueCodec.cpp:33） | ✓ |
| storage value | `rlp(trimmed)`，zero 跳过 | `encodeStorageValue`（trim + zero 不在 trie） | ✓ |
| account leaf | `rlp(nonce, balance-be, storageRoot, codeHash)` | "standard Ethereum four-tuple"（MPTBuilder.h:360） | ✓ |
| 节点落盘格式 | —（丢弃） | `StateKey{"/mpt/", content-hash} → RLP`（KeyPrefixes.h:51） | 落盘侧现成 |

### 5.5.2 修正后 B1 的定性

- **原**：OP 无历史 MPT → 需另起炉灶。
- **修正**：OP 的 `stateRootOf` 已在计算同一棵 trie，缺的只是**把 `computeTrieRoot` 的 `newNodes` 接出来落盘 + 改为增量构建**。`commitTrie` / `flushTrieNodes` / `ViewNodeStorage` / 提交路径（`mergeBackStorage` 已合并 flat + 任意 state 行）**全部现成可复用**。
- **已核实一致（2026-08-20 复核）**：`buildAndCollect` 与 `stateRootOf` 的 balance/nonce 两侧标量均归结为最短大端 RLP（同一 `rlp::encode` 模板 + 等价手工 trim，0 值两侧均编码为 `0x80`），**字节级一致**。由 33-vector 对拍 gate 持续守护；真正需持续对齐的是**账户集合/路径选择语义**（`sawEthereumRow` 门控、tombstone、first-touch 冷 slot 不回填），已有 golden 门锚定。

### 5.5.3 MPT 数据层链路（BaselineScheduler 写 MPT 的完整流程）

```
EthereumExecutor::executeTransaction → applyToStorage 直写 flat（/apps/<40hex> + s_code_binary）
  → BaselineScheduler::coExecuteBlock :466-480（per-tx 后）
  → shouldBuildMPT ? → buildMPTStateRoot :351-370（读父块头 parentStateRoot）
  → buildAndCollect :MPTBuilder.h:403（单遍 range 扫 flat delta → 每账户 storage trie → account trie）
  → commitTrie（增量合并，产 newNodes + root）
  → flushTrieNodes :498（批量写节点到 ViewNodeStorage）
  → 节点行 StateKey{"/mpt/", content-hash} → RLP（MPTNodeStorage.h:105）
  → finishExecute :164-260（setStateRoot 到块头 + calculateHash + 校验 :537）
  → coCommitBlock → mergeBackStorage 持久化（flat + /mpt/ 节点一起）
```

关键概念：**content-addressed**（节点 key = 内容 hash），因此给定任意 stateRoot 都能沿根下探解析——这是历史读取的前提，且 OP 只要落盘即可天然获得。

### 5.5.4 `BaselineScheduler` 不能驱动 OP（排除"换调度器"捷径）

执行循环层已统一（Task 5：OpScheduler 与 BaselineScheduler 都用共享 `SchedulerSerialImpl` + `ExecuteContext`/`BlockContext` 协议），但**块级编排层不可互换**：

| 矛盾 | 位置 | 性质 |
| --- | --- | --- |
| `BaselineScheduler` 无 OP `preBlockOpSteps`（deposit 注入 / L1Block 更新 / Jovian / DA） | 块级 | 缺失 |
| `BaselineScheduler:476` 只传 5 参 → OP `BlockContext`（fee/gas/hashes/chainId）无处注入 | 块级 | 语义丢失 |
| `buildMPTStateRoot` 在 OP 路径不可用（OP 的 root 计算归属 `stateRootOf`） | 块级 | 职责错位 |
| txpool 取交易 / header 发布机制不匹配 | 块级 | 不兼容 |

**结论**：历史能力必须由 `OpScheduler` 在自己的块级编排里补，而非引入 BaselineScheduler。

## 6. 开放决策：历史状态数据源的形态

**B1 修正后**（§5.5），方向①从"另起炉灶"降级为"接出节点 + 增量构建"。三选一：

| 方向 | 内容 | 代价 | 备注 |
| --- | --- | --- | --- |
| **①a 增量构建 + 落盘**（推荐） | OP finalize 用 `buildAndCollect`（增量 `commitTrie` + `flushTrieNodes` 落 `/mpt/`），root 与 `stateRootOf` 字节一致；历史读取复用 `HistoricalStateBackend` | **中（~3–5 人日）** | 复用 `commitTrie`/`flushTrieNodes`/`ViewNodeStorage`/提交路径；需 feature flag 门控 |
| **①b MVP：全量 + 落盘** | 保留全量 `stateRootOf`，额外把**两层** `computeTrieRoot` 的 `newNodes` 一并 flush 到 `/mpt/`（账户 trie 于 `StateRootCompute.h`、每账户 storage trie 于 `accountStorageRoot`/`StateRootCompute.cpp`，是两次独立调用；只 flush 外层账户 trie 会得到缺 storage 子 trie 的悬空 root）。content-addressed 同 key 覆盖无重复写放大，但每块变更路径上的新节点仍**净增长**（全量重建每块 O(N) 计算 + 变更子树新节点行），非零存储代价 | **小（~1–2 人日）** | 适合小链/验证链路；大链性能不可接受 |
| **② 扁平历史快照** | 为 OP 另建"物化到 block N 的 flat 状态"后端，替代 MPT 读取 | 中 | 与 ethereum-executor 的 flat 哲学一致，但无现成历史机制 |
| **③ MVP 收敛** | 仅 genesis（block 0）可查，其余高度返回明确错误 | 小 | 先验证整条链路（含 B3/M4/M5），等 ① 落地再放开 |

**推荐**：先 **③ + ①b**（跑通链路 + 修 B3/M4/M5 + 全量落盘验证），再演进到 **①a**（增量）。所有方向都需在 `OpScheduler` 块级编排内实现（§5.5.4）。

## 7. 测试计划

| 用例 | 断言 |
| --- | --- |
| `N > latest` | `InvalidBlockNumber` |
| `N == latest` | 等价 `coCallLatest` |
| 非 scenario B / root 无 MPT | 响亮 `InvalidStatus`（不静默） |
| 真历史状态（block N 写、N+1 改，call @N） | 读 block N 值（证明历史状态） |
| OP 语义保持 | 历史 receipt 含 OP 字段（l1_fee 等） |
| 历史 BLOCKHASH | 祖先哈希可回溯 |
| MultiVersionScheduler 透传 | RPC 历史分支到达 `OpScheduler::callAtBlock`（非静默降级） |
| 门控/错误通道 | 所有失败呈现为 JSON-RPC error 而非 null |
| **root 一致性对拍（①b/①a 核心正确性）** | 同一状态分别走 `stateRootOf` 与 `buildAndCollect`，断言 root 相等 + 历史读取可沿 root 下探（扩展 33-vector golden 门，范式见 `HashBuilderIncrementalTest`） |

## 8. 已知限制（文档化）

- 历史 eth_call 仅当前 fork 区间内与 op-geth 严格一致（跨 fork 边界用最新 fork 规则）。
- `range()` 类操作（`has_storage`/SELFDESTRUCT 清理）在历史路径以 latest 回答（M1 降级策略）。
- receipt OP 扩展字段为 FISCO 模拟产物，非 op-geth eth_call 对拍面。
- `pending` 标签无 mempool 语义。

## 9. 工作量评估（2026-08-20 修正：共享 trie 后大幅下调）

**执行层**（`OpScheduler::callAtBlock`/`coCallAtBlock` + `MultiVersionScheduler` 透传 + B3/M4/M5 修正）：

- 核心实现：0.5–1 人日
- 测试：0.5–1.5 人日
- 编译/回归：0.5–1 人日
- 对拍/评审：0.5–1 人日
- **执行层小计：约 2–4 人日**

**历史数据源（方向①，OP 侧 MPT 适配）**：

- ①b MVP（全量 `stateRootOf` + 节点落盘）：**~1–2 人日**（改动集中：`stateRootOf` 暴露 `newNodes` + finalize flush，~30–50 行）
- ①a 完整（增量 `buildAndCollect` + 落盘 + 历史读取复用 `HistoricalStateBackend` + root 一致性验证）：**~3–5 人日**
- 历史读取后端：复用 `HistoricalStateBackend`（大概率直接兼容，同一 trie），小适配 ~0.5–1 人日

**合计**：

- 方向③ + ①b（MVP 全链路）：约 3–5 人日
- 方向③ + ①a（完整，含对拍/评审）：约 5–8 人日
- 相比修正前的 4–8 人日：**①b 路径显著更低**，因核心 trie / 编码 / 落盘 / 读取基础设施全部现成
