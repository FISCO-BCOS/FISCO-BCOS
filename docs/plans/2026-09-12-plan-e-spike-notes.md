# Plan E Task 0 台账：S1–S7 spike + S8 全量查重 + E8 输入

- 任务：Plan E（OP fork 覆盖补漏）Task 0 —— 纯取证，不写测试、不改被跟踪文件。
- 日期：2026-09-12
- Worktree：`/Users/octopus/octo/code/FISCO-BCOS/.worktrees/merge-318-rehearsal`（分支 `feat/karst-on-318-merged`）
- 外部 oracle pin：
  - op-geth `e8800cffe53d459cde8a07c8e8f1de9d86e79e07`（实测 `git rev-parse HEAD`）
  - op-revm(optimism) `5f90f749caea14398554afb75062f7111b1fc554`（实测）
  - specs：`/Users/octopus/octo/code/blockchain-impl/optimism/op-ai-obsidian/protocol/`
- 执行者：ZCode subagent（Plan E Task 0）
- 过程文档：**untracked，不 commit**（`docs/plans/` 当前整目录 `??`）。本任务结束时
  `git status --porcelain | grep -v '^??'` 为空。

> 证据纪律：下文每节先给命令原文，再给**原始输出摘要**；结论只写在「判定」行。
> 凡静态读不出的，一律标 cannot-determine 并给最小读取集，不写成结论。

---

## 0. 环境核对

```
$ git rev-parse --abbrev-ref HEAD
feat/karst-on-318-merged
$ git status --porcelain | grep -v '^??'      # 空
$ git -C .../op-geth rev-parse HEAD           # e8800cffe53d459cde8a07c8e8f1de9d86e79e07
$ git -C .../optimism rev-parse HEAD          # 5f90f749caea14398554afb75062f7111b1fc554
```

---

## S8 全量查重（Task 0 核心产出）

关键字 grep 命令（原计划原样执行，一次）：

```
$ for k in 'MAX_TX_GAS_LIMIT\|7825' 'MAX_TX_GAS_LIMIT' '112687' '4788\|BEACON_ROOTS' \
       '2537\|bls12\|G1ADD' 'executionRequests\|depositRequest\|6110' \
       'operator_fee_charge\|OperatorFeeVault\|operatorFee' 'daFootprint\|DA footprint' \
       'withdrawalsRoot' 'EIP1559Params\|1559Params'; do
    printf '=== %s ===\n' "$k"; git grep -ln "$k" -- '*Test*.cpp' | head -5
  done
```

关键命中（`git grep -n` 精化后；只列与拟建格相关者）：

