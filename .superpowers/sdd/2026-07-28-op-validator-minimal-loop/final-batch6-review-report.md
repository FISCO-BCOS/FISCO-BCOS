# 终审批 6 独立复审报告:`SYS_HASH_2_TX` 补写(裁定 B —— OP 专用表 `s_eth_hash_2_rawtx`)

复审者:独立复审代理。工作目录 `/Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/ledger-bridge`,
分支 `feat-op-validator-loop`。审查范围 `ca28d9114..7b7e0afb3`(两提交,7 文件,+226/-18)。
复审期间 HEAD 被协调者推进到 `2dfe9a13e`(纯 sdd 台账,不含代码/文档改动),不影响本次审查结论。

**结论:ACCEPT。** 实现正确,断言有判别力(两次注入均按预期翻红),文档所有可核实的行号与
论断我逐条到源码复核后全部为真,边界零越界,全量回归零退化。发现 6 条 Minor、0 条
Critical/Important。

---

## 一、实现正确性(复审重点 1)

### 1.1 五张表的写入

`registerOpBlock`(`engine/bcos-engine/EngineServiceImpl.h:1141-1253`)现有且仅有 5 次
`storage2::writeOne`,与文档宣称的"五张表"一致:

| # | 表 | 键 | 值 | 行 |
|---|---|---|---|---|
| 1 | `SYS_NUMBER_2_HASH` | 块号十进制串 | blockHash 原 32 字节 | 1148-1150 |
| 2 | `SYS_HASH_2_NUMBER` | blockHash 原 32 字节 | 块号十进制串 | 1154-1157 |
| 3 | `c_ethBlockHeaderTable`(`s_eth_block_header`) | 块号十进制串 | `ethHeader.encode()` | 1165-1167 |
| 4 | `SYS_HASH_2_RECEIPT` | `toView(txHash)` | `receipt->encode()` | 1234-1237 |
| 5 | **`c_ethRawTxTable`(`s_eth_hash_2_rawtx`)** | **`toView(txHash)`(同一变量)** | `rawTransactions[index]` 原字节 | 1246-1251 |

**键的逐笔一致性:结构性成立,不是"测出来碰巧一致"。** 第 4、5 两次写入共用同一个局部
`const auto txHash = hashImpl.hash(rawTransactions[index]);`(:1230),因此**不存在**两表键漂移
的可能路径——除非有人删掉复用。这比"两处各算一次再靠测试比对"强一格,值得肯定。
`txHash = keccak(raw envelope)` 的前提是 OP 模式下 `hashImpl` 为 keccak256,该依赖既有注释点明
(:1202-1205),且回执表本来就依赖同一前提,不是本批新增风险。

### 1.2 失败语义:全有或全无,已核实

`view` 是 `handleOpNewPayload` 的**局部** fork(`:796` `auto view = m_globalStateStorage.get().fork();`,
`:967` `view.newMutable()`),五次写入全部落在这一层可变层;发布点是 `registerOpBlock` **返回之后**
的 `m_globalStateStorage.get().pushView(std::move(view))`(`:1117`)。因此任一 `writeOne` 抛出 →
异常沿 `co_await` 逃出 `registerOpBlock` → `pushView` 不执行 → 整个 view 析构 → **块完全不被登记**。
不存在"半登记"窗口。第 5 次写入与前四次处在完全相同的失败语义下,符合派单要求。

### 1.3 零漂移与签名污染(复审重点 3-③,独立复核)

```
$ git diff -w --numstat ca28d9114..7b7e0afb3 -- engine/bcos-engine/EngineServiceImpl.h
42	10	engine/bcos-engine/EngineServiceImpl.h
$ git diff ... | grep '^-' | grep -v '^---' | grep -vE '^-\s*//'
(空,退出码 1)
```

**实施者自陈成立**:10 行删除全部是 `//` 注释行,非注释删除为 0。唯一可执行改动是
`registerOpBlock` 循环内新增的一次写入。

**OP 依赖名未进签名**:新名 `SchedulerType::c_ethRawTxTable` 只出现在 `registerOpBlock` 的
**函数体**(:1250),而 `registerOpBlock` 本身是 `template <class OpExecuteResult>`、其调用点在
`if constexpr (c_opMode)` 之内,故通用组合根不会实例化该体。**已用构建独立证伪风险**:
`build/engine/test/test-bcos-engine` 用 `StubScheduler` / `BloomScheduler`(均**无**
`c_ethRawTxTable` 成员)实例化 `EngineServiceImpl`,重建通过、`*** No errors detected` ——
若该名进了任何被通用实例化的签名,这个目标会硬失败。T5b 的坑没有重踩。

