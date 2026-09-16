# Plan E —— Stage 2 设计文档（E3–E7 + Part 2）

> 前置：Plan E Stage 1 已完成（Task 0 台账 + `597bb6d00` E1a + `48b1bdaa5` E2 + E8=+0 零格收尾）。
> 本文件设计 Stage 2；经评审后以 writing-plans 落成 impl 计划 R2（补全代码块）。
> **工作树**：`merge-318-rehearsal`（`feat/karst-on-318-merged`）；语料只读；不推送；过程文档不入库。
> 依据：`docs/plans/2026-09-12-plan-e-spike-notes.md`（Task 0 台账）+ 本文件 §2 的本轮取证。
> 产物：本文件（untracked）。

## 0. 已确认的范围决策

1. **覆盖全部 Stage 2**：E3/E4/E5/E6/E7 + Part 2 的 E9–E12（格数按 §2 的缩减后口径）。
2. **E7 含生产修复方案**（经 §2 重新定向：修复对象不是 `>` 比较，而是缺失的**等值校验**）。
   生产变更**需单独授权**；未授权时本计划只交付"缺陷证明格 + finding"。
3. **E9 夹具落点**：新增独立夹具头（不动共享头 `OpEngineKarstTestHarness.h`，无需额外 gate）。

## 1. 依据（Task 0 台账的关键事实，逐条可复核）

| 事实 | 出处 |
|---|---|
| evmone **0.21.0 有完整 EIP-2537**（`g1_add/g1_msm/g2_add/g2_msm/pairing_check/map_fp_to_g1/map_fp2_to_g2`） | `vcpkg_installed/arm64-osx/include/evmone_precompiles/bls.hpp`（3882B） |
| OP 路径**确实喂** `parentBeaconBlockRoot`：`OpEngineService.cpp:425` 盖章 → `OpCommon.h:236` → `BlockInfo::parent_beacon_block_root`（`block.hpp:49`）；消费点 `system_contracts.cpp:35-37`（`EVMC_CANCUN` + `BEACON_ROOTS_ADDRESS`） | 台账 S2 |
| DA footprint **已有 per-tx Σ 累加**（非 deposit，含溢出检查）→ `seal.blobGasUsed` | `OpBlockExecute.cpp:508-525` |
| 本仓引擎对 DA 的校验只有范围（uint64 / pre-Jovian 为 0 / `<= gasLimit`），**无等值校验** | `OpEngineService.cpp:180-199` |
| E5 ①②**疑似已覆盖**（`EngineServiceTest.cpp:2077/1493/1776`），待 R2 确认是否走 OP 引擎路径 | 台账 S8（hedge 保留） |
| E6 仅 **③Jovian 公式差** 与 **⑤退款** 为新格（① ② ④ ⑥ 已覆盖） | 台账 S8 |
| E10 的 withdrawalsRoot 三态**大部分已覆盖**；剩余在 R2 复核 | 台账 S8 |

## 2. 本轮修正（推翻 Stage 1 台账中的一条结论）

### F-S3-1 **refuted**：`== gasLimit` 不是缺陷，与 op-geth 一致
- 反证 1（oracle 原文）：op-geth `core/block_validator.go:119-132`
  ```go
  if v.config.IsJovian(header.Time) {
      if header.BlobGasUsed == nil { return errors.New("nil blob gas used in post-Jovian block header, should store DA footprint") }
      blobGasUsed := *header.BlobGasUsed
      daFootprint, err := types.CalcDAFootprint(block.Transactions())
      ...
      } else if blobGasUsed != daFootprint {           // ← 等值校验（本仓缺）
          return fmt.Errorf("invalid DA footprint in blobGasUsed field (remote: %d local: %d)", ...)
      }
      if daFootprint > block.GasLimit() {              // ← 用的是 '>'，与 op-geth 同界
          return fmt.Errorf("DA footprint %d exceeds block gas limit %d", daFootprint, block.GasLimit())
      }
  }
  ```
