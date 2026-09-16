# Plan E —— OP fork 覆盖补漏（设计文档 v2）

> 四份既有计划（A/B/C/D）之后的新增计划。**前置**：Plan A 已完成（含 review 修复 `c7a0a2425`
> 与 fork 标签 `20e768419`）；与 Plan B 的关系：本计划**实现 Plan B 的边界首任务**（激活块
> 引擎路径扩展 + 逐 fork L1Info/收据形状），完成后 Plan B 剩余范围照旧。
> **工作树**：`merge-318-rehearsal`（分支 `feat/karst-on-318-merged`）；语料只读；不推送。
> **产物**：本文档（untracked）。执行授权：无（不推送、不动语料）。
>
> **v3 修订说明（当前版）**：v2 经第二轮文档审查（8 条 finding，N1–N8）后修订。**N1 为 HIGH**：
> v1/v2 的 E1 把 EIP-7934（区块 RLP 尺寸上限）与 EIP-7825（per-tx gas 上限，deposit 豁免的真实
> 归属）混为一谈，两个方向都框错——Round 1 未抓到该错误（reviewer 遗漏，如实记录），Round 2 修正。
> 另修正 E12 的层选择、E3/E2 的边界与输入构造、E11 格数与 E4/E5/E9 的表述缺漏。
>
> v1→v2（历史）：修正 3 处事实错误（E5 机制与 spec 相反、E7 边界方向反、E3 地址与实现现状错），
> 补齐 E9 接口设计与 fallback、spike 产物/判据、oracle 归属与逐 target 增量。

## 0. 范围决策（已确认）

- 覆盖层：**零夹具依赖格（Part 1）+ Plan B 边界首任务（Part 2）**。
- 判据：**外部 oracle 优先**——op-geth（语料源 `e8800cffe` / 引用源 `d0734fd5`）、
  op-revm（`5f90f749ca`）、EIP 官方参考向量；**禁止字面量自指**（review 规则 #38）。
- 交付：文档先行，评审通过后执行。
- **非目标**（维持 Plan C/D 边界）：EIP-2935/6110 的**系统调用接线**（6110 的**排除语义**在
  范围内，见 E5）、M5 superchain zip、M8 stateRoot 差分、Karst spec 页回填、M4 全 fork 连续链。

## 1. 依据与已修正的事实（执行时不得再凭记忆）

| 事实 | 出处（已核） | v1/v2 的错误 |
|---|---|---|
| **deposit 豁免属 EIP-7825（per-tx gas 上限，常量 2^24）**，不是尺寸上限 | 本仓 `bcos-evm/eth/state/state.cpp:385`、`state.hpp:170-172`、`transaction.hpp:17` | v1/v2 的 E1 挂到了 7934 上（N1） |
| **EIP-7934 是 RLP 编码区块的尺寸上限**（`block.Size() > params.MaxBlockSize`），与本仓 EL 角色关系待定性；本仓无该校验 | op-geth `core/block_validator.go:52-53`、`core/error.go:33`；本仓 grep `MaxBlockSize\|block.size()` 0 命中 | v1/v2 把它当成"max tx size"（N1） |
| OP 路径 **必须排除** EIP-6110 存款请求；`executionRequests` **必须是空数组** | `isthmus/exec-engine.md`「存款请求」+「引擎 API 更新」；本仓 `OpEngineService.cpp:134-136` | v1 写成"tx 触发产出请求"，方向相反 |
| Jovian `daFootprint` 必须**保持在 `gasLimit` 以下**；baseFee 改用 `max(gasUsed, blobGasUsed)` | `jovian/exec-engine.md`「DA 足迹块限制」 | v1 把 `== gasLimit` 判为有效（off-by-one） |
| EIP-4788 由**系统合约**实现（`BEACON_ROOTS_ADDRESS`，`EVMC_CANCUN` 档），返回 `block.parent_beacon_block_root` | 本仓 `bcos-evm/eth/state/system_contracts.cpp:35-37` | v1 写成"调 0x0b" |
| EIP-2537 预编译地址 **0x0b–0x11 共 7 个** | isthmus overview（EIP-2537） | v1 写 0x0b..0x0f、格数含糊 |
| Isthmus withdrawalsRoot 三态：pre-Canyon `nil` / Canyon 起 `keccak256(rlp(empty_code))` / Isthmus 起 MessagePasser storage root | `isthmus/exec-engine.md`「标头有效性规则」 | —（M2 注入的 `emptyRootHash()` 与该值一致，无需改） |
| Granite bn256Pairing 输入 > **112687** 字节 revert | `granite/exec-engine.md`「EVMA 更改」 | — |

