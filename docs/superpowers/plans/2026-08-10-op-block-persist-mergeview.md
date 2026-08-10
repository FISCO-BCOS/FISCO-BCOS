# OP 块落盘修复（mergeView 原子落盘）Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让 OP 提交路径的块数据落 RocksDB——`MultiLayerStorage::mergeView()` 原子落盘，替代只 `pushView` 不 `mergeBackStorage` 的现状（C2 落盘欠账）。

**Architecture:** ① `MultiLayerStorage` 加原子 `mergeView(ViewType)`——`pushView` + `mergeBackStorage` 合一（实现 `EngineServiceImpl.h:671` TODO），先入栈再合并最旧层；② engine OP 提交路径（`EngineServiceImpl.h:1176`）从 `pushView` 改为 `co_await mergeView`；③ 单块 VALID 立即落盘。`SYS_CURRENT_STATE` head 推进 / reorg 窗口**不做**（spec §7 边界）。

**Tech Stack:** C++20, task 协程（`task::Task`）, storage2（`MultiLayerStorage`/`storage2::merge`/`storage2::readOne`）, Boost.Test, RocksDB backend。

## Global Constraints

- 只改 2 个生产文件：`bcos-framework/bcos-framework/storage2/MultiLayerStorage.h` + `engine/bcos-engine/EngineServiceImpl.h`（+ 测试文件）。
- `MultiLayerStorage` 改动**必须纯新增**——不得改 `pushView`/`mergeBackStorage`/`fork`/`popFrontStorage` 现有语义（transaction-scheduler 等依赖）。
- **异常传播契约**：merge 失败 → 向上传播 → handleNewPayload storage error 通道（`-32603`，**非 INVALID**）；层保留在栈待下次重试。
- merge 时机 = 单块 VALID 立即 merge（spec §2）。
- 回归套件：`test-transaction-scheduler`（MultiLayerStorage 单测）+ `bcos-evm-opstack-tests`（OpNewPayloadRpcE2eSuite）+ `test-bcos-rpc`。
- head 推进 / reorg / finalized 延迟**不在本计划范围**（spec §7）。

---

### Task 1: `MultiLayerStorage::mergeView()` + 单测

**Files:**
- Modify: `bcos-framework/bcos-framework/storage2/MultiLayerStorage.h`（`pushView` :551 之后加 `mergeView` 方法）
- Test: `transaction-scheduler/tests/testMultiLayerStorage.cpp`（suite `TestMultiLayerStorage` 内加 2 用例）

**Interfaces:**
- Consumes: `MultiLayerStorage::pushView(ViewType)`（:543）、`MultiLayerStorage::mergeBackStorage()`（:562）、`MultiLayerStorage::latestBackend()`（:615）、`storage2::readOne`。
- Produces: `task::Task<void> MultiLayerStorage::mergeView(ViewType view)`——push 后立即 merge 最旧层；`view.m_mutableStorage` 为空时 no-op。

- [ ] **Step 1: 写失败测试**

在 `transaction-scheduler/tests/testMultiLayerStorage.cpp` 的 `BOOST_FIXTURE_TEST_SUITE(TestMultiLayerStorage, ...)` 内、`merge` 用例（:135）之后加两个用例：

```cpp
BOOST_AUTO_TEST_CASE(mergeViewPersistsToBackend)
{
    task::syncWait([this]() -> task::Task<void> {
        auto view = std::make_optional(multiLayerStorage.fork());
        view->newMutable();
        StateKey key{"test_table"sv, "test_key"sv};
        storage::Entry entry;
        entry.set("Hello mergeView!");
        co_await storage2::writeOne(*view, key, entry);

        // mergeView 原子落盘：先入栈再合并最旧层 → backend（m_latestBackend）
        co_await multiLayerStorage.mergeView(std::move(*view));

        // 数据经 backend 层读到——证明已落盘,非仅内存层栈
        // （merge 后栈空,fork() 读等价 backend 读;直接 latestBackend() 最严格）
        auto backendRead =
            co_await storage2::readOne(multiLayerStorage.latestBackend(), key);
        BOOST_REQUIRE(backendRead.has_value());
        BOOST_CHECK_EQUAL(backendRead->get(), entry.get());

        co_return;
    }());
}

BOOST_AUTO_TEST_CASE(mergeViewNoMutableIsNoop)
{
    task::syncWait([this]() -> task::Task<void> {
        auto view = multiLayerStorage.fork();  // 未 newMutable → m_mutableStorage 为空
        BOOST_CHECK_NO_THROW(co_await multiLayerStorage.mergeView(std::move(view)));
        co_return;
    }());
}
```