---

## 二、断言判别力:两次独立注入反证(复审重点 2)

两次注入均由我本人执行,**每次都做满六个动作**(注释/反转 → 重建 → 翻红 → 还原 → 重建 → 复绿),
in-tree 与 standalone 各走一遍。

### 注入 1:禁用新增写入

把 `EngineServiceImpl.h:1246-1251` 的六行整体注释掉 → `cmake --build build --target
bcos-evm-opstack-tests` 重建 → 运行:

```
[  FAILED  ] EngineOpBranch.RawTransactionEnvelopesAreRegisteredUnderEthTxHash
[==========] 239 tests ... [  PASSED  ] 238 tests.  [  FAILED  ] 1 test
```

**翻红,且红的正是预期用例,且只有它。** standalone 同步重建 → 131/131 恒绿(见 §三)。
还原 → 两目录各重建 → in-tree 239/239、standalone 131/131,`git status --porcelain` 空。

### 注入 2:反转反例断言对应的生产行为

在新写入之后额外插入一段"顺手也写通用表"的 `writeOne(..., ledger::SYS_HASH_2_TX, ...)` →
重建 → 运行:

```
EngineOpBranchTest.cpp:1659: Failure
Value of: readEntry(..., bcos::ledger::SYS_HASH_2_TX, toView(txHash)).has_value()
  Actual: true   Expected: false
  (deposit 0x7E / eip1559 0x02 / setcode 0x04 三条 SCOPED_TRACE 各一次)
[  PASSED  ] 238 tests.  [  FAILED  ] 1 test
```

**第 5 条反例断言精确翻红,三种类型各一次;断言 1-4 保持绿**(说明该断言确实独立承重,不是
被别的断言顺带覆盖)。全量跑下来**只有该用例红** —— 即:今天全仓**没有第二条**测试会阻止
"顺手往通用表写一份"的退化,这条反例断言是唯一的护栏。它的存在因此不是冗余,是必需。
还原 → 两目录各重建 → 全绿,`git status --porcelain` 空。

### 断言集本身的评价

五条断言的分工清楚,第 3 条("经生产解码器 `decodeOneRawTx` 解回正确形状")确实提供了
字节相等之外的**语义**判别力,写法正确。一条改进意见见 §五 M-4。

---

## 三、实施者三条自陈的独立核实(复审重点 3)

### ③ 已在 §1.3 核实(成立)

### ① "standalone 腿不是红绿见证" —— 成立,而且比实施者写的更弱

门控确实存在:`bcos-evm/test/CMakeLists.txt:69` `if(TARGET bcos-framework)`,其中列入
`opstack/EngineOpBranchTest.cpp`(:80)、`EngineNewPayloadGateTest.cpp`(:81)、
`OpSchedulerImplTest.cpp`(:75)、`EngineVersionGateTest.cpp`(:79)。standalone
`bcos-evm/build` 的测试目标只有 24 个 TU 目标文件,其中**不含**上述任何一个。

我进一步核到一条实施者没写、但让结论更强的事实:

```
$ grep -rln "OpSchedulerImpl.h\|OpEngineSeam.h" bcos-evm/ --include='*.cpp' --include='*.h'
bcos-evm/bcos-evm/engine/OpEngineSeam.h
bcos-evm/bcos-evm/engine/OpSchedulerImpl.h
bcos-evm/test/opstack/EngineOpBranchTest.cpp      ← 门控内
bcos-evm/test/opstack/EngineNewPayloadGateTest.cpp ← 门控内
bcos-evm/test/opstack/OpSchedulerImplTest.cpp      ← 门控内
bcos-evm/test/opstack/EngineVersionGateTest.cpp    ← 门控内
```

**本批改动的两个 bcos-evm 头文件,其全部 includer 都在门控之内。** 佐证:standalone 二进制
时间戳 `2026-07-29 21:44:09` 至今**未变**,而两个头文件的 mtime 是 `2026-07-29 22:53:08`——
我三轮注入/还原重建期间 standalone 一次都没有重编译。这**不是**假绿(我特意按派单的手法查过:
不是 CMake 未 reconfigure,而是标准依赖图上就没有边),但它意味着 standalone 的 131/131 对本批
是**空真**的:它连"改动没碰到 standalone 覆盖的层"都不是在检验,因为 standalone 编译单元里
根本没有任何一行会因本批改动而重编。