- 反证 2（spec 自证）：`jovian/exec-engine.md:125`「保持在 `gasLimit` **以下，就像 `gasUsed` 属性一样**」——
  `gasUsed <= gasLimit` 是 EVM 常规（等值合法），故该措辞即 `<=` 语义，本仓 `>` 正确。
- 结论：**撤回"生产修复 `>`→`>=`"**；E7 的边界格改为**对齐钉**（`<` 有效、`==` **有效**、`>` 拒绝）。
  若原方案落地，反而会制造与 op-geth 的分歧。

### E7 的缺陷证明格**改锚到等值校验**（= 已登记 finding **F-A2** 的实现证据）
- 本仓**缺** `blobGasUsed == 本地重算 daFootprint`：一个 `blobGasUsed` 与本地 Σ 不一致的 payload 会被接受；
  op-geth 会拒（上文 `!=` 分支）。
- 因此 E7 的 ② 格设计为：**构造"remote ≠ local"的 Jovian payload → 期望 INVALID**；本仓现状会 VALID
  → 该格 **RED = 缺陷证明**（按 Plan A 纪律：红即 finding，不改期望表）。
- 生产修复方案（见 §4）作为**独立授权项**：在引擎校验中补等值检查，与 op-geth 对齐。

## 3. 逐 Task 设计

### E3 —— EIP-4788 的 OP 接线格（`bcos-evm-opstack-tests`，+3）
- 事实：系统合约 `BEACON_ROOTS_ADDRESS`，`EVMC_CANCUN` 档启用，返回 `block.parent_beacon_block_root`；
  OP 路径已在 `OpEngineService.cpp:425` 盖章并喂入 block context（台账 S2）。
- 格：① Ecotone+ 档读系统合约得到 payload 的 beacon root（接线正确性，值来自输入、非自指）
  ② pre-Ecotone 档同地址无返回/空 ③ **未记录时间戳 → 空返回**（v2 的"重复读一致"已弃用；ring-buffer
  回绕规模原因记 cannot-determine）。
- oracle：EIP-4788 定义 + 本仓 `system_contracts.cpp:35-37` + 台账 S2 的注入点。
- 反向验证：断开注入（不设 `parentBeaconBlockRoot`）→ ① 必红。
- 前置：R2 首步确认本 target 的执行路径确实分派到系统合约（台账 S2 已给点，需落成一条可执行检查）。

### E4 —— EIP-2537 BLS12-381 执行面（`bcos-evm-opstack-tests`，+5）
- 地址 0x0b–0x11 共 7 个；本计划覆盖 **0x0b G1ADD（单位元 + 有效对，2 格）、0x0c G1MSM、0x0d G2ADD、
  0x0f PAIRING**（各 1 格）；0x0e/0x10/0x11 登记待补（规模）。
- oracle：**op-geth `core/vm/testdata/precompiles/bls*.json`（9 个，已在本机）** 作对照向量，pin 进
  新 `bcos-evm/test/opstack/op_geth_oracle.json` 的 `vectors` 段（记 op-geth commit `e8800cffe`）。
- 与 Task 6 分工：那里钉表上限（limits），这里钉语义（输入→输出）。
- 反向验证：篡改一个向量字节 → 对应格必红。

### E5 —— OP requests 恒空/排除（`test-bcos-engine`，+0~1，先查重）
- **首步**：确认台账中的 ①② 覆盖是否作用于 **OP 引擎路径**（`EngineServiceTest.cpp:2077/1493/1776`
  的 fixture 是哪个 service）。若确为 OP 路径 → **+0**，只把结论写回台账并引用；否则补该路径的格。
- 若需建格：① 含 deposit 的块 `requestsHash == c_emptyRequestsHash`（盖章点 `OpEngineService.cpp:429`，
  常量 `Constants.h:34`）② EIP-6110 排除语义（spec `isthmus/exec-engine.md`「存款请求」）。
- 反向验证：把盖章点改成 `h256{}` → ① 必红。

### E6 —— operator fee 收取/退款（`bcos-evm-opstack-tests`，+2）
- ① ② ④ ⑥ 已覆盖（台账给了用例名）；**新格只有**：
  ③ **Jovian 公式差**：同参数下 Isthmus `g*scalar/1e6+const` 与 Jovian `g*scalar*100+const` 的比值不同
  ⑤ **退款**：`charge(gasLimit) - charge(gasUsed)`，vault 净余额 == `computeOperatorCost(fee, gasUsed)`