| 格 | 关键字 | 既有覆盖（文件:行 / 用例名） | 决定 |
|---|---|---|---|
| **E1a** ① `limit+1` 拒绝 | `MAX_TX_GAS_LIMIT`/`7825` | `OpOsakaSemanticsTest.cpp:603` `KarstOrdinaryTxRejectsGasOverEip7825Cap`（== `MAX_GAS_LIMIT_EXCEEDED`） | 已覆盖 |
| **E1a** ② `enforce_max_tx_gas=false` 接受 | 同上 | `OpOsakaSemanticsTest.cpp:626` `KarstEthCallSkipsEip7825MaxGasLimit` | 已覆盖 |
| **E1a** ③ deposit 超限豁免 | 同上 | `OpOsakaSemanticsTest.cpp:649` `DepositExemptFromEip7825MaxGasLimit` | 已覆盖 |
| **E1a** ④ 恰等于上限**合法**（合法侧边界） | 同上 | 无（三例均用 `+1`） | **新格** — (a) `tx.gas_limit = evmone::state::MAX_TX_GAS_LIMIT`（=0x1000000）时 `opValidate(...)` 返回 `OpTxProperties`（`holds_alternative<OpTxProperties>`），无 `MAX_GAS_LIMIT_EXCEEDED`；(b) 目的地 `bcos-evm/test/opstack/OpOsakaSemanticsTest.cpp`（`DepositExemptFromEip7825MaxGasLimit` 附近） |
| **E1a** ⑤ pre-Osaka（Jovian）上限不适用 | 同上 | 无 | **新格** — (a) Jovian 配置 + `gas_limit = MAX_TX_GAS_LIMIT + 1`，断言：若返回 `error_code` 则 `!= MAX_GAS_LIMIT_EXCEEDED`（钉"该规则不适用"，不断言整体通过）；(b) 目的地 `bcos-evm/test/opstack/OpOsakaSemanticsTest.cpp`。**结果（已执行）**：commit `597bb6d00`；
套件 16→**18**、二进制 178→**180**；helper 实名 `jovianCfg()`（**makeJovianBlock 已删**——实测
`BlockInfo` 无 fork 字段，`makeOsakaBlock()` 直接复用）；反向验证 `state.cpp:385` `>`→`>=` 实测 RED 并还原。 |
| **E2** Granite/Isthmus 112687 执行边界 | `112687` | 仅**表值**：`OpForkScheduleTest.cpp:243 FjordOnwardCarryP256VerifyAndGraniteCapsBn256`、`OpPrecompilesTest.cpp:29 Bn256PairingInputLimitNoGasOverride`（均断言 `max_input_size==112687`，无执行面） | **新格** — (a) 下侧 586 对=112512B ≤112687 → 预编译执行成功（receipt status 0）；上侧 587 对=112704B >112687 → 被拒（`EVMC_FAILURE`/receipt status≠0）；输入用**合法曲线点**重复拼接（禁用垃圾字节）；(b) 目的地 `bcos-evm/test/opstack/OpOsakaSemanticsTest.cpp`（helper 实名 **`repeatInfinityG1Pairs`**——
原拟"合法对重复"被 F-E2-2 证伪：op-geth 无单对 true 向量，实际构造 = G1 无穷远点 + `two_point_match_3`
的 G2 点，每对 e==1，1 对 probe 先行验证）。**结果（已执行）**：commit `48b1bdaa5`；套件 18→**20**、
二进制 180→**182**；标签实测限 granite/holocene/isthmus（F-E2-1）；反向验证：kGraniteEntries
112687→112704 实测 RED + 注册变体 `V-GRANITE-bn254cap` 复跑 RED（exit 0，还原干净）。 |
| **E2** Jovian 81984 边界 | — | `OpHostTest.cpp:183` `JovianBn256PairingInputOverLimitFails`、`:213` `JovianBn256PairingInputAtLimitExecutes`；`OpNewPayloadRpcE2eTest.cpp:1032` `JovianPrecompileBn256PairOvercap` | 已覆盖（Jovian 档） |
| **E2** Karst 57600 边界 | — | `OpOsakaSemanticsTest.cpp:677` `KarstBn256PairingCapsAt300Pairs`（300 对成功 / 301 对 `EVMC_FAILURE`） | 已覆盖（Karst 档） |
| **E3** Ecotone+ 读系统合约得 beacon root | `4788`/`BEACON_ROOTS` | 无直接执行面用例；间接：`OpNewPayloadRpcE2eTest.cpp:831/879` `Isthmus/JovianSystemContractsReal` golden vector | **新格**（① 需在 R2 确认 golden 是否含 beacon slot） |
| **E3** pre-Ecotone 无合约/空返回；未记录时间戳→空返回 | 同上 | 无 | **新格** |
| **E4** EIP-2537 执行面（0x0b/0x0c/0x0d/0x0f…） | `2537`/`bls12`/`G1ADD` | 仅**旧 bcos-executor 路径**：`PragueTest.cpp:98-121`、`PragueE2ETest.cpp:108-130`、`CompatPrecompileGatingTest.cpp:145+`；OP 路径只有 `OpForkScheduleTest.cpp:272` 表断言「0x0c 属 PRAGUE」 | **新格**（evmone 支持面见 S1） |
| **E8** Holocene/Jovian version 负向 | `version`/`extraData` | `JovianExtraDataTest.cpp:282-284 execution_payload_extra_data_shape_is_validated`（双向 `rejectNeedle`） | **已覆盖（+0）** |
| **E5** ① OP 头 `requestsHash == c_emptyRequestsHash` | `executionRequests` | 疑覆盖：`EngineServiceTest.cpp:2077 finalizeEthBlockHeaderVersionGatesFields`（PRAGUE → `e3b0c442…b855`）；`EngineServiceTest.cpp:1493 karst_v3_build_v5_get_v4_commit_round_trip`（getPayloadV5 `executionRequests` present-and-empty） | **已覆盖（疑似，待 R2 确认是否 OP 引擎路径）** |
| **E5** ② 非空 requests 拒绝 | 同上 | `EngineServiceTest.cpp:1776 new_payload_v4_rejects_nonempty_lists_and_missing_fields`（`executionRequests={0x01}` → Invalid/"executionRequests"；缺失亦拒绝） | **已覆盖（疑似）** |
| **E5** ③ 6110 排除语义 | `6110`/`depositRequest` | 无直接用例（rpc 层 `EngineRpcTest` 仅 wire shape） | **新格** — (a) 期望：含 deposit 的块在 OP 路径 `getPayloadV5.executionRequests` 仍为空、`requestsHash==c_emptyRequestsHash`（本节点不合成 6110 请求）；**缺**：spec `isthmus/exec-engine.md` requests 节是否要求排除 deposit 的定论 + `Constants.h:30-34` 的请求类型清单；(b) 目的地 `engine/test/unittests/engine/OpEngineRequestsSemanticsTest.cpp`（新文件，需 CMake 重配） |
| **E6** ① Isthmus scalar-only | `operatorFee`/`OperatorFeeVault` | `OpTransitionTest.cpp:99-102 RoutesFeesToFourVaults`：vault balance `== gasUsed`（base/l1/sequencer 同测） | 已覆盖 |
| **E6** ② constant+scalar 公式 | 同上 | `OpTransitionTest.cpp:282-301 JovianReceiptMetaAndOperatorFormula`（`gasUsed*scalar*100+constant`）+ `:318` vault balance | 已覆盖 |
| **E6** ④ 零参数零收费 | 同上 | `OpZeroFeeSpikeTest.cpp:84 zero_fee_params_produce_zero_costs` `computeOperatorCost(...)==0` | 已覆盖 |
| **E6** ③ Jovian 公式差 / ⑤ 未用 gas 退款 | 同上 | 两分支已**分别**覆盖（Isthmus：`RoutesFeesToFourVaults`；Jovian：`JovianReceiptMetaAndOperatorFormula`，公式分支在 `RollupCost.cpp:260-277`）；退款无断言 | **新格** — (a) ⑤ 期望：vault 净余额 `== computeOperatorCost(fee, gasUsed)`；退款额 `== charge(gasLimit) - charge(gasUsed)`，其中 Jovian `charge(g)=g*scalar*100+constant`、Isthmus `g*scalar/1_000_000+constant`（op-revm `l1block.rs:184-196` + `constants.rs:28,31`）；deposit/空输入 `charge==0`；③ 若做差分格：同输入下两分支比值 100× vs 1e-6×；(b) 目的地 `bcos-evm/test/opstack/OpOperatorFeeChargeTest.cpp`（新文件） |
| **E6** ⑥ 受益账户 = OperatorFeeVault | 同上 | `OpTransitionTest.cpp:99-102 RoutesFeesToFourVaults` 与 `:282-318 JovianReceiptMetaAndOperatorFormula` 已按 vault 余额断言；`OpPredeploysTest.cpp:19 AddressesMatchOpStackNamespace`（地址）、`:32 SeedCreatesSixPredeployAccounts` | 已覆盖（余额口径） |
| **E7** ① per-tx 足迹公式 | `daFootprint` | `OpTransitionTest.cpp:277-281 JovianReceiptMetaAndOperatorFormula`、`OpReceiptMetaTest.cpp:80 JovianFillsDaFootprint`、`OpReceiptMetaTest.cpp:105 DaFootprintFollowsSnapshotNotTransitionCfg`、`OpFeeParamsTest.cpp:91 UnpacksDaFootprintGasScalarFromSlot8`（scalar 槽解包） | 已覆盖 |
| **E7** ②③ 块级 Σ 与 `< gasLimit`/`== gasLimit` 边界 | 同上 | **无既有测试覆盖**；仅实现位置 `OpBlockExecute.cpp:508-525`（Σ→`seal.blobGasUsed`）+ `OpEngineService.cpp:197`（`>`，非 `>=`） | **新格（且 ③ 为缺陷证明格，见 S3）** — (a) ② Σ `< gasLimit` → 有效；③ `== gasLimit` 当前被**接受**（`>` 判定），按 spec "below" 应为拒绝 → 格须按缺陷证明（红）设计，落脚前先定 spec 严格性；(b) 目的地 `opstack-executor/tests/OpDaFootprintTest.cpp`（新文件） |
| **E7** ⑤ deposit-only 块足迹 0 | 同上 | 无既有测试覆盖；仅实现位置 `OpBlockExecute.cpp:510-514`（跳过 deposit）+ `OpCommon.h:67-72` 注释 | **新格**（可选） — (a) 期望：仅含 deposit 的 Jovian 块 `seal.blobGasUsed == 0`（第一条 deposit 跳过、Σ 无项）；(b) 目的地 `opstack-executor/tests/OpDaFootprintTest.cpp` |
| **E8** (a) Holocene 档 `version!=0` 被拒 | `EIP1559Params`/`version byte` | `JovianExtraDataTest.cpp:284`（9B+0x01→"version byte does not match length"）；`CalcOpBaseFeeTest.cpp:133-141`；`EngineServiceTest.cpp:1696` | **已覆盖** |
| **E8** (b) Jovian 档 `version!=1` 被拒 | 同上 | `JovianExtraDataTest.cpp:282-283`（17B+0x00→同错）；`CalcOpBaseFeeTest.cpp:143-152`（`VersionByteMismatchIsRejected`）；`EthEngineServiceParityTest.cpp:1314` | **已覆盖** |
| **E10** Isthmus `withdrawalsRoot` 三态 | `withdrawalsRoot` | 大量：`OpEngineApiVersionsTest.cpp:225`、`OpEngineReviewFixTest.cpp:436`、`OpEngineServiceParityTest.cpp:1237/1364`、`EngineServiceTest.cpp:796/829`、`EngineTrackerTest.cpp:763` | 大部分已覆盖；三态剩余格在 E10 R2 复核 |

**查重净效应（直接决定格数）**：
- E1a：三例已覆盖 → **只补 2 格**（恰等上限合法、pre-Osaka 不适用），与计划一致。

## Stage 2 执行结果回填（R2-Z，2026-09-12）