**因此实施者的措辞"它在这里只是无回归检查"仍然偏宽,应改为"对本批无编译依赖,131/131 既非
红绿见证亦非有效无回归检查"。** 这是 Minor(自陈精度),不是失实。

**对"补进 §11"的意见:值得采纳,但要比实施者提的写法再收一格。** 建议条款:

> 多构建目录仓库的自验三步仍须每个目录各做一次(不放宽)。**但报告必须点名哪个目录是红绿
> 见证**,以及其余目录属于哪一档:(a) 编译到了改动但未翻红 = 有效的无回归检查;
> (b) **改动的 TU 依赖图对该目录为空 = 空真,不得计入证据**(判据:该目录的二进制在注入重建
> 后时间戳未变 / 无 TU include 被改文件)。**任何一批必须至少有一个目录构成红绿见证**;若
> 一个都没有,自验不成立。

不加最后一句的话,存在一个真实的失败模式:有人只在"看不到该测试"的目录跑注入,报"注入后
依然全绿",读者误读为"没有回归"。这正是本批 standalone 腿的形状,只是实施者主动披露了。

### ② 命名债 `ValidPayloadRegistersAllFourTables` —— 取舍**不成立**,但危害已被注释兜住

实施者的理由(改名会断掉批 3 报告"按名引用"的可追溯性)对称地成立于另一侧:批 3 报告是**已
归档的历史文档**,重定向的成本是**一次性一行注记**;而一个事实错误的用例名的成本由**此后每
一个读者**支付——尤其这个名字用了 "All" 这种穷尽性措辞(登记现写五张表,它只断四张)。
用一次性成本换持续成本,方向反了。

**但我不把它记为 Important**:实施者在用例上方与文件头注释里明确写了"名字过时、覆盖不缺、
第五张由 `RawTransactionEnvelopes...` 覆盖",误导窗口实际上已经关掉了。
**建议(Minor,M-1)**:下次自然触碰该文件时改名为 `ValidPayloadRegistersBlockTables`,并在
本批报告追加一行 "批 3 的 `ValidPayloadRegistersAllFourTables` 现名 `...BlockTables`"。

---

## 四、文档准确性(复审重点 4):逐条到源码核对

**spec §6.4 (f) 改写后的措辞与代码一致**(五张表 ✓、新表键值定义 ✓、"通用表仍不写" ✓、
诚实边界 ✓)。以下是我实际打开源码核对的每一条行号与论断:

| 论断 | 引用 | 核对结果 |
|---|---|---|
| storage2 重载未判 `has_value()` 即解引用(条目 r) | `LedgerMethods.h:233-235` | **属实,行号精确**。`:228` `readSome` 返回 `optional<Entry>` 序列,`:233` `for (auto& txEntry : transactions)`,`:235` `auto field = txEntry->get();` —— 无 `has_value()` |
| 通用表读侧 `createTransaction(...,false,false,false)` | `Ledger.cpp:1440-1443` | **属实,行号精确**。三个 `false` 逐字对上 |
| 解码 lambda 无 try/catch(条目 s 前半) | `Ledger.cpp:1417-1465` | **属实**。lambda 起于 `:1417`、止于 `:1465`,体内无 try |
| 成功回调在 `try` 内 → 双回调(条目 s 后半) | `RocksDBStorage.cpp:228-233` | **属实,行号精确**。`:228` `_callback(nullptr, std::move(entries));` 在 try 内,`:230-233` `catch(const std::exception&)` 再调一次 `_callback` |
| 同一 coroutine handle `resume()` 两次 | `LedgerMethods.cpp:558-568` | **属实**。`await_suspend` 的回调体末尾 `handle.resume();`,回调被调两次即 resume 两次 |
| `bcos-rpc` 把 eth RLP 喂 tars(条目 q ②) | `EngineEndpoint.cpp:164` | **属实,行号精确**。文件实际路径为 `bcos-rpc/bcos-rpc/web3jsonrpc/endpoints/EngineEndpoint.cpp`,`:164` `payload.transactions.push_back(transactionFactory.decodeTransaction(ref(txData)));` |
| `rawTransactions` 在任何生产路径上从未被赋值(条目 q ①) | 全仓 | **属实**。全仓非测试出现点仅 `Types.h:109`(声明)、`EngineServiceImpl.cpp:200/205`(判缺参)、`EngineServiceImpl.h:771/990/1206/1213/1220/1230/1247`(**全部是读**)、`OpDepositEncode.h:63`(注释)。**无任何赋值点** |
| tars 每字段 optional | `tars/Transaction.tars` | **属实**。`TransactionData` 1-15、`Transaction` 1-12 全部 `optional`;无 `sourceHash`/`mint`/`authorizationList` 字段 |
| tars 标签扫描空 catch | `/usr/local/include/tup/Tars.h:328-356` | **属实,行号精确**。`TarsSkipToTag` 宏尾 `catch (TarsDecodeException& e) { }` 空体 |
| 唯一映射器硬拒 `0x04`/`0x7E` | `Web3Transaction.cpp:408-413` | **属实,行号精确**。`magic_enum::enum_cast<TransactionType>(firstByte)`,失败即 `UnsupportedTransactionType` |
| `Transaction::verify` 无条件 ecrecover + forceSender | `Transaction.cpp:102-118` | **属实**。Web3 分支取 `extraTransactionBytes` 的 keccak → `recoverAddress` → `forceSender(sender)` |
| `BaselineScheduler.h:742` 只传 `HEADER` | 同上 | **属实**(调用跨 `:742-743`) |
| 通用表消费者名单(lightnode / storage-tool / archive-tool) | 全仓 | **属实**。`legacy/bcos-ledger/LedgerImpl.h:191`、`tools/storage-tool/storageTool.cpp:355`、`tools/archive-tool/archiveTool.cpp:340/409/464`、`ArchiveService.h:230` |

