# 终审批 9 复审报告(接续者 / 第二任复审者)

工作目录 `/Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/ledger-bridge`,分支 `feat-op-validator-loop`,BASE `dbc2a3e2f`。

被审对象:`3d96d0665`(三问初稿)+ `900ee0e22`(修复 + 6 例)+ `6f4359ac4`(z7 墓碑实测)+ `f735b07a9`(文档)。

> **派单文件缺失说明**:`final-batch9-review-brief.md` **在本 worktree 中不存在**(`find` 全仓无此文件;`dbc2a3e2f` 只提交了 `progress.md` + `review-batch9.diff`,复审派单只以 progress.md 的一段台账 + 协调者转述的 7 点存在)。本报告按协调者转述的 7 点执行,并以 `final-batch9-brief.md`(实施派单)+ `final-batch9-report.md`(实施者报告)+ `progress.md` 收口台账为事实基准。**这本身是一条记账缺口:复审派单未落盘,断流后无法复原。**

## 基线(接手时)

| 项 | 值 |
|---|---|
| `build/bcos-evm/test/bcos-evm-opstack-tests` | 246 例 `--gtest_list_tests` / 246 PASSED,二进制时间戳 **19:37:10** |
| `git status` | clean |

前一任复审者已完成 INJ-1~4,只有最后一句结论存活(其余记录随断流丢失):

> INJ-4 confirms the gap: with a real leak and no guard, the only red test is the new z6 — all 239 pre-existing tests pass.

即「真泄漏 + 删掉写路后置守护」下 239 既有例全绿、唯一红是 z6。本报告**采信不重做**,它等价于三探针中的第三条(删写路后置回读 → 1 红)。

---

## 注入实验(六步自验:改 → 重建 → 看现象 → 还原 → 重建 → 复绿)

### INJ-A:读路 `fetchAllStorage` 零值 throw 复原(批前状态)

改动:`Storage2Ledger.h:543` `if (evmc::is_zero(slotValue)) continue;` → 复原为批前的 `throw std::runtime_error(...)`。

- 重建:19:42:05(时间戳前移,非假绿)
- 现象:**246 跑 / 243 过 / 3 红**,红的正是
  - `Storage2Ledger.StateRootIgnoresZeroValuedSlotRow` (z1)
  - `Storage2Ledger.MixedZeroAndNonZeroSlots` (z4)
  - `Storage2Ledger.ZeroValuedSlotAndTombstoneBothFiltered` (z5)
- 还原 + 重建(19:43:09)+ 复绿 **246/246**,`git status` clean

**结论:与实施者报的「读路 throw 复原 → 3 红」逐例一致(不只是数量一致,集合也一致)。**

### INJ-B:`probeHasStorage` 去掉零值层(回退到批前判据)

改动:`Storage2Ledger.h:739-745` 的两层判据改回批前单层 `if (fieldKey.size() == kStorageSlotKeySize && liveContent(rawValue).has_value()) co_return true;`。

- 重建:19:44:46
- 现象:**246 跑 / 244 过 / 2 红**
  - `Storage2Ledger.HasStorageFalseWhenOnlyZeroValuedSlot` (z2)
  - `Storage2Ledger.ZeroValuedSlotAndTombstoneBothFiltered` (z5)
- 还原 + 重建(19:45:46)+ 复绿 **246/246**,clean

**结论:与实施者报的「2 红」一致。** 附带核实两件事:
- z4(混合)对 INJ-B **无判别力**且这是对的——混合表里还有非零槽,`has_storage` 无论有无零值层都是 true;z4 的 has_storage 断言是"防改过头"方向,判别力由 `stateRootOf` 那半边提供(它在 INJ-A 下翻红)。
- z3(只有非零槽 → true)在 INJ-B 下**不红**,同样正确:它是防过修的正向锚,不该对"少了一层过滤"敏感。
- 故 z2/z5 是零值层的**唯一**红绿见证;三探针红集 {z1,z4,z5} / {z2,z5} / {z6} 交集只在 z5 重叠一处,与实施者所称"互不重叠"略有出入(z5 同时被 INJ-A 与 INJ-B 覆盖),但方向上更好:z5 是两层过滤的联合锚,本应两边都响。