| WI / Task | 结果 | commit | 实测 |
|---|---|---|---|
| WI-24 查重门 | ✅ 双审通过 | 无（台账节） | 净增 **9 格**：E3 +2 / E7 +3 / WI-33 +2 / WI-34 +2；E4/E5/E6/E10/E11/E12 +0（既有覆盖）；spec 审查 refuted E4 的"+5"理由（corpus 值级钉 output）并已更正 |
| WI-26 / E7 | ✅ 双审通过 | `380c775d2` | executor 143→**146**：③ 边界 3 断言、② 等值缺陷格（`expected_failures(1)` 承载 F-A2，RED 事实已记录）、⑤ deposit-only 零封；变体 `V-DAFOOTPRINT-boundary` RED as required |
| E3 | ✅ 双审通过 | `1993d9fe5` | beacon root 2 格（pre-Ecotone 档位 gate + 未记录时间戳 revert 空）；97 字节部署码与 corpus 逐字节 MATCH；bcos-evm-eth-tests +2 |
| WI-34 | ✅ 完成（独立审查代理超时，控制台代跑同一检查清单） | `746a80189` |
| WI-35（G-N1 裁决） | ❌ **已作废（2026-09-13 勘误）** | `6a7e8733b` | **原依据是伪造引证，WI-35 重开**：所引 `specs/protocol/karst/overview.md:20` 在 pin `564a0ce` 上不存在（该文件 464 字节 / 16 行，末行即 `## Consensus Layer`；全 specs 树唯一 7825 命中在 `flashblocks.md:615`，`git log --all -S` 为空）。20M 依据真实（`guaranteed-gas-market.md:48` `MAX_RESOURCE_LIMIT=20,000,000`）但属 L1 侧担保气体、非 EL 规则 ⇒ **对齐方向未定**。行为分歧真实：FISCO 豁免（`OpForkSchedule.cpp:230`+`OpTransition.cpp:575`）vs op-geth 在 **preCheck** 套 2^24（`core/state_transition.go:379-383`；全树只有 eth_call 置 `SkipTransactionChecks`），且原记录"op-geth 记成失败回执"不准（容差 `:489-496` 只覆盖执行期错误）。已修 `DIVERGENCES.md` 条目与测试注释；待转交通知对方重开其 finding |
| WI-35（原裁决，已作废备查） | ✅ 当时判定 | `6a7e8733b` | 原记录：维持 FISCO 豁免，称 spec `karst/overview.md:20` 明文"not enabled for deposits"，2^24 < 20M ⇒ (2^24, 20M] spec-legal、op-geth 偏离；测试注释补出处；`DIVERGENCES.md` 新增 `eip7825_deposit_exemption`。**其 spec 依据不存在**（见上一条） |
| 计数口径勘误 | — | — | WI-34 实现者报的 `--list_content` 372→374 非本仓判据口径；`Running N` 实测 **334→336**；终审 Minor 同点已记 | getProof 历史 tag 2 格（块3 出证 / 错 root 拒绝 / 缺席 key 空证）；rpc 334→**336**（`Running N` 口径；实现者报的 372→374 是 `--list_content` 口径，非本仓判据） |
| WI-26 终审整改 | ✅ 复审 Approved | `8e868b41a` | 终审指出 F-A2 格的 local Σ 耦合守卫**同义反复**（`local` 取自同一 header）→ 改为 `localDaFootprintOfGoldenVector()` 独立重算（`flzCompressLen`/`estimatedDaSizeFromFlz`×scalar，不读 `blobGasUsed`）+ 双向断言；实测 Σ=593600 = header = 各 receipt `_op_da_footprint` 之和 |
| WI-33 | ❌→**阻塞（前提失效）** | 无 | `eth_config` 不在本分支（`git grep` 空；实现于 `feat/engine-cutover-on-prereqs` `53534e6cbd`，非祖先）→ delta 审计 S-KAR-5 已勘误为 branch-dependent |
| E5 / E6 / E4 / E10 / E11 / E12 | ✅ 关闭（+0） | 无 | 由 WI-24 查重裁定：既有用例名级覆盖（详见 Stage 2 查重表） |
| E9 夹具 | ✅ 取消 | 无 | E10–E12 均 +0 后无消费者 |

## Stage 1 验证块回填（§0.2 契约，2026-09-12）

### E1a（commit `597bb6d00`）
- 查重证据：本表 E1a ①–⑤ 行（三例已覆盖，两格新格）。
- oracle 引用：本仓 `transaction.hpp:17`（`MAX_TX_GAS_LIMIT=0x1000000`）、门控 `state.cpp:385`；
  外部三方对账见 S5 节（op-geth `params.MaxTxGas=1<<24`、op-revm `eip7825::TX_GAS_LIMIT_CAP`）。
- 断言强度：④ 结果级（接受侧）+ 既有 ① 拒绝侧 → `>` 双侧钉死；⑤ 不变量级（"该规则不适用"，
  两路径恒执行一断言）。
- 反向验证：`state.cpp:385` `>`→`>=` → `OrdinaryTxAtExactlyEip7825CapIsAccepted` 实测 RED
  （critical check holds_alternative 失败）；还原后 `git diff` 空、18/18 复绿。
- 预期 RED 项：无。计数增量：bcos-evm-opstack-tests +2。
- 台账回填：本节与上表 ④⑤ 行。

### E2（commit `48b1bdaa5`）
- 查重证据：本表 E2 两行（Granite/Isthmus 执行面空缺；Jovian/Karst 档已覆盖，不重复）。
- oracle 引用：specs `granite/exec-engine.md`（112687）；G2 点 = op-geth `e8800cffe`
  `bn256Pairing.json` 的 `two_point_match_3`（Expected=true）；上限常量 `OpPrecompiles.cpp:60/:27`。
  **G2 已迁移至 oracle 契约**：`bcos-evm/test/opstack/op_geth_oracle.json`（由
  `tools/op-geth-oracle/extract.sh` 再生，`d249092e7`），测试改为运行时加载。
- 断言强度：双侧尺寸边界（586 成功 / 587 拒绝）；拒绝格为结果级（F-S4-1 残留：两类 revert
  暂不可判别，由注册变体补偿——widen 后 587 变绿即证明拒绝由 cap 引起）。
- 反向验证：手工（kGraniteEntries 112687→112704 → 587 格 RED，实测 `[0 == 0]`）+ 注册变体
  `V-GRANITE-bn254cap`（`run.sh` RED as required，exit 0，还原干净）。
- 预期 RED 项：无。计数增量：bcos-evm-opstack-tests +2。
- 台账回填：本节与上表 E2 行。

### E8（+0，无 commit）
- 查重证据：`JovianExtraDataTest.cpp:282-284 execution_payload_extra_data_shape_is_validated`
  对 (a) Holocene version≠0、(b) Jovian version≠1 均有双向拒绝断言（`rejectNeedle` 原文引于
  「E8 输入」节）→ 两格已覆盖，不建格。
- **E8：+0**，(a)(b) 均已被 `JovianExtraDataTest` + `CalcOpBaseFeeTest` 双层覆盖（见「E8 输入」）。
- E5：①② 疑似已覆盖，E5 可能降到 **+0~1**（R2 定夺，见 finding）。
- E6：① ② ④ ⑥ 已覆盖 → 新格应只针对 **③ Jovian 公式差、⑤ 退款**。
- E2：Granite 执行面确是空缺，但**计划给的 fork 标签有误**（见 finding F-E2-1）。

---

## S1 evmone 的 EIP-2537 支持面

命令与原始输出：

```
$ EV=/Users/octopus/octo/code/FISCO-BCOS/build/vcpkg_installed/arm64-osx/include/evmone
$ grep -rn 'BLS12_381\|bls12' "$EV" | head -5
（空）
$ ls "$EV" | head
advanced_analysis.hpp advanced_execution.hpp baseline.hpp ... vm.hpp      # 核心头无 BLS
$ cat .../share/evmone/evmoneConfigVersion.cmake
set(PACKAGE_VERSION "0.21.0")
$ ls .../include/evmone_precompiles
bls.hpp bn254.hpp kzg.hpp secp256k1.hpp secp256r1.hpp ... pairing/bn254/
```

`bls.hpp`（`namespace evmone::crypto::bls`）符号（原文）：
`g1_add,g1_mul,g2_add,g2_mul,g1_msm,g2_msm,map_fp_to_g1,map_fp2_to_g2,pairing_check`，
并有 `BLS_FIELD_MODULUS`。头注释逐条指向 EIP-2537 ABI。

op-geth 向量：
```
$ ls .../op-geth/core/vm/testdata/precompiles/bls*.json
blsG1Add.json blsG1Mul.json blsG1MultiExp.json blsG2Add.json blsG2Mul.json
blsG2MultiExp.json blsMapG1.json blsMapG2.json blsPairing.json
```

判定：**evmone 0.21.0 提供完整 EIP-2537 原语**（在 `evmone_precompiles`，核心 `evmone/` 头
不含 BLS 字样，故原计划 `grep BLS12_381\|bls12` 在主 include 目录 0 命中——不是"无支持"）。
op-geth BLS 向量齐全，可作 E4 对照 oracle。**E4 不转 finding，按新格执行。**
注意：OP 路径无执行面 BLS 用例（`bcos-evm/test/opstack` 无 `*Bls*` 文件），
`OpBls2537Test.cpp` 仍非重复格。

---

## S2 OP 引擎路径是否喂 parentBeaconBlockRoot

命令与原始输出：

```
$ git grep -n 'parent_beacon_block_root\|setParentBeaconBlockRoot\|parentBeaconBlockRoot' \
    -- engine/bcos-engine opstack-executor/tests bcos-evm/bcos-evm/eth/state | head
engine/bcos-engine/OpEngineService.cpp:425: header->setParentBeaconBlockRoot(parentBeaconBlockRoot.value());
bcos-evm/bcos-evm/eth/state/block.hpp:49: hash256 parent_beacon_block_root;
bcos-evm/bcos-evm/eth/state/system_contracts.cpp:37: return block.parent_beacon_block_root;
$ git grep -n 'setParentBeaconBlockRoot' -- engine/bcos-engine
engine/bcos-engine/EngineServiceCommon.cpp:681  (Eth/通用 header)
engine/bcos-engine/OpEngineService.cpp:425       (OP header, Ecotone+ 分支)
```