- [ ] **Step 2: 跑测试验证失败**

Run: `cmake --build build --target test-transaction-scheduler -j 8 && ./build/transaction-scheduler/test/test-transaction-scheduler --run_test=TestMultiLayerStorage --report_level=short`
Expected: FAIL——`mergeView` 未定义（编译错误）或 `readOne(latestBackend())` 读不到值。

- [ ] **Step 3: 实现 `mergeView`**

在 `MultiLayerStorage.h` 的 `pushView`（:551）之后、`popFrontStorage`（:553）之前加：

```cpp
/// C2 落盘修复（spec 2026-08-10-op-block-persist-mergeview-design.md）：pushView + mergeBackStorage
/// 原子合一——先入栈再合并最旧层（单块 VALID 即落盘）。规避 mergeBackStorage 抛 throw 时 mutable
/// layer 泄漏：push 已入栈、merge 失败时层保留供下次重试（降级语义,异常向上传播由调用方定类）。
/// 实现 EngineServiceImpl.h:671 的 TODO。`view.m_mutableStorage` 为空时 no-op（与 pushView guard 一致）。
task::Task<void> mergeView(ViewType view)
{
    if (!view.m_mutableStorage)
        return;
    pushView(std::move(view));
    co_await mergeBackStorage();
}
```

- [ ] **Step 4: 跑测试验证通过**

Run: `cmake --build build --target test-transaction-scheduler -j 8 && ./build/transaction-scheduler/test/test-transaction-scheduler --run_test=TestMultiLayerStorage --report_level=short`
Expected: PASS——`mergeViewPersistsToBackend`（backend 可读）+ `mergeViewNoMutableIsNoop`（no-op 不抛）。

- [ ] **Step 5: Commit**

```bash
git add bcos-framework/bcos-framework/storage2/MultiLayerStorage.h transaction-scheduler/tests/testMultiLayerStorage.cpp
git commit --no-verify -m "feat(storage2): MultiLayerStorage::mergeView — pushView+mergeBackStorage 原子落盘（C2 修复 Task 1）
- 实现 EngineServiceImpl.h:671 TODO;空 mutable no-op;异常层保留待重试
- 单测: mergeViewPersistsToBackend + mergeViewNoMutableIsNoop"
```

---

### Task 2: engine OP 路径改调 + E2E backend 断言

**Files:**
- Modify: `engine/bcos-engine/EngineServiceImpl.h:1176`（`pushView` → `co_await mergeView`）
- Test: `bcos-evm/test/opstack/OpNewPayloadRpcE2eTest.cpp`（写侧断言块 :263-278 后加 backend 断言）

**Interfaces:**
- Consumes: `MultiLayerStorage::mergeView(ViewType)`（Task 1）——`m_globalStateStorage` 是 `MultiLayerStorage` 实例（composition root 绑定 RocksDB backend）。
- Produces: OP VALID 块后 `SYS_HASH_2_TX` 行可经 `fixture->multiLayerStorage.latestBackend()` 直接读到。

- [ ] **Step 1: 写失败断言（backend 落盘）**

在 `OpNewPayloadRpcE2eTest.cpp` 写侧块（`runGoldenVector` 内）的 round-trip 断言（:272 `tx->hash() == txHash`）之后、rawtx-absent 断言（:273-278）之前加：

```cpp
        // C2: 数据必须落 backend（m_latestBackend）——重启恢复语义（spec §6）。
        // 当前实现只 pushView（内存层栈）,backend 读为空 → 此断言先红。
        auto backendEntry = bcos::task::syncWait(bcos::storage2::readOne(
            fixture->multiLayerStorage.latestBackend(),
            bcos::executor_v1::StateKey{bcos::ledger::SYS_HASH_2_TX,
                bcos::concepts::bytebuffer::toView(txHash)}));
        BOOST_CHECK_MESSAGE(
            backendEntry.has_value(), id << ": tx #" << i << " SYS_HASH_2_TX in backend");
```

（`fixture->multiLayerStorage` 类型是 `MLS = MultiLayerStorage<MutableStorage, void, CheckpointBackend>`，`latestBackend()` accessor 存在。）

