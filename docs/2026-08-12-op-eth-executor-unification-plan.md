# opstack 与 ethereum 执行流程统一（执行层 + 存储适配层）实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在保留 op-geth 对拍对齐的前提下，把 opstack 块路径的逐笔执行统一到 `OpstackExecutor` 概念方法面、并把 `Storage2State` 并入带 poison 通道的共享读桥，消除三份重复（读桥/写回/转换）。

**Architecture:** ① `StorageStateView` 加可选 poison 通道成为 `SharedReadBridge`，`applyStateDiff` 加 error 通道；② 块路径 processOpBlock 重构为编排器，逐笔经 `executeTransaction`（新增 `env` 完整信封参数）/`executeDeposit`/`executeBlockStart`/`finalizeBlock`；③ Parity Switch（`constexpr bool`）双路径并存，双路径一致性测试 + op-geth golden 作闸门，全绿后删旧路径。

**Tech Stack:** C++20, bcos-task（协程）, storage2（MLS/View）, evmone/bcos-evm-opstack, Boost.Test, CMake（UNITY_BUILD OFF 目标）。

## Global Constraints

- 语义零变化：op-geth 对拍全绿（t8n 125 向量 + e2e 81 用例 + golden/engine 79）。t8n 腿经 TestState 不经桥；e2e 腿经真实读桥——两腿是不同性质闸门。
- 块级语义保留在编排器：deposit 前置/顺序、D-1 Jovian DA-footprint scalar 覆盖（读 `txs[0].data[176:178]`）、`isL1AttributesTx` 内容校验、`loadOpFeeParams` 块中途加载、blockGasLeft 显式递减、原子拒块（任何 throw → 整块丢 view）。
- 签名信封（§6.3.1）：`m_prepare` 的 `signedTxEnvelope` 必须用**完整签名信封**，不得用 `extraTransactionBytes()`（签名前像）。
- evmcRevision（§6.1，用户裁定）：块路径方法**豁免** ledgerConfig-evmcRevision 交叉检查，fork 唯一来源 `configAt(timestamp)`；eth_call 路径保留检查。
- 错误分类（§8）：编排器必须捕获 executor 的 `OpTxValidationFailed` 并**重抛为块级 `std::runtime_error`**（落入 executeOpBlock 双 catch 的 `catch(...)` → INVALID）；任何漏译类型静默变 -32603。空块/首笔非 deposit/deposit 顺序是编排器层检查，照抛 runtime_error。
- 执行等价口径（§6.3.3）：验收按"执行等价"（逐字段证明执行中立，状态根一致为最终验证），chain-id 拒绝 + low-S/EIP-2 拒绝**不豁免**。
- 存储读桥：默认以 `StorageStateView` 的 EVMAccount 读为共享实现；分歧 1（has_storage 探针，须含 tombstone/零值过滤）、分歧 2（/sys/ 路由，OP predeploy 不得落入 c_systemTxsAddress）按 Storage2State 语义并入；e2e golden + 定向用例为闸门。
- 写回：`applyStateDiffWithPoison` **不是薄封装**——/apps/ 路由、零值删行（removeOne+重读守卫）、EIP-161 create 守卫、账户删除 + ghost-delete tripwire、写透缓存逐项迁移；写回错误通道与读桥 poison **同一实例**，先设 flag 再 rethrow。
- 现有测试不得退步（tests-must-not-regress 约束）。
- 每阶段独立验收；Parity Switch 两分支用**同一桥类型**。

## 文件结构

