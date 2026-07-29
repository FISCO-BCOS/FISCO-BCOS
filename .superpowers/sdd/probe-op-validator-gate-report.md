# OP 验证者最小闭环 五探针留痕(design §7.4,Task 7)

日期:2026-07-29
分支:`feat-op-validator-loop`(基座 `feat-evm-ledger-bridge`,merge-base `42e62fcef`)
探针起点 HEAD:`fe2a40c29`
基线(N0,探针前捕获,见 `2026-07-28-op-validator-minimal-loop/n0-*.txt`):
in-tree `bcos-evm-opstack-tests` **206/206**、standalone **131/131**、
engine Boost `test-bcos-engine` **11/11**;本闭环新增 50 例合并过滤器
`--gtest_filter='Engine*:OpSchedulerImpl*:EthBlockHeader*:OpDepositEncode*'` **50/50**。

约定(沿桥项目先例 `.superpowers/sdd/probe-ledger-bridge-report.md`):每个探针记录
①注入点(diff)②翻红实际输出③回退命令④复绿输出⑤探针结束后 `git status`。全部注入均通过
`Edit` 临时改动源码 → `make bcos-evm-opstack-tests` → 跑对应测试捕获翻红 →
`rtk git checkout --` 精确回退 → 重新编译 → 再跑一次确认复绿。**探针注入代码全程未进入任何
commit**——每个探针小节末尾的 `git status` 均为 `clean`,可逐条核对。

翻红面口径:每个探针除"点名宿主用例"外,一并记录**同一注入下 50 例合并过滤器的完整失败名单**
(blast radius)——既证明探针有判别力,也证明它**没有**把无关用例一起打翻。

复绿命令(五处相同,不再逐条重复):
`./build/bcos-evm/test/bcos-evm-opstack-tests --gtest_filter='Engine*:OpSchedulerImpl*:EthBlockHeader*:OpDepositEncode*'`
→ `[  PASSED  ] 50 tests.`

---

## 探针 1:blockHash 校验探针(design §7.4 ①,§6.1 step 2)

**注入点**:`engine/bcos-engine/EngineServiceImpl.h`,`handleOpNewPayload` step 2 的重组头
哈希比对——短路为恒假:

```diff
+        // PROBE-1-INJECT(blockHash 校验探针,Task 7 留痕,不入 commit): 停用 blockHash 比对。
-        if (ethHeader.hash() != payload.blockHash)
+        if (false && ethHeader.hash() != payload.blockHash)
         {
```

**翻红命令**:`--gtest_filter='EngineNewPayloadMutation.TamperedBlockHashIsInvalidWithNullLatestValidHash'`

**翻红实际输出**:

```
[ RUN      ] EngineNewPayloadMutation.TamperedBlockHashIsInvalidWithNullLatestValidHash
.../EngineNewPayloadGateTest.cpp:1015: Failure
Expected equality of these values:
  status.status
    Which is: 1-byte object <00>          <- Valid
  bcos::engine::PayloadValidationStatus::Invalid
    Which is: 1-byte object <01>

.../EngineNewPayloadGateTest.cpp:1016: Failure
Value of: status.latestValidHash.has_value()
  Actual: true
Expected: false

.../EngineNewPayloadGateTest.cpp:1017: Failure
Value of: status.validationError.has_value()
  Actual: false
Expected: true

[  FAILED  ] EngineNewPayloadMutation.TamperedBlockHashIsInvalidWithNullLatestValidHash (3 ms)
```

**翻红面(50 例合并过滤器)**:3 例,`[  PASSED  ] 47 tests.`

```
[  FAILED  ] EngineOpBranch.BlockHashMismatchIsInvalidWithNullLatestValidHash
[  FAILED  ] EngineNewPayloadMutation.TamperedBlockHashIsInvalidWithNullLatestValidHash
[  FAILED  ] EngineNewPayloadMutation.NonEmptyExecutionRequestsLandsInBlockHashBucket
```

第三条同时翻红是**预期且有信息量**的:变异矩阵 #7(executionRequests 非空)因为
`NewPayloadRequest` 没有该载体,用"把 `requestsHash` 位移一位后重算 hash"作代理,其判决
恰恰落在 blockHash 失配桶——注入停掉这个桶,它自然一并失守。这条连带关系正是 design §6.1
step 2 把 #7 指派进 blockHash 桶的可观测证据。

