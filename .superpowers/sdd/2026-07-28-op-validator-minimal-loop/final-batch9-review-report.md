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
