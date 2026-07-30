# 终审批 9 报告:账本桥零值槽语义统一

分支 `feat-op-validator-loop`,工作目录 `.claude/worktrees/ledger-bridge`。派单:`final-batch9-brief.md`(rev.2)。

---

## 第一步:三问的答案(先交、等裁定)

### 问 1:`applyDiff` 写槽的全部代码路径,有没有绕过它的写入来源?

**桥自己的世界(`bcos-evm/` 非测试代码)里,写槽点恰好只有两个,都在 `applyDiff` 的子协程内:**

| file:line | 操作 | 说明 |
|---|---|---|
| `bcos-evm/bcos-evm/ledger/Storage2Ledger.h:314-315` | `storage2::removeOne(StateKeyView{tableName, keyView})` | 契约②零值分支 = 删槽 |
| `bcos-evm/bcos-evm/ledger/Storage2Ledger.h:319` | `account.setStorage(key, value)`(`EVMAccount::setStorage` → `bcos-framework/.../EVMAccount.h:213` → `storage2::writeOne`) | 非零分支 |
| `bcos-evm/bcos-evm/ledger/Storage2Ledger.h:370-372` | `removeOne` 扫删账户表全部字段行(含存量槽行) | `applyDeletedEntry` 契约① |
| `bcos-evm/bcos-evm/ledger/Storage2Ledger.h:374-375` | `removeOne` SYS_TABLES 标记行 | 同上 |

验证方式:`grep -rn "setStorage\|storage2::writeOne\|storage2::removeOne\|writeSome" bcos-evm/ --include='*.h' --include='*.cpp' | grep -v '^bcos-evm/test/'` → **只命中 `Storage2Ledger.h` 上面这几行**。播种路径也不例外:`bcos-evm/bcos-evm/ledger/LedgerSeed.h::seedFromTestState` 刻意合成一枚创世 `StateDiff` 再走 `applyDiff`,不自己写存储。

**绕过 `applyDiff` 的写入来源确实存在,但全部在桥的世界之外(即桥只读不写的那些账本):**

1. `transaction-executor/bcos-transaction-executor/vm/HostContext.h:288`
   `set(key, value)` → `m_recipientAccount.setStorage(*key, *value)` —— **零值照写,不删**。这正是 `Storage2Ledger.h:486-489` 注释预言的那条。
2. `bcos-ledger/bcos-ledger/Ledger.cpp:1844`(genesis alloc 的 `importAccount.storage` 逐对 `setStorage`)与 `:1873`(L2 systemConfig feature-flags 槽)—— **创世导入不过滤零值**,alloc 里写一个 `"0x00…00"` 就落一行零值槽。
3. `bcos-executor/src/vm/EVMHostInterface.cpp:81 setStorage` —— 旧执行器同样把零值当普通写。
4. 测试 fixture 直接写(`Storage2LedgerTest.cpp:142/242/290/594-600` 经 `EVMAccount::setStorage` 或 `storage2::writeOne`)—— 有意为之,是构造违规布局的唯一手段。

**结论:方案 (A) 成立。** 那条不变量的主体("**桥的写回**从不留下零值槽行")在语义上只覆盖 `applyDiff`,而 `applyDiff` 是桥世界里唯一的写槽者;1-3 号来源根本不经过桥的写回,它们产生的零值行恰恰是读路必须容忍(而非报错)的那批数据。把检查放在写路径,守护范围与不变量的语义范围**首次对齐**;放在读路径反而是"下游替上游背锅",而下游同时还要服务不遵守该契约的生产账本。

### 问 2:那条不变量今天有测试覆盖吗?——**有,但是间接的,而且本批的读路修复会把它抹掉**