其余 47 例(含 33 条金向量 gate)在注入下**仍全绿**:金向量的 `payload.blockHash` 本来就等于
重组头哈希,停用比对不改变它们的结论——判别力只落在被篡改的样本上,符合探针设计意图。

**回退**:`rtk git checkout -- engine/bcos-engine/EngineServiceImpl.h`

**复绿输出**:`[  PASSED  ] 50 tests.`

**探针后 `git status`**:`clean — nothing to commit`

---

## 探针 2:六项比对面比对探针(design §7.4 ②,§4.1/§6.1 step 5)

**注入点**:`engine/bcos-engine/EngineServiceImpl.h`,step 5 判决前——只吞掉**六项**的失配,
刻意保留 §5.1 的两条额外比对(`blobGasUsed`/`requestsHash`),把探针范围精确限定在"六项比对
面"这一术语所指的集合上:

```diff
+        // PROBE-2-INJECT(六项比对面探针,Task 7 留痕,不入 commit): 只吞掉「六项」的失配,
+        // 保留 §5.1 的两条额外比对(blobGasUsed/requestsHash),把探针范围精确限定在六项。
+        if (mismatchedField.has_value() && *mismatchedField != "blobGasUsed" &&
+            *mismatchedField != "requestsHash")
+        {
+            mismatchedField.reset();
+        }
         if (mismatchedField.has_value())
         {
             co_return makeStatus(PayloadValidationStatus::Invalid, latestValidHash,
```

**翻红命令**:50 例合并过滤器(六项各一例,需要整组观察)

**翻红面**:**恰好 7 例**,`[  PASSED  ] 43 tests.`

```
[  FAILED  ] EngineOpBranch.EachComparisonSurfaceFieldMismatchIsNamed
[  FAILED  ] EngineNewPayloadMutation.ComparisonSurfaceReceiptsRoot
[  FAILED  ] EngineNewPayloadMutation.ComparisonSurfaceLogsBloom
[  FAILED  ] EngineNewPayloadMutation.ComparisonSurfaceWithdrawalsRoot
[  FAILED  ] EngineNewPayloadMutation.ComparisonSurfaceStateRoot
[  FAILED  ] EngineNewPayloadMutation.ComparisonSurfaceGasUsed
[  FAILED  ] EngineNewPayloadMutation.ComparisonSurfaceTransactionsRoot
```

= 变异矩阵 #8.1–#8.6 **六例一个不少、一个不多**,外加 `EngineOpBranch` 里同语义的桩层用例。
这是"六项比对面"这个术语在测试面上的**逐项**证据,而不是"整块比对存在"的笼统证据。

点名用例(stateRoot)的翻红细节:

```
[ RUN      ] EngineNewPayloadMutation.ComparisonSurfaceStateRoot
.../EngineNewPayloadGateTest.cpp:1137: Failure
Expected equality of these values:
  status.status
    Which is: 1-byte object <00>          <- Valid
  bcos::engine::PayloadValidationStatus::Invalid
    Which is: 1-byte object <01>
Google Test trace: .../EngineNewPayloadGateTest.cpp:1130: stateRoot

.../EngineNewPayloadGateTest.cpp:1140: Failure
Expected equality of these values:
  *status.latestValidHash
    Which is: 4b7b3b87fa59a3fe0901ec85db62a62ce4afd1bec2ee1ba1961c7ca634909cac
  scenario.parentHash
    Which is: 45daac1c62119a8624509cd80f0b2543f6c78fd21457213af891d8a6d8b14f74
Google Test trace: .../EngineNewPayloadGateTest.cpp:1130: stateRoot

.../EngineNewPayloadGateTest.cpp:1141: Failure
Value of: status.validationError.has_value()
  Actual: false
Expected: true
```

第二条失败顺带留下一个**注入态的语义足迹**:六项比对被吞掉后,该块一路走到 step 6 被判 VALID,
`latestValidHash` 因而变成了**块自身的哈希**(`4b7b3b87…`)而不是 parent(`45daac1c…`)——
正说明这几例断言的 `latestValidHash==parentHash` 是**有判别力的**,不是恒真陪衬。

**回退**:`rtk git checkout -- engine/bcos-engine/EngineServiceImpl.h`

