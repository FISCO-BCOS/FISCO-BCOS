# FISCO OP Stack EL × specs.optimism.io 对齐审计 v2（含 Karst）— 设计文档

- **日期**：2026-08-21
- **状态**：已获用户确认（§1–§5 全部）
- **关联**：`docs/2026-08-19-opstack-el-spec-compliance.md`（v1 审计，基线为 op5429-01，已被本设计取代为审计起点而非结论来源）

---

## 1. 背景与目标

FISCO-BCOS 正在作为 Optimism 的执行客户端（EL）工作。目标：以 specs.optimism.io 为规范基线，对当前 `feat-opstack-e2e` 分支的 opstack 实现做逐条款对齐审计，识别所有与 spec 未对齐的点。要求：

- 不泛泛而谈：每个 spec 点必须有编号、原文依据、判定与证据。
- 每条证据双轨：FISCO 代码位置（文件:行号）+ 参照实现位置（op-geth 文件:行号；op-reth 仅在有争议或 op-geth 未覆盖处）。
- 产出可评审、可追踪、可勾销的单份报告。

## 2. 审计对象与基线

| 项 | 值 |
|---|---|
| FISCO 侧 | 分支 `feat-opstack-e2e`，HEAD `b7d112b3f`（worktree：`.claude/worktrees/op-alignment`） |
| 工作区差异 | 未提交改动（`OpstackExecutor.h/.cpp`、t8n golden 若干）不作证据主体，报告中单独注明 |
| 规范侧 | `/tmp/op-specs`（ethereum-optimism/specs@main，commit `2049036`，2026-08-05）；必要时在线核对 specs.optimism.io 最新页 |
| 参照（主） | `/Users/octopus/octo/code/op-geth`（optimism 分支本地源码，取证时注明 commit） |
| 参照（辅） | op-reth（GitHub main，在线拉取关键文件至 `/tmp/op-reth-ref`，仅用于 op-geth 未覆盖/争议点，如 Karst 相关） |
| 分叉范围 | **逐条审计**：Isthmus（默认）+ Jovian（`feature_op_jovian`）+ Karst；**概览扫描**：Delta、Monsoon、Lagoon/interop（仅 EL 侧 upgrade-transaction） |
| 审计单元 | 主干分叉章节按"章节"分区编号（§3.1），Karst 增量独立章节（M7） |

姊妹分支核对（2026-08-21 实测）：`feat-op-block-scheduler-standalone`、`feat-op-executor-e2e` 已并入 e2e（ahead=0）；`feat-op-block-scheduler` 与 `feat-op-historical-ethcall` 各自仅剩 docs/style 类未合入提交，无功能性代码——审计对象可干净锁定为 `feat-opstack-e2e` HEAD。

## 3. 条款库

### 3.1 来源章节与编号规则

条款来源（EL 相关）：

1. `specs/protocol/exec-engine.md`（Engine API 全部方法、校验规则、错误码）
2. `specs/protocol/deposits.md`（0x7E 交易、L1 attributes、回执）
3. `specs/protocol/withdrawals.md`（MessagePasser、withdrawalsRoot）
4. `specs/protocol/precompiles.md`、`predeploys.md`、`preinstalls.md`
5. `specs/protocol/derivation.md`（仅 EL 侧：engine queue、attributes matching、consolidation）
6. `specs/protocol/system-config.md`（仅 EL 侧：SystemConfig 读取/feature_flags）
7. 分叉 diff：`regolith/`、`canyon/`、`ecotone/`、`fjord/`、`granite/`、`holocene/`、`isthmus/`、`jovian/`、`karst/`（fees、l1-attributes、receipts、extraData 等条款在各 fork 的 exec-engine/diff 内）

编号：`S-<章节>-<序号>`，章节缩写：`EXE`（exec-engine）、`DEP`（deposits）、`WDL`（withdrawals）、`PRE`（precompiles）、`PDE`（predeploys）、`PIN`（preinstalls）、`DRV`（derivation EL 侧）、`SYC`（system-config）、`HOL`/`IST`/`JOV`/`KAR`（分叉 diff，Karst 全部归 `KAR` 独立区段）。跨分叉复述性条款只建一次（记适用分叉列），增量条款单独编号。

预估规模：**90–130 条**。

### 3.2 条款字段模板

```
| 编号 | 规范原文要点（引关键句） | 来源章节:行号 | MUST/SHOULD | 适用分叉 | 判定 | FISCO 证据 | op-geth 证据 | 备注 |
```

- 原文要点：英文原文关键句 + 一句中文释义，禁止改写丢失语义。
- 判定：✅ 符合 / 🟡 部分符合 / ❌ 缺失 / ➖ 不适用（EL 外责任，注明分工依据）。