### INJ-C:删掉写路后置回读守护(第三探针,独立复核前一任的 INJ-4)

改动:`Storage2Ledger.h:343-344` 的 `if (co_await storage2::existsOne(...))` 改为 `if (false)`(保留 throw 分支体,避免编译告警干扰)。

- 重建:19:48:05
- 现象:**246 跑 / 245 过 / 1 红** —— `Storage2Ledger.ApplyDiffZeroSlotWriteBackLeakThrows` (z6)
- 还原 + 重建(19:48:54)+ 复绿 **246/246**,clean

**结论:与实施者报的「1 红」一致,也与前一任 INJ-4「唯一红是 z6,239 既有例全绿」在同一方向上独立复现。** 三探针的翻红用例集合全部核实,**实施者的判别力自陈成立**。

---

## 派单第 3 点(必做):`removeOne` 墓碑警告的实测复核 + 漂移是否被钉住

### (3a) 实施者否决控制器警告 —— **成立**,已独立实测

代码路径读通了:`storage2::existsOne`(`Storage.h:352-370`)优先取存储的 `existsOne` 成员,否则回退 `readOne` + `static_cast<bool>`。测试用的 `LogicalDeleteStorage` 是 `MemoryStorage`,其 `existsOne`(`MemoryStorage.h:346-351`)是 `toOptional(readOneRaw(...)).has_value()`,而 `toOptional`(`:233-240`)只对 `Value` 变体返回值 → `DELETED_TYPE` → `nullopt` → **false**。z7 段一把这条**当场测掉**而不是推理,且它在基线里是绿的。**控制器的警告被正确否决,实施者的"要实测"这一步做对了。**

`readOne + liveContent` "类型上不可组合" 也复核成立:`liveContent()`(`Storage2Ledger.h:424-440`)判的是 `storage2::StorageValueType<Value>` 变体,只有 `range()` 交出这种原始变体;`readOne` 的返回是 `std::optional<Value>`,里面没有变体可判。

### (3b) 关键后续问题:**这个依赖是被钉住的,还是靠今天的实现巧合成立?**

答案分两半,**一半钉住、一半没钉住**,这是本次复审的主要新发现。

**INJ-D:漂移打在 `MemoryStorage::existsOne`(测试双替身走的那条路)**

改动:`MemoryStorage.h:346-351` 改成 `co_return !std::holds_alternative<NOT_EXISTS_TYPE>(raw);`(即"行物理存在"语义,墓碑判存在 —— 正是控制器警告的那个世界)。

- 重建 19:50:53;现象:**246 跑 / 245 过 / 1 红 = z7**,且 z7 的**三条断言同时响**:
  - `:1026` 段一原语断言(`existsOne` 应判 false,实际 true)
  - `:1040` 段二集成断言(合法零值写入下 `applyDiff` **抛了** —— 控制器警告的"守护永远在响"当场实现)
  - `:1044` `has_storage` 连带误真
- 还原 + 重建(19:52:31)+ 复绿 246/246

**→ 在 `MemoryStorage` 这条路上,判据被 z7 钉得很牢**,而且钉的不是"某个实现细节",而是漂移导致的**后果**(守护误报)。这比只断言原语更好。

**INJ-E:漂移打在生产路径 —— `MultiLayerStorage::View`**

生产 `Storage2Ledger<Storage>` 的 `Storage` 是 `MultiLayerStorage::ViewType`(`OpSchedulerImpl.h:818`)。`View` **没有** `existsOne` 成员,所以走 `storage2::existsOne` 的回退:`View::readOne`(`MultiLayerStorage.h:300-330`)→ `getValue<Value>`(`:30-39`)→ `DELETED_TYPE` → `optional{}` → false。**判据在生产路径上成立,但走的是与 z7 所钉完全不同的一段代码。**

改动(一个非常可能真实发生的漂移:有人给 `View` 加一个"省掉 Value 构造"的 `existsOne` 成员):

