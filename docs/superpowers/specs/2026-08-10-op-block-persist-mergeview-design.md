# OP 块落盘修复 — mergeView 原子落盘（方案 A）Design Spec

**日期**：2026-08-10
**状态**：设计定稿（brainstorming 用户批准）
**分支**：`feat-op-executor-e2e`
**问题追踪**：C2（`s_number_2_header` 落盘欠账，comparison doc §8.5:417）

## 1. 问题（已源码验证）

OP 提交路径 `EngineServiceImpl.h:1159-1176`（OP `handleNewPayload` VALID 分支）只做 `pushView`，**从不 `mergeBackStorage`**：

```cpp
co_await registerOpBlock(view, payload, *ethHeader, *executeResult);  // 写表到 view
m_globalStateStorage.get().pushView(std::move(view));                  // 只推内存层栈
co_return makeStatus(PayloadValidationStatus::Valid, ...);
```

### 证据链（全部源码级，2026-08-10 核实）

1. **OP 路径零 mergeBackStorage**：`grep mergeBackStorage` 全仓，生产落盘点仅 `BaselineScheduler.h:769`（正常出块）+ `EngineServiceImpl.h:677-678`（通用本地构建路径，`pushView`+`mergeBackStorage` 已配对）——**OP 提交路径（:1176）是唯一 pushView 不平衡点**。`EngineServiceImpl.h:671` 已带 TODO「merge pushView + mergeBackStorage into a single atomic mergeView()」，本 spec 即实现它。
2. **pushView ≠ 落盘**（`MultiLayerStorage.h:543-551`）：`m_storages.push_front(mutableStorage)` —— 只进**内存层栈**。
3. **mergeBackStorage = 唯一落盘**（`MultiLayerStorage.h:562-599`）：`storage2::merge(m_latestBackend, backStorage)` —— 合并进 `m_latestBackend`（=`backendStorage.open()`，RocksDB 后端句柄，:504）。
4. **正常路径参照**（`BaselineScheduler.h:769`）：commit 后 `mergeBackStorage(prewriteStorage)`，"one WriteBatch, one Write (RocksDBStorage2::merge)"（:762-763）。

### 后果

- **重启即丢**：`registerOpBlock` 写的所有表（`s_number_2_header` / `SYS_NUMBER_2_HASH` / `SYS_HASH_2_NUMBER` / 回执表 / `SYS_HASH_2_TX`）只存内存层栈，进程重启后全部丢失。**本 spec 修复块表落盘**；`SYS_CURRENT_STATE` head 推进仍缺失（裁定 A4）——重启后 backend 有块但 head 指针无，见 §7 边界与 §9.4 验收。
- **无界层增长 + 读放大**：每接受一个 VALID 块留一个 immutable 内存层，读取耗时随"进程启动以来接受的块数"线性放大（代码注释 task-5b review I3 自证）。mergeView 后每块 pop 一层，此问题同步解决。

## 2. 设计决策（用户 2026-08-10 拍板）

| 决策 | 结论 |
|---|---|
| **实施范围** | 最小落盘——只做 mergeView 原子落盘。`SYS_CURRENT_STATE` head 推进 + reorg 窗口留待后续，spec 明确标注边界。 |
| **merge 时机** | 单块 VALID 立即 merge。当前 FCU read-only + in-memory（裁定 A4）无真实 reorg，安全。 |
| **方案** | A：`MultiLayerStorage::mergeView()` 原子方法（实现 `EngineServiceImpl.h:671` TODO），engine OP 路径改调。 |

> **与 C2 记录的时间偏差（审查修正）**：comparison doc §8.5:417 原记录为「随 orchestration 层接真实节点时整层 merge（同批）」。本 spec 先落地 mergeView（获落盘路径的测试覆盖），head/reorg 仍在 §7 台账——**偏离 C2 原时序**。实施后需更新 C2 记录反映该拆分。

## 3. 架构与组件

### 3.1 `MultiLayerStorage::mergeView()`（纯新增，`bcos-framework/bcos-framework/storage2/MultiLayerStorage.h`）

```cpp
/// :671 TODO 实现——pushView + mergeBackStorage 合一。
/// 先入栈再合并最旧层（m_storages.back()，FIFO）。规避 mergeBackStorage 抛 throw 时
/// mutable layer 泄漏：push 已入栈、merge 失败时层保留供下次重试（降级语义）。
/// ⚠️ 非真原子（pushView 锁 m_listMutex、mergeBackStorage 锁 m_mergeMutex+m_listMutex，
/// 两个临界区）——OP 路径在 x_state（EngineServiceImpl.h:822）下串行保证一致性；
/// TODO :671 的「avoid holding x_state across a co_await」动机本 spec 未达成（engine 仍持
/// x_state 跨 mergeView 内部 co_await），记录留待后续。
task::Task<void> mergeView(ViewType view)
{
    if (!view.m_mutableStorage)
        co_return;   // ⚠️ 协程内须 co_return（return; 编译失败）——防空栈 mergeBackStorage
    pushView(std::move(view));
    co_await mergeBackStorage();   // 合并最旧层 → RocksDB backend
}
```

- **兼容性**：纯新增方法，不改现有 `pushView`/`mergeBackStorage`/`fork`/`popFrontStorage` 语义——`transaction-scheduler` 等现有调用方零影响。
- **安全性**：`pushView` 后栈非空 → `mergeBackStorage` 的 `NotExistsImmutableStorageError`（:568）不会触发。
- **协程**：`mergeView` 是 `task::Task<void>`（mergeBackStorage 是协程），engine OP 路径 `co_await` 调用。