**复绿输出**:`[  PASSED  ] 50 tests.`

**探针后 `git status`**:`clean — nothing to commit`

---

## 探针 3:错误分类探针(design §7.4 ③,§4.3/§6.1 step 4;-32603 vs INVALID)

**注入点**:`engine/bcos-engine/EngineServiceImpl.h`,`executeOpBlock` 的 `StorageError`
catch 体——把"存储故障 → -32603"改成"存储故障 → 对区块的共识裁决 INVALID":

```diff
         catch (const typename SchedulerType::StorageError& e)
         {
-            BOOST_THROW_EXCEPTION(
-                OpExecutionInternalError{} << bcos::errinfo_comment{
-                    std::string("OP block execution hit a storage failure: ") + e.what()});
+            // PROBE-3-INJECT(错误分类探针,Task 7 留痕,不入 commit): 把存储故障错判为对区块的
+            // 共识裁决(INVALID),复现"节点因自己读不到而投票反对一个块"的错误分类。
+            co_return makeStatus(PayloadValidationStatus::Invalid, latestValidHash,
+                std::string("OP block execution hit a storage failure: ") + e.what());
         }
```

**翻红命令**:`--gtest_filter='EngineNewPayloadMutation.StorageLayoutFaultIsInternalErrorNotInvalid'`

**翻红实际输出**:

```
[ RUN      ] EngineNewPayloadMutation.StorageLayoutFaultIsInternalErrorNotInvalid
.../EngineNewPayloadGateTest.cpp:1350: Failure
Expected: bcos::task::syncWait(scenario.fixture->service.newPayload(scenario.request, 4))
          throws an exception of type bcos::engine::OpExecutionInternalError.
  Actual: it throws nothing.

[  FAILED  ] EngineNewPayloadMutation.StorageLayoutFaultIsInternalErrorNotInvalid (1 ms)
```

**翻红面**:2 例,`[  PASSED  ] 48 tests.`

```
[  FAILED  ] EngineOpBranch.StorageErrorFromExecutionIsInternalErrorNotInvalid
[  FAILED  ] EngineNewPayloadMutation.StorageLayoutFaultIsInternalErrorNotInvalid
```

一例是纯桩(`StubOpScheduler` 直接抛 `StorageError`),一例是**真调度器 + 真 storage2 布局
违规行**(经 `Storage2Ledger::fetchAllStorage` throw → 毒旗 → `OpStorageError`)。两条同时
翻红,意味着这条分类路径**从桩层到真桥层都被钉住**。

**副产物(RTTI 旁路的运行期反证)**:真桥这一例的翻红本身证明 engine 侧
`catch (const typename SchedulerType::StorageError&)` 在本二进制里**确实绑上了**跨 `bcos-evm`
边界抛出的 `OpStorageError`——即 T4 记录的 typed-catch RTTI 旁路现象**没有**波及这对
engine 层 typed catch(详见本报告末"附:RTTI 旁路复扫")。

**回退**:`rtk git checkout -- engine/bcos-engine/EngineServiceImpl.h`

**复绿输出**:`[  PASSED  ] 50 tests.`

**探针后 `git status`**:`clean — nothing to commit`

---

## 探针 4:块登记接线探针(design §7.4 ④,测试 M-1;§6.1 step 6)

**注入点(主注入,生产代码)**:`engine/bcos-engine/EngineServiceImpl.h`,`registerOpBlock`
——跳过 `SYS_HASH_2_NUMBER` 写入,即 step 3 `parentKnown` **唯一读的那张表**:

```diff
-        storage::Entry hashToNumberEntry;
-        hashToNumberEntry.set(blockNumberStr);
-        co_await storage2::writeOne(view,
-            executor_v1::StateKey{
-                ledger::SYS_HASH_2_NUMBER, bcos::concepts::bytebuffer::toView(payload.blockHash)},
-            std::move(hashToNumberEntry));
+        // PROBE-4-INJECT(块登记接线探针,Task 7 留痕,不入 commit): 跳过 SYS_HASH_2_NUMBER 写入
+        // ——即 step 3 parentKnown 唯一读的那张表,链式对第二块因此必转 SYNCING。
+        // (原五行写入整体注释掉)
```

**翻红命令**:50 例合并过滤器