执行面注入点（计划未列，实测存在，供 E3 直接引用）：
```
opstack-executor/OpCommon.h:236:  blk.parent_beacon_block_root =
    toEvmcBytes32(requireHeaderField(env.parentBeaconBlockRoot(), ...));
```
消费点（EIP-4788 存储系统合约）：
```
bcos-evm/bcos-evm/eth/state/system_contracts.cpp:35-37
StorageSystemContract{EVMC_CANCUN, BEACON_ROOTS_ADDRESS,
    [](const BlockInfo& block, const BlockHashes&) noexcept {
        return block.parent_beacon_block_root; }},
```
测试可读路径：`evmone::state::BlockInfo::parent_beacon_block_root`（`block.hpp:49`）；
既有夹具已直接赋值先例 `opstack-executor/tests/OpT8nReplayTest.cpp:589`。

判定：**两处都喂**——header 侧 `OpEngineService.cpp:425`（Ecotone+ 盖章）；
执行/EVM block context 侧 `OpCommon.h:236`（由 header 字段搬入 `BlockInfo`）。
E3 的 R2 应引用 `OpCommon.h:236` + `system_contracts.cpp:35-37` + `block.hpp:49`。

---

## S3 DA footprint 是否有块级累加

命令与原始输出：

```
$ git grep -n 'daFootprint\|da_footprint\|FootprintGasScalar\|blobGasUsed' -- \
    bcos-evm/bcos-evm opstack-executor/*.cpp opstack-executor/*.h engine/bcos-engine | head -12
opstack-executor/OpBlockExecute.cpp:508  if (cfg.has_da_footprint)
opstack-executor/OpBlockExecute.cpp:510      uint64_t footprint = 0;
opstack-executor/OpBlockExecute.cpp:511      for (size_t i = 0; i < result.receipts.size(); ++i)
opstack-executor/OpBlockExecute.cpp:519          const auto term = *meta->da_footprint;
opstack-executor/OpBlockExecute.cpp:522          footprint += term;
opstack-executor/OpBlockExecute.cpp:524      seal.blobGasUsed = footprint;
opstack-executor/OpCommon.h:67   // Σ of meta.da_footprint over non-deposit receipts ...
engine/bcos-engine/OpEngineService.cpp:197  if (forkId >= OpForkId::Jovian &&
                                            *payload.blobGasUsed > payload.gasLimit)
```

`OpBlockExecute.cpp:504-525` 原文注释：
`// Jovian: header blobGasUsed slot = DA footprint (Σ da_footprint over non-deposit receipts).`
溢出检查 `footprint > uint64_max - term` → `OpConsensusError`；non-deposit 缺字段亦 `OpConsensusError`。
per-tx 项 `OpTransition.cpp:281`：`m.da_footprint = estimatedDaSizeFromFlz(props.flz_len) * scalar`。

spec（`jovian/exec-engine.md:102-126`）原文（中文译本）：
- `def daFootprint(block)`：跳过 `DEPOSIT_TX_TYPE`，`Σ daUsageEstimate * daFootprintGasScalar`。
- 「在块构建和标头验证期间，必须分别保证和检查块的 `daFootprint` **保持在 `gasLimit` 以下**」。

判定：**「仅 header 槽校验」的前提不成立**——本仓**已有 per-tx 累加实现**
（`OpBlockExecute.cpp:508-525`），且 block 级上限检查在 `OpEngineService.cpp:197`。
但检查符是 `>`（**不是 `>=`**）：`== gasLimit` 会被接受。spec 措辞为 "below"，
若按严格小于解释，则 `==` 应拒绝 → E7 的 ③ 是**缺陷证明格（F-A2 同族）**，不是普通绿格。
② `sum < gasLimit` 有效可作正向格；④ baseFee `max(gasUsed, blobGasUsed)` 在 R2 另行取证。

---

## S4 bn256 size-check 拒绝可否与数学失败判别

命令与原始输出：

```
$ git grep -n 'max_input_size\|MAX_INPUT\|PrecompileOverrides' -- bcos-evm/bcos-evm/opstack | head
OpHost.cpp:128:  if (entry->max_input_size > 0 && (size_t)msg.input_size > entry->max_input_size)
OpPrecompiles.cpp:27..  kIsthmusEntries/kJovianEntries/kKarstEntries/kGraniteEntries ...
```

消费包装层 `OpHost::call`（`OpHost.cpp:114-129`）原文：
```cpp
const auto* entry = m_overrides->find(msg.code_address);
...
if (entry->max_input_size > 0 && static_cast<size_t>(msg.input_size) > entry->max_input_size)
    return evmc::Result{EVMC_FAILURE, 0};
```
错误类型：`EVMC_FAILURE`（`evmc.h:290`，值 **1**），`gas_left==0`。
数学失败在 EVMC 枚举中有独立码 `EVMC_PRECOMPILE_FAILURE`（`evmc.h:346`，值 **12**）。

判定：**枚举层面可判别**（size 拒绝 = `EVMC_FAILURE`=1；evmone 预编译数学失败应为
`EVMC_PRECOMPILE_FAILURE`=12），但 evmone 的 VM 源码不在已安装产物中（`libevmone.a` 无可读
`nm` 符号；`evmone_precompiles/pairing/bn254/pairing.cpp` 仅 field/pairing 实现），
**运行时未实测**。故台账记：
「E2 上侧可断言 `status_code == EVMC_FAILURE`（比 `!= SUCCESS` 更强）—— 但需一次探针确认
evmone 数学失败确为 12；未确认前按计划残留处理：只断言被拒 `!=0` + 残留风险。」
最小读取集：`evmone::state::Host::call` 的预编译失败分支（vendored evmone 源或一次
`osakaHostCall` 坏点探针）。

---

## S5 EIP-7825 常量三方对账

命令与原始输出：

```
$ git -C .../op-geth show e8800cffe...:params/protocol_params.go | grep -n 'TxGasLimit\|7825'
42:  MaxTxGas uint64 = 1 << 24 // Maximum transaction gas limit after eip-7825 (16,777,216).

$ git -C .../optimism show 5f90f749ca...:rust/op-revm/src/spec.rs | grep -n '7825\|MAX_TX'
（空 —— spec.rs 无此常量）

$ git grep -n 'TX_GAS_LIMIT_CAP' 5f90f749ca... -- 'rust/**'
rust/alloy-op-evm/src/env.rs:133: cfg_env.tx_gas_limit_cap = Some(revm::primitives::eip7825::TX_GAS_LIMIT_CAP);
rust/alloy-op-evm/src/env.rs:387: .then_some(revm::primitives::eip7825::TX_GAS_LIMIT_CAP);
# revm-primitives 24.0.1（Cargo.lock），本机 cargo registry 源码：
#   .../revm-primitives-24.0.1/src/eip7825.rs:12
#   pub const TX_GAS_LIMIT_CAP: u64 = 16_777_216;

$ sed -n '15,19p' bcos-evm/bcos-evm/eth/state/transaction.hpp   # 实为 evmone vendored 头
17: constexpr auto MAX_TX_GAS_LIMIT = 0x1000000;  // 2**24      # namespace evmone::state
```

三方对账（全部 = 2^24 = 16,777,216 = 0x1000000）：

| 侧 | 符号 | 值 | 出处 |
|---|---|---|---|
| 本仓（vendored evmone） | `evmone::state::MAX_TX_GAS_LIMIT` | `0x1000000` | `bcos-evm/bcos-evm/eth/state/transaction.hpp:17` |
| op-geth | `params.MaxTxGas` | `1 << 24` | `params/protocol_params.go:42` |
| op-revm→revm | `revm::primitives::eip7825::TX_GAS_LIMIT_CAP` | `16_777_216` | revm-primitives 24.0.1 `src/eip7825.rs:12`，消费于 `rust/alloy-op-evm/src/env.rs:133,387` |

判定：三方一致 = 2^24。**计划写的 `rust/op-revm/src/spec.rs` 不含该常量（0 命中）**；
op-revm 经 revm-primitives 的 `eip7825::TX_GAS_LIMIT_CAP` 引用，符号名以本条为准。

---

## S6 Holocene 时序正文

命令与原始输出：