## 2. Task 0 —— spike（前置，产物 `docs/plans/2026-09-12-plan-e-spike-notes.md`）

每个 spike 给出可重放命令与结论；结论决定 E3/E4/E5/E7 的最终形态或转 finding。

| # | 问题 | 可执行检查 |
|---|---|---|
| S1 | evmone 是否实现 EIP-2537（BLS 预编译） | 跑 EEST 的 BLS 用例或 `git grep -n 'bls12\|BLS12_381' <evmone>/lib/evmone` |
| S2 | OP 引擎路径是否把 payload 的 `parentBeaconBlockRoot` 喂进 block context | `git grep -n 'parent_beacon_block_root\|parentBeaconBlockRoot' engine/ opstack-executor/ bcos-evm/bcos-evm/eth/state/` |
| S3 | DA footprint 是否有**块级累加**实现（或仅 header 槽校验） | `git grep -n 'daFootprint\|da_footprint\|FootprintGasScalar' -- bcos-evm opstack-executor engine` |
| S4 | 本仓 bn256 size-check 拒绝可否与 pairing 数学失败判别 | 读 precompile 包装层的错误/halt 类型；不可判别则 E2 降级（见 E2） |
| S5 | EIP-7825 上限常量的精确位置（三方交叉） | `git -C <op-geth> show <pin>:core/... \| grep -n '7825\|MaxTxGas'`、op-revm 侧同查；与本仓 `bcos-evm/eth/state/transaction.hpp:17`（`0x1000000`）对账 |
| S6 | Holocene「参数生效时序」是否有 spec 正文 | 读 `holocene/exec-engine.md` 的 Payload Attributes Processing / Base Fee Computation 两节；无正文则删 E8 时序格 |
| S7 | **EIP-7934 的角色定性**（EL 是否需要实现区块尺寸校验） | 读 op-geth `core/block_validator.go:52-53` 的调用路径（区块导入 vs payload 路径）+ 本仓是否持有 RLP 区块导入通路；判"无需实现"则记 cannot-determine 并引 M0 格 11 先例 |

## 3. Part 1 —— 零夹具依赖格（8 个 Task）

### Task E1a：EIP-7825 per-tx gas 上限 + deposit 豁免行为对（Karst，3 格）
- 目标二进制：`bcos-evm-opstack-tests`（预期 +3）；文件 `bcos-evm/test/opstack/OpOsakaSemanticsTest.cpp`。
- 事实（已核）：本仓实现位于 `bcos-evm/eth/state/state.cpp:385`
  （`enforce_max_tx_gas && rev >= EVMC_OSAKA && tx.gas_limit > MAX_TX_GAS_LIMIT`），
  常量 `MAX_TX_GAS_LIMIT = 0x1000000`（`transaction.hpp:17`），deposit 豁免由
  `deposit_exempt_from_max_tx_gas` 驱动（`state.hpp:170-172`）。
- 行为：① 普通 tx `gas_limit = 2^24` 通过 / `2^24 + 1` 拒绝（**方向由实现与 oracle 双证**：
  比较符 `>`，故恰等于上限是合法的）② deposit tx 超上限 → 豁免通过（Karst 配置位为 true）
  ③ pre-Osaka 档（Jovian 及以前）→ 上限不适用（普通 tx 超 cap 不被此规则拒）。
- **oracle 归属（定死）**：op-geth/op-revm 的 7825 常量统一进**新建**
  `tools/op-geth-oracle/extract.sh` + `bcos-evm/test/opstack/op_geth_oracle.json`
  （记两个 op-geth pin 分工：语料 `e8800cffe` / 引用 `d0734fd5`）；op-revm 侧继续用现有
  `op_revm_oracle.json` 扩展，**不混用**。三方对账：oracle 常量 × 本仓 `MAX_TX_GAS_LIMIT`。
