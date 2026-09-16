# OP Stack 全 fork 特性审计 —— delta 报告（基线：237 条款三合一交叉检查）

## 1. 元信息

| 项 | 值 |
|---|---|
| 日期 | 2026-09-12 |
| fork / 范围 | 全部 9 fork（Regolith→Karst），**delta 审计**：以 237 条款基线（`docs/2026-08-21-opstack-el-spec-audit-v2.md` + `2026-08-22-opstack-el-triple-crosscheck.md`，HEAD `0650ba623` @ 2026-08-22）为起点，复核遗留 ❌/🟡 的闭合状态 + 条款化 08-22 后新增面 |
| FISCO 侧 | 分支 `feat/karst-on-318-merged`，HEAD `d249092e7`；工作区未提交改动：无（仅 untracked docs/plans/） |
| specs 基线 | `/Users/octopus/octo/code/blockchain-impl/ethereum-optimism-specs/` @ `689a96f`（2026-06-19）；另用 `optimism/op-ai-obsidian/protocol/` 中文镜像（fork 增量页） |
| op-geth 参照 | `/Users/octopus/octo/code/blockchain-impl/op-geth/` @ `e8800cffe`（含 KarstTime + GetPayloadV5） |
| op-reth 参照 | 未使用（本 delta 无 op-geth 缺口条款需要它；Karst 上限的锚点为 op-revm） |
| op-revm 参照 | `blockchain-impl/optimism/rust/op-revm` @ `5f90f749ca`（Karst 常量锚点）；注意本地 revm 依赖 checkout `eefdfb53ab` 无 `eip7825::TX_GAS_LIMIT_CAP`（见 G-N2） |
| 方法 | 逐条款双轨证据，三路并行审计（Karst 重审 / 非-Karst 残留复核 / 新增面条款化），见 `.agents/skills/fisco-opstack-fork-audit/SKILL.md` |

## 2. 总体结论（相对 237 条款基线的 delta）

- **原 11 ❌ 的去向**：BL-2 eth_getProof **✅ 关闭**；Karst 8 ❌ → **2 ✅ 关闭 + 5 ➖ 改判（CL 职责，op-geth 同样未实现，行业一致）+ 1 🟡**；BL-1 拆两半：S-DRV-6 **✅ 关闭**（SYNCING/plane 纪律）、S-DRV-7 **🟡 降级为已知差距**（committed-tip 兄弟执行 ReorgUndo 明确 deferred）。
- **原 6 🟡 的去向**：S-EXE-17（错误码映射）、S-EXE-5/32（FCU attrs 深校验）**全部 ✅ 关闭**（`EngineRpcTest` 14 case、`EngineHelper.cpp:490-580`）。
- **新增面（08-22 后）**：14 条新条款 → ✅10 / 🟡2 / ❌0；**1 个新分歧 G-N1**（7825 超限 deposit 的可观测结果）。
- **重要谱系发现**：Jovian handoff 声称的 P0-1 commit `68bdf3695` **不在本分支谱系**（`git merge-base --is-ancestor` 失败）；本分支的 reorg 能力走另一条实现路线（`3e07d28c2c` defer one-level tip reorg），按本 head 实际代码判定。

**delta 后全量状态**：❌ 由 11 → **2**（S-DRV-7 的 reorg-undo、S-RPC-6 的历史-tag 测试锚缺失——后者代码已闭合）；🟡 由 6 → **2**（S-DRV-7、S-KAR-5 eth_config 零测试）+ 新增 🟡 1（G-N1）；无新增 ❌。

## 3. 需求矩阵（delta 部分；237 条款基线不重复，见 v2 文档）

### 3.1 Karst（M7）重审 — 8 ❌ 逐条

| S-号 | 旧→新 | FISCO 证据 | op-geth（e8800cffe）证据 | 测试证据 | 备注 |
|---|---|---|---|---|---|
| S-KAR-1 bn256Pairing 57,600B | ❌→✅ | `OpPrecompiles.cpp:42-46`；`OpForkSchedule.cpp:220-233` | `protocol_params.go:194` 仅 Jovian 81984 | `OpOsakaSemanticsTest.cpp:688`；`OpPrecompilesTest.cpp:133-138` 对照 op_revm_oracle | op-geth 无此值，FISCO 以 op-revm 钉扎（领先参照） |
| S-KAR-5 eth_config (EIP-7910) | ❌→🟡（**勘误 2026-09-12**） | 上述 file:line 取自 v2 审计的 **`feat-opstack-e2e`/`engine-cutover` 谱系**；**本分支 `git grep eth_config -- bcos-rpc` 为空**（`53534e6cbd` 非 HEAD 祖先，rc=1） | op-geth internal/ethapi Config | **本分支零实现零测试** | **G-K1 改为 branch-dependent**：实现与自带 6 用例在 `feat/engine-cutover-on-prereqs`；本分支无从补测（WI-33 关闭为阻塞） |
| S-KAR-9/11/12/13 NUT/bundle | ❌→➖ | 生产代码 0 命中（EL 执行面 deposit 路径在 `OpTransition.cpp` runDeposit） | op-geth 0 命中（`git grep nut bundle` 空）；生成方为 op-node `upgrade_transaction.go` | `OpKarstActivationTest.cpp:222/262/273` 激活块 deposits-only | 双方均未实现，行业一致 |
| S-KAR-10 Karst 激活 | ❌→✅ | `OpForkScheduleCodec.h:75-85`、`OpForkSchedule.cpp:237/:268-280/:282`；消费 `OpScheduler.h:1280+` | `params/config.go:519/:1024-1025/:1087-1088`、`genesis.go:324/403` | `OpKarstReleaseGateTest.cpp:21-27`、`OpKarstActivationTest.cpp:262-283`、`OpEngineKarstProfileTest.cpp` | 三重占位全部拆除 |
| S-KAR-14 激活块 gas = Σ bundle gasLimit | ❌→➖⚠️ | 无实现；**G-K2 需人工裁决**：FISCO 自建块（buildOpPayload）脱离 op-node 时 Σ gasLimit 无来源 | `git grep UpgradeGas e8800cffe -- core miner` 空 | 无 | 双方均未实现 |

