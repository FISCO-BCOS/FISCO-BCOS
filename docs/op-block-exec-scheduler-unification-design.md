# OP 块执行:engine 调用面统一 + 共识比对下沉(设计)

> 日期:2026-08-11。分支 `worktree-op-alignment`(基于 feat-op-executor-e2e @ 0ba5256e0)。
> 状态:设计已批准,经 4 个 sub agent 独立审查后修订(本版合并全部审查意见)。
> 对应 #23 方向的替代方案——不实施"块执行走 scheduler pipeline"。
> 前置:docs/opstack-scheduler-adapter-design.md §0(为什么 BaselineScheduler 承载不了 OP 块语义)。

## 背景与问题

OP 块执行(`executor_version >= 3`)由 EngineService 驱动:`op-node → engine API newPayload →
handleOpNewPayload → executeOpBlock → processOpBlock`。架构上它分层正确(执行在 OP 侧,engine 调
用),但有一个职责混杂:engine 的 `handleOpNewPayload` 内嵌了 **8 项 commitments 比对**
(receiptsRoot/logsBloom/withdrawalsRoot/stateRoot/gasUsed/txRoot/blobGasUsed/requestsHash,
约 50 行,EngineServiceImpl.h:1153-1201),这是 **OP 共识语义**却写在 engine-API 层。

本设计:把 OP 共识比对下沉到 OP 侧(可独立测试),让 engine 的 OP 分支瘦身为"API 语义 + 调用 +
注册",并收敛统一调用面。**不改任何执行语义**(processOpBlock/executeOpBlock/8 项判定规则全部
保留,只换代码位置)。

## 为什么不做"块执行走 scheduler"(替代方案论证)

| 路线 | 否决理由 |
|---|---|
| `executeBlock` 真实化(塞进 concept pipeline) | 签名矛盾:收 parsed txs(丢 raw bytes,OP 的 canonical 严格性与按 wire bytes 的 txRoot 无从谈起)、带 concept 的 executor 参数(OP 不用)、拿不到 commitments |
| 完整 BlockExecutor 纯虚抽象 | 只有 OP/generic 两种实现,且 `executeOpBlock` 已是块级契约;再加策略接口是过度设计 |
| 并行/chunk | op-geth Process 串行(§0 已证伪) |

## 设计

### 1. OP 侧纯函数(opstack-executor/OpEngineSeam.h,与 commitmentsOf 同文件)

两个纯函数,不依赖 engine 类型(`OpBlockCommitments` 为纯 bcos:: 类型,h256/h2048/u256/
optional<uint64_t>/optional<h256>):

```cpp
/// 执行结果投影的 commitments vs payload 声明的承诺。返回不匹配字段名,全匹配则 nullopt。
/// 契约(逐字复刻 engine 原比对语义,判定零改动):
///   - 逐字段、按序(receiptsRoot→logsBloom→withdrawalsRoot→stateRoot→gasUsed→txRoot→
///     blobGasUsed→requestsHash),首个不匹配即返回;
///   - txRoot 槽返回的字段名是字面量 "transactionsRoot"(非 "txRoot")——保 INVALID 错误串;
///   - blobGasUsed/requestsHash 为 optional:仅当 computed 侧 has_value 时进入比较,并解引用
///     announced 侧(announced 侧由前置保证有值,见 §4 契约);computed 侧 nullopt 时无论
///     announced 何值一律 SKIP(这是 pre-Jovian 真实路径:Isthmus 时 seal.blobGasUsed=nullopt
///     但 payload.blobGasUsed=0)。
inline std::optional<std::string> mismatchedFieldOf(
    const OpBlockCommitments& computed, const OpBlockCommitments& announced);

/// 从 payload/header 提取 8 项承诺 → OpBlockCommitments(announced 侧)。
///   - 5 项来自 ExecutionPayload:receiptsRoot/logsBloom/withdrawalsRoot/stateRoot/gasUsed;
///   - txRoot 来自执行前置的 computeTxRoot(*payload.rawTransactions);
///   - requestsHash 来自 ethHeader->requestsHash();
///   - blobGasUsed:payload.optional<u256> → 结构的 optional<uint64_t>,用 narrowU256ToU64 反向
///     窄化(engine 原比对是 u64→u256 加宽,方向相反,必须显式窄化);
///   - logsBloom:payload Bloom(std::array<byte,256>) → h2048,字节保真转换(逻辑仿照 engine 的
///     detail::toEthLogsBloom,搬进 OP 侧)。
inline OpBlockCommitments announcedCommitmentsOf(
    const bcos::framework::ExecutionPayload& payload, const bcos::h256& transactionsRoot,
    const bcos::protocol::BlockHeader& ethHeader);
```