```cpp
auto existsOne(const auto& key) -> task::Task<bool>
{ co_return !std::holds_alternative<storage2::NOT_EXISTS_TYPE>(co_await readOneRaw(key)); }
```

- 重建 19:53:22;现象:**246/246 全绿 —— 零红。**
- **有效性对照 INJ-E′**(防空实验):把同一个成员改成无条件 `co_return true`,重建 19:54:23 → **12 红**(`OpSchedulerImpl.ExecuteOpBlockSixWayComparisonSurface`、`EngineNewPayloadGate.*` 全部、`EngineNewPayloadMutation.*` 多条)。**证明该成员确实被 `storage2::existsOne` 选中、确实走到桥上,INJ-E 不是空实验。**
- 还原 + 重建(19:55:21)+ 复绿 246/246,clean

**→ 结论(Important 级):判据在生产路径上是"靠今天的实现巧合成立"的。**
- z7 只钉住了 `MemoryStorage::toOptional` 这一半;
- 生产真正依赖的 `MultiLayerStorage::View::readOne → getValue` 这一半 **没有任何测试钉住**:它一漂移,写路守护就会在**每一次合法的零值写入上抛**(生产语义是"块执行直接失败"),而全套 246 例仍然全绿。
- 之所以 INJ-E 零红而不是"E-b 三腿翻红",是因为**没有任何测试在 MLS View 之上走过桥的零值写分支**——`EbT8nReplayTest` 虽然是 `Storage2Ledger<CountingStorage<MLS view>>`,但它的 diff 里显然不含零值槽写入。这既解释了零红,也指出了补法。

**建议(不强制本批修)**:给 z7 补一条 MLS View 之上的孪生用例(或把 z7 参数化到两种存储),让"墓碑 ≡ 不存在"这条**跨层判据**在生产实际走的那条路上也有红绿见证。成本很低(EbT8nReplayTest 已有 MLS fixture 先例),收益是把一条"生产会每块失败"的隐患从"靠巧合"变成"被钉住"。

---

## 派单第 1 点:语义正确性

逐条到源码核对,**四处判据确实同口径**:

| 判据 | 位置 | 零值行为 | 核实 |
|---|---|---|---|
| 建根(通用) | `adapter/StateRootCompute.cpp:25-26` | `is_zero → continue` | ✅ 原文核对 |
| 建根(OP) | `opstack/OpBlockSeal.cpp:21-22` | `is_zero → continue` | ✅ 原文核对 |
| 全量读 | `Storage2Ledger.h:543-544` | 跳过 | ✅ |
| 单槽读 | `Storage2Ledger.h:771-791` `fetchStorage` | 行缺失与"行存在且 32 字节全零"都返回 `bytes32{}` | ✅ 本就一致,不是本批改的 |
| `has_storage` | `Storage2Ledger.h:739-745` | 零值行不判真 | ✅ |

因为建根侧本来就 `is_zero → continue`,**"跳过"与"槽不存在"建出的根必然逐字节相同**——z1 的根相等断言是恒真的那一半(它无论改不改都过),z1 真正的判别力来自 `ASSERT_FALSE(poisoned())` 与 `seenStorage.size()==1`。INJ-A 下 z1 翻红即由前者贡献。**结论正确、断言略有冗余,不是缺陷。**

KEEP 契约未被破坏:`probeHasStorage` 只影响 `Account::has_storage` 字段,账户存在性由 `fetchAccount` 的 `existsOne` 判据决定(两条独立路径),`GetAccountKeepsEmptyAccount` 一类既有用例在全部注入下都没红过。✅

`isZeroSlotValue` 实现正确:`size()==32 && 全 '\0'`,`noexcept` 诚实(`std::all_of` + 无抛谓词)。新增 `#include <algorithm>` 到位。

写路守护的形态判断我同意实施者:`assert(!is_zero(value))` 放在 `else` 分支前确实**结构上不可达**,永不可测;后置回读校验的是结果,可达可测,并且 INJ-C 证明它真的会响。**"测不出来的守护等于没有"这条与 §11 通则同构,应予确认。**

一个此前无人提的**新发现(Important)**——见下节"错误通道"。