| 文件 | 动作 | 职责 |
|---|---|---|
| `ethereum-executor/StorageStateView.h` | 改 | `SharedReadBridge`：+ poison 通道（`std::optional<std::string>*`）、has_storage 分歧并入、/sys/ 路由验证 |
| `ethereum-executor/BCOS2Evmone.h` | 改 | `applyStateDiffWithPoison`（error 通道 + 写侧契约迁移） |
| `ethereum-executor/tests/StorageBridgeTest.cpp` | 建 | 读桥 poison 通道 / has_storage / 写侧契约测试 |
| `opstack-executor/OpstackExecutor.h` | 改 | `executeTransaction` + `env` 参数；`executeBlockStart` 新方法；块路径方法注入桥/poison |
| `opstack-executor/OpBlockExecute.h/.cpp` | 改 | `OpBlockOrchestrator`（编排器）+ 旧 `processOpBlock` 保留（parity 参照） |
| `opstack-executor/OpSchedulerImpl.h` | 改 | + `m_hashImpl` 注入；`c_useConceptOrchestrator`；桥切到 SharedReadBridge |
| `opstack-executor/tests/OpOrchestratorParityTest.cpp` | 建 | 双路径一致性 runner（新建，MLS fixture） |
| `opstack-executor/tests/OpCallSchedulerEthCallTest.cpp` | 建 | eth_call happy path（补回归面） |
| `opstack-executor/tests/OpSchedulerImplSmokeTest.cpp` | 改 | 分类钉死用例（空块/首笔非 deposit/normal 无效/gas 超限 → INVALID） |
| `opstack-executor/tests/support/SeedPreState.h` | 改 | `applyDiff(seeding=true)` → 共享写回 |
| `opstack-executor/Storage2State.h` | 删 | 阶段三 |
| `opstack-executor/Storage2StateHelpers.h` | 删 | 阶段三（若仅被 Storage2State 引用） |

## 阶段一：存储适配层统一

### Task 1: SharedReadBridge poison 通道（读侧错误分类）

**Files:**
- Modify: `ethereum-executor/StorageStateView.h`
- Test: `ethereum-executor/tests/StorageBridgeTest.cpp`（新建）

**Interfaces:**
- Produces: `StorageStateView(Storage& storage, std::optional<std::string>* error = nullptr)`——`error == nullptr` 时行为与现状完全一致（读失败吞 absent）；`error != nullptr` 时读失败记录首个错误到 `*error`（poison 语义，不重抛），供块尾 `poisoned()` 判定。

- [ ] **Step 1: 写失败测试**

```cpp
BOOST_AUTO_TEST_CASE(PoisonChannelRecordsReadFailure)
{
    // 用一个读必失败的存储（构造期注入抛错的 backend，或用一个 key 形状非法的表）
    auto storage = makeFailingStorage();
    std::optional<std::string> err;
    bcos::executor_v1::eth::StorageStateView<decltype(storage)> view(storage, &err);
    // 读一个会触发存储层异常的表
    auto acc = view.get_account(evmc_address{});
    BOOST_CHECK(acc == std::nullopt);       // 读侧照旧返回 absent
    BOOST_CHECK(err.has_value());            // 但错误已记录（poison 语义）
}

BOOST_AUTO_TEST_CASE(NoChannelKeepsAbsentSilence)
{
    auto storage = makeFailingStorage();
    bcos::executor_v1::eth::StorageStateView<decltype(storage)> view(storage);  // 默认 nullptr
    auto acc = view.get_account(evmc_address{});
    BOOST_CHECK(acc == std::nullopt);
    // 无 error 指针时绝不抛、绝不留下任何可观测错误（现状契约）
}
```

- [ ] **Step 2: 运行确认失败**

Run: `rtk cargo test` 不适用（C++）；用 `ctest -R StorageBridgeTest --output-on-failure`（先建 `ethereum-executor/tests/CMakeLists.txt` 目标 `StorageBridgeTest`，链接 `ethereum-executor bcos-framework`）。
Expected: FAIL——`StorageStateView` 构造函数不接受第二参数，编译失败。

- [ ] **Step 3: 实现 poison 通道**

在 `StorageStateView` 加成员 `std::optional<std::string>* m_error = nullptr;`，构造第二参数 `error` 赋值；三个虚方法（`get_account/get_account_code/get_storage`）的 catch 分支改为：

```cpp
catch (...) {
    if (m_error != nullptr && !*m_error)
        *m_error = std::string("StorageStateView read failed");
    return std::nullopt;  // 或 {} / evmc::bytes32{}
}
```

保持 `noexcept`；`poison` 只记首个错误、不重抛（对齐 `Storage2State.h:15-18` 契约）。注意 `hasStorageImpl` 内部 `storage2::range` 的异常也须走同一通道。

- [ ] **Step 4: 运行确认通过**

Run: `ctest -R StorageBridgeTest --output-on-failure`
Expected: PASS。

- [ ] **Step 5: 提交**

```bash
git add ethereum-executor/StorageStateView.h ethereum-executor/tests/
git commit -m "feat(executor): SharedReadBridge optional poison channel"
```

### Task 2: has_storage 分歧并入共享读桥

**Files:**
- Modify: `ethereum-executor/StorageStateView.h`
- Test: `ethereum-executor/tests/StorageBridgeTest.cpp`