`OpEngineSeam.h` 需补 `#include <string>`(新返回类型用到;现 include 无 <string>)。
`announcedCommitmentsOf` 接收的 `ExecutionPayload` 是 **bcos-framework/engine/Types.h** 类型
(同库,opstack-executor 已 link bcos-framework),**不**违反"opstack-executor 不得依赖 bcos-engine"。

### 2. SchedulerType 依赖名通道(seam 纯度,审查必改点)

`EngineServiceImpl.h` 是 public/install 的模板头,不 link bcos-evm/opstack-executor,现有
seam(c_opMode 探测 + `SchedulerType::` 依赖名)就是让它**永不拼写任何 OP 类型名**。因此两个
纯函数不得在 engine 头内直接拼写——按既有模式(`commitmentsOf`/`computeTxRoot` L142-154
re-publish)经 SchedulerType 到达:

- **OpSchedulerImpl.h** seam 区新增两个 static re-publish(与 `commitmentsOf` 同构):
  ```cpp
  using CommitmentsT = bcos::evm::engine::OpBlockCommitments;   // 供依赖名构造/返回
  static CommitmentsT announcedCommitmentsOf(const ExecutionPayload&, const h256&, const BlockHeader&)
      { return bcos::evm::engine::announcedCommitmentsOf(...); }
  static std::optional<std::string> mismatchedFieldOf(const CommitmentsT&, const CommitmentsT&)
      { return bcos::evm::engine::mismatchedFieldOf(...); }
  ```
  `OpSchedulerImpl.h` 补 `#include <bcos-framework/engine/Types.h>`(同库)。
- **engine** 侧只写依赖名,`auto` 推导,不拼写 OP 类型:
  ```cpp
  const auto commitments = SchedulerType::commitmentsOf(*executeResult);
  const auto announced = SchedulerType::announcedCommitmentsOf(payload, transactionsRoot, *ethHeader);
  if (auto f = SchedulerType::mismatchedFieldOf(commitments, announced))
  {
      co_return makeStatus(PayloadValidationStatus::Invalid, latestValidHash,
          std::string("execution result does not match payload field: ") + *f);
  }
  ```
  原 L1153-1201 的 8 项 if/else 链整体替换为上述调用。原 `detail::toEthLogsBloom` 随投影
  逻辑移到 OP 侧(§1),engine 不再需要它。

### 3. engine 统一调用面(美化性重构,动机实事求是)

`handleNewPayload`(L583)本就是唯一调用面,`if constexpr (c_opMode)` 分派(L617-620)已存在。
本改动把它收进一个私有 `executePayload(request, version)`,收益是可读性(handleNewPayload
从 ~175 行瘦身为"版本 gate + 委托"),**不引入新的架构能力**:

```
newPayload → handleNewPayload
  ├─ 版本 gate + !c_opMode V4 守卫(L586-613 全部保留)
  └─ co_return co_await executePayload(request, version)
       ├─ [c_opMode]  → handleOpNewPayload(request, version)(原样搬入,含瘦身后的比对)
       └─ [else]      → 原通用段原样搬入
```

通用分支逐行搬入 else 分支(无跨函数依赖,行为中性);`c_opMode` SFINAE probe 不动。

### 数据流(OP 路径,改动后)

```
executePayload()
  → 版本 gate(-38005) / 静态 blockHash 验证 / 父链/时间戳/baseFee 检查   [engine,不变]
  → executeOpBlock → ExecuteResult                                     [OP 侧,不变]
  → SchedulerType::commitmentsOf(result)                               [engine,依赖名]
  → SchedulerType::announcedCommitmentsOf(payload, txRoot, header)     [OP 侧,依赖名]
  → SchedulerType::mismatchedFieldOf(computed, announced)              [OP 侧,依赖名]
  → 匹配 → registerOpBlock → mergeView → notifyBlockNumber → VALID
  → 不匹配 → INVALID(+latestValidHash=parent)
```

### 4. 错误处理(不变)+ announced 前置条件契约

- INVALID + latestValidHash=parent 映射、分类屏障、executeOpBlock 的 RTTI-bypass catch、
  registerOpBlock 的 OpExecutionInternalError 通道——全部保留,比对仍留在分类屏障内
  (parentKnown 之后)。
- **announced 侧 optional 必填前置条件**(纯函数契约,实现必须继承):
  - `validateOpNewPayloadRequest` 强制 `withdrawalsRoot`/`blobGasUsed` 存在
    (EngineServiceImpl.cpp:348-361);
  - `rebuildOpEthHeader` 无条件 `setRequestsHash(c_opEmptyRequestsHash)`(L546)。
  因此 `announcedCommitmentsOf` 内对这三个 optional 直接解引用是安全的;`mismatchedFieldOf`
  不得为 announced 缺值引入新的容错(那会把"现状 -32603"静默改为"跳过比对→错误 VALID")。