- 结局允许：若本仓检查缺失或门控与 oracle 不一致 → 用例红即 finding（不得调期望表）。

### Task E1b：EIP-7934 区块尺寸上限的**角色定性**（0 格，spike 产物）
- 事实（已核）：7934 是 RLP 编码**区块**尺寸上限（op-geth `core/block_validator.go:52-53`），
  本仓无任何对应校验（grep 0 命中）。
- 按 **S7** 定性：若判"EL 角色无需实现"（EL 走 payload 不收 RLP 区块，同 M0 格 11 先例）→
  记 cannot-determine 并写明最小读取集；若判"需要"→ **转 finding**（本仓缺校验），本计划不建格。
- **不产出**任何"max tx size"格（v1/v2 的错误已删）。

### Task E2：Granite bn256Pairing 执行边界（Granite，2 格）
- 目标：`bcos-evm-opstack-tests`（+2）。
- 行为：**对齐合法输入** 586 对（112512B ≤ 112687）不被 size 检查拒绝 / 587 对（112704B > 112687）
  被 size 检查拒绝。
- **输入构造（新增，必须遵守）**：下侧用**合法曲线点**拼 586 对（期望 pairing 成功，证明
  size 检查未误伤）；上侧用同样合法点的前 587 对（112704B > 112687，期望被 size 检查拒绝）。
  两侧都不得用垃圾字节——垃圾字节会以"数学失败"revert，与"size 检查拒绝"不可区分（格失效）。
- 判别手段（S4 决定）：优先用 precompile 包装层的**错误/halt 类型**；若无法区分，则下侧仍需
  成功（这是硬判据）、上侧按"被拒"断言，并在 spike notes 记录"拒绝原因不可判别"的残留。
  **不得**用"112687 字节原始输入"（非 192 倍数，混淆两类 revert）。

### Task E3：EIP-4788 的 OP 接线格（Ecotone 起，3 格）
- 目标：`bcos-evm-opstack-tests`（+3）。
- 事实（已核）：系统合约位于 `BEACON_ROOTS_ADDRESS`，`EVMC_CANCUN` 档启用，返回
  `block.parent_beacon_block_root`（`system_contracts.cpp:35-37`）。
- 行为：① Ecotone+ 档下，payload.parentBeaconBlockRoot = X → 经 block context 读系统合约得到 X
  （**接线正确性**，非自指：X 来自 payload 输入）② pre-Ecotone 档 → 地址无合约/空返回
  ③ **未记录时间戳 → 空返回**（spec 定义的边界；v2 的"重复读取一致"天然成立、非有效判据，
  已替换）。ring-buffer 回绕（存满后淘汰最旧）因规模原因记 cannot-determine。
- 依赖 S2；S2 若显示 OP 路径不喂该字段 → finding（缺陷证明）。

### Task E4：EIP-2537 BLS12-381 执行面（Isthmus 起，5 格）
- 目标：`bcos-evm-opstack-tests`（+5）。
- 地址范围：**0x0b–0x11 共 7 个**；本计划覆盖子集（其余登记为规模原因）：
  0x0b G1ADD（单位元 + 有效对，2 格）、0x0c G1MSM（1 格）、0x0d G2ADD（1 格）、
  0x0f PAIRING（1 格）；0x0e G2MSM / 0x10 MAP_FP_TO_G1 / 0x11 MAP_FP2_TO_G2 登记为
  「向量规模/优先级」暂缓，记入 §6。
- oracle（来源点名）：**ethereum/EIP-2537 仓库的 `tests/` 向量**（或 execution-spec-tests 的
  `eip2537_bls_12_381_precompiles` fixtures），以 JSON 形式 pin 进 `op_geth_oracle.json` 的
  `vectors` 段（记来源 repo + commit + 取值路径）；均不可得时退 evmone 自带向量
  （op-geth `core/vm/testdata/precompiles/bls*.json` 亦可作对照）并记录版本。
- 与 Task 6 分工：那里钉**表上限（limits）**，这里钉**语义（输入→输出）**。