**Interfaces:**
- Produces: `hasStorageImpl` 从"任何非字段 key → true"改为"只认 live 且非零的 32 字节 slot"（对齐 `Storage2State::probeHasStorage`，`Storage2State.h:674-697`）；零值/墓碑 slot 不再计为 has_storage。

**设计说明**：此改动同时影响 eth/eth_call 路径（共享桥）。EIP-7610 CREATE 碰撞判定依赖 `has_storage`——含零值槽行的账本下，旧语义会把"余额账户 + 零值槽"误判为有 storage，导致 CREATE 碰撞。Storage2State 语义（过滤零值/墓碑）更贴近真实账本。闸门：EEST/eth 现有测试 + e2e golden 必须保持全绿。

- [ ] **Step 1: 写失败测试（定向用例，golden 抓不到的场景）**

```cpp
BOOST_AUTO_TEST_CASE(ZeroValueSlotDoesNotCountAsStorage)
{
    // 账户 A 余额非 0、无 code，但账户表里有一个值为 32 字节全零的 storage slot 行
    auto storage = makeStorageWithAccount(A, /*balance=*/1, /*code*/ {}, /*zeroValueSlotKey*/ "k");
    bcos::executor_v1::eth::StorageStateView<decltype(storage)> view(storage);
    auto acc = view.get_account(A);
    BOOST_REQUIRE(acc.has_value());
    BOOST_CHECK(!acc->has_storage);   // Storage2State 语义：零值槽不算
}
```

- [ ] **Step 2: 运行确认失败**

Expected: FAIL——现状 `hasStorageImpl` 把非字段 key 一律计为 `has_storage=true`。

- [ ] **Step 3: 实现**

将 `hasStorageImpl` 的扫描改为：命中非字段 key 后，读取该槽值，仅当值长度 == 32 且**不全零**时返回 true（对齐 `probeHasStorage` 的 tombstone+零值双重过滤）；读失败走 poison 通道（Task 1）。

```cpp
// 在 while 循环命中 storage 槽后：
auto v = kv.value.get();  // storage2 Entry value
if (v.size() == sizeof(evmc_bytes32) &&
    !std::all_of(v.begin(), v.end(), [](char c) { return c == 0; })) {
    hasStorage = true;
    break;
}
```

- [ ] **Step 4: 运行确认通过**（`ctest -R StorageBridgeTest`）+ 回归 eth 路径 EEST 相关测试（`ctest -R EEST\|EthereumExecutor`）

- [ ] **Step 5: 提交**

```bash
git commit -am "feat(executor): has_storage ignores zero-value/tombstone slots (Storage2State semantics)"
```

### Task 3: /sys/ 路由分歧验证 + 定向用例

**Files:**
- Modify: `ethereum-executor/StorageStateView.h`（如需显式 /apps/ 路由）
- Test: `ethereum-executor/tests/StorageBridgeTest.cpp`

**Interfaces:**
- Produces: 断言 OP predeploy 地址**不落入** `c_systemTxsAddress`（`EVMAccount.h:280-291` 的 /sys/ 路由表）；若命中则对该地址改走显式 `/apps/`。

**设计说明**：`EVMAccount(storage, addr, false)` 把 8 个 `c_systemTxsAddress` 路由到 `/sys/`；`Storage2State` 恒走 `/apps/`。OP predeploy 不在 `c_systemTxsAddress`（PrecompiledTypeDef.h:143）——分歧在 OP 语料上潜伏。本任务把它钉死。

- [ ] **Step 1: 写测试**

```cpp
BOOST_AUTO_TEST_CASE(OpPredeploysNeverRouteToSys)
{
    // 对每个 OP predeploy 地址（L1Block 0x4200000000000000000000000000000000000015 等）：
    // EVMAccount 的路径判定不得是 /sys/
    for (auto addr : opPredeployAddrs()) {
        BOOST_CHECK(!isSystemTxAddress(addr));   // 不在 c_systemTxsAddress
    }
}
```

- [ ] **Step 2: 运行**——预期通过（现状即成立），测试目的是**锁定不变量**，防止后续合并时 OP predeploy 与 FISCO 系统地址发生碰撞。

- [ ] **Step 3: 提交**（若 Step 2 意外失败，说明某 predeploy 落入 /sys/——此时须对该地址在共享桥内显式走 `/apps/` 并重跑）

```bash
git commit -am "test(executor): lock OP predeploy /sys/ routing invariant"
```