**翻红面**:8 例,`[  PASSED  ] 42 tests.`

```
[  FAILED  ] EngineOpBranch.ValidPayloadRegistersAllFourTables
[  FAILED  ] EngineNewPayloadGate.IsthmusSingleTransfer
[  FAILED  ] EngineNewPayloadGate.JovianMultiTransfer
[  FAILED  ] EngineNewPayloadGate.IsthmusDepositOnly
[  FAILED  ] EngineNewPayloadGate.AllThirtyThreeGoldenVectors
[  FAILED  ] EngineNewPayloadGate.ChainedPairParentKnownThroughBlockRegistration
[  FAILED  ] EngineNewPayloadMutation.SamePayloadResubmittedAfterParentBecomesKnown
[  FAILED  ] EngineNewPayloadMutation.ForkchoiceWithAttributesRefusedButHeadStillAdvances
```

宿主用例(链式对)的翻红细节:

```
[ RUN      ] EngineNewPayloadGate.ChainedPairParentKnownThroughBlockRegistration
.../EngineNewPayloadGateTest.cpp:928: Failure
Value of: isBlockRegistered(fixture.storage, hashA)
  Actual: false
Expected: true

.../EngineNewPayloadGateTest.cpp:939: Failure
Expected equality of these values:
  forkchoiceResult.payloadStatus.status
    Which is: 1-byte object <02>          <- Syncing
  bcos::engine::PayloadValidationStatus::Valid
    Which is: 1-byte object <00>
...
[  FAILED  ] EngineNewPayloadGate.ChainedPairParentKnownThroughBlockRegistration (1 ms)
```

**如实记录一处与 spec §7.4 ④ 措辞的落差,并补做了直接观测**:spec 预言的可观测信号是
"两块链式用例**第二块必转 SYNCING**"。上面这次运行**没有直接观测到第二块**——因为第 4 步
`ASSERT_TRUE(forkchoiceResult.payloadStatus.latestValidHash.has_value())` 是 **fatal**
断言,在 A 未登记导致 FCU(head=A)返回 SYNCING 时**提前终止了该测试函数**,根本没跑到第 5 步。
翻红是真的、因果也是真的,但落点比 spec 预言早了一步。

为把 spec 预言的那一步也**实测出来**,在保持主注入的前提下追加了一处**测试侧临时注入**
(`PROBE-4-INJECT-B`,同样不入 commit):把该 fatal `ASSERT_TRUE` 降级为 `EXPECT_TRUE` +
`if` 守卫,让注入态继续跑到第 5 步。结果:

```
.../EngineNewPayloadGateTest.cpp:948: Failure
Expected equality of these values:
  statusB.status
    Which is: 1-byte object <02>          <- Syncing
  bcos::engine::PayloadValidationStatus::Valid
    Which is: 1-byte object <00>
chainB: validationError=<none>
```

`statusB.status == Syncing`,`validationError` 为空——**第二块确实必转 SYNCING**,与 spec
§7.4 ④ 的预言逐字吻合。这同时反向证明了链式对的因果链条:B 之所以在复绿态是 VALID,**唯一**
原因就是 A 的 VALID 分支往 `SYS_HASH_2_NUMBER` 写了那一行(不是 fixture 预登记、不是内存态)。

其余翻红的解释:33 条金向量 gate 与 `SamePayloadResubmitted…` 断言的是"VALID 后块已登记"
(`isBlockRegistered` / `registeredNumber.has_value()`),`ForkchoiceWithAttributes…` 需要先
把一个块登记进去再做 FCU——都直接依赖被注入掉的这行写入,翻红符合预期。

**回退**:`rtk git checkout -- engine/bcos-engine/EngineServiceImpl.h bcos-evm/test/opstack/EngineNewPayloadGateTest.cpp`

**复绿输出**:`[  PASSED  ] 50 tests.`

**探针后 `git status`**:`clean — nothing to commit`

---

## 探针 5:通用版本闸探针(design §7.4 ⑤,测试 C-2;§6.3)

**注入点**:`engine/bcos-engine/EngineServiceImpl.h`,构造函数的 `maxEngineVersion`
**默认实参**——把上界从 V3 抬到 4。选这一处是因为**通用组合根恰恰不传该参数**,吃的就是这个
默认值,所以它精确复现"OP 放宽外溢到通用路径"这一漂移形态:

```diff
-        std::uint32_t maxEngineVersion = static_cast<std::uint32_t>(EngineApiVersion::V3))
+        // PROBE-5-INJECT(通用版本闸探针,Task 7 留痕,不入 commit): 把上界默认值从 V3 抬到 4,
+        // 复现"OP 放宽外溢到通用组合根"这一漂移(通用组合根不传该参数,吃的就是这个默认值)。
+        std::uint32_t maxEngineVersion = 4U)
```

通用组合根 fixture 即 spec 指定的 `SchedulerSerialImpl` + `MockExecutorSerial` 本地复刻
(先例 `testSchedulerSerial.cpp:20-75`);**V4 拒绝在版本闸即完成,不触达 executor**。

**翻红命令**:50 例合并过滤器 + engine Boost `./build/engine/test/test-bcos-engine`

**翻红面**:3 例,`[  PASSED  ] 47 tests.`

```
[  FAILED  ] EngineVersionGate.GenericCompositionRootRejectsV4Unchanged
[  FAILED  ] EngineOpBranch.GenericCompositionRootStillRejectsV4
[  FAILED  ] EngineNewPayloadMutation.GenericCompositionRootStillRejectsV4
```

细节:

```
[ RUN      ] EngineVersionGate.GenericCompositionRootRejectsV4Unchanged
.../EngineVersionGateTest.cpp:281: Failure
Expected: bcos::task::syncWait(engineService.newPayload(request, 4))
          throws an exception of type bcos::engine::UnsupportedEngineApiVersion.
  Actual: it throws nothing.

[ RUN      ] EngineNewPayloadMutation.GenericCompositionRootStillRejectsV4
.../EngineNewPayloadGateTest.cpp:1308: Failure
Expected: ... newPayload(request, 4) throws ... UnsupportedEngineApiVersion.
  Actual: it throws nothing.
.../EngineNewPayloadGateTest.cpp:1310: Failure
Expected: ... getPayload(PayloadID{"0x1"}, 4) throws ... UnsupportedEngineApiVersion.
  Actual: it throws boost::wrapexcept<bcos::engine::UnknownPayload> with description "Unknown payload".
```

第二条失败尤其说明问题:注入后 `getPayload(id, 4)` **越过了版本闸**,一路走到缓存查找才因
"payload 不存在"报错——闸真的被抬开了,不是断言写法上的巧合。

**一处必须如实记录的负面发现**:同一注入下,**engine 自己的 Boost 测试套件
`test-bcos-engine` 11 个用例全绿**(`*** No errors detected`)。也就是说,既有 engine 测试面
**对"版本上界漂移"这件事完全没有判别力**——它不含任何"V4 必须被拒"的断言。通用组合根 V4 零
漂移这条性质,**目前只由本闭环新增的 `EngineVersionGate` / 变异矩阵 #12 这三条用例守着**。
这不是本次改动引入的缺口(既有套件本来就没有该断言),但它意味着:若将来有人删掉这三条用例,
漂移将无声发生。记录在案,不掩盖。

**回退**:`rtk git checkout -- engine/bcos-engine/EngineServiceImpl.h`

**复绿输出**:`[  PASSED  ] 50 tests.` + engine Boost `11/11 *** No errors detected`

**探针后 `git status`**:`clean — nothing to commit`

---

## 汇总:全程 `git status` 核对

五个探针共产生 5 次"注入 → 编译 → 翻红 → 回退 → 编译 → 复绿"往返(探针 4 的往返内含一处
额外的测试侧临时注入,与主注入同批回退)。每次往返结束后立即执行 `rtk git status`,全部返回:

```
* feat-op-validator-loop
clean — nothing to commit
```

探针注入代码(标注 `PROBE-N-INJECT` / `PROBE-4-INJECT-B` 注释)未出现在任何 git 历史提交中,
仅存在于本文档引用的 diff 片段里,供审查复核。

判别力矩阵(每个探针精确打翻了它该打翻的那一组,没有连带无关用例):

