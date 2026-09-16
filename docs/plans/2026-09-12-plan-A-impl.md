# Plan A 实施计划（impl）—— 2026-09-12-opstack-fork-test-plan 的执行版

> 规格源：`karst-on-5550/docs/plans/2026-09-12-opstack-fork-test-plan.md`（只读，不复制其代码块；
> 执行时按规格原文的 fenced 代码逐字落盘）。本文件记录：前提复核结论、执行偏差、逐 Task 状态与取证。
> 本文件 untracked，永不入库。

## 0. 前提复核（§0.1 brainstorming 结论）

| 前提 | 取证 | 结论 |
|---|---|---|
| Karst 已建模 | `OpForkId.h:41` `Karst`；`extraDataLayoutFor` Karst→`Jovian17` + static_assert | 成立 |
| 语料 pin | `.t8n-pin`=`759a9af0`；`getpayload/manifest.txt` 明确 corpus op-geth pin 实现 Karst 门 `GetPayloadV5`（`params/config.go:519` KarstTime） | 成立；三处 pin 不一致（e8800cffe/759a9af0/本机 d0734fd5）已由规格 Task 10 立为 finding，非新问题 |
| Regolith `rev` 兜底 | `OpForkSchedule.cpp:98` `regolithConfig().rev = EVMC_LONDON`；op-revm `spec.rs` BEDROCK\|REGOLITH→`SpecId::MERGE`(=`EVMC_PARIS`) | 分歧真实存在 → M0 台账第 10 格定夺 |
| Jovian `blobGasUsed` 缺等值校验 | `OpEngineService.cpp:180-199` 仅有范围校验（uint64 / pre-Jovian=0 / ≤gasLimit），无「==本地重算 daFootprint」比对 | 成立 → Task 10 立案 WI-01 item 2 |

**结论：四条前提全部成立，规格无需修订。**

## 执行偏差（相对规格）

- **D1（过程文档落点）**：规格 Task 1/10 要求回写设计文档与 `INDEX-OPFEAT.md`，但它们在 karst-on-5550
  （用户硬约束：绝对只读）。处置：所有过程文档落在本 worktree `docs/plans/`（untracked）；
  设计文档修订稿写为 `docs/plans/2026-09-12-plan-A-design-revision-pending.md`（待人工合入 karst-on-5550），
  `INDEX-OPFEAT.md` 立案条目写进该文件同节。**不触碰 karst-on-5550。**
- 其余按规格原文执行。

## 环境契约（§0.4 摘录，执行中反复自查）

- 唯一可改 worktree：本目录，分支 `feat/karst-on-318-merged`（HEAD ccfaf4521）。不推送。
- 语料 `~/.cache/fisco-t8n-corpus` 只读（Task 3 Step 4 的反向验证需要临时改一个 envelope —— 属规格明文
  授权的篡改-还原动作，前后 byte 级还原）。
- zsh `$VAR:path` 陷阱 → `"${VAR}:path"`；新增/删测试文件后必须 `cmake -B build -S .` 重配；
  python 用 `python3.11`；Boost 判红不用 `errors detected` 子串，计数口径 `Running N test cases`；
  `--run_test` 不吃逗号；cwd 必须是 worktree 根。
- git：只 `git add` 具体文件路径；`docs/**`、`.agents/**` 永不入库。

## Task 状态

- [x] 前提复核（§0.1）—— 四条前提全部成立
- [x] Task 0 基线：engine 338 / rpc 331 / evm-opstack 174 / executor 142
- [x] Task 1 M0 台账 —— `docs/plans/2026-09-12-m0-cell-audit.md`（12 格：6 个 `—`、1 个 Task 5 建格、2 个归 Plan B、3 个定夺立案）
- [x] Task 2 M1 派生式断言 —— commit `401351685`；`total=27 deviated=1 checked=26`；反向验证（getPayload 恒 V5）红→还原绿
- [x] Task 3 M2a getpayload provenance —— commit `fd19aa589`；executor 142→143；篡改 canyon_v2.json 红→字节级还原（shasum OK）→绿
- [x] Task 4 M2b 形状基线 —— commit `b77728ee0`；4 用例 × 9 格绿；engine 338→342
- [x] Task 5 M7 边界扫描 —— commit `d90301a51`；2 用例 × 27 格一次通过；engine 342→344
- [x] Task 6 Karst oracle —— commit `8d41142dc`；oracle rev `5f90f749ca…`、p256=6900（OSAKA）、bn254=57600；evm-opstack 174→175
- [x] Task 7 modexp 三长度 —— commit `fc78144ad`；套件 13→16 用例；evm-opstack 175→178
- [x] Task 8 M3 scalar 边界 —— commit `42adbdc9d`；3 用例绿；rpc 331→334
- [x] Task 9 变异 —— commit `02ee1ced5`；N1/N2/NEW-3/V-M7/V-KARST/V-M2 全 RED、控制组绿、跑完 `git diff` 到 HEAD 为空
- [x] Task 10 文档同步 —— 立案 F-A1..F-A5 + 设计修订见 `docs/plans/2026-09-12-plan-A-design-revision-pending.md`；无已入库文档改动，无需 commit
- [x] 终验：344/334/178/143 = 基线 +6/+3/+4/+1；8 个 commit 均无 `docs/**`、`.agents/**`

## 执行中发现并修正的计划 bug（不影响判据方向）

1. **Task 4**：`sorted(c_envelopeV2Keys)` 数组不能转 `initializer_list` —— 增加数组重载（纯编译修复）。
2. **Task 7**：计划假设 modexp 输出恒为 1 字节；EIP-2565 语义输出 = 模数长度（modLen=1024 时输出
   1024 字节、末字节 0x01）。断言改为按 EIP-2565 输出形状校验（输出宽度 + 零填充 + 末字节）。
3. **Task 6**：extract.sh 原正则抓到 `P256VERIFY_ADDRESS=256`（地址非 gas）；改为精确抓
   `P256VERIFY_BASE_GAS_FEE_OSAKA=6900`（Karst=Osaka，与本仓 op-geth `protocol_params.go:184`
   引用一致）。另 Cargo.lock 实际位于 `rust/Cargo.lock`。
4. **Task 9**：V-M7 表级变异被 `OpForkId.h` 的 static_assert 编译期拦截（BUILD FAILED）；改为
   「judgement 整体反转」（表 + 守卫一起降档），由运行时 M7 扫描变红——证明扫描判据独立于编译守卫。
5. **Task 9**：run.sh/make-variant.sh 原硬编码被测文件 `OpEngineService.inl`，新变体各自声明
   `mutated` 字段，restore/清洁检查按变体执行。
6. **Task 8**：计划用了不存在的宏 `BOOST_FIXTURE_TEST_SUITE_END()` → `BOOST_AUTO_TEST_SUITE_END()`；
   rpc 测试 unity 开启，新文件加 `SKIP_UNITY_BUILD_INCLUSION`。

## 最终验收清单结果

全部 12 条通过（见最终汇报；Task 10 的 INDEX-OPFEAT 回写因 karst-on-5550 只读约束以
pending 文档交付）。