### Task 4: applyStateDiffWithPoison + 写侧契约迁移

**Files:**
- Modify: `ethereum-executor/BCOS2Evmone.h`
- Test: `ethereum-executor/tests/StorageBridgeTest.cpp`

**Interfaces:**
- Produces:
  - `task::Task<void> applyStateDiffWithPoison(Storage& storage, evmone::state::StateDiff const& diff, evmc_revision rev, crypto::Hash::Ptr const& hashImpl, std::optional<std::string>* error)`——`error == nullptr` 时行为 == `applyStateDiff`；`error != nullptr` 时写失败**先设 flag 再 rethrow**（保持 `Storage2State::applyDiff` 时序）。
  - 写侧契约迁移：零值槽 **removeOne + 重读守卫**（对齐 `Storage2State.h:347-377`）、EIP-161 create 守卫（`:318-325`）、账户删除 range-删 + SYS_TABLES 标记 + ghost-delete tripwire（`:392-438`）、写透缓存一致性（`:383-389`）。

**设计说明**：这是全计划最重的语义迁移。**不要**在第一步就把全部契约搬完——分两个子步：先零值删行（影响 has_storage 一致性），再 EIP-161/账户删除/写透。每子步独立测。

- [ ] **Step 1: 写零值槽删行测试**

```cpp
BOOST_AUTO_TEST_CASE(ZeroValueSlotRemovedNotWritten)
{
    auto storage = makeEmptyStorage();
    std::optional<std::string> err;
    // diff 把账户 A 的槽 k 写为零值
    evmone::state::StateDiff d = makeDiffWithZeroSlot(A, "k");
    bcos::task::syncWait(
        bcos::executor_v1::eth::applyStateDiffWithPoison(storage, d, EVMC_PRAGUE, hashImpl, &err));
    BOOST_CHECK(!err.has_value());
    // 槽 k 的底层行必须被删除（不存在），而非存在零值行
    BOOST_CHECK(!rowExists(storage, accountTable(A), "k"));
}
```

- [ ] **Step 2: 运行确认失败**（现状 `applyStateDiff` 写零值行不删）

- [ ] **Step 3: 实现 `applyStateDiffWithPoison`（零值子步）**

在 `applyStateDiff` 基础上包一层：diff 中值为零的 storage 槽改为 `removeOne`（而非 `setStorage`），并**重读守卫**（删除后重读，若仍存在则 poison+throw）；`error != nullptr` 时所有写失败路径先 `*error` 赋值再 `throw`。EIP-161/账户删除/写透缓存子步在后续迭代完成（每项一个失败测试 → 实现 → 通过）。

- [ ] **Step 4: 运行确认通过** + 回归 `Storage2StateHelpersTest` 相关断言（迁移到新写回）

- [ ] **Step 5: 提交**

```bash
git commit -am "feat(executor): applyStateDiffWithPoison + zero-value slot removal contract"
```

### Task 5: executor 块路径方法注入桥/poison

**Files:**
- Modify: `opstack-executor/OpstackExecutor.h`
- Test: `opstack-executor/tests/OpstackExecutorTest.cpp`

**Interfaces:**
- Produces: `executeTransaction/executeDeposit/finalizeBlock/executeBlockStart` 增加注入参数（或重载）`std::optional<std::string>* error = nullptr`，使内部构造的 `StorageStateView` 带上块路径 poison 指针；`error == nullptr`（eth_call）行为不变。

**设计说明**：§8 的"读桥 poison → OpStorageError"对逐笔读必须有效——否则块内一笔读失败静默 absent，块尾 poison 判定为空，错误漏分类。

- [ ] **Step 1: 写测试**

```cpp
BOOST_AUTO_TEST_CASE(BlockPathReadFailurePoisons)
{
    auto storage = makeFailingStorage();
    std::optional<std::string> err;
    bcos::executor_v1::opstack::OpstackExecutor ex(receiptFactory, hashImpl);
    // 一笔正常 tx，执行中读 storage 失败
    BOOST_CHECK_THROW(
        bcos::task::syncWait(ex.executeTransaction(storage, header, tx, 0, ledgerConfig,
            /*call=*/false, fee, blockGasLeft, chainId, &hashes, &err)),
        std::exception);
    BOOST_CHECK(err.has_value());  // poison 已记录，块尾可判 OpStorageError
}
```

- [ ] **Step 2: 运行确认失败**（现状读失败静默 absent，err 为空）

