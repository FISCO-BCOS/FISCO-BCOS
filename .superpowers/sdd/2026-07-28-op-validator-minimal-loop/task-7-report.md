# Task 7 报告:五探针留痕 + N0 验收 + 文档回填

**状态**:完成。本任务**恢复正常编译与测试**(统一编译验证阶段已在 T6 之后完成),
全部数字为实跑输出。

分支:`feat-op-validator-loop`;探针起点 HEAD `fe2a40c29`;merge-base(相对
`feat-evm-ledger-bridge`)`42e62fcef8e19d029f5738b9ac0b32a0bddc4b31`。

一句话结论:五探针全部注入→翻红→回退→复绿并留痕,N0 三份基线合并后全过(in-tree
**206/206**、standalone **131/131**、engine Boost **11/11**),spec §8 验收清单 **11 项
逐项命令化执行、全过**,三份文档回填落地(spec 升 rev.3.1、README 增 engine 闭环状态节、
T6 遗留勘误与欠账台账入档)。RTTI 旁路复扫结论:**本闭环新增代码无需再补 `catch(...)`**。

---

## 0. 交付物清单

| 类别 | 文件 | 状态 |
|---|---|---|
| 探针留痕 | `.superpowers/sdd/probe-op-validator-gate-report.md` | 新建(20.6K) |
| N0 基线 | `.superpowers/sdd/2026-07-28-op-validator-minimal-loop/n0-intree-gtest.txt` | 新建(240 行) |
| N0 基线 | `…/n0-standalone-gtest.txt` | 新建(155 行) |
| N0 基线 | `…/n0-engine-boost.txt` | 新建(12 行) |
| 文档回填 | `docs/superpowers/specs/2026-07-28-op-validator-minimal-loop-design.md` | 改(rev.3 → rev.3.1) |
| 文档回填 | `bcos-evm/README.md` | 改(新增"engine 验证者闭环状态"节 + 双路计数更新) |
| 遗留承接 | `bcos-evm/test/opstack/EngineNewPayloadGateTest.cpp` | 改(**仅注释**:T6 §5 交叉引用勘误 + spec 修订已落) |
| 遗留承接 | `bcos-evm/test/CMakeLists.txt` | 改(**仅注释**:M5 `${CMAKE_SOURCE_DIR}` 遮蔽风险评估) |
| 本报告 | `…/task-7-report.md` | 新建 |

**生产代码零改动**:本任务未修改 `engine/` `bcos-evm/bcos-evm/` `bcos-codec/` 下任何
`.h/.cpp` 的**语义**;两处 `.cpp/.txt` 改动均为纯注释。

---

## A. 五探针留痕

留痕全文(含每个探针的注入 diff、翻红原文、回退命令、复绿输出、`git status`)见
`.superpowers/sdd/probe-op-validator-gate-report.md`。此处给判别力汇总:

| 探针 | 注入点 | 翻红例数/50 | 点名宿主用例 |
|---|---|---|---|
| ① blockHash 校验 | `EngineServiceImpl.h` step 2 哈希比对短路为 `if (false && …)` | **3** | `EngineNewPayloadMutation.TamperedBlockHashIsInvalidWithNullLatestValidHash` |
| ② 六项比对面 | step 5 判决前只 `reset()` 掉六项失配(保留 §5.1 两条额外比对) | **7** | `ComparisonSurface{ReceiptsRoot,LogsBloom,WithdrawalsRoot,StateRoot,GasUsed,TransactionsRoot}` **恰好六例** |
| ③ 错误分类 | `catch (StorageError&)` 由 throw `OpExecutionInternalError` 改为 return INVALID | **2** | `EngineNewPayloadMutation.StorageLayoutFaultIsInternalErrorNotInvalid`(真桥腿) |
| ④ 块登记接线 | `registerOpBlock` 跳过 `SYS_HASH_2_NUMBER` 写入 | **8** | `EngineNewPayloadGate.ChainedPairParentKnownThroughBlockRegistration` |
| ⑤ 通用版本闸 | 构造函数 `maxEngineVersion` 默认实参 V3 → 4 | **3** | `EngineVersionGate.GenericCompositionRootRejectsV4Unchanged` + 变异 #12 |