**q/r/s 三条均属实,无灌水。** 条目 q 置顶(k/l 之前)的理由"入口不通,后面判决语义再对也无从
发生"我认同:①的"生产上零赋值点"是我独立复核的结果,这确实使整条 OP newPayload 通路在生产
上不可达,优先级高于表级检索面。

**README 与 spec 内容一致**,无相互矛盾。

---

## 五、发现(按 Critical / Important / Minor 分级)

**Critical:0。Important:0。**

### Minor

**M-1 命名债**(详见 §3-②):`ValidPayloadRegistersAllFourTables` 名字已事实错误。建议下次触碰
该文件时改名 + 报告加一行重定向注记。已有注释兜底,不阻塞。

**M-2 注释里的方位词写反**:`EngineServiceImpl.h:1243-1244`
"see the comment **below** and `OpEngineSeam.h`'s `SYS_ETH_HASH_2_RAWTX`" —— 被指向的长论证段
在 `:1173-1200`,即**上方**;循环之后到函数结束(`:1252-1253`)没有任何注释。应为 "above"。

**M-3 standalone 腿的自陈仍偏宽**(详见 §3-①):应从"无回归检查"降为"对本批无编译依赖,
131/131 为空真,不构成任何一档证据"。同时建议按 §3-① 给出的写法把该区分写进 §11。

**M-4 断言 3 对 `0x02` / `0x04` 不作区分**:
`EXPECT_EQ(std::holds_alternative<DepositTx>(decoded.tx), index == 0)` 对 index 1 与 2 是**同一个
谓词**("不是 deposit"),因此第 3 条断言对这两种类型只证明了"解得出且非 deposit"。
`decodeOneRawTx`(`OpSchedulerImpl.h:660-676`)返回的 typed 变体带 `.type` 字段,本可再断
`eip1559` vs `set_code`。今天字节相等断言(第 2 条)已把类型字节钉死,故只是判别力略有余量
未用尽,不是缺陷。

**M-5 表名常量的再发布未被任何测试锚定**:`OpSchedulerImpl.h:717` 的
`c_ethRawTxTable = SYS_ETH_HASH_2_RAWTX` 若被改成别的字面量,**没有任何测试会红**——新用例走
`StubOpScheduler`,而该替身(`EngineOpBranchTest.cpp:515`)是从 `SYS_ETH_HASH_2_RAWTX` **同一个
常量**再发布的,断言与替身同源。这与既有 `c_ethBlockHeaderTable` 的形状完全一致(`:514`),
属沿袭而非新引入。若要闭合,最小代价是在 `EngineNewPayloadGateTest`(它的
`TxRootDriftScheduler:789` 是从**真** `OpScheduler::c_ethRawTxTable` 再发布的)加一条读该表的
断言。

