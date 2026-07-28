# Task 5a 报告:版本闸成员化 + opMode 编译期判据 + 通用组合根 V4 零漂移

**未编译验证**:按用户开发期指令,本任务代码与测试照写照提交,未执行 cmake/ctest。下述
"验证"均为静态走读 + API 先例映射比对,不构成编译/运行时证据。

## 改动点

### 1. `engine/bcos-engine/EngineServiceImpl.h`

- `isVersionSupported`:`static bool` → 非 static `const` 成员函数,上界比较对象从
  `static_cast<uint32_t>(EngineApiVersion::V3)` 改为读成员 `m_maxEngineVersion`;下界
  (`V1`)仍是编译期常量,只有上界"成员化"(逐字对应 spec §6.3 措辞"版本上界成员化")。
- 构造函数新增第 7 个参数 `std::uint32_t maxEngineVersion = static_cast<uint32_t>(EngineApiVersion::V3)`
  (追加在既有 `blockTxCountLimit` 之后,带默认值)。默认值保持 V3——通用组合根不传该参数即
  零漂移;新增成员 `m_maxEngineVersion` 插入声明顺序 `m_blockFactory` 之后、
  `m_forkchoiceState` 之前,初始化列表顺序与声明顺序逐字对齐(避免 `-Wreorder`,仓内近期有
  "全仓 FULLNODE 构建下 -Werror 修复"提交,对此从紧)。
- 新增 `public static constexpr bool c_opMode`:用
  `requires(SchedulerType& scheduler, ViewType& view) { scheduler.executeOpBlock(view,
  detail::AnyArg{}, std::declval<std::vector<bcos::bytes> const&>()); }` 编译期探测——**无运行时
  bool 分支**,与裁定 B1 的字面要求一致。
- `exchangeCapabilities`:`if constexpr (c_opMode)` 二选一调用
  `detail::supportedOpCapabilities()`(OP,V1-V4)或既有 `detail::supportedCapabilities()`
  (通用,V1-V3,函数体逐字未改)。

**设计决策(需要标注的分歧点)**:`c_opMode` 的探测表达式没有直接照抄 brief 里
`scheduler.executeOpBlock(...)` 的省略号——如果按字面用真实的 `bcos::evm::engine::OpBlockEnv`
类型名,engine/bcos-engine 这个头就必须 `#include` bcos-evm 的 `OpSchedulerImpl.h`,而
`engine` 这个 CMake 目标目前完全不链 bcos-evm(`add_subdirectory(bcos-evm)` 在
`add_subdirectory(engine)` 之前,且 `engine/CMakeLists.txt` 的 `target_link_libraries` 只有
`bcos-framework bcos-task bcos-utilities ledger`)——这会违反任务给的"库纯净"约束,并把
`evmone`/`evmc` 等一整套依赖污染进所有通用引擎消费者(包括 `engine/test`、
`libinitializer/EngineServiceInitializer.h`)。改用一个"万能 sink"模板转换算子
(`detail::AnyArg`,只声明不定义,与 `std::declval` 同款"仅在未求值上下文出现"契约)探测
第二参数,规避了具名 `OpBlockEnv`;第一参数用已知的真实 `ViewType&`,第三参数用
`std::declval<std::vector<bcos::bytes> const&>()`(与 `OpSchedulerImpl::executeOpBlock` 第三参
后续在 T5b/生产路径实际会传的类型一致)。三个参数分别是"真实具名类型 / 万能 sink /
真实具名类型",不是 brief 字面的省略号,但保留了"`requires{ scheduler.executeOpBlock(...); }`
编译期探测、零运行时 bool"的核心约束。**这一步是本仓库内没有直接先例的技巧**(见下表
"未直接先例"标注),已在下方逐条推导其标准 C++ 合法性(未求值上下文、SFINAE 友好的简单
requirement、模板转换算子的重载决议),但请审查者对此重点复核。

### 2. `engine/bcos-engine/EngineServiceImpl.cpp`

- 新增 `bcos::engine::detail::supportedOpCapabilities()`:调用既有
  `supportedCapabilities()` 后追加 `"engine_newPayloadV4"`/`"engine_getPayloadV4"` 两条。
  `supportedCapabilities()` 函数体本身逐字未动。

### 3. 新增 `bcos-evm/test/opstack/EngineVersionGateTest.cpp`

两条 GTest 用例(`framework` 门控——依赖 `bcos-framework`/`protocol-tars`/`bcos-crypto`/
`transaction-scheduler` 等,仅在 in-tree 构建可编译):

- `EngineVersionGate.GenericCompositionRootRejectsV4Unchanged`:通用组合根
  (`SchedulerSerialImpl` + 本地复刻 `MockExecutorSerial`)不传 `maxEngineVersion`(用默认值),
  `newPayload(request, 4)` 断言抛 `UnsupportedEngineApiVersion`——零漂移。附带
  `static_assert(!GenericEngineService::c_opMode)`。
- `EngineVersionGate.OpCompositionRootNotRejectedByVersionGate`:OP 组合根
  (`OpSchedulerImpl<ViewType>` + `StubExecutor`,`maxEngineVersion=4`),
  `newPayload(request, 4)` 断言**未抛** `UnsupportedEngineApiVersion`(brief 允许的两种断言
  方式之一,已在文件头注释说明选择理由);由于 T5b 的 OP 分支本体尚未落地,调用落入既有
  通用校验逻辑(`detail::validateExecutionPayload` 的 V2+ withdrawals 必填检查),据此额外
  断言返回 `Invalid` + `validationError` 含 "withdrawals"——一个确定性的、非"零 hash 巧合"的
  兜底出口,避免依赖 `parentHash==headBlockHash` 两者同为默认零值这种脆弱巧合。附带
  `static_assert(OpEngineService::c_opMode)`。