| 探针 | 注入点 | 翻红例数 / 50 | 落点 |
|---|---|---|---|
| ① blockHash 校验 | step 2 哈希比对短路 | 3 | 变异 #2 + #7(代理同桶)+ 桩层同语义 |
| ② 六项比对面 | step 5 只吞六项失配 | 7 | 变异 #8.1–#8.6 **恰好六例** + 桩层同语义 |
| ③ 错误分类 | StorageError → INVALID | 2 | 变异 #13(真桥)+ 桩层同语义 |
| ④ 块登记接线 | 跳过 `SYS_HASH_2_NUMBER` 写入 | 8 | 链式对(第二块→SYNCING,实测)+ gate 4 例 + 变异 #10/#11 + 桩层 |
| ⑤ 通用版本闸 | `maxEngineVersion` 默认值 V3→4 | 3 | `EngineVersionGate` 2 例 + 变异 #12 |

---

## 附:RTTI 旁路复扫(T4 发现的二进制级现象,对 T5b/T6 新增代码的复查)

T4 记录的现象:`libevmone.a`(`-fno-rtti`)引入了 `std::exception` 的**非唯一 typeinfo**,
使得跨该库边界传播的 `std::runtime_error` **不被** `catch (const std::exception&)` 绑定
(证据与走查见 `docs/audits/2026-07-12-typed-catch-rtti-investigation.md` 与
`OpSchedulerImpl.h` 那条 `catch (...)` 的注释)。本任务按要求复扫 T5b/T6 新增的全部 catch 点:

| 位置 | catch 形态 | 结论 |
|---|---|---|
| `bcos-evm/bcos-evm/engine/OpSchedulerImpl.h:678/684` | `catch (const std::exception&)` **+ `catch (...)` 兜底** | **已修**(commit `fe2a40c29`,统一编译验证阶段实测确认);两个 catch 体施加**相同**的 `poisoned()`-优先分类,旁路发生时结论不变、仅丢失原始 message |
| `engine/bcos-engine/EngineServiceImpl.h:759/765` | `catch (ConsensusError&)` / `catch (StorageError&)`,**无 `catch(...)`** | **无需补**,理由见下 |
| `bcos-codec/rlp/EthBlockHeader.cpp`、`OpDepositEncode.cpp`、`opstack/OpForkSchedule.cpp`、`engine/OpReceiptMap.h`、`engine/OpEngineSeam.h` | 无任何 catch | 不适用 |
| `bcos-evm/bcos-evm/ledger/Storage2Ledger.h`(既有,本闭环未新增 catch) | 4 处 `catch (const std::exception&)` **全部已配 `catch (...)`** | 桥项目已处理,复扫确认未回退 |

`EngineServiceImpl.h` 两处 typed catch 判定为**无需 `catch(...)` 兜底**,依据三条:

1. **类型精确匹配,不走基类漫游**:`OpSchedulerImpl` 用裸 `throw OpStorageError(...)` /
   `throw OpConsensusError(...)`(**不是** `BOOST_THROW_EXCEPTION`,故无 `boost::wrapexcept<>`
   包装层),engine 侧 `catch (const typename SchedulerType::StorageError&)` 是**同一类型**的
   精确匹配——不需要比对 `std::exception` 的 typeinfo,而后者正是 T4 那条旁路的唯一受害者;
2. **运行期已实证**:探针 3 在**真调度器 + 真 storage2 布局违规**下证明这条 typed catch
   确实绑上了(注入把它改成 INVALID 后测试立刻翻红,说明复绿态走的就是这个 handler);
   `OpConsensusError` 是同一头文件、同一宏、同一 throw 形态的姊妹类型,同理成立;
3. **上游已兜底**:`OpSchedulerImpl` 的 `catch (...)` 保证从 `executeOpBlock` 逃出来的异常
   **只可能**是这两个类型之一,engine 侧不存在"第三种异常悄悄溜过分类表"的通道。

**遗留的诚实交代**:真调度器路径下"`OpConsensusError` → INVALID"这一腿在 **engine 层**只有
桩用例(`EngineOpBranch.ConsensusErrorFromExecutionMapsToInvalid`,`StubOpScheduler` 直接抛)
覆盖,尚无"真 `OpSchedulerImpl` 抛 `OpConsensusError` → engine 判 INVALID"的端到端用例;
调度器层面则有 `OpSchedulerImpl.FirstTxNotAttributesDepositIsConsensusError` 真实覆盖。两段
各自成立、接缝处靠上述第 1/2 条论证,未做端到端实测——记录在案,列为可选补测。