**M-6 OP 专用表不在创世表清单里**(既有,本批扩大一格):`Ledger.cpp:2001-2012` 的
`constexpr static auto tables` 枚举了通用系统表,**不含** `s_eth_block_header`,现在也不含
`s_eth_hash_2_rawtx`。功能上今天不出问题——写入路径 `RocksDBStorage` 的
`isValid`(`bcos-storage/Common.h:35-43`)只检查表名非空,storage2 读写不要求预建表。但
`storageTool` 的表清单/表大小统计(`storageTool.cpp:125/756`)、`archiveTool` 的归档与回灌都是
按枚举表名工作的,**两张 OP 表对这些工具不可见**。这是 `s_eth_block_header` 落地时就存在的
缺口,批 6 只是又加了一张;spec §6.4 台账与本批报告**都没有记它**。建议补一条记账(不必本批
实现)。

**M-7 日期标注两处不一致**(琐碎):spec §6.4 (f) 与 rev.3.4 小节头写 "2026-07-29 终审批 6 落地",
README 对应段写 "2026-07-30 终审批 6 实现"。源文件 mtime 为 `2026-07-29 22:53`、提交时间为
`2026-07-30 09:28`,两者各有依据,但同一批的两份文档应统一口径。

---

## 六、边界遵守(复审重点 5)

`git diff --stat ca28d9114..7b7e0afb3` 全部 7 个文件:

```
bcos-evm/README.md
bcos-evm/bcos-evm/engine/OpEngineSeam.h
bcos-evm/bcos-evm/engine/OpSchedulerImpl.h
bcos-evm/test/opstack/EngineNewPayloadGateTest.cpp
bcos-evm/test/opstack/EngineOpBranchTest.cpp
docs/superpowers/specs/2026-07-28-op-validator-minimal-loop-design.md
engine/bcos-engine/EngineServiceImpl.h
```

`ports/`、`vectors/`、`golden/`、`transaction-scheduler/`、`bcos-rpc/` **零触碰** ✓;
`bcos-framework/.../ledger/LedgerTypeDef.h` **未改** ✓(仅被引用,`:99` 的 `SYS_HASH_2_TX` 原样)。
表名常量按裁定 B5 落在 `bcos-evm/bcos-evm/engine/OpEngineSeam.h:74`,与 `SYS_ETH_BLOCK_HEADER`
(`:51`)同处 ✓。

---

## 七、全量回归(复审重点 6)

全部为我本人在**还原注入后重新构建**的二进制上跑出:

| 腿 | 构建 | 结果 |
|---|---|---|
| in-tree `bcos-evm-opstack-tests` | `cmake --build build`(二进制 `16:02:14` 重建) | **239 / 239 PASSED** |
| standalone `bcos-evm/build` | `cmake --build bcos-evm/build` | **131 / 131 PASSED**(见 §3-① 的空真限定) |
| `test-bcos-engine` | `cmake --build build`(二进制 `16:02:42` 重建) | **`*** No errors detected`** |
| 桥 E-b 三腿 | in-tree | `EbT8nReplay.Vectors` / `MemoryLedgerT8nReplay.Vectors` / `OpT8nReplay.Vectors` **全绿** |
| `engine` 生产库 | `cmake --build build --target engine` | **Built target engine** |

**假绿排查**(派单点名的手法,已执行):
- 二进制时间戳:in-tree 与 engine 测试二进制均在我的重建后刷新(`16:02`),非陈旧产物;
- `--gtest_list_tests` 确认 `EngineOpBranch.RawTransactionEnvelopesAreRegisteredUnderEthTxHash`
  确实在二进制内(列表第 245 行);
- standalone 二进制时间戳未变,已单独查明原因为**依赖图上无边**(§3-①),非 CMake 未 reconfigure。

**收尾状态:`git status --porcelain` 为空,两次注入实验全部还原,两个构建目录均已重建并复绿。**
无任何残留。

---

## 八、给控制者的三条建议(不阻塞合入)

1. **采纳 §3-① 的 §11 修订**,尤其是"每批必须至少有一个目录构成红绿见证"与"依赖图为空的
   目录不得计入证据"两句;
2. **补一条 §6.4 记账(M-6)**:两张 OP 专用表不在 `Ledger.cpp:2001-2012` 的创世表清单里,
   `storageTool`/`archiveTool` 对它们不可见——既有缺口,批 6 扩大一格;
3. **条目 q 的置顶我背书**,并补一个我复核出的加强事实:`rawTransactions` 在全仓**没有任何
   生产赋值点**(不止"RPC 层从未赋值"),即这条通路在生产上是**完全断开**的,不是"接错了"。