**CMake 未接入**:本文件**未**加入 `bcos-evm/test/CMakeLists.txt`。原因:它与 T5b 的
`EngineOpBranchTest.cpp` 有完全相同的构建需求(需要 `${CMAKE_SOURCE_DIR}` 级 include 路径解析
`"engine/bcos-engine/EngineServiceImpl.h"`,需要把 `EngineServiceImpl.cpp` 编译进测试源而非链
`engine` 库防重复符号),而 task-5-brief.md 对 T5b 的这部分 CMake 改动已经明确写"该 CMake
改动随 Task 6 的 `bcos-evm/test/CMakeLists.txt` 一并落,本任务先在 brief 中钉死约束"——T5a
brief 本身的 Files 清单里也没有列 `CMakeLists.txt`。因此判定这条同一护栏同样适用于本文件,
留到 Task 6 一并接入(届时还需为 `SchedulerSerialImpl` 补 `transaction-scheduler`/`tbb` 链接
边)。此文件目前只是"写好但未挂进构建图"的状态,与用户"照写照提交"的开发期指令一致。

## API 先例映射表(自审)

| 调用/技巧 | 位置 | 仓内先例 |
|---|---|---|
| ctor 追加带默认值的尾参 | `EngineServiceImpl.h` 构造函数 | 同函数既有 `blockTxCountLimit` 参数本身就是这个模式(改动前就存在) |
| `static` → 非 static 只读成员函数,读实例成员 | `isVersionSupported` | 同类 `handleGetPayload`(既有 `const` 成员函数)已是同类调用点 |
| `requires(x){ x.method(...) -> Awaitable<T>; }` 编译期探测 | `scheduler_v1::TransactionScheduler` concept | `bcos-framework/bcos-framework/transaction-scheduler/TransactionScheduler.h:10-19`(已编译,仓内广泛实例化) |
| `template<class T> operator T() const;`(万能 sink,仅声明不定义) | `detail::AnyArg` | **无直接先例**;声明-不定义-仅未求值上下文使用的契约与 `std::declval`(标准库)一致;OpSchedulerImpl.h 注释里提到的 `evmc::address`/`bytes32` 隐式转换是非模板版本,方向相近但不是同一技巧——已在报告正文单独标注为需要重点复核的分歧点 |
| `detail::xxxCapabilities()` 自由函数 + `if constexpr` 二选一调用 | `exchangeCapabilities` | 同文件既有 `detail::supportedCapabilities()`/`validateExecutionPayload()` 等自由函数群,同一 `.cpp` 定义模式 |
| `SchedulerSerialImpl(ioServicePool)` | 测试 | `transaction-scheduler/tests/testSchedulerSerial.cpp:64-65` |
| `MockExecutorSerial`(createExecuteContext/executeTransaction/ExecuteContext::executeStep) | 测试 | `transaction-scheduler/tests/testSchedulerSerial.cpp:20-46`(本地复刻,追加了未用参数的 `(void)` 消警,非逐字抄) |
| `OpSchedulerImpl<ViewType>(receiptFactory, chainId, OpForkTimestamps{...})` | 测试 | `bcos-evm/test/opstack/OpSchedulerImplTest.cpp:302-303`/`340-341` |
| `StubExecutor` | 测试 | `bcos-evm/test/opstack/OpSchedulerImplTest.cpp:266-288`(逐字复刻) |
| `TrivialCheckpointStorage`/`MutableStorage`/`BackendMemStorage`/`MLS`/`StorageFixture` | 测试 | `bcos-evm/test/opstack/OpSchedulerImplTest.cpp:121-176`(逐字复刻,同一"每文件自留一份小桩"惯例) |
| `makeReceiptFactory()` | 测试 | `bcos-evm/test/opstack/OpSchedulerImplTest.cpp:186-191` |
| 手工拼 `BlockFactoryImpl`(不用 `bcos::test::createBlockFactory`) | 测试 | 构造序列取自 `bcos-framework/bcos-framework/testutils/faker/FakeBlock.h:60-70`,但**刻意不 include 该头**——它无条件拉入 `<boost/test/unit_test.hpp>`,而 `bcos-evm-opstack-tests` 是 GTest 目标、不链 `Boost::unit_test_framework`,直接 include 有引入非预期链接依赖的风险 |
| `EXPECT_THROW`/`TEST()`/`syncWait` | 测试 | 同目录 `OpSchedulerImplTest.cpp` 全文 |

## 测试清单

- `bcos-evm/test/opstack/EngineVersionGateTest.cpp`
  - `EngineVersionGate.GenericCompositionRootRejectsV4Unchanged`
  - `EngineVersionGate.OpCompositionRootNotRejectedByVersionGate`
  - 两处 `static_assert`(`!GenericEngineService::c_opMode` / `OpEngineService::c_opMode`)——
    编译期见证,不依赖运行时断言。

未新增/未修改任何 `ports/vectors/transaction-scheduler/` 下文件;
`engine/test/unittests/engine/EngineServiceTest.cpp` 未改动(其 `StubScheduler`/
`BloomScheduler` 均无 `executeOpBlock`,`c_opMode` 恒 false,原 10 条能力表/所有既有断言逐字
不变)。

## Commit

`feat(engine): 版本闸成员化(maxEngineVersion)+ opMode 编译期判据(if constexpr requires)+ 通用组合根 V4 零漂移`

改动文件:
- `engine/bcos-engine/EngineServiceImpl.h`
- `engine/bcos-engine/EngineServiceImpl.cpp`
- `bcos-evm/test/opstack/EngineVersionGateTest.cpp`(新增,未接入 CMake,随 Task 6)
- 本报告(`.superpowers/sdd/.../task-5a-report.md`,`.gitignore` 命中,需 `git add -f`)