- oracle：op-revm `l1block.rs:184-196`（公式）+ `constants.rs:28,31`；提取常量进现有 `op_revm_oracle.json`
  的 `operator_fee_*` 键（不新建文件）。
- 反向验证：改公式常量（除子 1e6→1e3）→ ③⑤ 必红。

### E7 —— Jovian DA footprint（`opstack-executor-block-tests` + `test-bcos-engine`，共 4 格）
- ② **等值缺陷格**（`test-bcos-engine`，期望 RED = F-A2 证明）：Jovian payload 的 `blobGasUsed` 与本地
  Σ 不一致 → 期望 INVALID。
- ③ 边界对齐格（`opstack-executor-block-tests`，期望绿，**1 用例内三条断言**，形如既有
  `checkModExpSizeBound` 的参数化风格）：`< gasLimit` 有效、`== gasLimit` **有效**（与 op-geth `>` 同界）、
  `> gasLimit` 拒绝；三条各自断言，避免"只测一侧"。
- ④ baseFee 耦合格：`gasMetered := max(gasUsed, blobGasUsed)`（spec `jovian/exec-engine.md`）。
- ⑤ deposit-only 块 `seal.blobGasUsed == 0`。
- oracle：op-geth `block_validator.go:119-132` + `types.CalcDAFootprint` + spec 伪码与 Fjord 常量。
- 反向验证：③ 把 `>` 改成 `>=`（临时）→ `==` 格必红（证明边界钉有效）；② 的 RED 本身即缺陷证据。

### E9 —— 跨 fork payload 夹具（**独立新头**，无 gate；`opstack-executor/tests/support/`）
- 接口：`byFork(OpForkId)` / `byTimestamp(uint64_t)` 两工厂；payload 形状复用 M2 的 `clShapedPayload`
  归一化，避免第二份形状表；**不修改** `OpEngineKarstTestHarness.h`。
- 验收：既有引擎路径测试零回归（四 target 计数只增不减）。

### E10 —— 激活块引擎路径（`test-bcos-engine`，+**3~15**，以 R2 复核为准）
- 首步复核台账"withdrawalsRoot 三态大部分已覆盖"的边界：Isthmus 三态（pre-Canyon nil /
  Canyon 起 `keccak256(rlp(empty_code))` / Isthmus 起 MessagePasser root）中哪一侧仍无引擎路径格。
- 只补缺口：预计集中在 **首个 Isthmus 块必带 MessagePasser root** 与 **pre-Isthmus 的两种旧形态负向**；
  Canyon/Holocene 形状若已被 M2+校验器覆盖则记 +0 并引用。
- 依赖 E9。

### E11 —— 逐 fork L1Info 存款形状（`opstack-executor-block-tests`，+4）
- ① Ecotone ② Isthmus ③ Jovian 逐字节对拍 ④ Fjord/Granite 负向（不得解出附加字段）。
- oracle：`{ecotone,isthmus,jovian}/l1-attributes.md` + op-node `l1_block_info.go` 打包字节序。

### E12 —— 逐 fork 收据 meta 形状（`opstack-executor-block-tests`，+4，**执行层**）
- ① Canyon depositNonce ② Ecotone scalar 组切换 ③ Isthmus operatorFee ④ Jovian DA scalar。
- 层选择依据：rpc 层 `combineReceiptResponse` 是 meta-presence 驱动、不看 fork（Plan A M3 结论）。
- oracle：op-geth `gen_receipt_json.go`/`receipt_opstack.go` + specs。

## 4. 生产变更（独立授权项，未授权则只交付 finding + 缺陷证明格）

**F-A2 修复方案**：在引擎校验中补 Jovian 等值检查，与 op-geth 对齐。

