# 全分支复审 · `[需验证]` 实验统一执行

工作目录:`/Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/ledger-bridge`,分支 `feat-op-validator-loop`。
**你现在独占构建目录**,其余复审者已全部结束。

## 背景

刚完成 5 个视角的**只读**全分支复审。因为当时构建目录被占用,所有需要"改一行 → 重建 → 看是红是绿"才能证实的论断都被标成 `[需验证]` 并写清了最小实验步骤。**你的唯一任务是把这些实验跑完,给出实测结论。**

## 输入(五份报告,逐份扫出所有 `[需验证]` 项)

```
.superpowers/sdd/2026-07-28-op-validator-minimal-loop/wb-review-p1-report.md   # 共识语义
.superpowers/sdd/2026-07-28-op-validator-minimal-loop/wb-review-p2-report.md   # 解码与执行(C-1/C-2 两条最小验证步骤)
.superpowers/sdd/2026-07-28-op-validator-minimal-loop/wb-review-p3-report.md   # 存储与生命周期
.superpowers/sdd/2026-07-28-op-validator-minimal-loop/wb-review-p4-report.md   # 代码组织(I-1 的 concept 方案能否软失败)
.superpowers/sdd/2026-07-28-op-validator-minimal-loop/wb-review-p5-report.md   # 测试有效性(E1-E6 六个实验,E1/E2 是其核心论断)
```

每份报告都写明了改哪一行、跑哪个 target、预期什么现象。**按报告写的做,不要自己重新设计实验。**

## 优先级(时间不够就按这个顺序砍)

1. **P5 的 E1、E2** —— 该报告的两条核心论断,任一翻红则对应发现必须降级
2. **P2 的 C-1、C-2** —— 两条 Critical,报告说只需新增测试用例、不改生产代码即可证实
3. P5 的 E3-E6
4. P3、P1 的 `[需验证]` 项
5. **P4 的 I-1**(最特殊,见下)

## P4 I-1 单独说明

它想验的是:`requires std::derived_from<typename S::ConsensusError, std::exception>` 这类**嵌套 requires** 在没有该成员的 `SchedulerSerialImpl` 上是**软失败(SFINAE-friendly,探针求值为 false)**还是**硬错(编译失败)**。

这条很重要:本分支在 `c0288b8b0` 踩过同类坑(OP 依赖名进了成员函数签名 → 通用组合根实例化直接编译失败,`if constexpr` 救不了)。**只需写一个最小 TU 试编译**,不必改生产代码。若硬错,P4 给了退化方案(探针不变 + `if constexpr` 内首行 `static_assert`),也请一并试编。

## 纪律(踩过的坑,逐条都有事故)

- **每个实验都是六步**:改 → 重建 → 看现象 → 还原 → **重建** → 复绿。少一步都不算数(批 3 踩过"只还原不重建")。
- **多构建目录**:in-tree(`cmake --build build --target bcos-evm-opstack-tests -j8`)与 standalone 各是一个。**注意 engine 相关测试只编进 in-tree**(`if(TARGET bcos-framework)` 门控),standalone 对它们是"依赖图无边"的空真 —— 报告里必须**点名哪个目录构成红绿见证**,不要把空真当证据。
- **假绿排查**:每次重建后确认二进制时间戳真的刷新了,必要时 `--gtest_list_tests` 确认用例在二进制内。本闭环出过一次 `exit=0` 但 CMake 根本没重新配置的假绿。
- **收尾硬要求**:所有注入必须还原 + 重建 + 确认全绿,`git status` 干净。中途若判断某个实验做不了,写进报告说明原因,**不要留下未还原的改动**。

## 交付

报告写 `.superpowers/sdd/2026-07-28-op-validator-minimal-loop/wb-verify-report.md`,按"实验编号 / 来源报告 / 改了什么 / 实测现象 / 结论(证实 / 证伪 / 需降级 / 无法执行)"逐条列表。

**特别标出任何与原报告预期不符的结果** —— 那比确认更有价值。

用 `rtk git`。返回四行 STATUS / CONFIRMED / REFUTED / CONCERNS。