- [ ] **Step 3: 实现**：`m_prepare/m_finish` 与 `executeDeposit/finalizeBlock` 内部构造 `StorageStateView(storage, error)`（透传注入的 error 指针）。

- [ ] **Step 4: 运行确认通过** + 回归 `OpCallScheduler` 测试（eth_call 不传 error，行为不变）

- [ ] **Step 5: 提交**

```bash
git commit -am "feat(opstack): thread poison channel through block-path executor methods"
```

### Task 6: 块路径切到 SharedReadBridge + Parity Switch 骨架

**Files:**
- Modify: `opstack-executor/OpSchedulerImpl.h`（`executeOpBlock` step1-6）、`opstack-executor/OpBlockExecute.h`（`processOpBlock` 模板化签名可切桥）
- Test: `opstack-executor/tests/OpTwoPhaseTest.cpp` / `OpNewPayloadRpcE2eTest.cpp`（e2e golden）

**Interfaces:**
- Produces: `constexpr bool c_useSharedBridge = false` 起步；`executeOpBlock` 的桥从 `Storage2State` 切到 `SharedReadBridge`（poison 通道开）。**两条分支同一桥类型**——本任务把旧路径也搬到 SharedReadBridge 上跑（不是保留 Storage2State），保证 step4-6（poisoned/visitAccounts/stateRootOf）类型不擦除、共用。
- 注意：`Storage2State` 的 `fetchAllStorage/visitAccounts/stateRootOf` 依赖（seal/stateRoot）必须由 `SharedReadBridge` 承载（Task 1 已列额外方法迁移项——本任务完成剩余迁移）。

- [ ] **Step 1: 改 `executeOpBlock` step2**：`SharedReadBridge bridge(storage, &poisonErr)` 替换 `Storage2State`；`processOpBlock` 的 `view` 参数与 `applyDiff` 回调改为桥形态。

- [ ] **Step 2: 跑 e2e golden**（`ctest -R OpNewPayloadRpcE2eTest`）——**预期可能红**（读/写语义差异在此暴露）。逐条排查，差异项并入共享桥（回到 Task 2/4 的语义），直到全绿。这是阶段一的核心闸门。

- [ ] **Step 3: 跑全量阶段一相关测试**：`ctest -R "OpTwoPhase|OpNewPayloadRpcE2eTest|StorageBridge|OpstackExecutor|OpSchedulerImpl"` 全绿。

- [ ] **Step 4: 提交**

```bash
git commit -am "refactor(opstack): block path reads/writes through SharedReadBridge (parity via e2e golden)"
```

**阶段一闸门（§10）**：e2e golden 全绿 + 写侧契约迁移测试全绿。Storage2State 仍保留（供阶段二 parity 参照），但块路径已不再使用它执行。

## 阶段二：块编排器 + 逐笔 concept 化

### Task 7: executeTransaction 增加 env（完整签名信封）参数

**Files:**
- Modify: `opstack-executor/OpstackExecutor.h`
- Test: `opstack-executor/tests/OpstackExecutorTest.cpp`、`opstack-executor/tests/OpRlpDecodeTest.cpp`

**Interfaces:**
- Produces: `executeTransaction(storage, blockHeader, transaction, contextID, ledgerConfig, call, fee, blockGasLeft, chainId, blockHashes, evmc::bytes_view env = {})`——`env` 为完整签名信封；`env` 为空时（eth_call/concept 路径）回退 `transaction.extraTransactionBytes()`（现状行为不变）；`m_prepare` 的 `signedTxEnvelope` 优先用 `env`。

- [ ] **Step 1: 写失败测试（R5 关键）**

```cpp
BOOST_AUTO_TEST_CASE(FullEnvelopeDrivesL1CostNotPreImage)
{
    // 同一笔 tx，L1 fee 参数非 0：
    //   a) env = 完整签名信封（raw EIP-2718 字节）
    //   b) 不传 env（回退 extraTransactionBytes 前像）
    // 两条路径产出的回执 L1 fee 必须不同（完整信封的 calldata 长度 > 前像）——
    // 该差异证明 env 参数确实被消费。
    auto envelope = makeFullEnvelope();            // 完整签名信封
    auto tx = opEnvelopeToTars(envelope);          // tars Transaction
    OpFeeParams fee{/*l1_base_fee*/ 1, /*l1_blob_base_fee*/ 1, /*l1_base_fee_scalar*/ 1000, /*...*/};
    auto r1 = syncWait(ex.executeTransaction(storage, header, tx, 0, cfg, false,
        fee, blockGasLeft, chainId, &hashes, /*env=*/envelope));
    auto r2 = syncWait(ex.executeTransaction(storage, header, tx, 0, cfg, false,
        fee, blockGasLeft, chainId, &hashes));
    auto l1fee1 = receiptL1Fee(r1);
    auto l1fee2 = receiptL1Fee(r2);
    BOOST_CHECK(l1fee1 != l1fee2);   // 完整信封 vs 前像 → 不同 L1 cost
}
```