### 测试

1. **新单测 `opstack-executor/tests/OpMismatchedFieldTest.cpp`**(注册进
   `opstack-executor-detail-tests` target,`tests/CMakeLists.txt` 显式列源文件):
   - 8 字段逐一制造差异,断言返回精确字段名;txRoot 槽断言字面量 **`"transactionsRoot"`**;
   - 多字段同时不匹配 → 报告首个(receiptsRoot 优先,验证顺序);
   - 全匹配 → nullopt;
   - **optional 4 元矩阵**:computed=nullopt & announced=value → nullopt(SKIP,pre-Jovian 真实
     路径);computed=value & announced=value → 比对;computed=value & announced=nullopt →
     契约钉死为解引用(announced 由前置保证,单测按"有值"构造)。
2. **announcedCommitmentsOf 单测**:payload 构造 → 断言 8 项投影正确,blobGasUsed 窄化、
   logsBloom 字节保真。
3. **回归口径(修正)**:
   - **e2e(65 用例:63 single + 2 chained)全绿 + 纯函数单测(8 字段 × 4 元矩阵)全绿** →
     证明判定不变;
   - **t8n(126 向量)全绿** → 证明执行/seal 不变(t8n 回放走 processOpBlock→sealOpBlock,不走
     engine 的 8 项比对,不能用于证明判定不变);
   - 当前 e2e invalid 向量只覆盖 8 项里的 **3 项**(stateRoot/gasUsed/receiptsRoot);
     logsBloom/withdrawalsRoot/txRoot/blobGasUsed/requestsHash 无端到端覆盖——**建议补 5 个
     invalid e2e 向量**进 manifest + coverage-matrix,端到端证明 INVALID + latestValidHash=parent
     映射(可作后续增量,不阻塞本设计)。

### 明确不做

- ❌ executeBlock 真实化 / concept pipeline 承载 OP 块执行(签名矛盾)
- ❌ BlockExecutor 纯虚策略抽象(过度设计)
- ❌ 并行 / chunk(op-geth 串行)
- ❌ processOpBlock / executeOpBlock / 8 项判定规则任何改动

### 文件清单

| 文件 | 改动 |
|---|---|
| `opstack-executor/OpEngineSeam.h` | 新增 `mismatchedFieldOf` + `announcedCommitmentsOf`;补 `#include <string>`;logsBloom 转换逻辑 |
| `opstack-executor/OpSchedulerImpl.h` | re-publish 两个纯函数为 static 成员(`CommitmentsT` 别名);include bcos-framework/engine/Types.h |
| `opstack-executor/tests/OpMismatchedFieldTest.cpp` | **新增**单测(8 字段/顺序/4 元矩阵/字段名) |
| `opstack-executor/tests/CMakeLists.txt` | `OpMismatchedFieldTest.cpp` 加入 `opstack-executor-detail-tests` |
| `engine/bcos-engine/EngineServiceImpl.h` | 比对段瘦身为一行调用;`executePayload` 收敛 |
| `docs/op-block-exec-scheduler-unification-design.md` | 本文档 |

### 验证步骤

1. **最快单点**:`cmake --build build --target opstack-executor-detail-tests` + `ctest
   --test-dir build -R OpstackExecutorDetailTests`(秒级,覆盖 8 项判定 + 字段名 + 可选语义,
   不碰 engine 编译图)。
2. `cmake --build build --target opstack-executor-block-tests` + ctest(判定回归门,~126 t8n +
   e2e 65)。
3. `cmake --build build --target init`(生产 OP 组合根,含 `static_assert(c_opMode)`,证明
   purity 未破)。
4. `cmake --build build --target engine` + EngineServiceTest(通用路径回归)。

## 决策记录

- 2026-08-11:用户选定"架构统一"为动机、"engine 统一调用面"为统一层;方向 = 比对下沉 +
  调用面收敛,替代"块执行走 scheduler"(签名矛盾,已证伪)。
- 2026-08-11:4 个 sub agent 独立审查(技术正确性/架构一致性/完整性与测试/实现风险),
  修订合并:① `announcedCommitmentsOf`/`mismatchedFieldOf` 全走 `SchedulerType::` 依赖名
  (seam 纯度,OpSchedulerImpl.h 补进文件清单);② 字段名逐字 `"transactionsRoot"`;
  ③ optional 4 元矩阵钉死"仅 computed 有值才比";④ 回归口径修正(t8n 126/e2e 65,
  t8n 不证明判定)。