### 3.3 取证与判定标准

- **✅**：FISCO 行为与规范一致，给出 `文件:行号` 证据 + 尽量补测试证据；op-geth 对应实现给出 `文件:行号`。
- **🟡**：实现存在但有偏差（占位常量/硬编码/架构映射不等价），写明偏差点、影响面。
- **❌**：无对应实现、被显式拒绝、或未接线（如零消费者），给出规范依据与 op-geth 参照位置。
- **➖**：非 EL 责任（sourceHash 派生、p2p、batcher、op-node 侧），注明分工依据（spec 原文或 op-node 职责说明）。
- 证据纪律：每条判定必须有可 grep 的锚点；op-geth 证据注明文件路径、行号与 commit；op-reth 证据注明文件路径与 commit。

## 4. 报告结构

单文件：`docs/2026-08-21-opstack-el-spec-audit-v2.md`

1. 元信息（对象/基线/参照版本/方法/日期）
2. 总体结论与评级（与 v1 报告对比：哪些缺口已关闭、对应提交/合并来源）
3. 需求矩阵（分章节编号，判定+证据列）
   - A 执行语义（deposits/fees/l1-attributes）
   - B Engine API 校验面（newPayload/FCU/getPayload 方法、校验、错误码、能力协商）
   - C 构建面（buildOpPayload 对 attributes 的消费：gasLimit/extraData/baseFee/withdrawals）
   - D 派生边界（attributes matching、consolidation、reorg/非顶端父块、payloadId 派生）
   - E RPC 用户面（block/tx/receipt JSON OP 字段、eth_getProof）
   - F 创世/predeploy/preinstall（op-deployer allocs 注入、地址矩阵、SystemConfig feature_flags）
   - G Karst 专项（`S-KAR-*` 全量 + Delta/Monsoon 概览）
4. 差距清单（Blocker/Major/Minor/已知差距，含修复指向与涉及文件）
5. Delta/Monsoon 概览扫描（仅差异点摘要，不逐条）
6. 动态验证记录（测试套件运行结果：bcos-evm opstack 测试、opstack-executor 测试、t8n golden）
7. 附录 A：完整条款库；附录 B：参照版本与拉取记录（op-geth commit、op-reth 文件清单）

## 5. 执行阶段

- **P0 基线锁定**：记录 spec 快照与 op-geth commit；编译 + 跑现有测试（`bcos-evm/test/opstack` 21 个测试文件、`opstack-executor` 测试、t8n golden），结果存档供 §6 使用。
- **P1 条款库建设**：通读 §3.1 全部章节，提取条款（产出附录 A 初稿）。本阶段最耗时，不做判定。
- **P2 取证**（串行，每模块内按条款走）：
  - M1 deposits/fees/l1-attributes（`bcos-evm/bcos-evm/opstack/` + `opstack-executor/`）
  - M2 withdrawals/precompiles/predeploys/preinstalls
  - M3 Engine API 校验面（`engine/bcos-engine/EngineServiceImpl.{h,cpp}` + `bcos-rpc` Engine 三件套）
  - M4 构建面（`buildOpPayload`/attributes 消费/extraData/baseFee）
  - M5 派生边界（consolidation/reorg/payloadId/attributes matching）
  - M6 RPC 用户面 + 创世
  - M7 Karst 专项（spec `karst/` diff × FISCO `OpForkSchedule` 占位现状）
- **P3 汇总**：写报告（§4 结构，差距分级 + 修复指向）。
- **P4 自审**：抽查 ≥10 条 ✅/❌ 判定的 FISCO 证据可 grep 到；与 v1 报告逐项交叉核对"已关闭缺口"及其对应提交。

## 6. 验证标准

- 动态验证（P0 的测试运行结果）作为证据的一部分写入报告 §6。
- P4 自审通过后报告才可交付。
- 判定为"已实现"的条款 ≥90% 有可 grep 的 FISCO 锚点；判定为"缺失/部分"的条款 100% 有 op-geth（或 op-reth）参照位置。

## 7. 产出物

1. `docs/2026-08-21-opstack-el-spec-audit-v2.md`（主报告）
2. 条款库（报告附录 A，随报告落盘）
3. 参照记录（报告附录 B）

## 8. 明确不做（YAGNI）

- 不修代码——本设计只产出审计报告；修复按报告差距清单另行排期。
- 不做 Delta/Monsoon 逐条审计（仅概览）。
- 不审计 interop/Lagoon（EL 侧仅 upgrade-transaction 相关，归入概览扫描）。
- 不重跑 v1 已覆盖且分支未变化的取证（v1 证据仍有效处标注"复用 v1"并重验行号）。