### 新发现(Important):新守护的 throw 落在**共识**错误通道,而它守护的是**节点本地 bug**

`applyDiff` 刻意不 `noexcept`、也**从不置毒旗**(`Storage2Ledger.h:222` "strict tripwire 允许上抛";`:223-229` 无 try)。因此新守护的 `std::runtime_error` 沿 `processOpBlock` 上抛,落到 `OpSchedulerImpl.h:834-844` 的 catch:`bridge.poisoned()` 为 **false**(这条 throw 不经毒旗)→ 抛 `OpConsensusError` → 按 `OpSchedulerImpl.h:72-79` 的契约 **"Maps to INVALID on the caller side, never -32603"**。

问题:这条守护的触发含义是"**桥自己的写回有 bug,零值行没删掉**",属于节点本地故障,不是"这个 payload 不合法"。用 INVALID 回答一个**合法** payload,在 OP Stack 上的后果是节点拒绝规范链并永久分叉 —— 比 -32603(不承诺、可重试)严格更坏。这正是既有用例 `EngineNewPayloadMutation.StorageLayoutFaultIsInternalErrorNotInvalid`(`EngineNewPayloadGateTest.cpp:1696-1720`)专门防的那一类错误分类,而新守护站错了边。

诚实标注:这不是本批**新造**的通道缺陷 —— `applyDiff` 既有的两处 tripwire(`:289` `/sys/` 路由、`:385` ghost 账户)走的是同一条误分类通道。**但本批在这条通道上新增了一个触发点,把爆炸半径扩大了。** 最省的修法是让 `applyModifiedEntry` 的不变量违规走毒旗(或抛一个 `executeOpBlock` 能与共识错误区分的类型),使其映射到 -32603;同时可顺手把两处既有 tripwire 一并归位。**建议记为跟进项,由协调者裁定是否本批修。**

### 新发现(Minor):非 32 字节槽**值**这条路**全无测试**

`grep` 全 `bcos-evm/test/opstack/*.cpp`:`size mismatch` / `length_error` / 类似断言 **零命中**。也就是说:

- 实施者诚实边界 ② 说的是"`isZeroSlotValue` 对'32 字节键 + 非 32 字节值'有意判有内容,与 `fetchAllStorage` throw 形成有意不对称且**无专门用例**";
- 实测更进一步:**这条不对称的两侧都没有用例** —— 连**终审 M-1 自己修的那个 throw**(`fetchStorage:781-785` / `fetchAllStorage:526-530`,"值长度 != 32 不再静默降级为零值")也从来没有红绿见证。
- 唯一沾边的 `StorageLayoutFaultIsInternalErrorNotInvalid` 注入的是 `op_gate_injected_layout_fault`(29 字节**键**),走的是 `fieldKey.size() != 32` 那条 throw,**不是**值长度那条。

方向上的判断我同意实施者(`has_storage=true` 在 EIP-7610 方向偏保守 = 拒绝 CREATE,安全侧);**但"有意"的不对称如果没有用例,下一个人有极大概率把它"顺手统一掉"**。建议补一条两句话的用例把不对称钉住(同时白捡 M-1 的覆盖)。

---

## 派单第 5 点:见证归属

**实施者的自陈完全成立,已独立复核:**

- `bcos-evm/test/CMakeLists.txt:69-71`:`Storage2LedgerTest.cpp` 在 `if(TARGET bcos-framework)` 门控内(连同 `LedgerRootTest / EbT8nReplayTest / OpSchedulerImplTest / Engine*Test`)。
- standalone 二进制 `bcos-evm/build/test/bcos-evm-opstack-tests` 时间戳 **2026-07-29 21:44:09**(批 9 之前),`--gtest_list_tests` **131 例、`Storage2Ledger` 命中 0**。→ **131/131 对本批是彻底的空真,实施者主动声明这一点是对的。**
- 追加核实"空真是否掩盖了编译风险":`Storage2Ledger.h` 的全部 includer 是 `engine/OpSchedulerImpl.h`、`engine/OpEngineSeam.h`(均 header-only,无生产 `.cpp` 包含)+ 5 个 in-tree 门控测试。standalone 的 `bcosevm::opstack` 库**根本不编译这个头**,所以"没重建 standalone"不隐藏编译失败。✅