### 3.2 非-Karst 残留复核

| S-号 | 旧→新 | FISCO 证据 @ `d249092e7` | 测试锚点 | 备注 |
|---|---|---|---|---|
| BL-1/S-DRV-6 非tip parent -32603 | ❌→✅ | `OpEngineService.inl:1076-1085`（SYNCING）；`:1087-1134` 非tip parent 落 stored parent plane | `OpEngineServiceParityTest.cpp:1149`、`OpEngineImportFcuTest.cpp:328` | 不再 blanket -32603 |
| BL-1/S-DRV-7 FCU head 回退 | ❌→🟡 | `EngineTracker.cpp:106-121`（不静默忽略）；`OpEngineService.inl:1521-1560`（whole-plane 重建）；**ReorgUndo deferred**：`OpScheduler.h:12/:653-661`，E2E `#if 0`（`OpNewPayloadRpcE2eTest.cpp:1654-1880`） | `EngineTrackerTest.cpp:382`、`OpEngineImportFcuTest.cpp:890/1355/1526` | committed-tip 兄弟执行仍不可落地；handoff 的 `68bdf3695` 不在本谱系 |
| BL-2/S-RPC-6 getProof 非创世 | ❌→✅ | `EthEndpoint.cpp:1344-1365/:1415-1420`（任意 blockTag + 请求块 header stateRoot） | `EthGetProofIntegrationTest.cpp:206-248` | 测试全用 `"latest"` tag——非-latest 显式锚缺失（Minor） |
| S-EXE-17 错误码映射 | 🟡→✅ | `utils/Common.h:33-38`（-38002/-38003…，新增 `-38006 TooDeepReorg`）、`EngineEndpoint.cpp:164-180` | `EngineRpcTest.cpp:269/426/444/1114/1137/1244` | — |
| S-EXE-5/32 FCU attrs 深校验 | 🟡→✅ | `EngineHelper.cpp:490-580`（withdrawals/beaconRoot/逐条 raw-tx/noTxPool/gasLimit/eip1559Params） | `EngineProtoAlignB1Test.cpp:78-314`（14 case） | 校验顺序与 op-geth 一致 |
| （新观察）forkchoiceUpdatedV4 | ➖ | `EngineEndpoint.cpp:146-149` unimplemented（注释：Karst 构块走 V3，有意识决定） | — | 非回归 |

### 3.3 新增面条款化（08-22 后）