```
$ grep -n -A10 'Payload Attributes Processing' .../holocene/exec-engine.md | head -20
25:  - [Payload Attributes Processing](#payload-attributes-processing)   # 仅 TOC 命中英文串
$ grep -n '^#' .../holocene/exec-engine.md
99:#### 负载属性处理            # <-- 正文标题被译为中文（即 Payload Attributes Processing）
109:#### 基本费用计算
```

正文（`:99-108`）摘录：
- 「在全新世激活之前，`PayloadAttributesV3` 中的 `eip1559Parameters` 必须为空，否则被视为无效。」
- 「在全新世激活时及之后，与某些 `PayloadAttributesV3` 对应的任何 `ExecutionPayload` 必须包含
  `extraData` 格式为标头值。`分母` 和 `弹性` 值必须与 `eip1559Parameters` 对应，**除非两者都为 0**；
  当两者都为 0 时，必须用先前的 EIP-1559 常量填充 `extraData`。」

判定：**有正文**（是中文译本，英文 grep 只命中 TOC，故原命令 -A10 看似"只有目录"）。
E8 的"时序格"不需删除；但对应规则（pre-Holocene 必须空、0,0→Canyon 常量）已由
`JovianExtraDataTest.cpp:131`、`:94`、`:301` 覆盖，故 E8 仍为 +0。

---

## S7 EIP-7934 角色定性

命令与原始输出：

```
$ git -C .../op-geth show e8800cffe...:core/block_validator.go | sed -n '45,60p'
func (v *BlockValidator) ValidateBody(block *types.Block) error {
    // check EIP 7934 RLP-encoded block size cap
    if v.config.IsOsaka(block.Number(), block.Time()) && block.Size() > params.MaxBlockSize {
        return ErrBlockOversized
    }
    ...
$ # 常量与错误：
params/protocol_params.go:214: MaxBlockSize = 8_388_608 // maximum size of an RLP-encoded block
core/error.go:34: ErrBlockOversized = errors.New("block RLP-encoded size exceeds maximum")

$ git grep -rn 'MaxBlockSize\|EIP-7934\|EIP7934\|ErrBlockOversized\|block size cap'   # 全仓
（空）
```

判定：op-geth **在区块导入（`ValidateBody`）按 `IsOsaka` 门控**执行
`block.Size() > 8_388_608`；**本仓全仓 0 命中**。
结论：**EL 无需实现**（同 M0 格 11「登记为已知偏离，不实现」先例）。
计划要求写 cannot-determine + 最小读取集，故补记：

- cannot-determine：仅就"本仓无对应实现/无对应测试"而言确定；"是否需要"取决于 OP 链是否
  把 7934 视为 EL 强制项（op-geth 是 EL 侧实现，但 OP 出块由 op-node/batcher 约束）。
- 最小读取集：`optimism/op-ai-obsidian/protocol/{isthmus,jovian}/exec-engine.md` 是否有 7934 正文；
  op-node `derive/` 是否有 block-size 拒绝。若两处皆无 → 确认为「EL 无需实现」。
  （本次未展开，标 cannot-determine，不写 finding 编号。）

---

## E8 输入（追加要求 —— 实测签名与覆盖判定）

### 1. 解码/校验侧入口函数（确切签名，原文）

本仓**不存在** `decodeOptimismExtraData`/`decodeEip1559Params(extraData)` 这类"解码整个
extraData"的导出入口。版本字节校验由**形状校验器**承担，共两个层次：

```cpp
// bcos-framework/bcos-framework/engine/OpBaseFee.h  (namespace bcos::engine)
// :59  —— 只解 8 字节 params，size!=8 抛异常；不接触 version
inline std::pair<std::uint32_t, std::uint32_t> decodeEip1559Params(
    std::span<const bcos::byte> params);   // throws bcos::engine::InvalidEngineEncoding

// :74  —— 长度 + version + 非零对；返回错误串，不抛
inline std::optional<std::string> validateOpExtraDataShape(
    std::span<const bcos::byte> extraData, bool allowEmpty = true);

// :107 —— 按 fork 布局校验（OP 引擎路径实际调用者）
inline std::optional<std::string> validateOpExtraDataForLayout(
    std::span<const bcos::byte> extraData, OpExtraDataLayout layout);
```

常量（同头 `:51-54`）：`c_holoceneExtraDataBytes=9,c_jovianExtraDataBytes=17,
c_holoceneExtraDataVersion=0x00,c_jovianExtraDataVersion=0x01`。

上层入口（测试实际使用）：
```cpp
// engine/bcos-engine/EngineServiceCommon.h
// :66  namespace bcos::engine::detail
bcos::bytes encodeOptimismExtraData(const PayloadAttributes&);            // 编码，size!=8 抛 InvalidEngineEncoding
// :68
std::optional<std::string> validateExecutionPayload(
    const ExecutionPayload&, std::uint32_t version);                      // 内含 validateOpExtraDataShape
// :182 namespace bcos::engine::engine_common
std::optional<std::string> validatePayloadAttributes(
    const PayloadAttributes&, std::uint32_t version,
    std::vector<bcos::bytes>* decodedForcedTxs = nullptr);                // 属性侧，不查 encoded version 字节
```
OP 引擎路径调用点：`engine/bcos-engine/OpEngineService.cpp:169` →
`validateOpExtraDataForLayout(payload.extraData, extraDataLayoutFor(forkId))`。

> 关键更正：版本字节校验**不在** `engine::detail`，而在 `bcos::engine`（`OpBaseFee.h`）。
> 计划 Step 2/3 代码骨架里的 `engine::detail::decodeEip1559Params(wrongVersion)` /
> `engine::detail::decodeOptimismExtraData(...)` **符号不存在**，R2 不得照抄。

### 2. (a)/(b) 覆盖判定

**(a) Holocene 档 `version != 0` 被拒 → 已覆盖。**
- `JovianExtraDataTest.cpp:284`（用例 `execution_payload_extra_data_shape_is_validated`，:258）：
  `fromHexWithPrefix("0x01000000fa00000006")`（9B + version 0x01）→ 错误
  `"version byte does not match length"`。
- `CalcOpBaseFeeTest.cpp:133-141`（用例 `VersionByteMismatchIsRejected`）：9B 置 0x01 →
  抛 `"version byte does not match length"`。
- 布局层命名错误：`OpBaseFee.h:123-126` 有 `"version byte must be 0x00 on the OP path
  (Holocene/Isthmus)"`；`OpEngineService.cpp:169` 在 OP 路径消费。

**(b) Jovian 档 `version != 1` 被拒 → 已覆盖。**
- `JovianExtraDataTest.cpp:282-283`：`0x00000000fa000000060000000000000000`
  （17B + version 0x00）→ 同错 `"version byte does not match length"`。
- `CalcOpBaseFeeTest.cpp:143-152`：17B 置 0x00 → 同错。
- 布局层：`OpBaseFee.h:133` 期望 `c_jovianExtraDataVersion`；`EngineServiceTest.cpp:1696`
  与 `EthEngineServiceParityTest.cpp:1314` 另有「17 字节带 Holocene version byte 被拒」用例。

**结论：E8 = +0**。计划列举的四个"已知既有用例"之外，实际还有
`execution_payload_extra_data_shape_is_validated`、`VersionByteMismatchIsRejected`
两条直接命中 (a)/(b) 的用例（前者在 JovianExtraDataTest 内，计划未列）。
R2 应把本签名与覆盖清单写回 impl 计划，并把 E8 的 +0~2 定为 **+0**。

---

## 转 finding / cannot-determine