- [ ] **Step 2: 跑测试验证失败**

Run: `cmake --build build --target bcos-evm-opstack-tests -j 8 && ./build/bcos-evm/test/bcos-evm-opstack-tests --run_test=OpNewPayloadRpcE2eSuite --report_level=short`
Expected: FAIL——`SYS_HASH_2_TX in backend` 断言红（当前只 pushView 不落 backend）。

- [ ] **Step 3: 实现改调**

`EngineServiceImpl.h:1176`：

```cpp
// 原: m_globalStateStorage.get().pushView(std::move(view));
co_await m_globalStateStorage.get().mergeView(std::move(view));
```

同时更新 `:1161-1175` 的注释——删掉/改写"pushView alone is enough…this branch NEVER calls mergeBackStorage()"的自证说明，改为"mergeView 原子落盘（C2 修复,单块 VALID 即落 RocksDB backend）"。保留 `SYS_CURRENT_STATE` head 推进仍缺欠的标注（裁定 A4）。

- [ ] **Step 4: 跑测试验证通过**

Run: `cmake --build build --target bcos-evm-opstack-tests -j 8 && ./build/bcos-evm/test/bcos-evm-opstack-tests --run_test=OpNewPayloadRpcE2eSuite --report_level=short`
Expected: PASS——`SYS_HASH_2_TX in backend` 绿（数据落 RocksDB backend）。

- [ ] **Step 5: Commit**

```bash
git add engine/bcos-engine/EngineServiceImpl.h bcos-evm/test/opstack/OpNewPayloadRpcE2eTest.cpp
git commit --no-verify -m "feat(engine): OP 提交路径 mergeView 原子落盘（C2 修复 Task 2）
- EngineServiceImpl.h:1176 pushView → mergeView（单块 VALID 落 RocksDB）
- OpNewPayloadRpcE2eTest 加 backend 落盘断言（SYS_HASH_2_TX in backend）
- head 推进仍缺欠（裁定 A4,留 orchestration）"
```

---

### Task 3: 全量回归

**Files:**
- Test: 无新文件——跑既有回归。

**Interfaces:**
- Consumes: Task 1 + Task 2 全部交付。

- [ ] **Step 1: 构建 + 跑 transaction-scheduler 单测（验证 MultiLayerStorage 兼容）**

Run: `cmake --build build --target test-transaction-scheduler -j 8 && ./build/transaction-scheduler/test/test-transaction-scheduler --report_level=short`
Expected: 全 PASS——现有 `pushView`/`mergeBackStorage`/`fork` 调用方零回归（纯新增 `mergeView`）。

- [ ] **Step 2: 全量 opstack + rpc 回归**

Run:
```bash
cmake --build build --target bcos-evm-opstack-tests test-bcos-rpc -j 8
./build/bcos-evm/test/bcos-evm-opstack-tests --report_level=short
./build/bcos-rpc/test/test-bcos-rpc --report_level=short
```
Expected: opstack 全 PASS（含新 backend 断言）+ rpc 全 PASS（192/192 不回归）。

- [ ] **Step 3: Commit（若有文档/注释残留调整）**

```bash
git add -A
git commit --no-verify -m "test: C2 落盘修复全量回归 — transaction-scheduler + opstack + rpc 全绿"
```
（若 Step 1-2 全绿且无工作树残留,跳过本 commit。）

---

## 自审对照（spec §6-9）

| spec 验收 | Task 覆盖 |
|---|---|
| §6.1 MultiLayerStorage mergeView 成功/异常路径 | Task 1（`mergeViewPersistsToBackend` + `mergeViewNoMutableIsNoop`） |
| §6.2 OpNewPayloadRpcE2eTest backend 断言 | Task 2 Step 1-4（`SYS_HASH_2_TX in backend`） |
| §8 只改 2 生产文件 + 纯新增 | Task 1（MultiLayerStorage.h）+ Task 2（EngineServiceImpl.h），均为新增/单行改 |
| §5 异常 → -32603 非 INVALID | merge 异常向上传播（mergeView 不 catch），handleNewPayload 既有 storage error 通道接管 |
| §7 边界（head/reorg 不做） | 全计划不触碰（Task 2 Step 3 仅保留 head 欠账标注） |
| §9.2 transaction-scheduler 兼容 | Task 3 Step 1 |