- 位置：`engine/bcos-engine/OpEngineService.cpp` 的 DA 校验块（现 `180-199`，`<= gasLimit` 一带）。
- 变更：新增——Jovian 档下 `payload.blobGasUsed` 必须等于本地重算的 Σ（复用 `OpBlockExecute.cpp:508-525`
  的同一计算，抽出为共享调用，避免第二份公式——review 规则 #21/#37）。
- 判据（oracle 原文）：`blobGasUsed != daFootprint` → INVALID，错误信息含 remote/local（op-geth 同形）。
- **blast radius（R2 必须逐一核）**：哪些既有测试/夹具会因"更严"而变红——特别是
  ① `OpL1EdgeGateTest` 的 DA 用例（它们喂的是 remote==local 还是构造值？）
  ② `OpKarstActivationTest` 的 deposit-only 块（`0 == 0`，应无影响）
  ③ M2 语料的 `blobGasUsed` 与实际交易是否一致（语料是 op-geth 产出，应一致）
  ④ 任何用 `blobGasUsed` 作"占位值"的引擎路径测试。
- 未授权时的替代：② 格按"期望 INVALID"写、实测 RED，作为 finding 证据提交；**不改实现**。

## 5. 顺序与依赖

```
E3 / E4 / E6（独立，可并行准备）
E7-② （缺陷证明，独立）→ E7-③④⑤
E5（先查重）→ 视结果 +0 或建格
E9（新夹具头）→ E10（先复核三态缺口）→ E11 → E12
§4 生产修复：仅在获得授权后执行，且必须先完成 blast radius 复核
```

## 6. 验收

> 执行期的判据与验证标准以 `...-stage2-impl-r2.md` §0「验证契约」为**唯一**依据（本设计不重复）；
> 契约含单格生命周期 10 个触发点、Task 验证块模板、失效模式 checklist 与残留登记格式。

- [ ] E3 +3 / E4 +5 / E6 +2 / E7 +4（其中 ② 为**期望 RED** 的缺陷证明格，单独记录）/ E5 与 E10 按复核结果落定
- [ ] E9 夹具头落地且四 target 计数只增不减；E11 +4 / E12 +4
- [ ] 全部新用例带 `fork-<name>` 标签、decorator 单行 + `clang-format off/on`
- [ ] 新增/注册变异变体：E2 已有 `V-GRANITE-bn254cap`；至少再为 E6（公式）与 E7-③（边界）各注册 1 个
- [ ] F-S3-1 的 refuted 结论**三处落地**：Task 0 台账（标 refuted + 反证原文）、本设计 §2、
      以及 impl 计划 R1 的 E7 行（把"缺陷证明格"改锚到等值校验）——撤回误判必须留痕，不得只在本文档
- [ ] F-A2 的缺陷证明格（E7-②）在**未授权生产变更**的情况下仍作为 finding 证据留存
- [ ] 过程文档不入库；无推送；语料只读

## 7. cannot-determine / 转出

- EIP-7934 区块尺寸：按台账 S7 判"EL 无需实现"→ cannot-determine + 最小读取集（或转 finding）。
- EIP-2935/6110 的系统调用接线（CL 面）→ Plan D；6110 的**排除语义**在 E5。
- EIP-2537 的 0x0e/0x10/0x11 与大体量向量：规模原因待补。
- EIP-4788 ring-buffer 回绕：规模原因 cannot-determine。
- M4 全 fork 连续链、M5 zip、M8 stateRoot 差分：维持 Plan B/C/D。

## 8. 风险

| 风险 | 处置 |
|---|---|
| 误判边界方向（F-S3-1 的教训） | **每个边界格先引 oracle 的比较符原文**（op-geth 代码行或 spec 的类比句），再写断言 |
| E7-② 的期望 RED 被误当"实现坏了" | 设计明文标注为缺陷证明格，红即结论；不调期望表 |
| §4 生产变更伤及既有测试 | blast radius 逐项复核（§4 四条）后才动手；未授权不动 |
| E5/E10 的"已覆盖"是误判 | 两 Task 首步都做独立复核（R2 前完成），结果写回台账 |
| E9 新夹具头与共享头重复 | 只做归一化与工厂，不复制共享头的 seam/ledger 逻辑；必要时共享头只读 include |
