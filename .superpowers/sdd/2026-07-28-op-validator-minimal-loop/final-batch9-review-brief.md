# 终审批 9 复审派单

工作目录:`/Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/ledger-bridge`,分支 `feat-op-validator-loop`。
**不豁免编译与测试。** 回归基线(批 6 收口):in-tree 239/239、standalone 131/131、`test-bcos-engine` 11 例、E-b 三腿 33×3。实施者报 in-tree **246/246**。

## 输入

- 派单(rev.2):`.superpowers/sdd/2026-07-28-op-validator-minimal-loop/final-batch9-brief.md`
- 实施者报告:`.superpowers/sdd/2026-07-28-op-validator-minimal-loop/final-batch9-report.md`
- 差异包:`.superpowers/sdd/2026-07-28-op-validator-minimal-loop/review-batch9.diff`(BASE `3d96d0665` → HEAD)

## 本批要点

把「零值槽 = 槽不存在」(以太坊语义,geth 的 trie 删零值槽)统一到桥的所有判据上。修前两处判据互相矛盾:

- `fetchAllStorage:485` **查**零值 → `throw` → `visitAccounts` 接住 → `poison()` → 而 `stateRootOf` 正走这条路 → **生产账本上每个 OP 块 -32603,节点完全不可用**;
- `probeHasStorage:682` **不查** → `has_storage` 误真 → `eth/state/state.cpp:259 .has_initial_storage` → `eth/state/host.cpp:91 is_create_collision`(EIP-7610)→ **同一 CREATE2 在 op-geth 成功、本桥 INVALID**。

裁定方案:读路改为**跳过**零值槽;「`applyDiff` 从不留下零值槽行」这条不变量的守护**移到写路径**,形态是「`removeOne` 之后回读该键,行若仍存活就 `throw`」(后置**结果**校验,而非意图断言——后者在该分支结构下是同义反复、永不触发、无法被测试)。

## 复审重点

1. **语义正确性**:所有判据是否真的统一到「零值 = 不存在」?有没有改过头(非零槽被误跳过)、或漏改?
   - **KEEP 契约是否完好** —— "零值槽 = 不存在" 与 "账户存在但 code 为空 = 存在" 是两回事,后者必须继续返回存在。这是本桥核心语义。
   - **墓碑过滤(`liveContent`,`:388-405`,"终审 I-1" 的产物)是否仍然有效** —— 零值过滤是叠加在它之上的第二层,不是替代。
2. **守护是否真的净增加(本批最关键)**。实施者的核心论断:只改读路会**确定**造成守护净减少,因为 `Storage2LedgerTest.cpp:274` 的 `EXPECT_FALSE(has_storage)` 今天有力**正是因为** `probeHasStorage` 不判零值;改对之后 `applyDiff` 漏写零值槽那条断言照样通过。
   请**独立核实**,并验证新增的写路后置校验补上了这个缺口:删掉它 → 重建 → 必须有用例翻红;并且**在删掉它的同时让 `applyDiff` 真的漏写一行零值槽 → 确认没有任何既有用例翻红**(这才证明缺口真实存在过)。
3. **实施者否决了协调者的一条技术警告,请独立复核**。协调者警告 `removeOne` 写的是 `DELETED_TYPE` 墓碑,故 `existsOne` 可能对刚删的键返回 true、使后置校验"永远在响",并建议改用 `readOne` + `liveContent`。实施者**实测否决**:`existsOne` 把 `DELETED_TYPE` 判为不存在(而 `range()` 仍能扫到该行),且 `readOne + liveContent` 在类型上不可组合。
   **请自己实测确认 `existsOne` 的墓碑语义**,并回答:**若该判据将来随 storage2 实现漂移,有没有测试会翻红?**(即这个依赖是被钉住的,还是靠今天的实现巧合成立的。)
4. **反证实验复核**:实施者报三探针各自翻红且互不重叠——读路 `throw` 复原 → 3 红;`probeHasStorage` 去零值层 → 2 红;删写路后置回读 → 1 红。**至少独立复现其中两个**,确认红的用例集合与它报的一致。
5. **红绿见证归属**:实施者已声明 standalone 二进制时间戳停在 7-29 未变、对本批是空真、不作证据,唯一见证是 in-tree。**核实这条声明属实**(上一轮正是靠"报 Built target 但二进制比源码旧"发现空真的)。
6. **文档**:§6.4 新增条目 t/u 是否属实、行号是否对;桥 design §6 撤销 rev.2 零值毒旗限定的改动是否准确。实施者自陈四条"诚实边界",逐条判断是否如实、有无低估:
   - ①"每块 -32603"是读码 + 同路径实证的**合成推理**,未在真实节点端到端复现;
   - ②`isZeroSlotValue` 对"32 字节键 + 非 32 字节值"有意判"有内容",与 `fetchAllStorage` 同情形 `throw` 形成**有意不对称且无专门用例**;
   - ③后置回读多一次层内查表(仅零值写入时),未做性能测量;
   - ④**接入阻塞项仍在**:条目 t 的第一个生产毒旗触发点(非 20 字节 hex 的 `/apps/` 表名)本批未修,它牵涉"OP 状态根如何覆盖非 20 字节地址的 FISCO 原生账户"这一**尚无裁定的语义问题**。
7. **边界**:`ports/` `vectors/` `golden/` `transaction-scheduler/` `bcos-rpc/` 零触碰;只动了 `Storage2Ledger.h` 与 `Storage2LedgerTest.cpp` 两个代码文件;通用组合根零漂移。

**不要重复实施者在同一代码上跑过的测试**,但第 2、3、4 点的实验必须你自己跑。

## 纪律

- 自验六步:改 → 重建 → 看现象 → 还原 → **重建** → 复绿。缺一不算。构建目录若陈旧**先 reconfigure**;假绿判据是二进制时间戳 + `--gtest_list_tests`(本闭环出过 `exit=0` 但 CMake 没重配的假绿)。
- 本仓已知 evmone `-fno-rtti` 的 typed-catch 旁路(`std::exception` typeinfo 跨库非唯一)。实施者论证 `visitAccounts:256` **不受**影响(throw 与 catch 同头文件同 TU、typeinfo 同一份;`VisitAccountsUnknownKeyPoisons` 同路径实证;且 `catch(...)` 兜底),若你复审时触及该结论请一并核。
- **每完成一个注入就提交一次结论。** 本闭环已有两次 watchdog 停滞 + 四次 API 断流。
- **收尾硬要求:所有注入必须还原 + 重建 + 确认复绿,`git status` 干净。**
- 用 `rtk git`。报告写 `.superpowers/sdd/2026-07-28-op-validator-minimal-loop/final-batch9-review-report.md`,`git add -f` 提交。

返回四行 STATUS / FINDINGS / TESTS / CONCERNS。