- [ ] **Step 2: 运行确认失败**（现状两路相同——env 参数不存在）

- [ ] **Step 3: 实现**：加 `evmc::bytes_view env = {}` 参数；`m_prepare` 里 `signedTxEnvelope` 取 `env`，空则回退 `transaction.extraTransactionBytes()`。

- [ ] **Step 4: 运行确认通过** + 回归 eth_call（不传 env，行为不变）

- [ ] **Step 5: 提交**

```bash
git commit -am "feat(opstack): executeTransaction full signed-envelope param (R5)"
```

### Task 8: executeBlockStart 新方法

**Files:**
- Modify: `opstack-executor/OpstackExecutor.h`、`opstack-executor/OpBlockExecute.h/.cpp`
- Test: `opstack-executor/tests/OpstackExecutorTest.cpp`

**Interfaces:**
- Produces: `task::Task<void> executeBlockStart(Storage& storage, protocol::BlockHeader const& header, uint64_t chainId, int64_t blockGasLeft, evmone::state::BlockHashes const* hashes = nullptr, std::optional<std::string>* error = nullptr)`——封装 `system_call_block_start`（`OpBlockExecute.cpp:101-102` 现逻辑：`sanitizeStateDiff(system_call_block_start(view, block, hashes, cfg.rev, vm))` → 写回），供编排器在块首调用。块路径豁免 evmcRevision 检查（§6.1），fork 来自构造期 `m_forkConfig`。

- [ ] **Step 1: 写测试**：驱动 `executeBlockStart` 后，断言 EIP-2935 块哈希槽（N-1）被写入（现 processOpBlock 行为快照）。

- [ ] **Step 2: 运行确认失败**（方法不存在）

- [ ] **Step 3: 实现**：从 `processOpBlock` 提取块首系统调用段，封装为方法（复用 `m_vm/m_forkConfig/m_hashImpl`，写回经注入桥/`applyStateDiffWithPoison`）。

- [ ] **Step 4: 运行确认通过**

- [ ] **Step 5: 提交**

```bash
git commit -am "feat(opstack): executeBlockStart (block-start system call on executor surface)"
```

### Task 9: OpBlockOrchestrator（逐笔 concept 化）

**Files:**
- Modify: `opstack-executor/OpBlockExecute.h/.cpp`（新 `OpBlockOrchestrator` 函数/结构，旧 `processOpBlock` 保留）
- Test: `opstack-executor/tests/OpOrchestratorParityTest.cpp`（新建，双路径 runner）

**Interfaces:**
- Produces:
  - `OpBlockResult OpBlockOrchestrator::run(Storage& storage, protocol::BlockHeader const& env, std::span<const OpBlockTx> txs, OpstackExecutor& executor, uint64_t chainId, evmone::state::BlockHashes const& hashes, std::optional<std::string>* error)`——块级循环：`executeBlockStart` → 首笔 deposit 强制/顺序校验 → `loadOpFeeParams` → 逐笔（normal: `opEnvelopeToTars` → `executeTransaction(..., env)`；deposit: `executeDeposit`）→ `finalizeBlock` → 产出 `OpBlockResult{receipts, txTypes, gasUsed, finalizeDiff}`。任何 throw → 整块失败（discard-writes 由 view 生命周期保证）。executor 的 `OpTxValidationFailed` **捕获 → 重抛 `std::runtime_error`**（§8 翻译）。
  - 双路径 runner 测试：`opstack-executor/tests/OpOrchestratorParityTest.cpp`，在 MLS fixture 上同 pre-state 分别驱动旧 `processOpBlock` 与新编排器，比对 stateRoot + receipts + gasUsed；**并各自对 golden 绝对断言**。

- [ ] **Step 1: 写双路径 parity 测试（runner 基建）**