唯一见证是 in-tree 的 246,这个归属是诚实的。

---

## 派单第 6 点:文档 t·u 与四条诚实边界

### 记账条目 t / u —— 逐条到源码核对

| 断言 | 核实 |
|---|---|
| t①`addressFromTableName` 对非 20 字节 hex 的 `/apps/` 表名抛 → 毒旗 → `stateRootOf` 每块作废 | ✅ `Storage2Ledger.h:471-485` 确有 `decoded.size() != 20 → throw std::length_error`;`visitAccounts:261-276` 无条件 catch → `poison()`。**Minor 精度问题**:`/apps/HelloWorld` 这个具体例子先在 `boost::algorithm::unhex`(`:476`)上抛 `non_hex_input`,而不是文档写的 `std::length_error`;结论(毒旗、每块 -32603)不变,机制描述不准 |
| t 通则"任何新增 `poison()` 调用点都必须回答'真实链上会不会命中'" | ✅ 值得固化,建议一并进 §11 |
| u①`MemoryLedger.cpp` `has_storage = !storage.empty()` 是同类缺口 | ✅ `MemoryLedger.cpp:18-19` 原文 `!account.storage.empty()`;`:54-59` `applyDiff` 零值 `erase`;`MemoryLedger.h:94` `accounts()` 返回可变引用。"触发面限于测试代码" + "正确修法是收窄 `accounts()` 而不是加零值过滤" —— 两条判断都成立 |
| u②生产写路径对零值照写不删 | ✅ `transaction-executor/.../vm/HostContext.h:286-289` `set` 无条件 `setStorage`;`bcos-ledger/Ledger.cpp:1844` 创世 alloc `setStorage(evmKey, evmValue)` 无零值判断。行号准确 |
| 桥 design §6 撤销 rev.2 限定 | ✅ 用删除线保留原文 + rev.3 说明,可追溯性做对了 |

**无灌水。** t 与 q 同级置顶的判断我同意:q 是"请求进不来",t 是"进来了被自家守护打死",两者都是接入阻塞项而不是精度问题。

### 四条诚实边界的复审判定

| # | 实施者自陈 | 我的判定 |
|---|---|---|
| ① "每块 -32603" 是合成推理,未在真实节点端到端复现 | **诚实,且若有偏差是偏保守(低估了严重性)**。两端我都独立核对了:触发链 `stateRootOf → visitAccounts → visitAccountsImpl:536 → fetchAllStorage` 在代码里成立;零值行的生产来源在 `HostContext::set` 成立(任何 Solidity 的 `delete`/清零 SSTORE 都造一行)。关键在于 `visitAccounts` 是**全量**遍历(design §6 "全量重建=正确性版"),所以**全状态里只要存在一行零值槽,就会毒掉每一个块**——不需要"这个块碰到了它"。"每块"因此不是夸张而是下界。剩余缺口只是"某条真实链上是否确有这样一行",这需要真节点,合理保留 |
| ② `isZeroSlotValue` 对非 32 字节值的有意不对称、无专门用例 | **诚实但低估**:不对称的**两侧都无用例**,连 M-1 自己修的值长度 throw 也无覆盖。见上节 Minor 发现 |
| ③ 后置回读多一次层内查表,未做性能测量 | **可接受**。成本上界 = 每块 SSTORE-to-zero 的条数 × 一次 `readOneRaw`,与已有的每槽写入同阶,且只在零值分支付出。不需要本批测量 |
| ④ 接入阻塞项 t① 未修(牵涉"OP 状态根如何覆盖非 20 字节地址的 FISCO 原生账户",无裁定) | **诚实且判断正确**。它确实不是三行能修的语义裁定问题,标为阻塞项 + 留待裁定是对的处置 |

**四条边界无一条是掩饰;其中 ① 若有偏差是偏保守,② 应扩写。**

---

## 派单第 7 点:边界遵守

`git diff --name-only dbc2a3e2f~4 dbc2a3e2f` 全量 7 个文件:

```
.superpowers/sdd/.../final-batch9-report.md
.superpowers/sdd/.../progress.md
.superpowers/sdd/.../review-batch9.diff
bcos-evm/bcos-evm/ledger/Storage2Ledger.h        ← 生产代码 1
bcos-evm/test/opstack/Storage2LedgerTest.cpp     ← 测试 1
docs/superpowers/specs/2026-07-27-real-ledger-bridge-design.md
docs/superpowers/specs/2026-07-28-op-validator-minimal-loop-design.md
```

- `ports/` / `vectors/` / `golden/` / `transaction-scheduler/` / `bcos-rpc/` —— **零触碰**(grep 过滤后空)✅
- 通用组合根零漂移:唯一生产代码文件是 OP 专属的 `bcos-evm/bcos-evm/ledger/Storage2Ledger.h`,未碰任何通用调度/组合根 ✅
- 派单第二步约束"只动 `Storage2Ledger.h`;若 (A) 需要动 `applyDiff` 所在文件可以动" —— `applyDiff` 就在同一文件,**实际只动了 1 个生产文件**,严格守住 ✅

---

## 回归(我自己跑的,不重复实施者)

| 腿 | 二进制时间戳 | 结果 |
|---|---|---|
| in-tree `bcos-evm-opstack-tests` | 19:55:21(收尾复绿) | `--gtest_list_tests` **246** / **246 PASSED** |
| `test-bcos-engine` | 20:00:23(本次重建) | `*** No errors detected` |
| `bcos-evm-eth-tests` | 15:12:12(源未变,重建为 no-op) | 4 test cases,`*** No errors detected` |
| E-b 桥三腿(`EbT8n*` + `AllThirtyThreeGoldenVectors`) | 同 in-tree | 3/3 PASSED |
| standalone `bcos-evm/build` | **2026-07-29 21:44:09,未重建** | 对本批**空真**,不作证据(与实施者口径一致) |

**收尾:所有注入(INJ-A / B / C / D / E / E′)全部还原 + 重建 + 复绿,`git status` 干净。**

---

## 汇总判定

**通过。** 修复的语义方向正确(以太坊"零值 ≡ 不存在"),范围最小(1 个生产文件),守护搬家的归属判断正确,三探针判别力自陈**逐例核实成立**,文档 t/u 无灌水,四条诚实边界无掩饰,边界零越界。

需要协调者裁定的三条跟进(均**不影响本批通过**):

1. **(Important)** 新守护的 throw 走 `OpConsensusError` → **INVALID**,而它守护的是节点本地 bug,按 §4.3 原则应是 **-32603**。既有的两处 `applyDiff` tripwire 同病,本批把爆炸半径扩大了一个触发点。
2. **(Important)** "`existsOne` 把墓碑判为不存在"这条判据,只在 `MemoryStorage` 一侧被 z7 钉住(INJ-D → z7 三条断言全响);**生产实际走的 `MultiLayerStorage::View::readOne → getValue` 一侧零钉住**(INJ-E → 0 红,INJ-E′ 12 红证明实验有效)。一旦漂移,写路守护会在每一次合法零值写入上抛,而 246 例全绿。
3. **(Minor)** 非 32 字节槽**值**这条路全无测试(不只是不对称无用例,M-1 自己修的 throw 也无覆盖);t① 的 `/apps/HelloWorld` 例子实际抛 `non_hex_input` 而非文档写的 `std::length_error`。

---
---

# re-review(fix 轮验收,2026-07-31)

BASE `4f13c37b5` → `bc3208f4c`(fix 轮六提交 + 台账)。基线接手:in-tree `--gtest_list_tests` **249** / **249 PASSED**,二进制 10:27:27,`git status` clean。

实施者已自验的四条(INJ-E 重放 → 1 红且只有 z8 / INJ-H → 1 红且只有 z9 / INJ-F1 → 4 红 / INJ-G 阶梯前缀)**采信不重跑**。以下只做协调者点名的三处最长推理链 + 例行项。

## R-1(派单第 1 点):(d2) 替身有效性与"共用同段 catch"推论