| 编号 | 类型 | 内容 / owner-next-step |
|---|---|---|
| **F-E2-1** | defect-plan（格标签错误） | 计划 E2 两格同时挂 `fork-jovian`/`fork-karst`，并用 586 对（112512B）断言成功。实测各 fork 上限：Granite/Holocene/Isthmus `112687`、**Jovian `81984`**（`OpPrecompiles.cpp:35`）、**Karst `57600`**（`:46`）。586 对在 Jovian/Karst 会被 size 拒绝 → 计划里的成功格在 Jovian/Karst 档必然 RED（且是"因正确上限而红"，与判据无关）。**修法**：E2 两格标签限定 `fork-granite/holocene/isthmus`；或改用每档自身边界（另立格）。**owner-next-step**：Task E2 R2 修订（Stage 2 R2 补代码时同步改本计划） |
| **F-S3-1** | defect-plan（前提错误 + 潜在缺陷） | 计划称 DA footprint「仅 header 槽校验」；实测**已有 per-tx Σ**（`OpBlockExecute.cpp:508-525`）。block 上限检查 `OpEngineService.cpp:197` 用 `>`，故 `== gasLimit` 被接受；spec（jovian/exec-engine.md:125）要求 `daFootprint` "保持在 gasLimit 以下"。E7 ③ 是**缺陷证明格**（F-A2 同族），应先按"证明缺陷"设计（红），不可当绿格写。**owner-next-step**：Task E7 R2（设计缺陷证明格）+ design-revision-pending 登记 F-A2 同族 |
| **F-S4-1** | cannot-determine（可判别性） | size 拒绝 = `EVMC_FAILURE`(1)，预编译数学失败疑为 `EVMC_PRECOMPILE_FAILURE`(12)，枚举可判别；evmone VM 源码不在已安装产物中，运行时未实测。最小读取集见 S4。**owner-next-step**：Task E2 R2 首步跑一次坏点探针（`osakaHostCall` 用合法长度但不在曲线上的点）确认 evmone 数学失败码；确认则上侧断言 `== EVMC_FAILURE`，否则退回 `!=0` |
| **F-S5-1** | doc-correction | op-revm 的 EIP-7825 常量不在 `rust/op-revm/src/spec.rs`（0 命中），而在 revm-primitives `eip7825::TX_GAS_LIMIT_CAP`（`rust/alloy-op-evm/src/env.rs:133` 消费）。**消费者**：Task E1a R2（引用 S5 对账表的三方符号/值） |
| **F-S6-1** | doc-correction | Holocene `Payload Attributes Processing` 正文存在，但标题被译为 `#### 负载属性处理`（`exec-engine.md:99`），英文 grep 只命中 TOC。E8 时序格不删。**消费者**：Task E8 R2 修订（维持 +0，引用 :99-108 正文行号） |
| **F-S7-1** | cannot-determine | EIP-7934 本仓 0 命中，初判「EL 无需实现」；"是否需要"取决于 OP specs/op-node 是否要求，最小读取集见 S7。**owner-next-step**：Task 0 收尾/设计修订（design-revision-pending 立项）；下一动作 = 读 `{isthmus,jovian}/exec-engine.md` 与 op-node `derive/` 有无 block-size 拒绝，二者皆无则确认"EL 无需实现"，否则立 finding 编号 |
| **F-E5-1** | needs-r2 | E5 ①② 疑已被 `EngineServiceTest.cpp` 覆盖（`:1493-1496/:1776-1785/:2077-2079`）。E5 可能由 +2~3 降为 +0~1；R2 需确认这些用例是否走的正是 OP 引擎路径（`OpEngineService`）还是通用 `EngineServiceImpl`，再定格数。**owner-next-step**：Task E5 R2（确认路径后定格数） |

---

## 自审

- 完整性：S1–S7 spike + **S8 全量查重** + 「E8 输入」齐全；每节含命令原文与原始输出摘要。
- 证据性：所有结论指向具体命令/行号；S4/S7 的不可静态判定项显式标 cannot-determine，
  未把推断写成结论。
- 纪律：本任务**未改任何被跟踪文件**；台账 `docs/plans/2026-09-12-plan-e-spike-notes.md`
  为 untracked；结束时 `git status --porcelain | grep -v '^??'` 为空（见最终核对）。
- 遗留疑问：F-E5-1（E5 是否走 OP 路径）、F-S4-1（evmone 数学失败码）、F-S7-1（7934 是否 EL 需要）、
  F-S3-1（DA footprint `== gasLimit` 是否应拒绝 —— spec "below" 的严格性未定；按缺陷证明格设计前须先定）。

---

# Stage 2 查重（R2-0 / WI-24）—— 2026-09-12，HEAD `d249092e7`，worktree `merge-318-rehearsal`

方法：R2 计划 Step 1 的 12 组关键字 `git grep -- '*Test*.cpp'` 起步，按需扩到 corpus
（`opstack-executor/tests/t8n/vectors/*.json` + `manifest.txt`，`OpT8nReplayTest` 硬断言
目录集合 == manifest 集合并全量重放）与 `engine/test/unittests/engine/OpEngine*`。
**所有"疑似覆盖"均读到用例体/向量体确认不变量**；行号只对 HEAD `d249092e7` 负责。

## 查重表（格 → 关键字 → 既有覆盖（用例名 + 文件:行）→ 决定）

