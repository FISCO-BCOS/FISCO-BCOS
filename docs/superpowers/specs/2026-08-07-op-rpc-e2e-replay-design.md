# W6：L2 端到端真链对拍（真实 RPC 路径块执行重放）设计

> 目标：从真实 `engine_newPayload` RPC 请求形态出发，驱动 FISCO OP 执行器完整链路（真实解析 → OP 引擎 → 落库），与 op-geth v1.101702.2 金标准逐块对拍七项断言。
> 状态：**Approved**。日期：2026-08-07。
> 关联：对比方案 `docs/opstack-opgeth-e2e-comparison.md` §3 L2；W1（EngineHelper 真实解析）/W2（OP composition root）/W3（PBFT 门控）。分支：`feat-op-executor-e2e`。

---

## 1. 背景与动机

对比方案三层递进：L0 静态对拍（W4）→ L1 动态差分 gate（W5）→ **L2 端到端真链对拍（W6）**。

L1 现有基建（`feat-op-validator-loop` 的 `EngineNewPayloadGateTest`）已覆盖 33 向量 + 链式双块 + mutation matrix，但它的请求是**测试内手工构造的 `NewPayloadRequest`**（`makeGoldenRequest` 直读 golden JSON 填 C++ 对象），**没有走 RPC 解析路径**。W6 的核心增量正是对比方案 §3 L2 段落明言的一环：

> 形态：`EngineNewPayloadGateTest` 从"测试内手工构造 NewPayloadRequest"升级为"真实 RPC 请求 → EngineHelper.parse → EngineService"。

W1 修复的 `EngineHelper::parseNewPayloadRequest`（rawTransactions/withdrawalsRoot 填充 + decode 容错）在生产形态下从未被真实请求驱动过——W6 harness 正是它的生产路径验收。

## 2. 决策记录

| # | 决策 | 依据 |
|---|---|---|
| **D1** | 形态 = **进程内 RPC 对拍**：真实 JSON-RPC params → `parseNewPayloadRequest(V4)` → `EngineService.newPayload(4)` → `executeOpBlock`。无网络、无端点分派 | 用户选定；对比方案 §3 L2 原文形态 |
| **D2** | **两债留在 W6 外**：PBFT retry loop、V4 生产互通（capability 协商）不阻塞对拍 harness，记入对拍报告待办 | 用户选定；进程内 harness 不走共识也不走端点协商 |
| **D3** | **V4 端点折中**：绕开端点桩（`newPayloadV4` 是 `-38005` 硬桩），直接调 `parseNewPayloadRequest(params, factory, V4)` + `EngineService.newPayload(request, 4)`。`EngineHelper.h:68` 公开 API 接受 V4（W1 `EngineHelperTest` 已用 V4 通过） | 用户选定；OP 引擎分支强制 version==V4（`EngineServiceImpl.h:72` `c_opIsthmusPayloadVersion=4`，"Isthmus+ 禁 V3"），而 RPC 端点 V4 未实现 |
| **D4** | **语料 vendored 进 e2e**：`t8n/golden/engine/`（41 文件含 chainA/B + SHA256SUMS + manifest）+ `t8n/generator/` 从 `feat-op-validator-loop` 拷入，成为 e2e 跟踪文件 | 用户选定；语料是 op-geth v1.101702.2 确定性生成的纯数据，与代码解耦 |
| **D5** | 成功标准 = **全量 33 向量 + chainA/B 链式双块**，每条走真实解析→引擎全链路，七项断言全等，产出对拍报告 | 用户选定 |
| **D6** | **方案 A：独立新测试** `OpNewPayloadRpcE2eTest.cpp`（自包含），不移植 val-loop 的 `EngineNewPayloadGateTest` | 用户选定；干净聚焦 L2 增量，不背 mutation matrix 等无关基建 |
| **D7** | **断言框架 = Boost.Test**（本分支 `bcos-evm/test/opstack` 惯例，`BOOST_TEST_MODULE BcosEvmOpstackTests`）；val-loop gate 测试的 GTest 不沿用 | 探查确认 |

## 3. 组件变更清单

| 文件 | 变更 |
|---|---|
| `bcos-evm/test/opstack/t8n/golden/engine/`（41 文件） | **新增（vendored）**：从 `feat-op-validator-loop` 拷入，含 `chained/chainA·B.golden.json` + `SHA256SUMS` + `manifest.txt` |
| `bcos-evm/test/opstack/t8n/generator/`（main.go/cases.go/regen.sh） | **新增（vendored）**：从 `feat-op-validator-loop` 拷入，未来再生成用 |
| `bcos-evm/test/opstack/OpNewPayloadRpcE2eTest.cpp` | **新增**：W6 harness（Boost.Test），33 + 2 个用例 |
| `bcos-evm/test/CMakeLists.txt` | **修改**：`bcos-evm-opstack-tests` 目标加 harness 源 + `EngineHelper.cpp` 直编入 + bcos-rpc include 路径 + `OP_T8N_GOLDEN_ENGINE_DIR` compile definition（沿用 val-loop 组合模式） |
| `docs/opstack-opgeth-e2e-comparison.md` | **修改**：追加 L2 章节——逐向量七项断言结果表 + 差异归因 + 待办（V4 端点/PBFT retry/能力广播） |

## 4. 数据流