```cpp
BOOST_FIXTURE_TEST_SUITE(OpOrchestratorParity, ParityFixture)

// ParityFixture：seedPreState（MLS 存储）+ golden header + realConverter + 两条执行入口
//  (old) processOpBlock(view, block, hashes, txs, cfg, vm, chainId, factory, applyDiff)
//  (new) orchestrator.run(...)

BOOST_AUTO_TEST_CASE(SameStateRootAsLegacyProcessOpBlock)
{
    auto golden = loadGoldenVector("isthmus_legacy_transfer");
    seedPreState(golden.preState);
    // --- old path ---
    auto oldResult = runLegacyProcessOpBlock(golden);
    // --- new path ---
    auto newResult = runOrchestrator(golden);
    // 双轨断言：
    BOOST_CHECK_EQUAL(stateRootOf(oldResult), stateRootOf(newResult));       // 互比
    BOOST_CHECK_EQUAL(stateRootOf(newResult), golden.expectedStateRoot);      // 绝对断言 vs op-geth
    BOOST_CHECK_EQUAL(oldResult.gasUsed, newResult.gasUsed);
    BOOST_CHECK_EQUAL(receiptsEqual(oldResult.receipts, newResult.receipts), true);
    BOOST_CHECK_EQUAL(txTypesEqual(oldResult.txTypes, newResult.txTypes), true);
}
BOOST_AUTO_TEST_CASE_SUITE_END()
```

- [ ] **Step 2: 运行确认失败**（`OpBlockOrchestrator` 不存在，编译失败）

- [ ] **Step 3: 实现 `OpBlockOrchestrator::run`**：逐行从 `processOpBlock`（OpBlockExecute.cpp:93-207）搬运块级逻辑，逐笔替换为 executor 概念调用。块级校验（空块/首笔非 deposit/顺序/D-1 覆盖/isL1AttributesTx 内容/blockGasLeft 超限）原样保留；normal 分支改为物化 `protocol::Transaction` + `executeTransaction(..., env=完整信封)`；`OpTxValidationFailed` 捕获重抛 runtime_error。receipt/txTypes/累积 gas 组装与旧路径逐字一致。

- [ ] **Step 4: 运行 runner**——逐向量跑（125 t8n 向量中块执行相关的 + e2e 81 中可驱动 runner 的）。**预期红**——逐条 diff 新旧 stateRoot，差异定位到 §6.3.2（chain-id/low-S 拒绝）或 §6.3.3（执行等价字段），按 §6.3 处置（修转换层或落回共享核直通）。

- [ ] **Step 5: 提交**

```bash
git commit -am "feat(opstack): OpBlockOrchestrator concept-driven per-tx + parity runner"
```

### Task 10: 错误分类钉死 + eth_call 回归补测

**Files:**
- Modify: `opstack-executor/tests/OpSchedulerImplSmokeTest.cpp`、`opstack-executor/tests/OpCallSchedulerEthCallTest.cpp`（新建）
- Modify: `opstack-executor/OpSchedulerImpl.h`（编排器翻译接线）

**Interfaces:**
- Produces: 分类钉死用例（空块/首笔非 deposit/normal 无效 tx/gas 超限 → `OpConsensusError`）；eth_call happy path 用例（seeded ledger + L1Block 槽 + `StorageStateView` 读 + `loadOpFeeParams` + `executeTransaction`）。

- [ ] **Step 1: 写分类钉死测试**

```cpp
BOOST_AUTO_TEST_CASE(NormalInvalidTxClassifiesConsensusNotStorage)
{
    // 一笔 normal 无效 tx（如 nonce 超前）→ 编排器翻译后必须落 INVALID
    auto result = executeBlockThrowing(invalidTxBlock());
    BOOST_CHECK(result.errorKind == ConsensusErrorKind::INVALID);  // 非 -32603
}
// 同构用例：空块、首笔非 deposit、deposit 顺序错、gas 超限
```

- [ ] **Step 2: 运行确认失败**（现状 SmokeTest 容忍两种分类——新用例钉死）

- [ ] **Step 3: 实现**：编排器在 `catch (const OpTxValidationFailed&)` 处重抛 runtime_error；`executeOpBlock` 的分类路径验证 `catch(...)` → INVALID。补 eth_call happy path（OpCallSchedulerEthCallTest）。

- [ ] **Step 4: 运行确认通过**

- [ ] **Step 5: 提交**

```bash
git commit -am "test(opstack): pin error classification + eth_call happy-path regression"
```