| 格 | 关键字 | 既有覆盖（用例名 + 文件:行） | 决定 |
|---|---|---|---|
| E3-① beacon root 接线（Ecotone+ 读系统合约==payload 值） | `BEACON_ROOTS\|4788\|beacon` | **`IsthmusSystemContractsReal`**（`opstack-executor/tests/OpNewPayloadRpcE2eTest.cpp:831`）+ **`JovianSystemContractsReal`**（`:879`）：corpus `isthmus/jovian_system_contracts_real.json` 含真实 EIP-4788 字节码（`0x000f3df6…ac02`），env `parentBeaconBlockRoot=0x0b0b…`，postState 值级钉 ring 槽 == 输入 root；引擎路径执行（gasUsed/receiptsRoot 同钉） | 已覆盖（① +0；但仅 Isthmus/Jovian 档，Ecotone 档无向量——接线 fork 泛型，不另建） |
| E3-② pre-Ecotone 同地址无返回（负向） | 同上 | 无直接格：corpus canyon/regolith 向量无 BEACON_ROOTS 读取 tx；bcos-evm/executor 测试 0 命中 beacon | **新格** |
| E3-③ 未记录时间戳 → 空返回 | 同上 | 无：`isthmus_system_contracts_real` 只覆盖"旧槽被覆写"，未覆盖"查无此时间戳→0"分支 | **新格** |
| E4 0x0b/0x0c/0x0d/0x0f 可达性、定价与**输出语义** | `BLS12\|bls\|0x0b` | corpus `isthmus_precompile_bls_g1add/g1msm/g2add/pairing.json`（manifest.txt:94-103）经 **`OpT8nReplayTest`** 全量重放（目录循环 `OpT8nReplayTest.cpp:1459`），golden 钉 gasUsed(0xeb07=375+intrinsic)+receiptsRoot，**且 `_op_expected.receipts[].output` 携带完整预编译返回数据**（g1add=`0x…0572cb`，g1msm 128B / g2add 256B / pairing 32B），由 **`OpT8nReplayTest.cpp:1137-1140`** `ctx.checkOptional(p + ".output", …, hexBytes(receipt->output()))` 逐字节值级断言 → 可达、定价、**输入→输出语义**三层均已被 corpus 值级钉死 | **已覆盖（E4 = +0）**——计划 5 格（status 0 + output==Expected）与 corpus 覆盖重复，建格取消（见更正 F-E4-1；spec review 更正：原判"corpus 不钉输出字节"有误，已核实 refuted） |
| E5-① OP 含 deposit 块 `requestsHash == c_emptyRequestsHash` | `requestsHash\|6110` | **`op_golden_vector_rebuild_matches_op_geth_block_hash`**（`engine/test/unittests/engine/OpEngineServiceParityTest.cpp:1281`）：Isthmus deposit-only 向量整头 hash 与 op-geth 字节相等 → `rebuildOpEthHeader`（`OpEngineService.cpp:429`）的盖章被传导钉死 | 已覆盖（+0） |
| E5-② EIP-6110 排除语义（present-but-empty） | `executionRequests` | **`validate_op_newpayload_request_static_rules`**（`engine/test/unittests/engine/OpEngineReviewFixTest.cpp:238`，断言体 :252-256：非空/缺席均拒 `"executionRequests must be a present-but-empty list"`）+ **`op_fcu_getpayload_newpayload_roundtrip_messagepasser_root`**（`OpEngineServiceParityTest.cpp:1189`，build→getPayloadV4→newPayloadV4 全程 `{}`）+ **`KarstPayloadTimestampAcceptsGetPayloadV5`**（`OpEngineKarstProfileTest.cpp:57`，:68-69） | 已覆盖（+0） |
| E6-①②④⑥（台账既判） | `operatorFee\|OperatorFeeVault` | 维持台账 S8 结论：`RoutesFeesToFourVaults`（`OpTransitionTest.cpp:44`）、`ReceiptCarriesL1AndOperatorMeta`（`:110`）、`ReceiptMetaFollowsSnapshotNotTransitionCfg`（`:494`）、`OperatorScalarsOmittedWhenBothZero`（`OpReceiptMetaTest.cpp:66`） | 已覆盖（不变） |
| E6-③ Jovian 公式差（同参数比值） | `operator_fee` | **拆分覆盖**：Isthmus 数值被 `RoutesFeesToFourVaults`（`OpTransitionTest.cpp:44`，vault==gasUsed，scalar 1e6/const 0 → 除子改动必红）钉死；Jovian 数值被 `JovianReceiptMetaAndOperatorFormula`（`:246`，`g*1*100+500`，meta+vault 双断言）钉死。**无"同参数比值"单格**——两用例参数不同（1e6/0 vs 1/500） | 已覆盖（拆分形式；计划中"同参数比值"措辞作废，反向验证目标改为"除子/乘子常量任一改动 → 两用例之一必红"，仍成立） |
| E6-⑤ 未用 gas 退款 | `operator_fee\|charge` | 退款净额不变量已被钉：`RoutesFeesToFourVaults`（`OpTransitionTest.cpp:44`）gasLimit=100000、gasUsed=21000，vault==f(**gasUsed**) —— 若按 gasLimit 收不退，vault==100000 必红；`JovianReceiptMetaAndOperatorFormula`（`:246`）vault==computeOperatorCost(fee,**gasUsed**) 同理 | 已覆盖（+0；F-E5-1/E6 Step 2 取消） |
| E7-① `>` 拒绝格 | `blobGasUsed\|DA footprint` | **`DAFootprintExceedsGasLimitRejected`**（`opstack-executor/tests/OpL1EdgeGateTest.cpp:82`，blobGasUsed=gasLimit+1 → Invalid + `"DA footprint"`）+ corpus **`invalid_jovian_transfer_basic_static_11`**（`_op_expected.reject` = 同错误串）+ `JovianDABlobGasUsedMetersTheIncrease` 同族校验 `validateOpBlobGasUsed`（`OpEngineService.cpp:186-199`，`>` 原文） | 已覆盖（+0） |
| E7-② remote≠local 等值缺陷（F-A2） | `blobGasUsed` | 无：全仓所有 DA 用例/corpus 向量的 payload 均 remote==local（corpus 扫描仅 `invalid_*_static_11` 一条 `>`，无 `!=`Σ） | **新格**（期望 RED，`expected_failures(1)`） |
| E7-③ 边界对齐（`<`/`==`/`>` 三断言） | `blobGasUsed == gasLimit` | `>` 侧已覆盖（E7-①）；**`==` 合法侧零覆盖**（corpus 中 blobGasUsed 最大为 gasLimit 的 4 倍拒绝值与远小于 limit 的正常值，无等值格；B-5b 只测 gasLimit+1） | **新格**（1 用例 3 断言，`>` 断言与既有重复可接受——同格双侧） |
| E7-④ baseFee `max(gasUsed, blobGasUsed)` 耦合 | `gasMetered\|max(gasUsed` | **`JovianDABlobGasUsedMetersTheIncrease`**（`bcos-tars-protocol/test/CalcOpBaseFeeTest.cpp:190`）：blob 20M > gasUsed 5M → 1'041'666'666（增）；同头 Jovian=false → 916'666'667（减）——max() 语义双侧值级钉死 | 已覆盖（+0） |
| E7-⑤ deposit-only 块 `seal.blobGasUsed == 0` | `deposits-only\|blobGasUsed == 0` | 无块级格：`JovianActivationDepositOnlySeals`（`OpL1BlockDepositTest.cpp:740`）只断言 seal 不抛；`JovianMissingDaFootprintIsConsensusReject`（`OpReceiptEncodeTest.cpp:464`，:492 断言 `*zeroSeal.blobGasUsed==0`）是**合成单 receipt 显式 0**，非 deposits-only 块 | **新格** |
| E10-三态 pre-Canyon nil | `withdrawalsRoot\|MessagePasser` | **`RegolithVerifyArmAcceptsAbsentWithdrawalsRoot`**（`opstack-executor/tests/OpSchedulerTest.cpp:2759`，announced 无字段 + seal 零哨兵两形态都收）+ **`FcuV1RegolithReturnsPayloadIdThenGetPayloadV2`**（`OpEngineApiVersionsTest.cpp:385`，:402/:407 缺席）+ 形状基线矩阵（`OpEnginePayloadShapeBaselineTest.cpp:290/:321` per-fork presence） | 已覆盖（+0） |
| E10-三态 Canyon（空列表→派生 root） | 同上 | **`FcuV2CanyonReturnsPayloadIdThenGetPayloadV3IsUnsupportedFork`**（`OpEngineApiVersionsTest.cpp:413`，:431-434 present）+ **`EmptyPasserStorageSealsEmptyRootConstant`**（`OpL1BlockDepositTest.cpp:907`）与 **`MessagePasserStorageDrivesWithdrawalRoot`**（`:849`）值级钉 seal 侧；派生值 == emptyRootHash 由 `op_newpayload_rejects_executed_withdrawals_root_mismatch`（`OpEngineServiceParityTest.cpp:1236`，:1267-1269）钉 | 已覆盖（+0） |
| E10-三态 Isthmus MessagePasser | 同上 | golden hash（`OpEngineServiceParityTest.cpp:1281`）+ **`op_newpayload_accepts_announced_withdrawals_root`**（`OpEngineReviewFixTest.cpp:421`，presence 必须、值不锁空）+ roundtrip（`:1189`）+ mismatch 拒绝（`:1236`） | 已覆盖（+0） |
| E10-激活块 ±1 时间戳（引擎路径） | `activation\|fork boundary` | **`EachForkSwitchesExactlyAtItsActivation`** / **`ActivationTimestampsAreExact`**（`OpForkBoundarySweepTest.cpp:198/:248`，9 fork × 前/激活/后三格）+ `JovianActivationBlockRejectsUserTx` 等（`OpKarstActivationTest.cpp:203-261`）+ `HoloceneActivationUsesConstantBaseFee`（`OpEngineApiVersionsTest.cpp:309`） | 已覆盖（+0；E10 由 +3~15 改 **+0**） |
| E11-①②③ Ecotone/Isthmus/Jovian 逐字节 L1Info | `l1-attributes\|L1Info` | corpus 传导：每 fork `*_deposit_only.json` 首笔即真实 op-node L1 attributes 存款（ecotone 164B/0x440a5e20、isthmus 176B/0x098999be、jovian 178B/0x3db6be2b），`OpT8nReplayTest.cpp:1181-1196` **双向 postState** 断言把 L1Block slot1/3/7（jovian 另 slot8）逐值钉死；Jovian 另有直接偏移级格 `NonZeroL1ParamsAlignWithUnpackOpFeeParams`（`OpL1BlockDepositTest.cpp:483`） | 已覆盖（传导值级；**E11 = +0**，计划 +4 作废；若仍要独立于 corpus 的单格可作可选加固，不列任务） |
| E11-④ Fjord/Granite 无附加字段（负向） | 同上 | `fjord/granite_deposit_only.json`：164B、slot3/8 无 operator/DA 内容，postState 双向钉死"多解即红" | 已覆盖（传导；+0） |
| E12-① Canyon depositNonce | `depositNonce` | **`RegolithDepositOmitsReceiptVersion`**（`bcos-evm/test/opstack/OpDepositTest.cpp:94`）+ **`CanyonDepositSetsReceiptVersion`**（`:123`，meta 出场值级）；执行层传导：corpus `canyon_deposit_*` receiptsRoot + `OpT8nReplayTest.cpp:1055-1060` 逐 tx opStackMeta 断言 | 已覆盖（+0） |
| E12-② Ecotone scalar 组 | `l1BaseFeeScalar` | **`EcotoneCalldataGasUsedBecomesL1GasUsed`**（`OpReceiptMetaTest.cpp:140`）+ corpus `ecotone_l1fee_edge.json`（receiptsRoot 传导） | 已覆盖（+0） |
| E12-③ Isthmus operatorFee | `operatorFee` | **`IsthmusHasFeesWithoutDa`**（`OpReceiptMetaTest.cpp:34`，operator_fee 出场 + da_footprint 缺席）+ corpus isthmus 向量传导 | 已覆盖（+0） |
| E12-④ Jovian DA scalar | `daFootprintGasScalar` | **`JovianFillsDaFootprint`**（`OpReceiptMetaTest.cpp:80`）+ **`JovianReceiptMetaAndOperatorFormula`**（`OpTransitionTest.cpp:246`，da_footprint==estimatedDaSize(env)×2 值级）+ `DaFootprintFollowsSnapshotNotTransitionCfg`（`OpReceiptMetaTest.cpp:105`） | 已覆盖（+0；E12 由 +4 改 **+0**） |
| WI-33 eth_config（G-3） | `eth_config\|EthConfig` | **零命中**：`git grep 'eth_config\|EthConfig' -- '*Test*.cpp'` 0 行；`bcos-rpc/test/unittests/rpc/` 无 `EthConfigTest.cpp`（ls 实测） | 复核确认 G-3 成立 → **新格 +2**（current / last） |
| WI-34 getProof 非-latest（G-4） | `GetProof\|getProof` | `EthGetProofIntegrationTest.cpp` 全部 blockTag 实参均为 `"latest"`（:232/:248/:297/:307，读用例体确认）；无历史数字 tag 用例 | 复核确认 G-4 成立 → **新格 +2**（block-3 出证 + 拒绝/负向） |

