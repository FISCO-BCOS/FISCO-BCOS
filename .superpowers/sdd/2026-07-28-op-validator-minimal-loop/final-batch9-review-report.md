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