### Task E5：OP requests 恒空/排除语义（Isthmus 起，2–3 格）
- 目标：`test-bcos-engine`（预期 +2~3）。
- **v2 重写**（v1 方向错误）：行为是 **OP 路径不得产出请求**，而非产出：
  ① OP 头 `requestsHash` 必须等于 empty-requests 哈希常量——**OP 盖章站点**为
  `OpEngineService.cpp:429`（`if (forkId >= OpForkId::Isthmus) header->setRequestsHash(...)`，
  常量定义在 `bcos-framework/bcos-framework/engine/Constants.h:30-34` 的 `0xe3b0c442…`；
  Eth 侧对照站点 `EngineServiceCommon.cpp:686`）；
  ② 非空 `executionRequests` 必须被拒（`OpEngineService.cpp:134-136` 的 OP 专属规则——
  **执行前先查重**：`OpEngineReviewFixTest`/`EngineRpcTest` 等已有相关用例，只补 OP 专属缺口）；
  ③ EIP-6110 存款请求**排除**语义（spec `isthmus/exec-engine.md`「存款请求」）：若 S3/查重显示
  eth 层通用收集路径可为 OP 档产出 6110 请求，则钉"OP 档下不得出现"；若收集路径本就与 OP 无关，
  此格降级为注释引用并记原因。
- 与 M2 的分工：M2 已钉 V4 参数形状；本格钉**头字段常量与排除语义**。

### Task E6：operator fee 收取/退款语义（Isthmus/Jovian，6 格）
- 目标：`bcos-evm-opstack-tests`（+6）；文件复用 `OpFeeParamsTest` 夹具。
- 行为：① Isthmus scalar-only 收费 ② Isthmus constant+scalar ③ Jovian 公式差（同参数两 fork
  数值不同）④ 零参数零收费 ⑤ 未用 gas 退款 ⑥ 受益账户 = OperatorFeeVault。
- oracle：op-revm `l1block.rs:174-201` 公式与常量 → **扩展 `op_revm_oracle.json`**
  （新增 `operator_fee_*` 键，Task 6 同一契约文件）+ op-geth `protocol_params.go` 交叉核对。

### Task E7：Jovian DA footprint 块级语义（5 格）
- 目标：`opstack-executor-block-tests`（+5，与 `OpL1EdgeGateTest` 同层）。
- 行为（**v2 修正边界方向**）：
  ① per-tx 足迹公式格（`max(minTransactionSize, (intercept + fastlzCoef*fastlzSize)//1e6)
  × daFootprintGasScalar`，deposit tx type `0x7E` 跳过）——常量取自 Fjord/Jovian spec；
  ② 块级累加 `< gasLimit` **有效** / ③ `== gasLimit` **拒绝**（spec：必须保持在 gasLimit
  **以下**）④ baseFee 更新用 `max(gasUsed, blobGasUsed)`（DA 重块抬升后续块 baseFee）
  ⑤ deposit-only 块足迹为 0。
- **交叉引用已登记 finding F-A2**（engine 层缺 `blobGasUsed == 本地重算 daFootprint` 等值校验）：
  E7 的 ②③ 正是该缺口的证明格——**结局可能是"证明 F-A2 缺陷"而非绿格**。
- oracle：`jovian/exec-engine.md` 伪码 + Fjord spec 常量 + op-geth 实现（语料 pin 已含 Jovian）。
- 依赖 S3（是否有块级累加实现）。

### Task E8：Holocene 1559 参数 header 负向（3–4 格）
- 目标：`test-bcos-engine`（+3~4，`JovianExtraDataTest` 同层）。
- 行为（仅 spec 已取证的 header 规则）：`version != 0` / `denominator == 0` / `elasticity == 0` /
  长度 > 9 字节 → 全部 fail-closed；9 字节合法形态已有正例。
- **时序格（条件性）**：v1 的"下一块生效"无 spec 正文支持——按 S6 先取证；有正文才建格，
  无正文则删除并在 spike notes 记录（不得凭记忆写期望）。

## 4. Part 2 —— Plan B 边界首任务（1 夹具 + 3 消费）

### Task E9：跨 fork payload 夹具（夹具层，无新格）——**含接口设计草案与 fallback**
- 现状障碍（已核）：`ImportServiceFixtureT` 的 `seamScheduler` 固定 `legacy(false)`；
  `OpSchedulerSeam` 删除拷贝/移动；`OpEngineService` 以引用持有；`validRequest` 硬编码
  Isthmus 与 `has_da_footprint=false`。