五次往返每次结束 `rtk git status` 均为 `clean — nothing to commit`;提交前全仓
`grep -rn "PROBE-" engine/ bcos-evm/ bcos-codec/` 为空。

### 探针执行的三处如实记载(不掩盖)

1. **探针 ④ 的落点比 spec §7.4 措辞早一步**。spec 预言"两块链式用例**第二块必转
   SYNCING**";主注入下该用例确实翻红,但第 4 步
   `ASSERT_TRUE(forkchoiceResult.payloadStatus.latestValidHash.has_value())` 是 **fatal**
   断言(A 未登记 → FCU(head=A) 返回 SYNCING → 提前终止函数),**根本没跑到第 5 步**。
   为把 spec 预言的信号实测出来,在保持主注入的前提下追加了一处**测试侧临时注入**
   (`PROBE-4-INJECT-B`,把该 fatal 断言降级为 `EXPECT_TRUE` + `if` 守卫),结果:
   `EngineNewPayloadGateTest.cpp:948` 报 `statusB.status` = `<02>`(Syncing)、
   `validationError=<none>` —— **第二块确实必转 SYNCING**,与 spec 逐字吻合。两处注入
   同批回退。
2. **探针 ⑤ 暴露一条既有测试面的判别力缺口**。同一注入下 engine 自己的 Boost 套件
   `test-bcos-engine` **11 个用例全绿**(`*** No errors detected`)——既有 engine 测试面
   **对"版本上界漂移"完全没有判别力**,它不含任何"V4 必须被拒"的断言。"通用组合根 V4 零
   漂移"这条性质**目前只由本闭环新增的 3 条用例守着**。非本次改动引入,但记录在案。
3. **探针 ① 连带打翻变异 #7 是预期且有信息量的**。#7(executionRequests 非空)因载体缺失
   用"`requestsHash` 位移后重算 hash"作代理,判决落在 blockHash 失配桶——停掉该桶它必然
   一并失守,这正是 spec §6.1 step 2 把 #7 指派进该桶的可观测证据。

---

## B. N0 相对基线与验收清单

### B.1 三份基线(捕获时点 = Task 6 结束 / 统一编译验证之后 / **本任务探针之前**)

| 路 | 命令 | 存档 | 行数 |
|---|---|---|---|
| in-tree gtest | `./build/bcos-evm/test/bcos-evm-opstack-tests --gtest_list_tests \| sort` | `…/n0-intree-gtest.txt` | 240 |
| standalone gtest | `./bcos-evm/build/test/bcos-evm-opstack-tests --gtest_list_tests \| sort` | `…/n0-standalone-gtest.txt` | 155 |
| engine Boost | `./build/engine/test/test-bcos-engine --list_content 2>&1 \| sort` | `…/n0-engine-boost.txt` | 12 |

**实测陷阱确认**:`--list_content` 的输出确实走 **stderr**——不带 `2>&1` 直接重定向
stdout 会得到空文件。已按 brief 用 `2>&1` 捕获,产出 11 个用例名 + 1 个套件名共 12 行。

**关于 `| sort` 的一处口径说明**:`--gtest_list_tests` 的输出是"套件名顶格 + 用例名缩进"
两层结构,`sort` 会把全部缩进行排到顶格行之前,**打散套件与用例的从属关系**。三份存档按
spec §8 字面口径保留 `sort` 形态(作为集合比对基线,判"有无增删改名"完全够用);本报告中
需要套件归属的分析(下面的新增名单)另用**未排序**输出与**源码 `TEST(...)` 扫描**做,
两条独立路径互证。

### B.2 探针全部回退后的重捕获 vs 基线对照

```
diff <(重跑 --gtest_list_tests | sort) n0-intree-gtest.txt      -> IDENTICAL
diff <(重跑 standalone 同法)          n0-standalone-gtest.txt   -> IDENTICAL
diff <(重跑 --list_content 2>&1|sort) n0-engine-boost.txt       -> IDENTICAL
```

三份**逐字节相同**——探针注入全部回退干净,且本任务的注释改动未增删任何用例。

合并后全过:in-tree **206/206**、standalone **131/131**、engine Boost **11/11**。

### B.3 新增名单(相对 merge-base `42e62fcef`)

方法:对 merge-base 与 HEAD 两个 tree 的 `bcos-evm/test/**/*.cpp` 做 `TEST(...)` /
`TEST_F(...)` 正则扫描后取差集(比"重建 base 分支再列测试"便宜且精确)。结果:

- merge-base **156** → HEAD **206**,**新增 50、删除 0、改名 0**;
- 与二进制实测一致:in-tree 206 = 156 + 50;
- **standalone 名单与 merge-base 逐条相同(131 不变)**——50 例全部落在
  `if(TARGET bcos-framework)` 守卫块内,即 `Types.h` 等通用件改动**零外溢**
  (spec §9 风险表"Types.h 通用件改动外溢 → N0 基线回归兜底"这一条的兑现证据)。

50 例逐条:

- `EngineNewPayloadGate` (5): AllThirtyThreeGoldenVectors, ChainedPairParentKnownThroughBlockRegistration, IsthmusDepositOnly, IsthmusSingleTransfer, JovianMultiTransfer
- `EngineNewPayloadMutation` (18): ComparisonSurfaceGasUsed, ComparisonSurfaceLogsBloom, ComparisonSurfaceReceiptsRoot, ComparisonSurfaceStateRoot, ComparisonSurfaceTransactionsRoot, ComparisonSurfaceWithdrawalsRoot, ForkchoiceWithAttributesRefusedButHeadStillAdvances, GenericCompositionRootStillRejectsV4, NonEmptyExecutionRequestsLandsInBlockHashBucket, NonEmptyExpectedBlobVersionedHashesIsInvalid, NonEmptyWithdrawalsIsInvalid, NonZeroBlobGasUsedIsInvalidUnderIsthmus, NonZeroExcessBlobGasIsInvalid, SamePayloadResubmittedAfterParentBecomesKnown, StorageLayoutFaultIsInternalErrorNotInvalid, TamperedBlockHashIsInvalidWithNullLatestValidHash, TimestampVersionGateRejectsMismatch, UnknownParentIsSyncing
- `EngineOpBranch` (12): BlockHashMismatchIsInvalidWithNullLatestValidHash, ConsensusErrorFromExecutionMapsToInvalid, EachComparisonSurfaceFieldMismatchIsNamed, ForkchoiceWithAttributesRefusedButHeadStillAdvances, GenericCompositionRootStillRejectsV4, GetPayloadIsRefusedInOpMode, NonConsecutiveBlockNumberIsInvalid, NonZeroBlobGasUsedIsInvalidUnderIsthmus, StorageErrorFromExecutionIsInternalErrorNotInvalid, TimestampVersionGateRejectsMismatch, UnknownParentIsSyncingAndWritesNothing, ValidPayloadRegistersAllFourTables
- `EngineVersionGate` (2): GenericCompositionRootRejectsV4Unchanged, OpCompositionRootNotRejectedByVersionGate
- `EthBlockHeader` (4): AllThirtyThreeGoldenVectors, DepositOnlyIsthmus, IsthmusSingleTxTransferBasic, JovianMultiTxTransferMulti
- `OpDepositEncode` (4): AllDepositTransactionsAcrossThirtyThreeGoldenVectors, ContractCreationDepositNilTo, DepositWithMintAndValue, IsthmusTransferBasicDepositEnvelope
- `OpSchedulerImpl` (5): ConfigAtThresholds, ExecuteBlockThrowsInOpMode, ExecuteOpBlockSixWayComparisonSurface, FirstTxNotAttributesDepositIsConsensusError, ThrowingStorageIsStorageError

### B.4 spec §8 验收清单逐项(11 项,命令 + 输出)