### Task 11: executeOpBlock 切到编排器 + hashImpl 注入

**Files:**
- Modify: `opstack-executor/OpSchedulerImpl.h`
- Test: `opstack-executor/tests/OpTwoPhaseTest.cpp`、`opstack-executor/tests/OpNewPayloadRpcE2eTest.cpp`

**Interfaces:**
- Produces: `OpSchedulerImpl(..., crypto::Hash::Ptr hashImpl)` 新构造参数（构造 `OpstackExecutor` 用）；`constexpr bool c_useConceptOrchestrator = false`；`executeOpBlock` 编排器分支 + Parity Switch；`libinitializer/Initializer.cpp` OP 装配点补 `hashImpl` 实参（`m_protocolInitializer->cryptoSuite()->hashImpl()`）。

- [ ] **Step 1: 改构造签名 + 装配**（`OpSchedulerImpl.h` + `libinitializer/Initializer.cpp:485-492`），全量编译通过。

- [ ] **Step 2: 翻转 Parity Switch 为 `true`**，跑 `ctest -R "OpTwoPhase|OpNewPayloadRpcE2eTest|OpOrchestratorParity"`——**预期红**（e2e 首次走编排器，差异逐条按 §6.3/§5 处置），直到全绿。

- [ ] **Step 3: 全量阶段一+二回归**：`ctest -R "opstack|Opstack|OpNewPayload|OpTwoPhase|OpScheduler|StorageBridge|EngineService"` 全绿。

- [ ] **Step 4: 提交**

```bash
git commit -am "feat(opstack): executeOpBlock through orchestrator (parity switch on)"
```

**阶段二闸门（§10）**：阶段一闸门 + 双路径一致性测试全绿 + 分类钉死用例全绿。

## 阶段三：清理与全量回归

### Task 12: 删除旧路径与 Storage2State

**Files:**
- Delete: `opstack-executor/Storage2State.h`、`opstack-executor/Storage2StateHelpers.h`（若仅被它引用）
- Modify: `opstack-executor/OpBlockExecute.h/.cpp`（删旧 `processOpBlock` 保留 `OpBlockOrchestrator`）、`opstack-executor/tests/support/SeedPreState.h`（`applyDiff(seeding=true)` → 共享写回）、`opstack-executor/tests/CMakeLists.txt`
- Test: 全量

**Interfaces:**
- Produces: `c_useConceptOrchestrator` 常量删除；`SeedPreState` 改用 `applyStateDiffWithPoison(..., seeding 语义)`；`Storage2State` 引用清零。

- [ ] **Step 1: 改写 SeedPreState**：`applyDiff(seeding=true)` 迁移到共享写回（保留 seeding 语义：播种时不删零值行、不触发 ghost-delete tripwire）——先改测试基建，`ctest -R "OpNewPayloadRpcE2eTest|OpTwoPhase|SeedPreState"` 保持全绿。

- [ ] **Step 2: 删除 `Storage2State.h` + `Storage2StateHelpers.h` + 旧 `processOpBlock`**；`grep -rn "Storage2State\|processOpBlock"` 清零（含注释引用清理）。

- [ ] **Step 3: 全量构建 + 全量测试回归**：`ctest` 全绿；golden 再确认（t8n 125 + e2e 81 + golden/engine 79）。

- [ ] **Step 4: 提交**

```bash
git commit -am "refactor(opstack): remove Storage2State and legacy processOpBlock path"
```

## Self-Review 结果（写时内嵌）

- **Spec 覆盖**：§5.1→T1/T2/T3/T6；§5.2→T4；§5.3→T12；§6.1→T8；§6.3.1→T7；§6.3.2→T9/T10（拒绝语义）；§6.3.3→T9（执行等价）；§7→T11；§8→T10；§9→T6/T9/T10/T12；§10→三阶段闸门；§11 R1-R8→T7(签名信封)/T2(has_storage)/T10(分类)/T4(写侧)/T6(桥)/T9(转换)。
- **占位扫描**：无 TBD/TODO；每个代码步骤给出具体代码或精确契约。
- **类型一致性**：`StorageStateView(storage, error)`、`applyStateDiffWithPoison(storage, diff, rev, hashImpl, error)`、`executeTransaction(..., env={})`、`executeBlockStart(...)`、`OpBlockOrchestrator::run(...)`、`OpSchedulerImpl(..., hashImpl)` 在后续任务中签名一致。