- **接口草案**（执行时定稿）：
  ```cpp
  // 两个显式工厂（避免 v2 的 "timestampOrFork" 歧义参数）：
  static ImportServiceFixtureT byFork(OpForkId fork);        // 内部用 canonical ladder 反查时间戳
  static ImportServiceFixtureT byTimestamp(uint64_t tsSec);  // 时间戳边界格（激活块 ±1）用
  // 内部：OpForkSchedule::parse(<ladder>).configAt(ts) → 注入 seam 与 validRequest 的 fork 档
  // payload 形状：复用 M2 的 clShapedPayload 归一化（按 fork 决定 withdrawals/
  // withdrawalsRoot/blobGas 出场），避免第二份形状表
  ```
- **fallback（gate 被拒时的路径）**：若不允许改共享头 `OpEngineKarstTestHarness.h`，
  则在 **新建** 的独立夹具头（如 `opstack-executor/tests/support/OpForkPayloadFixture.h`）中实现，
  共享头只读不改；E10–E12 的用例全部基于新头。两条路径的格内容一致，只差落点。
- ⚠️ **硬约束 gate**：改共享头版本的执行需用户确认；fallback 版本无需确认（新增文件）。
- 验收：既有引擎路径测试零回归（四 target 计数只增不减）。

### Task E10：激活块引擎路径扩展（5 fork × 3 格 = 15 格）
- 目标：`test-bcos-engine`（+15，激活块 ±1 经真实引擎路径）。
- 行为：Isthmus（首个 Isthmus 块必带 MessagePasser storage root；pre-Isthmus 必须是
  `keccak256(rlp(empty_code))` 或 `nil`——**三态规则已核 spec**）/ Canyon（首个 Canyon 块带
  withdrawals）/ Holocene（首个 Holocene 块带 9B EIP1559Params）/ Fjord、Granite（形状不变
  的正向 + 反向格）。
- oracle：`OpEngineService.cpp:96-146` 校验器规则 + specs 对应 fork 节 + op-node 派生。
- Karst/Jovian 已有引擎路径激活块格（`OpKarstActivationTest`），不重复。
- 依赖：E9。

### Task E11：逐 fork L1Info 存款形状（4 格）
- 目标：`opstack-executor-block-tests`（+4）。
- 行为（4 格 = 3 正向 + 1 负向）：① Ecotone（+blob scalars）② Isthmus（+operatorFee scalars，
  含 v5 布局移位）③ Jovian（+DA footprint scalar）的 L1 attributes 存款 tx **逐字节对拍**；
  ④ **负向格**：Fjord/Granite 档下同一 L1Info 输入不得解出 Ecotone+ 的附加字段
  （v2 标 +4 却只列 3 项，此格即第 4 格）。
- oracle：`{ecotone,isthmus,jovian}/l1-attributes.md` 布局表 + op-node `l1_block_info.go`
  打包/解包字节序。
- 依赖：E9。

### Task E12：逐 fork 收据 **meta 形状**最小版（4 格）——**执行层**
- 目标：`opstack-executor-block-tests`（+4）。
- **层选择修正（N2）**：rpc 层的 `combineReceiptResponse` 是 meta-presence 驱动、不看 fork
  （Plan A 的 M3 结论；复核 `ReceiptResponse.cpp` 唯一 "fork" 字样在 `:115` 注释）。
  故"逐 fork 收据形状"必须落在**执行层**（谁把 fork 映射成 meta 形状就在哪测）。
- 行为（每 fork 1 格）：① Canyon：deposit 收据 meta 携带 depositNonce/depositNonceVersion
  ② Ecotone：meta 从 `l1_fee_scalar` 切到 `l1_base_fee_scalar`/`l1_blob_base_fee_scalar`
  ③ Isthmus：`operator_fee_scalar`/`operator_fee_constant` 出场 ④ Jovian：`da_footprint_gas_scalar`
  出场（v2 把 Isthmus/Jovian 挤成一格，此处分立）。
- rpc 层的 meta→JSON 出场规则已有覆盖（`Web3ResponseTest`/Plan A 的 `ReceiptFieldBaselineTest`），
  不重复；其余逐 fork 全字段仍归 Plan B 本体。

## 5. 执行顺序与依赖