| # | 条目 | 命令 | 输出 | 判 |
|---|---|---|---|---|
| 1 | 金向量 gate 33/33 | `--gtest_filter='EngineNewPayloadGate.*'` | `5 tests from 1 test suite ran. [ PASSED ] 5 tests.` | ✅ |
| 1b | 33 向量交叉断言逐条留痕 | 同上 + `--gtest_output=xml`,数 `v_<id>_*` 属性 | `status 33 / block_hash 33 / tx_root 33 / tx_count 33`;失配才输出的 `v_<id>_hdr_*` 属性 **0 条** | ✅ |
| 2 | 两块链式 + 未知 parent→SYNCING | `ChainedPairParentKnownThroughBlockRegistration` | OK(该用例内含"B 先投→SYNCING、A→VALID、FCU、B 再投→VALID、两高度头 RLP 都在"六步) | ✅ |
| 3 | 变异矩阵 13 类 18 例 | `--gtest_filter='EngineNewPayloadMutation.*'` | `18 tests … [ PASSED ] 18 tests.` | ✅ |
| 4 | 金值单测 | `--gtest_filter='EthBlockHeader.*:OpDepositEncode.*'` | `8 tests from 2 test suites ran. [ PASSED ] 8 tests.` | ✅ |
| 5 | 通用组合根 V4 零漂移 | `--gtest_filter='EngineVersionGate.*'` + 变异 #12 + 探针⑤ | `2 tests … [ PASSED ] 2 tests.`;探针⑤反证 3 例翻红 | ✅ |
| 6 | 基线零回归(N0 三份) | 见 B.1/B.2 | 三份 diff IDENTICAL;206/206 + 131/131 + 11/11 | ✅ |
| 7 | 五探针留痕在案 | — | `.superpowers/sdd/probe-op-validator-gate-report.md`(五节齐全) | ✅ |
| 8 | ports/vectors/transaction-scheduler 零 diff | `git diff --stat 42e62fcef -- ports/ bcos-evm/test/opstack/t8n/vectors/ transaction-scheduler/` | **空** | ✅ |
| 9 | 桥 spec §10.1 + `Storage2Ledger.h` 头注条件式许可(T1 义务) | `git diff 42e62fcef -- docs/…/2026-07-27-real-ledger-bridge-design.md bcos-evm/bcos-evm/ledger/Storage2Ledger.h` | 两处均已改写为"嵌套拓扑声明 / 安全前提 / 失效判据"三条式 | ✅ |
| 10 | RPC 端点整体豁免 | `git diff --stat 42e62fcef -- bcos-rpc/` | **空**(`EngineEndpoint.{h,cpp}` 零改动) | ✅ |
| 11 | 库目标纯净 | `grep -rl "nlohmann\|gtest" bcos-evm/bcos-evm/ \| grep -v statetest.hpp`;`bcos-codec/bcos-codec/rlp/`、`engine/bcos-engine/` 同法 | 三处均**空** | ✅ |

补充跑到的两个整体口径(brief 的"当前基线"复核):
`--gtest_filter='Engine*:OpSchedulerImpl*:EthBlockHeader*:OpDepositEncode*'` → **50/50**;
全量 opstack in-tree → **206/206**。均与 brief 给出的基线一致,**零回归**。

---

## C. 文档回填(三份)

### C.1 spec 升 rev.3.1(`2026-07-28-op-validator-minimal-loop-design.md`)

四处改动,**rev.3 原文其余部分逐字保留**,文件头新增 rev.3.1 说明段落列明这四处:

1. **§7.3 latestValidHash 断言口径修订(T6 遗留裁定 I2 落地)**。原文
   "非 blockHash 桶的 INVALID 用例同时断言 `latestValidHash==parentHash`(仅 blockHash
   失配桶恒断言 null)" → 改为:非 blockHash 桶**且已过 parentKnown(§6.1 step 3)**的
   INVALID 才断言 parentHash(即 #8.1–#8.6 与父子块号连续性一类);**静态校验阶段
   (§6.1 step 2)被拒的全部用例统一断言 null**,含 blockHash 失配桶(#2/#7)与 #3–#6。
   并写明修订理由(step 2 尚未查过 parent,声称"最近有效祖先 = parent"是未经验证的断言;
   Engine API 允许 null)与来龙去脉出处。
2. **§8 第三条同口径修订** + 全表逐项打钩 + 实测值回填(见 C.4)。
3. **§6.1 新增 step 3b:父子块号连续性校验**(T5b 实现已有、rev.3 漏载)。载明判据
   `payload.blockNumber == *parentBlockNumber + 1`、分档(INVALID + latestValidHash =
   parentHash,归"已过 parentKnown"档),以及**为什么它不是可选加分项**:step 6 的两张
   登记索引都**以块号为键**,缺此校验时"parent 合法但 blockNumber 任意"的 payload 会静默
   覆写既有高度的索引条目——后果是**链索引损坏**,而非仅仅放行一个坏块。
4. **§6.4 欠账台账追加 5 条**(新增子表 a–e,rev.3 原文清单原样保留在上):
   - **a. `mergeBackStorage()` 永不调用**:每接受一块只 `pushView` → 不可变层**无界增长**
     + 读放大**随已接受块数线性增长**;最小闭环下是伸缩性问题,生产接入前必须解决
     (承 T5b 审查 I3);
   - **b. `SYS_CURRENT_STATE` current number 随 FCU head 推进写入**(裁定 A4,与 a 同属
     编排层,一并落地);
   - **c. `executionRequests` 校验未实现**:`NewPayloadRequest` 没有该成员,§6.1 step 2 的
     "在场且空"约束当前**真空成立**——不是"检查通过",是"没有可检查的载体";以
     `static_assert(!requires(...){ r.executionRequests; })` 钉住,载体一旦加入立刻翻红;
   - **d. RPC 端点注册 + "错误码"口径澄清(承 T6 审查 M4)**:-38005/-38003/-32603
     **是意图文档,不是线上 JSON-RPC 码**,异常类型→错误码的映射在本仓尚未实现;测试断言的
     是**异常类型**,不得被读作"错误码已验证";
   - **e. Holocene EIP-1559 baseFee 父子一致性校验**(裁定 A7,并列备查)。

### C.2 `bcos-evm/README.md`

- 模块列表新增 `engine/` 条目(`OpSchedulerImpl.h` / `OpReceiptMap.h` / `OpEngineSeam.h`,
  构建边界同 `Storage2Ledger.h`);
- **新增"engine 验证者闭环状态(op-validator-loop,2026-07-29)"一节**,分两半:
  - **已交付**:OP 分支七步、执行经真桥、判据是**离线 op-geth 金值**(不是自算自验)、
    实测数字(33/33、13 类 18 例、50/50、206/206、131/131、11/11)、通用组合根零漂移
    及其探针反证;
  - **明确不构成的宣称**(措辞严格守 spec §10 边界 + §6.4 台账,逐条列出):
    **不构成与真实 op-node 互操作可用**、RPC 端点整体豁免(`bcos-rpc`/`EngineEndpoint`
    零改动,gate 与单测**直调** `EngineServiceImpl`)、错误码是意图文档非线上码、
    attributes 构块未做、链头进度表不写、`mergeBackStorage` 永不调用、
    `executionRequests` 校验真空成立、extraData 形状校验与 Holocene baseFee 校验未做、
    SYNCING 完整语义/JWT/重组窗口/增量 stateRoot 均未做。
- 双路计数更新:in-tree 156 → **206**、standalone **131 不变**、差值 25 → **75**,并
  说明新增 5 个测试文件落在同一 `if(TARGET bcos-framework)` 守卫块的理由;
- "边界"一节补一段:本闭环**不解除任何一条 park**,并指向新增的状态节与 §6.4 台账。

### C.3 一并落地的两处**纯注释**遗留承接

- `EngineNewPayloadGateTest.cpp`(latestValidHash discipline 注释块):把"spec 修订待
  T7"改为"**修订已落(spec rev.3.1)**,本段保留作 provenance 而非活跃 caveat",并附
  **T6 §5 交叉引用勘误**——原指针 `task-6-report.md §5` 有误,该内容在 **§4 前言与
  §8b(I1/I2 行)**,§5 是无关的三条偏离台账(承 progress.md"Task 6: minor (deferred):
  报告 §5 交叉引用应为 §4/§8b")。
- `bcos-evm/test/CMakeLists.txt`:补 **M5 `${CMAKE_SOURCE_DIR}` 遮蔽风险**的评估结论
  (见 D.3)。

### C.4 spec §8 实测回填样式

清单 11 项全部由 `- [ ]` 改为 `- [x]`,并在条目内嵌入实测值,例如:

```
- [x] 基线零回归:… 三份存档 `.superpowers/sdd/2026-07-28-op-validator-minimal-loop/n0-*.txt`,
      合并后全过;新增名单入报告 —— **实测:in-tree 206/206、standalone 131/131、
      engine Boost 11/11;本闭环新增 50 例(相对 merge-base 42e62fcef 的 156 例),
      零删除、零改名;standalone 名单与 merge-base 逐条相同,即通用件改动零外溢**
```

另新增第 11 项"库目标纯净"(brief 要求的 rg 项,rev.3 清单原本未列)。

---

## D. 遗留承接

### D.1 T6 报告 §5 交叉引用勘误 —— **已处理**

见 C.3。同时核对了 T6 §5 自身引用的三处源码位点,**全部仍然准确**(行号未漂移):
`EngineServiceImpl.h:802-808`(txRoot 第六项比对)、`Storage2Ledger.h:467-471`
(`fetchAllStorage` 的布局违规 throw)、`OpSchedulerImpl.h:539-559`(duck-typing seam)。

顺带发现一处**生产代码的消息文本瑕疵**(不影响行为,未改):`Storage2Ledger.h:469` 的
异常消息写作 `"Storage2Ledger::visitAccounts: unknown key in account table …"`,但该
throw 实际位于 `fetchAllStorage`(:446)体内——诊断信息会指向错误的函数名。列为可选清理项。

### D.2 M3 / M4 —— **已处理(记账式)**

- **M3 FCU 腿归属**:变异矩阵 #11(`ForkchoiceWithAttributesRefusedButHeadStillAdvances`)
  测的是 **forkchoiceUpdated** 语义,却挂在 `EngineNewPayloadMutation` 套件下。这是
  spec §7.3 表格自身的编排(#11 就写在 newPayload 变异矩阵里),**实现与测试都没有错**,
  是**分类学上的名不副实**。判定:**不改**——改套件名会打散 §7.3 与测试的逐行同构关系,
  代价大于收益;记录在案。
- **M4 "错误码"实为异常类型而非线上 JSON-RPC 码**:已**升格为 spec §6.4 欠账台账第 d 条**
  并写进 README 的"不构成的宣称"清单(见 C.1/C.2)——从报告里的一句注脚变成两份长期文档
  里的显式条目。

### D.3 M5 `${CMAKE_SOURCE_DIR}` 头文件遮蔽风险 —— **已评估并记档**

风险面实测:仓库根目录 44 个顶层条目中,与标准/常用头**同名**的只有一个 —— `concepts/`
(与 C++20 标准头 `<concepts>` 同名)。判定**无害**,三条依据写进了 CMakeLists 注释:

1. 预处理器的 include 搜索**跳过目录项**继续查找,`<concepts>` 仍解析到标准库;
2. 全量 in-tree 目标构建干净、**206/206** 全绿即实证;
3. 暴露面有界——这三条路径是**该测试目标 PRIVATE**,不向外传播。

同时写明补救方向:一旦将来有顶层**文件**与头名冲突,修法是把这三条**收窄到实际需要的
子目录**,而不是放宽守卫。

### D.4 RTTI 旁路扫描 —— **已扫,结论:无需再补**

背景(T4):`libevmone.a`(`-fno-rtti`)引入 `std::exception` 的**非唯一 typeinfo**,
跨该库边界传播的 `std::runtime_error` **不被** `catch (const std::exception&)` 绑定
(`docs/audits/2026-07-12-typed-catch-rtti-investigation.md`)。逐文件扫描 T5b/T6 新增
与相关的全部 catch 点:

| 位置 | 形态 | 结论 |
|---|---|---|
| `bcos-evm/bcos-evm/engine/OpSchedulerImpl.h:678/684` | `catch (const std::exception&)` **+ `catch (...)` 兜底** | **已修**(`fe2a40c29`,统一编译验证阶段)。两个 catch 体施加**相同**的 `poisoned()`-优先分类,旁路发生时结论不变、仅丢失原始 message |
| `engine/bcos-engine/EngineServiceImpl.h:759/765` | `catch (ConsensusError&)` / `catch (StorageError&)`,**无 `catch(...)`** | **无需补**,三条依据见下 |
| `bcos-codec/rlp/EthBlockHeader.cpp`、`OpDepositEncode.cpp`、`opstack/OpForkSchedule.cpp`、`engine/OpReceiptMap.h`、`engine/OpEngineSeam.h` | 无任何 catch | 不适用 |
| `bcos-evm/bcos-evm/ledger/Storage2Ledger.h`(既有) | 4 处 `catch (const std::exception&)` **全部已配 `catch (...)`** | 桥项目已处理,复扫确认未回退 |

engine 两处 typed catch 判**无需兜底**的依据:

1. **类型精确匹配,不走基类漫游**。`OpConsensusError`/`OpStorageError` 定义为
   `struct X : std::runtime_error`(`OpSchedulerImpl.h:76/84`),且以**裸 `throw`** 抛出
   (**不是** `BOOST_THROW_EXCEPTION`,故无 `boost::wrapexcept<>` 包装层);engine 侧
   `catch (const typename SchedulerType::StorageError&)` 是**同一类型**的精确匹配——不需要
   比对 `std::exception` 的 typeinfo,而后者正是 T4 那条旁路的唯一受害者。
2. **运行期已实证**。探针 ③ 在**真调度器 + 真 storage2 布局违规行**下证明这条 typed catch
   确实绑上了(把它改成 INVALID 后测试立刻翻红 → 复绿态走的就是这个 handler)。
   `OpConsensusError` 是同头文件、同宏、同 throw 形态的姊妹类型,同理成立。
3. **上游已兜底**。`OpSchedulerImpl` 的 `catch (...)` 保证从 `executeOpBlock` 逃出来的异常
   **只可能**是这两个类型之一,engine 侧不存在"第三种异常悄悄溜过分类表"的通道。

**因此本轮无代码修复,也就无"修完跑测试"这一步**——替代验证是探针 ③ 的红/绿两态实测。

**遗留的诚实交代**:真调度器路径下"`OpConsensusError` → engine 判 INVALID"这一腿,在
**engine 层**只有桩用例(`EngineOpBranch.ConsensusErrorFromExecutionMapsToInvalid`,
`StubOpScheduler` 直抛)覆盖;调度器层面则有
`OpSchedulerImpl.FirstTxNotAttributesDepositIsConsensusError` 真实覆盖。两段各自成立、
接缝处靠上述第 1/2 条论证,**未做端到端实测**——列为可选补测(与 D.1 的消息文本瑕疵、
D.2 的 M3 归属并列)。

---

## E. 提交与留痕纪律

- **探针注入代码零入库**:五次往返每次以 `rtk git checkout -- <精确路径>` 回退并贴
  `git status`;提交前全仓 `grep -rn "PROBE-"` 为空。
- **零触碰面复核**(提交前实测):`ports/`、`bcos-evm/test/opstack/t8n/vectors/`、
  `transaction-scheduler/`、`bcos-rpc/` 相对 merge-base 的 `git diff --stat` **全空**;
  `golden/` 本任务未触碰。
- **`git add` 精确路径**(不用 `git add .`)。实测口径修正一条:brief 提示"spec/README 受
  `info/exclude` 影响需 `add -f`",但 `git check-ignore -v` 实测
  `docs/superpowers/specs/…`、`bcos-evm/README.md`、`bcos-evm/test/CMakeLists.txt`
  **均未被忽略**,普通 `add` 即可。真正被忽略的是 `.superpowers/sdd/`
  (`.superpowers/sdd/.gitignore` 内容为 `*`)。
- **`.superpowers/sdd/` 产物的入库决定(请控制器复核)**:spec §8 明文要求 N0 三份
  "**存档** `.superpowers/sdd/n0-*.txt`"、五探针留痕"**在案**",且 spec/README 现在按路径
  引用探针报告——未入库的引用会是悬空的。故本任务对 **N0 三份 + 探针报告 + 本报告**
  用 `git add -f` 入库。**这与桥项目先例不同**(`probe-ledger-bridge-report.md` 当时未提交),
  但与本仓已有的 `task-5a-report.md` 被强制入库一致。如控制器认为该目录应保持纯 scratch,
  单独 revert 这几个路径即可,不影响其余改动。

---

## F. 未尽事项 / 建议(全部为可选,不阻塞验收)

1. `Storage2Ledger.h:469` 异常消息误写函数名(`visitAccounts` → 应为 `fetchAllStorage`);
2. "真 `OpSchedulerImpl` 抛 `OpConsensusError` → engine 判 INVALID"缺端到端用例;
3. 既有 engine Boost 套件对"版本上界漂移"无判别力(探针 ⑤ 发现),若要加固可在
   `engine/test` 侧补一条"通用组合根拒 V4"的常驻断言;
4. 变异矩阵 #11 的 FCU 腿挂在 `EngineNewPayloadMutation` 套件下(名不副实,已裁定不改);
5. spec §6.4 台账 a/b 两条(`mergeBackStorage` + `SYS_CURRENT_STATE`)必须与编排层接入
   **同批**落地,不宜再拆。
