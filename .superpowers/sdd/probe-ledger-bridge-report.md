# 真账本桥五探针留痕(spec §7,Task 7)

日期:2026-07-28
分支:`feat-evm-ledger-bridge`(基座 `feat-evm-opstack-port` PR #5361 之上,真账本桥 Task 1-6 之后)
基线:`.superpowers/sdd/task-6-report.md`(Task 6 末态,in-tree 154/154、standalone 130/130——
standalone 130 为推导值 = 154 − 24,24 是仅 in-tree 编译的三个守卫文件
`Storage2LedgerTest`/`LedgerRootTest`/`EbT8nReplayTest` 用例数之和,终审 M-3 补注;非独立
`ctest` 实测重跑值,口径同 task-7-report.md §"standalone 双路"一节)

约定:每个探针记录①注入点(diff)②翻红实际输出③回退命令④复绿输出⑤探针结束后 `git status`。
全部注入均通过 `Edit` 临时改动源码 → 编译 → 跑对应测试捕获翻红 → `rtk git checkout --` 精确
回退该文件 → 重新编译 → 再跑一次确认复绿。**探针注入代码全程未进入任何 commit**——每个探针
小节末尾的 `git status` 均为 `clean`,可逐条核对。

---

## 探针 1:毒旗探针(design §4.3)

**注入点**:`bcos-evm/bcos-evm/ledger/Storage2Ledger.h`,`get_account()` 的 `catch` 块——把
"catch 后 `poison(...)`"改成"catch 后什么也不做(吞异常不置毒旗)":

```diff
         catch (const std::exception& e)
         {
-            poison(e.what());
+            // PROBE-1-INJECT(毒旗探针,Task 7 留痕,不入 commit): 吞异常不置毒旗。
         }
         catch (...)
         {
-            poison("Storage2Ledger::get_account: unknown exception");
+            // PROBE-1-INJECT(毒旗探针,Task 7 留痕,不入 commit): 吞异常不置毒旗。
         }
```

**翻红命令**:`--gtest_filter='Storage2Ledger.PoisonOnInjectedStorageException'`

**翻红实际输出**:

```
[ RUN      ] Storage2Ledger.PoisonOnInjectedStorageException
.../Storage2LedgerTest.cpp:166: Failure
Value of: bridge.poisoned()
  Actual: false
Expected: true

.../Storage2LedgerTest.cpp:167: Failure
Value of: bridge.firstError().empty()
  Actual: true
Expected: false

[  FAILED  ] Storage2Ledger.PoisonOnInjectedStorageException (0 ms)
```

这条测试同时覆盖 design §4.3 要求的第二个断言点(`bridge.firstError().empty()` 应为
`false`);测试体内随后构造的 `freshBridge`(新桥实例)一节在本次注入下**未翻红**——这是
预期的:注入只影响被下毒实例的 catch 分支,不触碰"新实例不受影响"这条独立于 catch 逻辑的
不变式(`m_poisoned` 是实例成员,新实例默认 `false`)。该断言的通过本身就是"实例隔离"这条
契约在毒旗真正生效时(复绿态)与不生效时(本次注入态)均成立的证据——它验证的是构造函数
默认状态,不依赖 catch 分支是否置位。

**回退**:`rtk git checkout -- bcos-evm/bcos-evm/ledger/Storage2Ledger.h`

**复绿输出**:

```
[ RUN      ] Storage2Ledger.PoisonOnInjectedStorageException
[       OK ] Storage2Ledger.PoisonOnInjectedStorageException (0 ms)
[  PASSED  ] 1 test.
```

**探针后 `git status`**:`clean — nothing to commit`

---

## 探针 2:KEEP 探针(design §3/§4.4)

**注入点**:`bcos-evm/bcos-evm/ledger/Storage2Ledger.h`,`fetchAccount()` 末尾——把"存在但空"
账户折叠为 `nullopt`:

```diff
         account.has_storage = co_await probeHasStorage(tableName);
+
+        // PROBE-2-INJECT(KEEP 探针,Task 7 留痕,不入 commit): 把"存在但空"折叠为 nullopt。
+        if (account.nonce == 0 && account.balance == intx::uint256{0} && !account.has_storage &&
+            account.code_hash == evmone::keccak256(evmc::bytes_view{}))
+            co_return std::nullopt;
+
         co_return account;
```

**翻红命令**:全量 `bcos-evm-opstack-tests`(既覆盖专项消毒单测,也覆盖三腿回放,验证"或"
两支中哪一支先翻红)

**翻红实际输出**(9 个测试失败,`154 - 9 = 145 PASSED`):

```
[  FAILED  ] Storage2Ledger.ExistenceAfterCreate (0 ms)
[  FAILED  ] Storage2Ledger.EmptyAccountNormalization (0 ms)
[  FAILED  ] Storage2Ledger.PoisonOnInjectedStorageException (0 ms)
[  FAILED  ] Storage2Ledger.WriteThroughNegativeCacheThenCreate (0 ms)
[  FAILED  ] Storage2Ledger.WriteThroughPositiveCacheOverwriteAndDelete (0 ms)
[  FAILED  ] Storage2Ledger.SeedFromTestStateEmptyAccountEnsuresExists (0 ms)
[  FAILED  ] Storage2Ledger.VisitAccountsSkipsLogicallyDeletedAccountMarker (0 ms)
[  FAILED  ] Storage2Ledger.VisitAccountsUnknownKeyPoisons (0 ms)
[  FAILED  ] LedgerRoot.ThreeBackendsSameRootWithCodeAndSlots (0 ms)
[  PASSED  ] 145 tests.
```

brief 点名的核心断言(`Storage2Ledger.EmptyAccountNormalization`)实际翻红细节:

```
[ RUN      ] Storage2Ledger.EmptyAccountNormalization
.../Storage2LedgerTest.cpp:95: Failure
Value of: result.has_value()
  Actual: false
Expected: true
[  FAILED  ] Storage2Ledger.EmptyAccountNormalization (0 ms)
```

**归因说明**:三腿回放(`OpT8nReplay`/`MemoryLedgerT8nReplay`/`EbT8nReplay`,含
`empty_account_cleanup` 系向量)在本次注入下**均未翻红**——KEEP 违规折叠只发生在
`Storage2Ledger::fetchAccount`(被 `get_account`/`visitAccounts` 共用),但
`empty_account_cleanup` 向量的比对结构对这条特定的"全零折叠"分支恰好没有判别力(该向量
里被折叠的空账户在向量语义下post-state 判定路径与本注入的触发条件未重合),这印证了 design
§7 原文"预期...或消毒单测翻红"用"或"而非"且"的措辞——本探针命中的是"单测"这一支,9 个直接
翻红的用例已充分证明 KEEP 违规可被判别,不依赖回放腿。

**回退**:`rtk git checkout -- bcos-evm/bcos-evm/ledger/Storage2Ledger.h`

**复绿输出**:`154 tests from 26 test suites ran. [  PASSED  ] 154 tests.`

**探针后 `git status`**:`clean — nothing to commit`

---

## 探针 3:空账户播种探针(design §5 rev.2 ensure-exists)

**注入点**:`bcos-evm/bcos-evm/ledger/LedgerSeed.h`,`seedFromTestState()` 循环体首——跳过
全空账户(不再把它塞进 `diff.modified_accounts`,ensure-exists 契约因此不会作用于它):

```diff
     for (const auto& [addr, account] : pre)
     {
+        // PROBE-3-INJECT(空账户播种探针,Task 7 留痕,不入 commit): 跳过全空账户的 ensure-exists。
+        if (account.nonce == 0 && account.balance == intx::uint256{0} && account.code.empty() &&
+            account.storage.empty())
+            continue;
         evmone::state::StateDiff::Entry entry;
```

**翻红命令**:全量 `bcos-evm-opstack-tests`

**翻红实际输出**(2 个测试失败):

```
[  FAILED  ] Storage2Ledger.SeedFromTestStateEmptyAccountEnsuresExists (0 ms)
[  FAILED  ] LedgerRoot.ThreeBackendsSameRootWithCodeAndSlots (0 ms)
[  FAILED  ] 2 tests, listed below:
```

brief 点名的核心断言翻红细节:

```
[ RUN      ] Storage2Ledger.SeedFromTestStateEmptyAccountEnsuresExists
.../Storage2LedgerTest.cpp:423: Failure
Value of: result.has_value()
  Actual: false
Expected: true
[  FAILED  ] Storage2Ledger.SeedFromTestStateEmptyAccountEnsuresExists (0 ms)
```

`LedgerRoot.ThreeBackendsSameRootWithCodeAndSlots` 同时翻红是因为该测试的三后端同根场景
显式包含一个完全空账户(`ts[0x03_address]`,注释"KEEP 契约:存在但字段全默认")——播种跳过
后 `Storage2Ledger` 侧建根缺一个账户叶,与 TestState/MemoryLedger 两侧的根不再相等,是这条
探针的第二重、独立于单测断言的翻红信号。三腿回放(33 向量)在本次注入下未翻红——33 个向量
的 `pre` 未包含"完全空账户"这一具体形态(与探针 2 同理,向量的具体取值对这一条特定路径没有
判别力),同样落在 design §7"预期...单测翻红"这一支。

**回退**:`rtk git checkout -- bcos-evm/bcos-evm/ledger/LedgerSeed.h`

**复绿输出**:`154 tests from 26 test suites ran. [  PASSED  ] 154 tests.`

**探针后 `git status`**:`clean — nothing to commit`

---

## 探针 4:建根逐字段矩阵(design §7 探针 4,已是常驻 gtest,记录一次输出)

**说明**:`LedgerRoot.FieldMatrixEachMutationChangesRoot`(`bcos-evm/test/opstack/LedgerRootTest.cpp`
Task 5 落地)本身就是这条探针的常驻实现——基线根 vs 分别篡改 nonce/balance/code(→codeHash)/
单槽后的根,四例均断言不等于基线。无需注入,按 spec §7 原文"已常驻,记录一次输出"执行。

**输出**(`--gtest_filter='LedgerRoot.*'`,含同文件另两组测试一并跑出):

```
[ RUN      ] LedgerRoot.ThreeBackendsSameRootWithCodeAndSlots
[       OK ] LedgerRoot.ThreeBackendsSameRootWithCodeAndSlots (0 ms)
[ RUN      ] LedgerRoot.LargeAccountThousandSlotsAndLargeCode
[       OK ] LedgerRoot.LargeAccountThousandSlotsAndLargeCode (5 ms)
[ RUN      ] LedgerRoot.FieldMatrixEachMutationChangesRoot
[       OK ] LedgerRoot.FieldMatrixEachMutationChangesRoot (0 ms)
[----------] 3 tests from LedgerRoot (5 ms total)
[  PASSED  ] 3 tests.
```

无源码注入,无需回退;`git status` 全程未变(`clean`)。

---

## 探针 5:接线完整性探针(design §7 探针 5,rev.2 wiring probe)

**已有记录(审查者执行)**:`.superpowers/sdd/progress.md` 第 7 行(Task 6 完成记录)载明
"审查者实证接线探针 33 条批量翻红→复绿,证据待 Task 7 留痕"——该记录由 Task 6 的独立代码
审查流程产生。本仓未能定位到该次审查执行过程的原始终端输出文件(commit `109f158ad` 消息与
`task-6-report.md` 均未内嵌该次探针的具体 diff/红绿日志,只有 progress.md 这一行摘要性
断言),如实记载这一留痕缺口,而非虚构一份未曾亲见的终端记录。为补齐"不许无痕勾选"的纪律
(design §7 结尾),本任务在此**独立重新执行**一次同语义的注入,产出下方可核验的实际输出,
作为本探针的主证据;并按 brief 要求额外复跑一次当前态的常驻绿灯确认。

**注入点**:`bcos-evm/test/opstack/EbT8nReplayTest.cpp`,`Storage2Backend::afterVector()`——
把 `RecordProperty`/`ASSERT_GT` 读取的读写计数从真实 `CountingStorage` 计数改为硬编码 0,
复现"gate 接了一个从不触碰 storage2 的假后端"时这条常驻断言应有的可观测后果(等价于 brief
"E-b Backend 临时换 MemoryLedger"场景的可观测面——无论假后端具体是"恒 nullopt"还是
"误接 MemoryLedger",对 `Storage2Ledger` 真实 `CountingStorage` 的读写计数而言都是"零增量",
这正是 `ASSERT_GT(reads, 0U)` 要拦截的可观测信号):

```diff
-        const auto reads = ledger->counting.readCount;
-        const auto writes = ledger->counting.writeCount;
+        // PROBE-5-INJECT(接线完整性探针,Task 7 留痕,不入 commit): 模拟假后端接线——
+        // 断言读到的计数与真实桥完全脱钩(恒 0),复现"gate 换假后端"的可观测后果。
+        const std::size_t reads = 0;
+        const std::size_t writes = 0;
```

**翻红命令**:`--gtest_filter='EbT8nReplay.*'`

**翻红实际输出**(33 条独立失败,逐向量各一条,首尾各摘 2 条,`grep -c` 确认总数为 33):

```
[ RUN      ] EbT8nReplay.Vectors
=== t8n replay leg: storage2 ===
.../EbT8nReplayTest.cpp:157: Failure
Expected: (reads) > (0U), actual: 0 vs 0
isthmus_access_list: zero storage2 reads observed — Storage2Ledger not wired to the t8n replay (wiring-integrity probe, design §7 #5)

.../EbT8nReplayTest.cpp:157: Failure
Expected: (reads) > (0U), actual: 0 vs 0
isthmus_big_block_130tx: zero storage2 reads observed — Storage2Ledger not wired to the t8n replay (wiring-integrity probe, design §7 #5)

... (中间 29 条,每个向量各一条,格式相同) ...

.../EbT8nReplayTest.cpp:157: Failure
Expected: (reads) > (0U), actual: 0 vs 0
jovian_tx_reverted: zero storage2 reads observed — Storage2Ledger not wired to the t8n replay (wiring-integrity probe, design §7 #5)

[  FAILED  ] EbT8nReplay.Vectors (... ms)
```

`grep -c "zero storage2 reads observed"` 对完整输出计数 = **33**,与 33 个向量一一对应
(`ASSERT_GT` 是非致命断言语境下的 fatal-in-current-function 行为——每次失败只退出
`afterVector` 这一层,回放循环继续跑下一个向量,因而 33 次注入触发 33 次独立失败,而不是
第一个向量失败就整体中止)。

**回退**:`rtk git checkout -- bcos-evm/test/opstack/EbT8nReplayTest.cpp`

**复绿输出 + 当前态常驻断言确认**(brief 要求的"复跑一次常驻断言(EbT8nReplay 全绿 +
storage2_reads>0)作为当前态确认",用 `--gtest_output=xml` 解析确认):

```
[ RUN      ] EbT8nReplay.Vectors
=== t8n replay leg: storage2 ===
[       OK ] EbT8nReplay.Vectors (44 ms)
[  PASSED  ] 1 test.
```

XML `<properties>` 摘录(`storage2_reads`/`storage2_writes` 是每向量末覆写的属性,
`RecordProperty` 语义下最终值 = 最后一个向量 `jovian_tx_reverted` 的计数):

```xml
<property name="storage2_reads" value="188"/>
<property name="storage2_writes" value="36"/>
<property name="known_diverges" value="0"/>
```

与 `.superpowers/sdd/task-6-report.md` 记录的 `jovian_tx_reverted` 样本值(188 reads /
36 writes)完全一致,佐证 progress.md 引用的 Task 6 审查发现与本次独立复现指向同一底层
事实,尽管本次复现的注入手法(硬编码计数为 0)与 Task 6 审查者当时具体如何"注入假后端"
细节不必然逐字相同。

**探针后 `git status`**:`clean — nothing to commit`

---

## 附加项 1:漂移防线单测(Task 5 审查 Minor,已落地为非探针的常驻测试)

`bcos::evm::accountStorageRoot`(`adapter/StateRootCompute.h`,`MemoryLedger`/`Storage2Ledger`
建根引擎)与 `bcos::evm::opstack::opStorageRoot`(`opstack/OpBlockSeal.h`,OP 区块头
`withdrawalsRoot` 引擎)是design §6 头注明文的"验证过的同构造"——secure-trie、键
`keccak256(slot)`、值 `rlp(trim(value))`——但因分层单向依赖 + upstream-diff golden 锚定两条
理由,**物理重复**而非共用符号。这意味着两处未来的独立改动可能悄悄漂移出一致性而没有编译期
信号。Task 7 在 `bcos-evm/test/opstack/OpBlockSealTest.cpp` 新增
`OpBlockSeal.AccountStorageRootMatchesOpStorageRootOnSameMap`,在同一 storage map(含普通槽/
零值槽/空 map)上钉死两者逐字节相等,作为这对关系的唯一运行期兜底。该测试文件**不受**
`if(TARGET bcos-framework)` 守卫,in-tree 与 standalone 两路构建均参与编译并已验证通过
(见下"验收清单"一节)。

## 附加项 2:pushView 有意省略说明(Task 6 审查 Minor)

已在 spec `docs/superpowers/specs/2026-07-27-real-ledger-bridge-design.md` §10.1 补记:
`EbT8nReplayTest.cpp` 的每向量独立 fixture 从不调用 `MultiLayerStorage::pushView()`,原因是
该 fixture 的"每向量新建、用后即弃"隔离架构下不存在"view 内写入需要对其他 view/下一个区块
可见"这个 `pushView` 要解决的问题,不是遗漏;该边界仅对本测试成立,真实编排接入时需重新
设计。详见 spec §10.1。

---

## 汇总:全程 `git status` 核对

五个探针 + 无源码改动的探针 4,共产生 4 次"注入→翻红→回退→复绿"往返(探针 1/2/3/5),每次
往返结束后立即执行 `rtk git status`,全部返回:

```
* feat-evm-ledger-bridge
clean — nothing to commit
```

探针注入代码(标注 `PROBE-N-INJECT` 注释)未出现在任何 git 历史提交中,仅存在于本文档引用
的 diff 片段里,供审查复核。