```
Task 0（S1–S7 spike，含 E1b 的 7934 角色定性）→ E1a/E2/E6/E8（独立）→ E3/E4（S1/S2 后）
→ E5/E7（S3 + 查重后）
E9（gate 或 fallback）→ E10 → E11 → E12
```
构建纪律沿用：串行构建；新增文件必须 `cmake -B build -S .` 重配；新用例必须带 `fork-<name>`
标签且 decorator **单行 + clang-format off/on**（工具链缺陷，见 `20e768419` 的 commit message）。

## 6. 验收清单（本计划完成 = ）

- [ ] **E1–E8 全部落地**：或全绿，或每个因"本仓缺实现"转 finding 的项有 finding 编号与台账记录
      ——两者都算完成，**不得调期望表迁就**
- [ ] Task 0 六项 spike 结论写入 `docs/plans/2026-09-12-plan-e-spike-notes.md`（含可重放命令与原始输出）
- [ ] 新 oracle 产物就位：`tools/op-geth-oracle/extract.sh` + `op_geth_oracle.json`；`op_revm_oracle.json` 扩展 `operator_fee_*`
- [ ] E9 完成后既有引擎路径测试零回归；E10 的 15 格、E11 的 4 格、E12 的 3 格全绿，
      且每个激活块格断言 ±1 时间戳两侧
- [ ] 全部新用例带 fork 标签，可用 `--run_test=@fork-<name>` 选择
- [ ] 变异验证：E2（bn256 边界）与 E6（operator fee 公式）各新增 1 个变异变体进 harness，且 RED
- [ ] 逐 target 增量与预期一致（E1a +3 / E2 +2 / E3 +3 / E4 +5 / E5 +2~3 / E6 +6 / E7 +5 /
      E8 +3~4 → bcos-evm 与 engine；E10 +15 → engine；E11 +4、E12 +4 → executor），
      四 target 计数只增不减；**E1b 产出 0 格**（定性结论或 finding 编号）
- [ ] 过程文档不入库；无推送；语料只读

## 7. cannot-determine / 转出

- EIP-7934 区块尺寸校验：按 S7 若判"EL 无需实现"→ 本条即其落点（同 M0 格 11 先例）；
  若判"需要"→ 以 finding 形式转出，不在本计划建格。
- EIP-4788 ring-buffer 回绕（容量边界）：规模原因记 cannot-determine，最小读取集 =
  EIP-4788 的 ring-buffer 常量 + 本仓系统合约实现。
- EIP-2935/6110 的**系统调用接线**（CL 面）→ Plan D 或上游规格；6110 的**排除语义**在 E5。
- EIP-2537 的 0x0e/0x10/0x11 地址、BLS 大向量 → 规模原因暂缓，登记待补。
- Karst spec 页空壳 → 逐值判据继续落 op-geth/op-revm（M0 纪律）。
- M4 全 fork 连续链、重启/持久化跨 fork、逐 fork 收据全字段 → Plan B 本体。
- M8 stateRoot 差分 → Plan D。

## 8. 风险

| 风险 | 处置 |
|---|---|
| evmone 缺 2537（S1） | E4 降级为 finding（缺陷证明）或转 Plan C；不造期望 |
| OP 引擎路径不喂 beacon root（S2） | E3 转 finding |
| DA 无块级累加实现（S3） | E7 转 finding，交叉引用 F-A2 |
| bn256 两类 revert 不可判别（S4） | E2 降级为表值+尺寸检查格，理由入 spike notes |
| E9 共享头 gate 被拒 | 走 fallback：新建独立夹具头，格内容不变 |
| op-geth 常量名跨版本漂移（Task 6 教训） | 提取脚本先 `git show <pin>:<path>` 定位再抓，JSON 记两个 pin 分工 |
| 新用例与既有覆盖重复（E5 尤其） | 每个 Task 首步查重（`git grep` + label 切片），只补缺口 |
| EIP 编号与层归属混用（N1 的根因） | 每个新格先写"facility 层 + EIP 编号 + spec 出处"三元组，再写断言；EIP 编号存疑时先 grep 本仓实现与 op-geth 双向核对 |
| E1b 定性结论影响验收口径 | E1b 允许 0 格：产出 either cannot-determine 条目 + 最小读取集，either finding 编号（写入 §6 验收） |
