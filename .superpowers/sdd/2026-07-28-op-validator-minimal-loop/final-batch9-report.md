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

## 裁定与协调者补充(2026-07-30)

控制器批准 **(A) + 后置回读校验形态**,并给出一条必须先验的技术警告:`removeOne` 在
MultiLayerStorage 可变层上写的是 `DELETED_TYPE` **墓碑**而非物理擦除,若判据把墓碑当"存在",
后置校验会在**每一次正常的零值写入**上误报——"不是会响的守护,是永远在响"。要求**实测**而非推理。

**实测结论(测试 (z7),两段)**:

1. **原语段**:在 `LOGICAL_DELETION` 存储上 `setStorage` 一槽后 `removeOne`,断言
   (a) `range()` **仍能扫到**该 32 字节键(墓碑物理存在,前提成立);
   (b) `storage2::existsOne` 对**同一个键返回 false**。
   → `existsOne` 把墓碑判为不存在,后置回读判据可用,不会误报。
2. **集成段**:在同一 `LOGICAL_DELETION` 存储上跑 `applyDiff` 的零值分支,`EXPECT_NO_THROW`,
   且随后 `has_storage == false`、不毒旗。

**判据保留 `existsOne`,不改成 "`readOne` + `liveContent()`"**,理由已写进源码注释:
`liveContent()` 判别的是 `range()` 交出的**原始变体** `storage2::StorageValueType<Value>`;
`readOne`/`existsOne` 已经在层间解析里做完了同一件事(墓碑 → `nullopt`,
`MemoryStorage.h:233-240 toOptional` / `MultiLayerStorage.h:32-36,227-256`),它们的返回值里
**没有变体可判**。两者同源、不重复——"readOne + liveContent" 在类型上不可组合。

协调者点 1(`LeakyDeleteStorage` 的红是否纯):**纯**。它只在 (z6) 内局部构造;同一用例里先用
非零 diff 播种并 `ASSERT_NO_THROW`(证明 `removeOne` 空操作不干扰正常写回路径),再用零值 diff
触发守护;并附一段对照——同样两枚 diff 打在正常 `MutableStorage` 上 `EXPECT_NO_THROW`。
反证实验(见下)证明 (z6) 是**唯一**因该守护翻红的用例。