## 最终格数表（后续 WI-25~30 以此为准，优先级高于 R2 计划原计数；**Stage 2 净增 9 格**）

```
E3:  +2（② pre-Ecotone 负向、③ 未记录时间戳空返回；① 已被 corpus system_contracts_real 覆盖）
E4:  +0（spec review 更正，原 +5 作废：corpus BLS 向量的 `_op_expected.receipts[].output`
        携带完整返回数据（g1add 0x…0572cb / g1msm 128B / g2add 256B / pairing 32B），
        由 OpT8nReplayTest.cpp:1137-1140 逐字节断言，全 manifest 重放（目录循环 :1459）——
        计划 5 格（status 0 + output==Expected）与 corpus 值级覆盖重复，建格取消；
        可达性 + RequiredGas 定价 + 输出语义三层均已被 corpus 钉死）
E5:  +0（原 preset 三处走通用 EngineServiceImpl 而非 OpEngineService，但 OP 路径由
        OpEngineReviewFixTest:252-256 + ParityTest:1189 + golden ParityTest:1281 独立覆盖）
E6:  +0（③⑤ 均被 RoutesFeesToFourVaults(:44) 与 JovianReceiptMetaAndOperatorFormula(:246)
        以值级不变量覆盖；"同参数比值单格"不存在但等价检测力已在，记降级说明）
E7:  +3（②等值缺陷格、③边界对齐格、⑤deposit-only 零格；① `>` 拒绝 +0（OpL1EdgeGateTest:82
        + corpus static_11 双覆盖）、④ baseFee max() +0（CalcOpBaseFeeTest:190）——原 +4 减 1）
E10: +0（三态双侧 + 激活块 ±1 全有引擎/执行路径覆盖；原 +3~15 作废）
E11: +0（四格均由 corpus 逐 fork 真实字节 + 双向 postState 传导钉死；原 +4 作废）
E12: +0（bcos-evm 层直接格 OpDepositTest:94/:123 + OpReceiptMetaTest:34/:80/:140/:105 +
        corpus 传导；原 +4 作废）
E9:  不变（夹具头，无格；E10 清零后其消费者仅剩 E7-③⑤ 若选择落 executor 域）
WI-33: +2（eth_config current/last；G-3 零测试复核确认）
WI-34: +2（getProof block-3 出证 + 负向；G-4 仅-latest 复核确认）
```

## 查重更正 / 转 finding（本轮新增）

| 编号 | 类型 | 内容 |
|---|---|---|
| **F-E4-1** | defect-plan（前提被语料推翻 → **建格取消**） | R2 计划 E4 Step 0 称"0x0b/0x0d/0x0f 不在任何表里、唯一依赖注释主张"，实测 corpus `isthmus_precompile_bls_g1add/g1msm/g2add/pairing.json`（manifest.txt:94-103）经 `OpT8nReplayTest` 全量重放且 golden 绿——**可达性与 RequiredGas 定价已被语料证明**。**spec review 第二轮进一步更正**（初判"corpus 不钉输出字节"被 refuted）：`_op_expected.receipts[].output` 携带完整预编译返回数据（g1add `0x…0572cb`、g1msm 128B、g2add 256B、pairing 32B），`OpT8nReplayTest.cpp:1137-1140` 对每个 manifest 向量做 `checkOptional(p + ".output", …, hexBytes(receipt->output()))` 逐字节比较 → **corpus 即 op-geth oracle（含输出字节），E4 建格取消（+5 → +0）**。已入库的 `bcos-evm/test/opstack/op_geth_oracle.json` + `tools/op-geth-oracle/extract.sh`（d249092e7，"test(evm): load the bn256 G2 point from the op-geth oracle contract"）保留现状：现为 bn256 G2 契约的 oracle；若日后需要 BLS 独立格（如 corpus 之外的向量），此处即其归属地。R2 计划的 E4 Task 应整体删去或改为"引用本行收口" |
| **F-E5-1（闭合）** | needs-r2 → resolved | 原疑问"EngineServiceTest:2077/1493/1776 是否走 OP 路径"——实测三处 fixture `makeEngineServiceImpl`（`EngineServiceTest.cpp:222`）返回**通用 `EngineServiceImpl`**（Eth/通用 lane），非 `OpEngineService`；故 preset 命中不直接覆盖 E5。但 OP 路径由 OpEngineReviewFixTest/ParityTest/golden 三条独立覆盖 → **E5 = +0**，本 finding 关闭 |
| **F-R2-0-1** | doc-correction（计划措辞） | E6-③ 的"同参数下 Isthmus 与 Jovian 收费数值比值不同"无单格；等价检测力由两条不同参数的值级用例承担（见查重表 E6-③ 行）。R2 的 E6 反向验证（除子 1e6→1e3）仍有效。E6 Task 按计划 Step 1 判 **+0 跳过建格** |

## 自审

- 每行含用例名 + 文件:行；corpus 行另给向量文件名与 `_op_expected` 键。
- 三处"疑似覆盖"复核结果与预期**不符**并如实记录：E7 `>` 格确认为真覆盖（+0）；E6 ③⑤ 为
  拆分/净额式覆盖（+0，措辞降级）；E5 preset 命中走错 lane 但 OP 侧另有覆盖（+0）。
- E4 的 Step 0 前提被 corpus 推翻（F-E4-1）——本轮最大修正；且初判"corpus 不钉输出字节、
  E4 保留 +5"经 **spec review refuted**（`OpT8nReplayTest.cpp:1137-1140` 的 receipts[].output
  逐字节断言 + :1459 目录循环），已复核确认并改判 **E4 = +0**。教训：查 corpus 时只看了
  postState，漏了 `_op_expected.receipts[].output` —— 传导覆盖判定必须枚举 `_op_expected`
  的全部断言键，不能只挑两个。
- 纪律：本任务只增补本 untracked 台账；`git status --porcelain | grep -v '^??'` 为空（见下）。

| WI-35 结项（2026-09-13） | ✅ 闭环（分支 B） | `b66cfa56b` | 第二实现（op-revm `handler.rs:81-100` 对 deposit 直接返回 Ok、绕过 revm 基线 `validate_env` 里的 7825 cap `validation.rs:150-159`）= 与 FISCO 同向豁免；op-geth 偏离（`state_transition.go:379-383`）。specs 沉默 ⇒ 定性为"matches op-revm; op-geth differs"，非 spec-aligned。下游传播（发布门禁断言）已补真实依据注释 |

| WI-31（生产修复） | ✅ 完成（双审 Approved） | `d6ca9860c` + `975c2ec58` | Jovian DA 等值门置于范围门**之前**（镜像 op-geth `core/block_validator.go:129` 先等值、`:132` 后范围），消息含 `DA footprint`；导出 `daFootprintOfEnvelopes`（全 primitive 链 header-inline；`nm -uC build/engine/libengine.a \| grep bcos::evm` = **0**）；测试：E7 ② 去 decorator 真绿、E7 ③ 改「remote==local、变 block gas limit」三格；验证：五 target 344/336/182/146/7 全绿、harness 9/9 RED、`V-DAFOOTPRINT-boundary` RED、Σ=593600 独立重算一致 |