- **没有任何测试覆盖 `fetchAllStorage:485` 的那条 throw 本身。** 全仓 grep `write-back leak` / `zero-valued` 只命中源码注释,没有一条测试构造"零值槽行 → 期待 poison"。也就是说:**直接删掉这条 throw,不会有任何测试翻红**——它是一条零覆盖的运行时守护。
- **但契约②"写零 = 删槽"有一条间接覆盖**:`Storage2LedgerTest.cpp:235 WriteThroughPositiveCacheOverwriteAndDelete`,在 `applyDiff` 写零值后断言 `EXPECT_FALSE(accountAfterDelete->has_storage)`(:274)。今天这条断言之所以有力,**恰恰是因为 `probeHasStorage` 不判零值**:如果 `applyDiff` 漏写成一行零值槽,`has_storage` 会是 `true`,测试翻红。
- **这就是本批最要紧的一条发现**:派单第二步要求把 `probeHasStorage` 也改成"零值 = 不存在"。改完之后,**即使 `applyDiff` 真的写了一行零值槽,`has_storage` 依然是 `false`,:274 那条断言照样通过**。也就是说——

  > 只做"读路跳过"(无论是 (A) 的读路部分、还是 (B)/(C) 在生产模式下的行为),会**同时**抹掉两处守护:零覆盖的 throw + 唯一那条间接测试覆盖。**守护净减少是确定会发生的,不是风险,除非写路径补上一条真的会响的检查。**

- 因此 (A) 的写路径检查**不能是形式上的断言**。`applyModifiedEntry:308-321` 现在的结构是 `if (is_zero) removeOne; else setStorage;`,在 `setStorage` 前写一句 `assert(!is_zero(value))` 是**同义反复**:该分支结构上不可能带着零值走到 `setStorage`,断言永不可能触发,也**永远无法被测试**(六步自验里"改回旧判据 → 翻红"根本无从下手)。它对未来重构有防呆价值,但它不是"守护",测不出来的守护等于没有。

  **真正会响、且可测的形态是把检查做成"删槽的后置校验"**:零值分支 `removeOne` 之后,回读一次该键(`storage2::existsOne`),行若仍然存活就 `throw`。理由:
  - 它检查的是**结果**而不是**意图**——`removeOne` 有没有真的生效、底层有没有把它降级成一次写入,都会被抓到;
  - 它**可达、可测**:仿现有 `support/ThrowingStorage.h` 装饰器写一个 `removeOne` 为空操作的 `LeakyDeleteStorage`,`applyDiff` 立刻在**写**的时候 throw,派单第三步"写回路径写零值槽 → 断言在写的时候就失败"这一条因此能真正落地(而不是写一条永不执行的断言);
  - 代价可接受:`removeOne` 的墓碑/擦除落在最上层可变层,紧随其后的 `existsOne` 是一次层内内存查表,不下盘;且只在"槽被写零"时发生。
  - 无生产误报风险:检查只针对**本次 diff 自己写零的那些键**,与账本里既有的零值行无关(那些行归读路跳过)。

  这仍然是 (A)(检查在写路径、读路只实现以太坊语义、不引入模式状态),只是把"断言"具体化为"后置回读校验"。**若控制器认为这已超出 (A) 的授权形态,请在裁定里明确;我按裁定实现。**

- 关于 (B):代价除了派单已列的,还有一条——**桥没有地方能安全判定"我现在是哪个模式"**。全仓无 `ProductionMode`/`strictInvariant` 类开关(已复核为空),新开关的唯一真值来源只能是调用方手填,而调用方(`OpSchedulerImpl::executeOpBlock`)对"这条链的账户表是谁写的"并无知识。
- 关于 (C):`assert` 在 Release 下消失,而生产就是 Release;更糟的是它连"改回旧判据 → 翻红"的自验都做不到(测试构建里 throw 变 abort,`EXPECT_THROW` 抓不到)。不推荐。

### 问 3:除 `fetchAllStorage` / `probeHasStorage`,还有第三、第四处零值判据吗?

盘了全部"行存在 ⇒ 有内容"与"零值特殊处理"的点:

| # | 位置 | 对零值槽的口径 | 与"零值 = 不存在"是否一致 | 处置 |
|---|---|---|---|---|
| 1 | `Storage2Ledger.h:485-494 fetchAllStorage` | **throw**(poison) | ❌ 反向 | 本批改为跳过 |
| 2 | `Storage2Ledger.h:682 probeHasStorage` | 不看值,**判 true** | ❌ 反向 | 本批改为跳过 |
| 3 | `Storage2Ledger.h:708-728 fetchStorage` / `get_storage` | 行不存在 → 全零;行存在且值为零 → 全零 | ✅ **已经一致**(两种情况对调用方不可区分,正是以太坊语义) | **不动** |
| 4 | `applyModifiedEntry:308-321`(写路)| 零值 → `removeOne`,不写零行 | ✅ 一致(它就是那条不变量) | (A) 在此加后置校验 |
| 5 | `applyModifiedEntry:329-333`(槽缓存写穿)| 零值 → 缓存全零 `bytes32` | ✅ 与 #3 一致 | 不动 |
| 6 | `adapter/StateRootCompute.cpp:25-26 accountStorageRoot` | `is_zero` → `continue`,不进 trie | ✅ 一致 | 不动(**这就是"零值槽不进 trie"的实现依据**,控制器结论复核通过) |
| 7 | `opstack/OpBlockSeal.cpp:21-22 opStorageRoot` | 同 #6 | ✅ 一致 | 不动(受 upstream-diff golden 追踪,零触碰) |
| 8 | `ledger/MemoryLedger.cpp:19 get_account` | `has_storage = !storage.empty()` | ⚠️ **口径依赖 map 里没有零值**(`applyDiff` 保证:`MemoryLedger.cpp:56-59` 零值 `erase`);但 `MemoryLedger::accounts()` 是 public 可变引用("for test/seed callers"),越过 `applyDiff` 塞一个零值槽 → `has_storage` 误真,与 #2 同类缺口 | 见下 |
| 9 | `bcos-ledger/GenesisStateRoot.h:39-40` | 注释明确"零值槽是 no-op,跳过" | ✅ 一致 | 不动 |

**没有第五处。** `has_storage` 的下游消费链只有一条,已复核到底:`eth/state/state_view.hpp:25 Account::has_storage` → `eth/state/state.cpp:259 .has_initial_storage = cacc->has_storage` → `eth/state/account.hpp:48` → `eth/state/host.cpp:91 if (acc.has_initial_storage)`(`is_create_collision`,EIP-7610)。控制器结论(#2 误真 → CREATE2 在本桥 INVALID 而在 op-geth 成功)复核成立。

**#8 的处置建议:不在本批修。** 理由:(a) 派单限定"只动 `Storage2Ledger.h`";(b) `MemoryLedger` 是 E-b 内存后端,不在生产账本路径上,它的 `accounts()` 缺口只能由测试代码自己触发,不构成接入阻塞;(c) 若要收口,正确做法是收窄 `accounts()` 的可变暴露面,那是独立的一改。**已作为残留边界记入 spec §6.4。**

### 附:`visitAccounts:256` 的 `catch (const std::exception&)` 是否受 `-fno-rtti` typed-catch 旁路影响?

**不受影响,零值槽 throw 今天确实会 poison,控制器的"每个 OP 块 -32603"成立;而且即使受影响也一样 poison。** 两条独立依据:

1. **实证**:`Storage2LedgerTest.cpp:618 VisitAccountsUnknownKeyPoisons` 走的是**同一条 throw 路径**(`fetchAllStorage` 抛 `std::runtime_error`,穿过 `visitAccountsImpl` 的协程与 `syncWait`,由 `visitAccounts:256` 接住),断言 `EXPECT_TRUE(bridge.poisoned())` 且今天是绿的。
2. **机理**:该旁路的成因是 throw 点与 catch 点分处不同的编译单元/库(其中一侧是 `-fno-rtti` 的 `libevmone.a`,`std::exception` 的 typeinfo 非唯一)。`fetchAllStorage` 与 `visitAccounts` 都是**同一个头文件里的模板**,在**同一个 TU** 内被实例化,typeinfo 同一份,匹配成立。`OpSchedulerImpl.h:846-869` 那处 `catch(...)` 兜底之所以必要,是因为它接的是 `processOpBlock`——跨库,情形不同,两者不矛盾。
3. 兜底:即便 (1)(2) 都不成立,`visitAccounts:260-263` 的 `catch (...)` 一样 `poison()`。**poison 触发面不因 RTTI 旁路而缩小**,控制器的结论无需修正。

---

## 第二步/第三步:实现与测试

(裁定后填写)