协调者点 2(#8 记账要写清同类性与正确修法):已照办,见 §6.4 条目 **u** ①。

## 第二步:实现

只动 `bcos-evm/bcos-evm/ledger/Storage2Ledger.h`(方案 (A) 的写路径改动也落在同一文件——
`applyDiff` 就在这个文件里,未触碰第二个源文件)。

| 处 | 改动 |
|---|---|
| `fetchAllStorage`(旧 `:485-494`)| 零值槽行由 `throw std::runtime_error` 改为 `continue`。注释写明:旧行为的理由只在 E-b 世界成立,而这条读路同时服务生产账本,留在读路的后果是 `stateRootOf → visitAccounts` 每个 OP 块毒旗 → 节点每块 -32603;守护没消失,搬去了 `applyModifiedEntry` |
| `probeHasStorage`(旧 `:682`)| 新增**零值层**过滤:`fieldKey.size()==32` 且 `liveContent()` 判活后,再要求 `!isZeroSlotValue(*content)` 才 `co_return true`。文档注释写明两层过滤(终审 I-1 墓碑层 + 本批零值层)是**叠加**而非替代,并写明误真的后果链是共识级(`state.cpp:259 has_initial_storage` → `host.cpp:91 is_create_collision` → CREATE2 在 op-geth 成功而在本桥 INVALID) |
| 新增 `isZeroSlotValue(std::string_view)` | 仅当内容**恰为 32 字节全零**时判真。长度不为 32 的内容**不**在此判真——那不是合法槽值,把它降级成"零/不存在"正是终审 M-1 修过的静默降级;此处保守判"有内容"(`has_storage=true`,EIP-7610 方向偏保守=拒绝在其上 CREATE),真正的长度校验与 throw+毒旗留给权威读路 `fetchStorage`/`fetchAllStorage` |
| `applyModifiedEntry` 零值分支 | `removeOne` 之后 `existsOne` 回读,行仍存活即 `throw std::runtime_error`(契约② write-back leak)。注释写明为何不是 `assert(!is_zero(value))`(同义反复、不可达、不可测)、为何判据是 `existsOne` 而不是 `liveContent()`、以及实测依据 |
| 文件头 + 三处 doc 注释 | 新增"零值槽语义"整段(统一到所有判据 / 理由是生产写路径对零值照写不删 / 守护在写路);`visitAccounts` 的 poison 触发面描述里把"stored zero-valued slot"删掉改为"长度不为 32 的槽值";`fetchAllStorage` doc 写明两层过滤叠加 |

**KEEP 契约未被改坏**(派单硬约束):"零值槽 = 槽不存在"只作用于**槽**;"账户存在但字段全默认 =
账户存在"仍由 `fetchAccount` 的 `existsOne` 判据保证返回 `Account` 而非 `nullopt`。(z2) 用
`ASSERT_TRUE(account.has_value())` + `EXPECT_FALSE(has_storage)` 同时钉住这两件事。
**墓碑过滤未被破坏**:(z5) 专测零值层与墓碑层同时生效。

## 第三步:测试(+7,全部在 `bcos-evm/test/opstack/Storage2LedgerTest.cpp`)

| 编号 | 用例 | 钉住什么 |
|---|---|---|
| (z1) | `StateRootIgnoresZeroValuedSlotRow` | **本批最核心**:含零值槽行的账户表上 `visitAccounts` 不毒旗、零值槽不进建根 map,且 `stateRootOf` 与"该槽压根不存在"的账本**逐字节相等**;附反向哨兵 `EXPECT_NE(空状态根)`,排除"两边都全废/都空"的空真 |
| (z2) | `HasStorageFalseWhenOnlyZeroValuedSlot` | 只有一个零值槽 → `has_storage == false`;同时断言账户**仍然存在**(KEEP 契约)+ 单槽读路同口径 |
| (z3) | `HasStorageTrueWithNonZeroSlotOnly` | 防改过头 |
| (z4) | `MixedZeroAndNonZeroSlots` | 混合 → `has_storage == true`,建根 map 只含非零槽,根与"只有非零槽"的账本相等。**零值行刻意排在非零行之前**(ORDERED 存储按键序扫),迫使 `probeHasStorage` 真的继续往后扫而不能第一行就判负 |
| (z5) | `ZeroValuedSlotAndTombstoneBothFiltered` | 零值层与终审 I-1 墓碑层**叠加**生效:`LOGICAL_DELETION` 存储上一槽墓碑 + 一槽存活零值 → `has_storage == false`、建根 map 空、不毒旗 |
| (z6) | `ApplyDiffZeroSlotWriteBackLeakThrows` | 方案 (A) 的守护在**写**的时候就失败:`LeakyDeleteStorage`(`removeOne` 空操作)→ `EXPECT_THROW(applyDiff, std::runtime_error)`;附正常存储的 `EXPECT_NO_THROW` 对照 |
| (z7) | `ExistsOneTreatsTombstoneAsAbsentSoZeroWriteDoesNotFalseFire` | 协调者要求的实测(见上) |

### 自验六步(反证实验:三个改动点各注入一次)

**红绿见证目录点名:in-tree `build/`。** `Storage2LedgerTest` 在 `if(TARGET bcos-framework)` 门控内,
**standalone `bcos-evm/build` 对本批全部改动是"依赖图无边"的空真**——`Storage2Ledger.h` 的
唯一非测试 includer 是 `bcos-evm/bcos-evm/engine/OpSchedulerImpl.h`,它同样在该门控内;
本轮 standalone 重配重建后二进制时间戳**停在 7-29 21:44 未变**,131/131 不作为红绿证据,
只作为"没碰到 standalone 覆盖面"的无回归检查(§11 通则 (a)(b)(c))。

| 注入 | 改回的旧判据 | 重建后翻红 |
|---|---|---|
| PROBE-1 | `fetchAllStorage` 零值行改回 `throw` | **3 例红**:(z1) / (z4) / (z5),其余 27 绿 |
| PROBE-2 | `probeHasStorage` 去掉零值层过滤 | **2 例红**:(z2) / (z5),其余 28 绿 |
| PROBE-3 | 删掉 `applyModifiedEntry` 的删槽后置回读 | **1 例红**:(z6),其余 29 绿 |
| 还原 | — | **重建 → 246/246 复绿** |

三个探针各自命中不同的用例集合,没有一个探针"全绿"(即没有哪个改动点是无覆盖的),
也没有哪个探针把整套打崩(红的原因纯)。

> **表述勘误(终审批 9 复审 + fix 轮)**:本报告与 progress.md 曾把三探针的判别力概括成
> **"互不重叠"**,这不准确。三个红集是 {z1,z4,z5} / {z2,z5} / {z6},**(z5) 同时被 PROBE-1
> 与 PROBE-2 覆盖**。准确的表述是:**三探针的红集互不相同、且各自非空;(z5) 是墓碑层与
> 零值层的联合锚,它两边都响正是它该有的样子**——(z5) 构造的就是"零值槽 + 墓碑"两种行同时
> 存在的账户表,读路 throw 复原会让它红(零值行不该 throw),probeHasStorage 去零值层也会
> 让它红(零值行不该判 has_storage)。判别力结论不变,只有"互不重叠"这四个字要改。

## 回归(全部亲跑,非推理)

| 腿 | 结果 |
|---|---|
| in-tree `build/` `bcos-evm-opstack-tests` | **246 / 246 PASSED**(基线 239 + 本批 7) |
| standalone `bcos-evm/build`(**先 `cmake -S bcos-evm -B bcos-evm/build` 重配**) | **131 / 131 PASSED**(空真,见上) |
| `test-bcos-engine` | `*** No errors detected` |
| E-b 桥回放三腿 | `EbT8nReplay.Vectors` / `MemoryLedgerT8nReplay.Vectors` / `OpT8nReplay.Vectors` **全绿**(33×3) |

**零触碰核验**:`git diff --name-only 6771dffa0..HEAD` 仅 3 个文件(报告 + `Storage2Ledger.h` +
`Storage2LedgerTest.cpp`)+ 本次文档提交;`ports/` `vectors/` `golden/` `transaction-scheduler/`
`bcos-rpc/` 零触碰。
**通用组合根零漂移**:`Storage2Ledger.h` 的非测试 includer 只有 `OpSchedulerImpl.h`,而后者的
`executeBlock`(通用路径)直接 `throw std::logic_error`,桥在通用路径上**结构性不可达**,
零漂移不是"测出来的",是依赖图上就不成立。

## 文档

- **`op-validator-minimal-loop-design.md` §6.4 新增 rev.3.5 两条**:
  - **t(与 q 同级置顶)**:把"**毒旗机制在生产账本上的触发点**"列为 op-node 实连前置清单项。
    已确认的生产触发点两个:①**非 20 字节 hex 的 `/apps/` 表名**(`addressFromTableName` 抛
    `length_error`;真实链上 `/apps/HelloWorld` 这类**合法**的 FISCO 合约表名普遍存在,实测
    `poisoned=1`)——**本期未修**,它牵涉"OP 状态根该不该、以及如何覆盖非 20 字节地址的 FISCO
    原生账户"这个尚无裁定的语义问题;②零值槽行——本批已修。并附一条通则:**任何新增的
    `poison()` 调用点都必须回答"真实链上会不会命中"**。条目里明写它此前**只存在于代码注释里**,
    五轮审查没人当接入阻塞项看,是"发现了但没传播到位"的又一例。
  - **u**:零值槽语义修复的落地范围 + 残留边界两条(`MemoryLedger` 同类缺口及其**正确修法是
    收窄 `accounts()` 可变暴露面**、而非给 `get_account` 再加一层过滤;生产写路径对零值照写不删
    **不是缺陷**,记录它是为了钉住"读路必须容忍零值行"这个前提的出处)。
- **`real-ledger-bridge-design.md` §6**:原 "零值槽毒旗的适用域(rev.2 限定)" 那条**加删除线并
  写明 rev.3 撤销理由**——该"限定"没有任何机制承载(全仓无 E-b/生产模式开关),而
  `stateRootOf` 是生产必经路径,规则等价于"接真实链后每块 -32603";改写为读路统一以太坊语义 +
  守护搬写路的完整表述。
- `bcos-evm/README.md` 未提及零值槽/`has_storage`,无需同步。

## 诚实边界(主动披露)

1. **PROBE-2 下 (z1)/(z4) 不翻红**:它们走 `visitAccounts` 而不读 `has_storage`。即
   `probeHasStorage` 的零值层由 (z2)/(z5) 独家见证,不是被四条用例重复覆盖。
2. **"每块 -32603" 未在真实链上端到端复现**:本批的证据链是"生产写路径不过滤零值(已读码确认
   `HostContext.h:288` / `Ledger.cpp:1844`)"+"零值行会让 `fetchAllStorage` throw 并被
   `visitAccounts` 接住置毒旗(`VisitAccountsUnknownKeyPoisons` 同路径实证)"+"`stateRootOf`
   必经 `visitAccounts`(`OpSchedulerImpl.h:892`)"三段推理的合成,不是一次真实节点复现。
   同理,§6.4 条目 t 里"非 20 字节表名"的 `poisoned=1` 是控制器的实测,我复核了机理未复跑。