```
[golden JSON]  bcos-evm/test/opstack/t8n/golden/engine/*.golden.json
   ├─ rawTransactions       真实 EIP-2718 字节（deposit + 普通 tx 混合）
   ├─ encodedHeaderHex      op-geth 完整 RLP header
   └─ blockHash / transactionsRoot   交叉断言用
        ↓ harness：RLP 解码 encodedHeaderHex → 提取 payload 全部字段
        │   (parentHash/feeRecipient/stateRoot/receiptsRoot/logsBloom/number/
        │    gasLimit/gasUsed/timestamp/extraData/prevRandao/baseFee/
        │    withdrawalsRoot/blobGasUsed/excessBlobGas/parentBeaconBlockRoot)
        ↓  + rawTransactions + blockHash → 构造 op-geth 发送形态的
        │    engine_newPayloadV4 params (JSON)。字段名严格遵循
        │    engine_newPayloadV4 规范 ExecutionPayload schema；构造形态以
        │    W1 `EngineHelperTest.cpp`（bcos-rpc/test/unittests/rpc/）的
        │    V4 params 既有样例为参照
   EngineHelper::parseNewPayloadRequest(params, factory, ApiVersion::V4)   ← W1 真实解析
        ↓
   EngineService<OpSchedulerImpl>::newPayload(request, 4)                   ← W2 装配 + OP 引擎
        ↓   handleOpNewPayload：Isthmus 门(ts) → version==4 → 六步执行
   OpSchedulerImpl::executeOpBlock → processOpBlock → sealOpBlock → 落库
        ↓
   解码产出的 FISCO block header → 七字段
        ↓
   vs golden（encodedHeaderHex 解码字段 / golden.blockHash / golden.transactionsRoot）
```

## 5. 七项断言

| # | 字段 | golden 来源 | FISCO 来源 |
|---|---|---|---|
| 1 | blockHash | `golden.blockHash`（op-geth `block.Hash()`，永不自算） | 执行后 block 的 hash |
| 2 | stateRoot | header RLP 解码 `stateRoot` | `header->stateRoot()` |
| 3 | receiptsRoot | header RLP 解码 `receiptsRoot` | `header->receiptsRoot()` |
| 4 | withdrawalsRoot | header RLP 解码 `withdrawalsRoot` | `header->withdrawalsRoot()` |
| 5 | gasUsed | header RLP 解码 `gasUsed` | `header->gasUsed()` |
| 6 | txRoot | `golden.transactionsRoot` | `computeTxRoot(rawTransactions)` |
| 7 | logsBloom | header RLP 解码 `logsBloom` | `header->logsBloom()` |

失败时 dump 21 个 header 字段逐项定位（仿 gate 测试 header dump 模式：`hdr_01_parentHash` … `hdr_21_slotNumber`），并在对拍报告中记录差异归因。

## 6. 错误处理与边界

- **配置**：EngineService 装配用 OP 配置（`OpForkSchedule` + isthmus/jovian 时间戳，对应 W2 的 `[chain]` 配置）。`isIsthmusActiveAt(payload.timestamp)` 为真才进 OP 分支——33 向量全为 `isthmus_*`/`jovian_*`（Isthmus+），pre-Isthmus `-38005` 门不触发
- **V4 绕过**：不经过 `newPayloadV4` 端点桩。直调 parse(V4) + `newPayload(4)`。V4 端点桩债留 W6 外（D3）
- **链式双块（chainA/B）**：顺序投递 A → B（B parentHash=A），跨块 state 延续；复用 gate 测试双块语义（B parent-known 后 VALID）。断言 A、B 各自七项
- **落库隔离**：`executeOpBlock` 落库后从 block header 断言；用 StorageFixture/MemoryLedger 隔离测试态
- **Assertion 框架**：Boost.Test（D7），`BOOST_REQUIRE`/`BOOST_CHECK_EQUAL`
- **语料信任度**：vendored 语料 + `SHA256SUMS` 锚定（generator 重新生成属 W6 外待办）

## 7. 测试与 CMake

- **用例**：33 个向量各 1 个 `BOOST_AUTO_TEST_CASE`（jovian_deposit_only、isthmus_transfer_multi、jovian_da_mix、isthmus_setcode_7702、isthmus_tx_reverted … 全量）；链式双块 chainA/chainB 各 1 个 case（共 2）；合计 35 个用例
- **CMake**（`bcos-evm/test/CMakeLists.txt`，沿用 val-loop 组合模式，见 §3）：
  - harness 源加入 `bcos-evm-opstack-tests`
  - `EngineHelper.cpp`（bcos-rpc）直编入测试二进制（与 `EngineServiceImpl.cpp` 同模式，避免拖 bcos-rpc 依赖闭包）
  - `target_include_directories` 加 bcos-rpc 路径
  - `target_compile_definitions` 加 `OP_T8N_GOLDEN_ENGINE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/opstack/t8n/golden/engine"`
  - link `bcosevm::opstack` + `bcos-framework`

## 8. 验收标准

- 33 向量 + chainA/B（35 用例）全部走「真实 JSON params → parseNewPayloadRequest(V4) → newPayload(4) → executeOpBlock」路径
- 35 用例七项断言全等（blockHash/stateRoot/receiptsRoot/withdrawalsRoot/gasUsed/txRoot/logsBloom）
- 链式双块跨块 state 延续正确（A→B 顺序投递均 VALID）
- `docs/opstack-opgeth-e2e-comparison.md` 追加 L2 对拍报告（逐向量结果表 + 差异归因 + 待办清单）
- 现有 OP 测试不回归

## 9. 不在 W6 范围（记入对拍报告待办）

- **V4 端点桩**（`newPayloadV4` RPC 端点实现，生产 op-node 互通时修）
- **PBFT retry loop**（OP 模式 proposal 短路后无限重推；禁 sealer/抑制重推决策）
- **V4 能力广播**（`supportedOpCapabilities()` 广告 V4 实为正确——引擎强制 V4；留给生产互通验证）
- **generator 重新生成 golden**（语料信任度由 vendored SHA256SUMS 锚定；如重生成需 op-geth v1.101702.2 环境）
- **W4/W5**（L0 静态矩阵 / L1 块级断言扩充）——独立工作项，可并行