| 编号 | spec/op-geth 出处 | FISCO 证据 | 测试锚点 | 判定 |
|---|---|---|---|---|
| S-7825-1 | op-geth `protocol_params.go:42` MaxTxGas=1<<24；`state_transition.go:381-382` `>` + ErrGasLimitTooHigh | `transaction.hpp:17`、`state.cpp:385`（同 `>`、Osaka 门、MAX_GAS_LIMIT_EXCEEDED） | `KarstOrdinaryTxRejectsGasOverEip7825Cap:614` | ✅ 三方一致 |
| S-7825-2 Osaka 门控 | op-geth `state_transition.go:381` | `state.cpp:385` `rev >= EVMC_OSAKA` | `OverCapTxIsNotRejectedByEip7825BeforeOsaka:746` | ✅ |
| S-7825-3 eth_call 跳过 | op-geth `transaction_args.go:495` SkipTransactionChecks | `enforce_max_tx_gas=false`（`state.hpp:170-172`） | `KarstEthCallSkipsEip7825MaxGasLimit:637` | ✅ |
| S-7825-4 超限 deposit 可观测结果 | op-geth `state_transition.go:484-495`：**失败回执**（回滚、nonce+1、记全额 gas） | FISCO：直接豁免（`OpForkSchedule.cpp:230`、`OpTransition.cpp:575`）→ **成功执行 21000** | `DepositExemptFromEip7825MaxGasLimit:660-685` | 🟡 **G-N1**：机制分歧（"不 brick 派生"目标双方满足，可观测结果不同） |
| S-BND-1 regolith rev | op-revm `spec.rs:40` MERGE(=PARIS) | `OpForkBoundarySweepTest.cpp:70/:61-64` 显式 pin LONDON | `EachForkSwitchesExactlyAtItsActivation:198` | 🟡（= 既有 F-A1，非新发现） |
| S-BND-2 extraData layouts | op-node `eth.ExecutionPayload` 注释 | `OpForkBoundarySweepTest.cpp:227-229` | 同上 | ✅ |
| S-BND-3 operator fee / DA flags | Isthmus/Jovian specs | `:183-188` | 同上 | ✅ |
| S-BND-4 边界 ±1 | — | `ActivationTimestampsAreExact:248-265` | 同名 | ✅ |
| S-GRAN-1 bn256 112687 | specs `granite/exec-engine.md:17`；op-geth `protocol_params.go:172` | `OpPrecompiles.cpp:59-62/:26-32` | `GraniteBn256PairingAt586/587:777/:793` | ✅ |
| S-GRAN-2 Jovian 81984 | specs `jovian/exec-engine.md:193`；op-geth `:194-197` | `OpPrecompiles.cpp:34-40` | `KarstBn256PairingCapsAt300Pairs:695` | ✅ |
| S-GRAN-3 Karst 57600 | specs `karst/exec-engine.md:20-22`；op-revm `precompiles.rs:223`；**op-geth 无** | `OpPrecompiles.cpp:45-51`（P256 6900=protocol_params.go:184） | 同 case `:698-714` | ✅（op-revm 锚点） |
| S-ORA-1 G2 oracle 契约 | op-geth `bn256Pairing.json` two_point_match_3 pair-1 G2 | `tools/op-geth-oracle/extract.sh` + `op_geth_oracle.json`；运行时加载 `:324-333` | G2 oracle 用例 `:316` 起 | ✅ 可再生契约 |
| E8 复核 | Holocene/Jovian extraData 版本校验 | `JovianExtraDataTest.cpp:282-284` | 同文件 | ➖ 已覆盖 |

## 4. 差距清单（delta 后全量）

| 编号 | 级别 | 条款 | 问题 | 修复指向 | 参照 |
|---|---|---|---|---|---|
| G-1（原 BL-1 残半） | Major | S-DRV-7 | committed-tip 兄弟块执行/undo（ReorgUndo）deferred，E2E `#if 0` | `OpScheduler.h:653-661` 起的 scheduler slice；E2E 解禁 | — |
| G-2 | Major | S-7825-4 | 超限 deposit 可观测结果与 op-geth 分歧（成功 21000 vs 失败回执全额 gas） | **需人工裁决**：改 FISCO 侧 deposit 路径（`OpTransition.cpp:575` 一带）对齐 op-geth，或登记为有意豁免（决策来源） | op-geth `state_transition.go:484-495` |
| G-3 | Minor | S-KAR-5 | eth_config 实现就绪但**零测试** | `bcos-rpc/test/...` 增加 current/last 断言 | op-geth internal/ethapi Config |
| G-4 | Minor | S-RPC-6 | getProof 代码闭合但缺**非-latest tag** 的显式测试锚 | `EthGetProofIntegrationTest` 增历史 tag 用例 | — |
| G-5 | Minor | S-KAR-14 | 自建块（不经 op-node）场景的激活块 Σ upgradeGas 无来源 | 需人工裁决归属（CL vs EL vs 不适用） | op-node `upgrade_transaction.go:115-142` |
| 已知差距 | — | S-BND-1 | regolith rev=LONDON（F-A1，登记偏离） | 独立评审立项 | op-revm `spec.rs:40` |
| 已知差距 | — | S-DRV-7/E7 | F-A2：engine 缺 blobGasUsed==本地重算等值校验 | Plan E Stage 2 §4（待授权） | op-geth `block_validator.go:127` |
| 已关闭 | — | BL-2、S-DRV-6、S-EXE-17、S-EXE-5/32、S-KAR-1、S-KAR-10 | 本次 delta 确认闭合（各见 §3 证据） | — | — |

## 5. 测试补齐清单

| 条款 | 建议落点 | 断言内容 |
|---|---|---|
| S-KAR-5 (G-3) | `bcos-rpc/test/unittests/rpc/EthConfigTest.cpp` | eth_config current/last 返回的字段与 fork 档位 |
| S-RPC-6 (G-4) | `EthGetProofIntegrationTest.cpp` | 非创世数字 blockTag 的 stateRoot 出证 + 拒绝用例 |
| S-KAR-14 (G-5) | 待裁决后定 | 自建块激活块的 gasLimit 提升与次块恢复 |
| 其余 | Plan E Stage 2（`...-stage2-impl-r2.md`）已排期：E4 BLS 5 格、E7 4 格（含 F-A2 缺陷格）、E6 残格、E3/E5、E9–E12 | 见该计划 §0 验证契约 |