3. **`isZeroSlotValue` 对"长度不为 32 的槽值"判"有内容"是一个有意的不对称**:`probeHasStorage`
   会给出 `has_storage=true`,而 `fetchAllStorage`/`fetchStorage` 会 throw+毒旗。两者不矛盾
   (一个偏保守、一个报错),但**这个组合没有专门用例**——`VisitAccountsUnknownKeyPoisons`
   覆盖的是"未知键长",不是"32 字节键 + 非 32 字节值"。属已知的窄覆盖缺口,不影响本批结论。
4. **`applyDiff` 的后置回读增加了一次层内查表**(仅在槽被写零时)。未做性能测量;依据是
   `removeOne` 刚在最上层写过该键,`existsOne` 命中最上层即返回、不下盘。若后续有热路径
   证据表明它不可忽略,正确的下一步是**把它折进 `removeOne` 的返回值**(让存储层告知"删掉了
   什么"),而不是把守护删掉。

---

# fix 轮(复审跟进 F-1 / F-2 / F-3 落地)

接续者报告。BASE = 复审收口的 `7b7e0afb3`,分支 `feat-op-validator-loop`,工作目录
`.claude/worktrees/ledger-bridge`(独占 `build/`)。

## 0. 前任半成品的验收(不重写)

接手时工作区里躺着前任 watchdog 停滞前留下的未提交改动:`Storage2Ledger.h`(F-1 生产侧)、
`Storage2LedgerTest.cpp`(F-1 桥层三条断言)、未跟踪的 `support/LeakyDeleteStorage.h`。

验收结论:**编过、且全绿**。二进制时间戳 20:16:54 > 源 20:16:27(前任确实建过),
`--gtest_list_tests` 246 / **246 PASSED**。F-1 改的是错误**分类**语义(毒旗置位)而非行为,
三条既有用例只是各加了两句断言,**无既有用例因语义迁移翻红**,不存在"该改断言还是改实现"
的判断题。按纪律**先把 F-1 提交掉**(`ecbfb8f`),再往下做。

唯一半途而废的一步是 `LeakyDeleteStorage.h` 抽了头但测试文件还在用文件内的匿名命名空间副本,
本轮补完(见 F-1 下半)。

## F-1 —— 写回失败的错误分类归位

### 生产侧(前任完成,本轮验收 + 注释加强)

`applyDiff` 整体 `try/catch` 包裹,四级 catch 全部 `poison(...)` 后 `throw;` 重抛。
形态是**整体包裹**而非逐 throw 点改写,把"写回路径的任何失败都是本地故障"表达成一条不变量,
新增 throw 点自动继承。

### 下半:分类层见证(本轮新增)

复审要求的是"该守护触发时得到 **-32603 而不是 INVALID**",这句话只能在 `OpSchedulerImpl`
的分类层证明——前任只在桥层断了毒旗。新增:

- `OpSchedulerImpl.ApplyDiffWriteFailureIsStorageErrorNotInvalid`(记作 **(d2)**),与既有的
  (d) `ThrowingStorageIsStorageError` 分工:**(d) 钉读路毒旗通道,(d2) 钉写路**。
- helper `expectOpStorageErrorWithMessage`,按 §11 通则配**正反两个标识**:
  - 正例 = `what()` 含桥那一层自己的判据文字(只能经 `poison → firstError → OpStorageError`
    传出;`executeOpBlock` 的 `catch(...)` 兜底拿不到 `what()`,会换成自己那句固定诊断串);
  - 反例 = `OpConsensusError` **单独 catch 并当场判失败**、连同它的 `what()` 一起报出(而不是
    让 `EXPECT_THROW` 只回一句 "wrong exception type"),外加 `what()` 不得含兜底串特征
    `"typed catch bypassed"`。
- 新增 `support/WriteFailingStorage.h`(`ThrowingStorage` 的镜像:写路抛、读路直通)。

**一条反直觉的实测(已写进 `LeakyDeleteStorage.h` 头注释,免得下一个人再试一遍)**:
桥层同一守护用的 `LeakyDeleteStorage` 在分类层**触发不了**。把它包在 `OpSchedulerImplTest`
的向量块执行上,`removeOne` **一次都没被调到**(`kVectorId` 这个块既无零值槽写入、也无账户
删除),用例只报 "none thrown"。原因是 `applyDiff` 的三条 tripwire(`/sys/` 路由、
ghost-delete、契约②泄漏)全都要求 `StateDiff` 长成特定形状,而真实块执行凑不出那个形状;
"底层存储写入失败"则对**任何**块都成立(`applyDiff` 必写 nonce/balance),是分类层唯一稳的
写路径见证。读路刻意直通,保证毒旗的**唯一来源**是 `applyDiff` 的 catch,见证归属不糊。

### 附带:前任"RTTI 阶梯"注释的独立复核(结论一致,注释按实测加强)

前任声称 catch 阶梯的顺序是 `-fno-rtti` 旁路的实测结果。本轮**重做了两个实验**:

| 实验 | 改动 | 现象 |
|---|---|---|
| INJ-F1 | 只抑制 `catch (const std::runtime_error&)` 里的 `poison` | **4 红**(桥层三条 + (d2)) ⇒ 这些 throw 全部落在**第一级** |
| INJ-G | 整条 `runtime_error` 级删掉 + 余下各级 `poison` 打可区分前缀 | 同样 4 条见证的 `firstError()` **全是 `[VIA-ELLIPSIS]`,零条 `[VIA-EXCEPTION]`** |

即:`std::runtime_error` **确实不匹配** `catch (const std::exception&)`,但匹配
`catch (const std::runtime_error&)`。**前任的注释成立**;本轮把它从"给三条 catch 打前缀"
一句话扩写成上面两个实验的完整记录,并补一条此前没说的推论:**`catch (const std::exception&)`
那一级在本构建下形同虚设**(保留是为了 RTTI 正常的工具链)。INJ-G 还顺带证明了 (d2) 的反例
标识有效——它当场抓住了"毒旗置了但消息丢了"这种退化。

提交:`ecbfb8f`(生产 + 桥层)、`add588a`(分类层 + RTTI 复核)。

## F-2 —— 把"墓碑 ≡ 不存在"钉到生产实际走的那一半

复审 INJ-E 的发现:(z7) 只钉住 `MemoryStorage::existsOne → toOptional`,而生产的
`Storage2Ledger<Storage>` 里 `Storage` 是 `MultiLayerStorage::ViewType`——`View` **没有**
`existsOne` 成员,走的是 `View::readOne → getValue<Value> → DELETED_TYPE → nullopt` 这条
**完全不同的代码**,零覆盖。

新增 **(z8)** `MlsViewExistsOneTreatsTombstoneAsAbsentSoZeroWriteDoesNotFalseFire`,与 (z7)
同构两段(原语:range 仍见墓碑 + `existsOne` 判 false;集成:view 上零值分支不抛不毒旗),
外加本文件局部的 MLS fixture(`TrivialCheckpointStorage` stub + `MlsFixture`,可变层
`LOGICAL_DELETION`,与 `OpSchedulerImplTest.cpp` / `EbT8nReplayTest.cpp` 同构)。

**自验 = 复审 INJ-E 的原样重放**(注入打在 **worktree 内**的 `MultiLayerStorage.h::View`,
给它加"省掉 Value 构造"的 `existsOne` 成员):

| | 复审当时 | 本轮 |
|---|---|---|
| INJ-E 翻红数 | **0** | **1,且只有 (z8)** |

(z8) 的**四条断言全响**:原语腿、集成腿(`applyDiff` 抛了)、`has_storage` 误真、毒旗置位。
还原 → 重建 → 复绿。缺口从"靠今天的实现巧合成立"变成"被钉住"。

提交:`6bf3478`。

## F-3① —— 非 32 字节槽**值**

复审说这条路"不对称的两侧都没用例";实测更糟:**连终审 M-1 自己修的那个 throw 也从未有过
红绿见证**。唯一沾边的 `StorageLayoutFaultIsInternalErrorNotInvalid` 注入的是 29 字节**键**,
走 `fieldKey.size() != 32` 那条**完全不同**的 throw。

新增 **(z9)** `NonThirtyTwoByteSlotValueThrowsOnReadButCountsAsContentForHasStorage`,
三条腿 + **三个独立 bridge 实例**(毒旗是黏的,共用会互相污染):

1. `get_storage` → `fetchStorage` 的 `length_error` → 毒旗 + 安全值(M-1 见证 A);
2. `visitAccounts` → `fetchAllStorage` 的 `length_error` → 毒旗 + 提前终止(见证 B,也是
   `stateRootOf` 走的那条 = "每块作废"的路);
3. `has_storage` 侧的**有意不对称**:`isZeroSlotValue` 只对"恰 32 字节全零"判真,长度违规的
   内容保守算作"有内容" → `has_storage=true` 且不毒旗。

自验 **INJ-H**(把 M-1 的两条 throw 还原成批前的静默降级 `memcpy-if-32`)→ **1 红且只有 (z9)**,
这本身就是"这条 throw 此前零覆盖"的证明 → 还原 → 重建 → 复绿。

提交:`ba34b0a`。

## F-3② —— spec §6.4 条目 t① 异常类型勘误

t① 原文写 `addressFromTableName` 抛 `std::length_error`,与它自己举的 `/apps/HelloWorld`
不符。实际分两种:

- 表名含**非 hex 字符**(`/apps/HelloWorld` 正是这种)→ 先在 `boost::algorithm::unhex` 抛
  `boost::algorithm::non_hex_input`,**根本走不到** `addressFromTableName` 自己的 throw;
- 表名全 hex 但解码后长度不是 20 字节 → 才是 `std::length_error`。

结论(毒旗、每块 -32603)不变,改的只是机制描述。顺带把 `applyDiff` `catch(...)` 兜底的
依据记进 spec,并**到 boost 源码原文核对**:
`non_hex_input : virtual hex_decode_error : virtual boost::exception, virtual std::exception`
(`boost/algorithm/hex.hpp:51-53`)——**不**派生自 `runtime_error`/`logic_error`。

提交:`a695f19`。

## 表述勘误:三探针"互不重叠"

已在本报告"反证实验"节与 `progress.md` 就地改为准确表述(红集互不相同、各自非空;
(z5) 是墓碑层 + 零值层的**联合锚**,被 PROBE-1/PROBE-2 双覆盖是它该有的样子)。

## 回归

| 腿 | 二进制时间戳 | 结果 |
|---|---|---|
| in-tree `bcos-evm-opstack-tests` | 收尾重建 | `--gtest_list_tests` **249** / **249 PASSED**(246 + (d2)/(z8)/(z9)) |
| standalone `bcos-evm/build` | 未重建 | 对本轮**空真**(新增用例全在 `if(TARGET bcos-framework)` 门控内),不作证据,口径与前几轮一致 |

所有注入(INJ-F1 / INJ-G / INJ-E / INJ-H)**全部还原 + 重建 + 复绿**,`git status` 干净。

## 诚实边界(本轮新增)

1. **(d2) 用的不是 (z6) 那条守护**。复审的原话是"该守护触发时得到 -32603 而不是 INVALID";
   本轮实测证明 (z6) 的注入在分类层不可达(见 F-1 下半),(d2) 因此换了 `applyDiff` 的另一条
   失败模式(底层写入失败)。钉住的是**分类逻辑**(`applyDiff` 抛 → 毒旗 → `OpStorageError`),
   这条逻辑对 `applyDiff` 的**所有** throw 点共用——F-1 的形态刻意是整体 try/catch 正是为此。
   但严格说:**(z6)/ghost/`/sys/` 三条 tripwire 各自在分类层仍无端到端见证**,它们的分类正确性
   是"共用同一段 catch"的推论,不是实测。要补需要能凑出特定 `StateDiff` 形状的块级注入,
   超出本轮范围。
2. **(z8) 只覆盖 MLS View 的单可变层形态**。fixture 是 `fork() + newMutable()` 的一层可变层
   + backend,没有 immutable 层叠加、没有 checkpoint 历史。`View::readOne` 的多层解析
   (mutable → immutable → cache → backend)只有第一段被走到。跨层墓碑(上层墓碑遮住下层
   实值)这一情形**未覆盖**——它是 `getValue` 的另一条分支。
3. **INJ-E 的重放我改了注入位置**:复审在主仓改,本轮在 **worktree 内**的
   `bcos-framework/.../MultiLayerStorage.h` 改(第一次误改到主仓,已 `cp` 还原并确认该文件
   `git status` 干净)。两处是同一份代码的两个 checkout,结论可比。
4. **(z9) 第 (3) 腿断言的是当下的有意取向,不是不可动摇的规范**。若将来裁定"长度违规应当在
   `has_storage` 侧也报错",这条断言就该改——注释里写明了它是"有意的不对称"而非遗漏,改它
   需要连注释一起改,这是它的全部防呆价值。