### 推论成立,而且是**结构性**成立,不是巧合

`ecbfb8f68` 的形态是把 `applyDiff` 的**整个方法体**(两个 for 循环)包进一段 try,四级 catch 全部 `poison(...); throw;`。因此推论的成立条件是"所有写路 throw 点都在这段 try 之内",这一条由**可见性**保证:

- `applyModifiedEntry`(`:341`)与 `applyDeletedEntry`(`:433`)都是 **private**,全仓唯二调用点是 `applyDiff` 的 `:246` / `:248`,**都在 try 内**(`grep` 全仓核对,除注释外无第三处引用);
- `Storage2Ledger` 的 public 面只有 ctor / `get_account` / `get_account_code` / `get_storage` / `poisoned` / `firstError` / `applyDiff` / `visitAccounts` —— **唯一的写入口就是 `applyDiff`**,没有"不经 applyDiff 的写入口"可绕。

所以三条形状相关 tripwire(契约②零值泄漏 z6、ghost-delete、`/sys/` 路由)与 (d2) 注入的"底层写失败"**共用的不是一段碰巧相同的代码,而是同一个方法体的唯一出口**。新增 throw 点自动继承,这正是实施者选"整体包裹而非逐点改写"的收益。**推论成立,记录即可,不是残留缺口。**

补充:分类层那一侧也不依赖"哪一级 catch 命中"——`executeOpBlock` 的两条 catch(`OpSchedulerImpl.h:839` / `:845`)与其后的 `:874` **都是 `poisoned()`-first**,只要毒旗置位就走 `OpStorageError`。而四级 catch 里 `catch (...)` 兜底保证毒旗**一定**置位。故"置旗"与"分类"之间没有第二个可断的环。

### 一处如实记账(不是缺口):第二个 `applyDiff` 调用点在分类之外

`LedgerSeed.h:47` `seedFromTestState` 是全仓第二个 `applyDiff` 调用点,**不在** `executeOpBlock` 的分类 try 内。核实其调用方全部是测试(`EbT8nReplayTest / OpSchedulerImplTest / LedgerRootTest / Storage2LedgerTest / EngineNewPayloadGateTest / T8nReplayHarness.h`,零生产调用),且其入参类型是 `evmone::test::TestState`(向量类型),Engine API 路径根本到不了。**所以不存在"播种失败被答成 INVALID"的可能**——那里压根没有分类层。如实记一笔即可。

## R-2(派单第 2 点):RTTI 新推论的表述强度 —— **实施者的调和不成立,但结论方向对**

### 我先纠正自己:上一轮复审报告里"`visitAccounts` 同 TU **直抛**"的说法是错的

`visitAccounts:327` 是 `task::syncWait(visitAccountsImpl(visitor))`,而 `visitAccountsImpl` / `fetchAllStorage` 都是 `task::Task<>` 协程。**它和 `applyDiff` 一样穿 syncWait/协程边界**。所以"直抛 vs 穿协程"这个对照**从一开始就不成立**——两边是同一种路径。

### 实测(INJ-R / INJ-R2):真正的判别式是**异常类型族**,与协程边界无关

不需要新的猜测,现成的绿用例已经先给出一半反证:z9 第 (2) 腿(fix 轮前后都绿)断言 `visitAccounts` 的 `firstError()` **含原文** "storage slot value size mismatch",而 `catch(...)` 兜底写的是一句不含该子串的固定文案 ⇒ 当时只有两级的 `catch (const std::exception&)` **确实接住了** 一个穿协程抛出的 `std::length_error`。这与"穿协程即失效"直接矛盾。

为把条件测准,做了两轮注入(**同一个 TU、同一个二进制**,消除 TU 差异变量):

**INJ-R**(给 `applyDiff` / `visitAccounts` / `get_storage` 三处 catch 阶梯打 `[A-]/[V-]/[G-]` 前缀,并用 `ADD_FAILURE()` 把 `firstError()` 打出来),重建 10:40:04 —— 四个探针各自落在**最派生**的匹配级,符合预期,但派生级遮蔽了 `std::exception` 级,不构成判据。