### 3.2 `EngineServiceImpl.h` OP 提交路径（:1176 一处改动）

```cpp
// 原: m_globalStateStorage.get().pushView(std::move(view));
co_await m_globalStateStorage.get().mergeView(std::move(view));
```

`m_globalStateStorage` 是 `GlobalStateStorage`（`MultiLayerStorage` 实例），构造时已绑定 RocksDB backend（composition root 传入），故 mergeView 直接落 RocksDB。

## 4. 数据流

```
VALID 块
  → registerOpBlock(view, ...)        // 写 s_number_2_header/…/SYS_HASH_2_TX 到 view
  → mergeView(view)                   // push（入内存栈）+ merge（合并最旧层）
  → RocksDB backend 落盘
  → 返回 VALID
```

**merge 目标是「最旧层」**（`mergeBackStorage` 取 `m_storages.back()`，FIFO，`MultiLayerStorage.h:570`），**非刚 push 层**（`pushView` 是 `push_front`）。

**前置条件（审查修正）**：「单块 VALID 立即落盘」仅在 **push 前栈空**时成立——稳态生产即此（:677-678 已配对、:1176 唯一不平衡）；栈非空时（测试 fixture 的 `seedPreState`/`registerVerifiedBlock` 积压，或 merge 失败后）merge 目标为**最旧层**、最近块落盘延迟一阶（失败降级见 §5）。因此：
- 生产单块稳态：push 块 → merge 块（栈空时块即最旧）→ pop，数据逐块落盘 ✓
- 栈积压时：merge 掉最旧层，最近块留内存，下个 merge 才落盘（one-block lag）

## 5. 错误处理

| 场景 | 行为 |
|---|---|
| merge 抛异常（存储故障） | 异常经 mergeView 向上传播 → handleNewPayload 的 storage error 通道（`-32603`，**非 INVALID**）——与"落盘是存储层职责"一致 |
| merge 失败降级 | 层保留在栈上，下次 mergeView 重试（故障恢复后数据补落盘）；栈短暂累积可接受 |
| `view.m_mutableStorage` 为空 | mergeView 直接返回（no-op，与 pushView 的 guard 一致） |

## 6. 测试

1. **MultiLayerStorage 单测**（`transaction-scheduler/tests/testMultiLayerStorage.cpp` 或同族）：
   - `mergeView` 成功路径：层内容落 backend（`latestBackend()` 可读到）。
   - 异常路径：`mergeBackStorage` 注入故障后层保留在栈（下次 mergeView 重试）。
2. **OpNewPayloadRpcE2eTest 扩展**（`bcos-evm/test/opstack/OpNewPayloadRpcE2eTest.cpp`）：
   - VALID 块后断言 `SYS_HASH_2_TX` 行可经 **backend 层**直接读到（重启恢复语义——不再是纯内存层栈）。
   - ⚠️ **0x04 (EIP-7702)/malformed 交易被 `registerOpBlock` 跳过（D7，不写 `SYS_HASH_2_TX`）**——断言需排除含 7702 的向量（`*_setcode_7702*`），或用 deposit/legacy 向量 + 断言 `s_number_2_header`（总是写）。
   - ⚠️ **单桶 backend 必需（Task 2 review 实证）**：fixture 的 `BackendMemStorage` 必须构造为 `{1}`（单桶）。默认多桶（`hardware_concurrency()*2+1`）下 `MemoryStorage::range(RANGE_SEEK)` 只 seek 桶 0，桶 1+ 泄漏非 SYS_TABLES 键 → `visitAccounts` 破坏 → stateRoot 空根（36/36 RED 全在 stateRoot）。单桶后 range 正确。**生产不受影响**（`RocksDBStorage2` 单有序存储，无桶）。

## 7. 边界（明确不做，本 spec 范围外）

| 不做 | 原因 / 去向 |
|---|---|
| `SYS_CURRENT_STATE` head 推进 | 裁定 A4；重启后 backend 有块但 head 指针缺失——留 orchestration 层（comparison doc §8.5 已记录） |
| reorg 窗口处理 / finalized 延迟 merge | 当前 FCU read-only + in-memory 无真实 reorg；接真实 op-node 出现 reorg 时需 `popFrontStorage` + safe/finalized 标签对齐，另立 spec |
| 多块批量 merge 性能优化 | YAGNI——单块立即 merge 与正常路径同成本 |

## 8. 全局约束

- **只改 2 个文件**：`bcos-framework/bcos-framework/storage2/MultiLayerStorage.h` + `engine/bcos-engine/EngineServiceImpl.h`（+ 测试）。
- **`MultiLayerStorage` 改动必须纯新增**——不得改 `pushView`/`mergeBackStorage`/`fork` 现有语义（transaction-scheduler 等依赖）。
- **异常传播契约**：merge 失败 → storage error（-32603），绝不判 INVALID（块执行本身有效）。
- **回归套件**：`bcos-evm-opstack-tests`（OpNewPayloadRpcE2eSuite）+ `transaction-scheduler` 相关单测全绿。

## 9. 验收标准

1. OP VALID 块后，`SYS_HASH_2_TX` 行可经 backend 层读到（新断言绿）。
2. `MultiLayerStorage` 现有调用方（transaction-scheduler）测试全绿（兼容性）。
3. 全量 opstack 回归绿（rpc 192/192 + opstack 153/153 不回归）。
4. **head 推进未交付（审查修正）**：重启后块表可读但 `SYS_CURRENT_STATE` 头仍缺失（§7 接受范围）——**验收仅覆盖块表落盘**，不得把"重启恢复链状态"当已修复声明。