**INJ-R2**(把三处都**剥回"只有 `catch (const std::exception&)` + `catch (...)`"**,即 fix 轮之前 `visitAccounts` 与 `applyDiff` 的原形态),重建 10:41:01,四个探针实测:

| 探针 | 抛出类型 | 抛出路径 | 落点 |
|---|---|---|---|
| `applyDiff` ← `applyDeletedEntry` ghost tripwire | `std::runtime_error` | 协程 + syncWait | **`[A-ELLIPSIS]`**(逃过 `std::exception` 级) |
| `visitAccounts` ← `fetchAllStorage` unknown key | `std::runtime_error` | 协程 + syncWait | **`[V-ELLIPSIS]`**(同样逃逸) |
| `get_storage` ← `fetchStorage` 值长度 | `std::length_error` | 协程 + syncWait | **`[G-EXCEPTION]`**(正常命中) |
| `visitAccounts` ← `fetchAllStorage` 值长度 | `std::length_error` | 协程 + syncWait | **`[V-EXCEPTION]`**(正常命中) |

还原(`git checkout` 两文件)+ 重建(10:42:02)+ 复绿 **249/249**,clean。

**同一个 `visitAccounts` 的同一级 catch 上,`runtime_error` 逃逸而 `length_error` 命中** —— 协程边界这个变量被彻底排除。

### 应当写进 §11/RTTI 补遗的精确表述(建议原文)

> **适用条件(实测,勿弱化也勿放大)**:在本构建(链入 `-fno-rtti` 的 `libevmone.a`)下,`catch (const std::exception&)` 对 **`std::runtime_error` 子树不生效**——异常会越过该级落到 `catch (...)`;对 **`std::logic_error` 子树(含 `std::length_error`)正常生效**。判别式是**异常类型族**,**与是否穿越 `task::syncWait`/协程边界无关**(同一处 catch 上两族行为相反,已实测)。
> **推论**:任何需要"保证捕获"的 catch 阶梯,必须**显式写出 `catch (const std::runtime_error&)` 一级**,或以 `catch (...)` 兜底;只写 `catch (const std::exception&)` 会漏掉本仓最常用的异常族。
> **不要写成**"`catch (const std::exception&)` 形同虚设":它对 `logic_error` 族是有效且唯一正确的那一级,删掉会让 `length_error` 失去消息。

据此,`Storage2Ledger.h` 现有注释里的两句需要调整:①"`std::exception` 这一层按实验二**在本构建下形同虚设**" —— **过强**,应改为"对本文件 `runtime_error` 系的 throw 点形同虚设,对 `logic_error` 系(`length_error`)是实际生效的那一级";②"非唯一的那份 typeinfo 是 `std::exception` 的" —— 与实测不符(若 `std::exception` 的 typeinfo 非唯一,`length_error` 也该逃逸;实际没有),该机制归因**未被证实**,建议降格为"机制未定,判别式是类型族,以实测为准"。

### 由此暴露的新缺口(Important):读路的 `runtime_error` **今天就在丢消息**

fix 轮把四级阶梯只加在了 `applyDiff`。`get_account` / `get_account_code` / `get_storage` / `visitAccounts` 仍是两级形态,于是按上表:**读路每一个 `runtime_error` 都落进 `catch (...)`,`firstError()` 被替换成固定文案,原始诊断丢失**。INJ-R2 已实测到具体一例:`visitAccounts` 接 `fetchAllStorage` 的 "unknown key in account table '/apps/…'" → `firstError()` 变成 "unknown exception"。

后果不是分类错误(毒旗照置,仍是 -32603),而是 **-32603 的可诊断性**:`executeOpBlock` 把 `firstError()` 原样塞进 `OpStorageError`,运维看到的是 "unknown exception"。讽刺的是 (d2) 的 helper `expectOpStorageErrorWithMessage` **专门断言** `what()` 不得退化成兜底串——写路有这条守护,读路没有,而读路是毒旗的主要来源。**建议把同一四级阶梯复制到三个读方法 + `visitAccounts`**(纯机械改动,无语义风险),并给读路补一条与 (d2) 对称的消息保真断言。

