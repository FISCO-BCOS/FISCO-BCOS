# opstack DA / Operator Fee 参数化矩阵测试 — 设计(v2,含三代理审查修订)

> 日期:2026-08-17(初稿);v2 修订 2026-08-17(三代理并行审查后)
> 范围:FISCO opstack Jovian DA gas / operator fee 参数化矩阵补齐(仅此专项)
> 参考:op-geth(`core/types/rollup_cost.go` + `rollup_cost_test.go`)、**op-revm 20.0.0**(OP Foundation revm fork)、**contracts-bedrock `GasPriceOracle.sol`**(Solidity 权威)。op-reth v1.1.5 降级为 L1 fee 交叉参考。
> 前置核实:FISCO 公式与 op-geth 逐位一致(`gas×scalar/1e6+constant` Isthmus、`gas×scalar×100+constant` Jovian);缺口是**测试覆盖** + 部分**已识别实现缺口**(见「单独立案」)。

## 背景与缺口

FISCO 已有 `bcos-evm/test/opstack/RollupCostTest.cpp`(11 用例)+ `OpFeeParamsTest.cpp`(3 用例),覆盖主路径。对照 op-geth `rollup_cost_test.go` 13 用例,缺:pre-Isthmus→0、参数槽空→0、TotalRollupCost、标量提取极端值、溢出边界、fork 切换同输入。

**三代理审查确认的修正**(v1 缺陷,已并入本版):
1. `computeOperatorCost` 是纯公式求值器,pre-Isthmus→0 与参数空→0 的门在调用方 `OpTransition.cpp:395`(`cfg.has_operator_fee ? computeOperatorCost(...) : 0`)。
2. op-reth v1.1.5 全树无 operator fee;revm 19.4.0 只有 Isthmus 公式、无 JOVIAN/KARST SpecId、deposit 判定 0x7F(真实 0x7E)。→ Rust 参考改用 **op-revm 20.0.0**。
3. FISCO 无 pre-Ecotone L1 公式、无 bedrock/regolith `OpFork`、无「首块 Ecotone→Bedrock 回退」→ 该类用例**裁剪**(单独立案,非测试缺口)。
4. FISCO L1 fee 在 ≥2^256 处饱和,op-geth 用 big.Int 无界 → 真实分歧,入 **known_divergence** 表。
5. op-geth 参考常量需 **gas=1618** + 钉死参数组才能复现。
6. CI `TOOLS=OFF` → runner 放 TESTS 域,`add_test` 挂 ctest。
7. 溢出上限:Isthmus 77 bits、**Jovian 103 bits**。

## 方案 A(v2):四端对拍

### 层次总览

```
┌─ A 层:CI 内单测(确定性,锚定快照)─────────────────────────┐
│  OpFeeParamsTest + RollupCostTest 增补 ~12 用例(经门封装)  │
│  opstack-executor 增补 shape 校验测试(validateJovianBlockShape) │
├─ B 层:四端对拍 harness(离线生成快照 + CI 对拍)────────────┤
│  JSON 网格(唯一输入源)                                    │
│    ├─ FISCO runner      (C++, 经 has_operator_fee 门封装) │
│    ├─ op-geth runner    (Go,  mock StateGetter)           │
│    ├─ op-revm runner    (Rust, 20.0.0, operator_fee_charge/tx_cost) │
│    └─ Solidity 权威端   (GasPriceOracle.sol getOperatorFee)│
│  四端输出逐位对拍 → 提交 4 份期望快照入 CI                 │
├─ C 层(相邻):jovian DA 极端值 t8n 向量(复用现有 t8n harness)│
└──────────────────────────────────────────────────────────┘
```

### JSON 网格规格(`da_matrix.json`,唯一输入源 = 规格)

输入(每行一用例):
```json
{
  "schema_version": 1,
  "cases": [
    {
      "id": "isthmus_baseline",
      "slots": {"1": "0x..", "3": "0x..", "7": "0x..", "8": "0x.."},
      "envelope_ref": "contract_call_tx",
      "gas": 1618,
      "block_time": 0,
      "fork": "isthmus",
      "known_divergence": null
    }
  ],
  "envelopes": {
    "contract_call_tx": "0x02..",
    "empty_tx": "0x.."
  }
}
```
- `slots`:OpFeeParams 4 个存储槽,L1Block 地址 1/3/7/8(32B big-endian hex)。布局见 OpFeeParams.h(与 L1Block.sol / op-geth 逐位一致)。
- `envelope_ref`:**引用** `envelopes` 注册表(避免每行重复 300B hex 抄错;复用 RollupCostTest 的 kEmptyTx/kContractCallTx 字节)。
- `gas`:交易 gas(十进制)。**基准行钉死 op-geth 参数组:baseFee=1000e6、blobBaseFee=10e6、baseScalar=2、blobScalar=3、opScalar=1439103868、opConst=1256417826609331460、gas=1618**(否则 ithmusOperatorFee=1256417826611659930 等参考常量无法复现)。
- `block_time`:显式块时间(三端 fork→timestamp 映射必须保证**只有目标 fork 激活、后续 fork 未激活**;pre-Isthmus 用例尤其不能 let isthmus 激活)。
- `fork` 枚举(裁剪后):`ecotone / fjord / granite / holocene / isthmus / jovian / karst`。
- `known_divergence`:可选字符串,登记已知分歧(见下)。

输出(每端各产一份,逐位对拍):
```json
{ "id": "isthmus_baseline", "l1_cost": "0x..", "operator_cost": "0x.." }
```

**网格覆盖 7 类**(裁剪后):
| 类 | 用例 | 目的 |
|---|---|---|
| 基准 | isthmus/jovian 各一,op-geth 钉死参数组 | 主路径锚定(参考常量可复现) |
| 全 max | scalar=0xffffffff, constant=0xffffffffffffffff, da=0xffff;**slot1/slot7 约束 < 2^256** | 解包+计算上限(避免触发饱和分歧) |
| 溢出 | gas=u64max × scalar=u32max(×100 Jovian) | 无回绕验证(Isthmus 77 bits / Jovian 103 bits) |
| pre-Isthmus | fjord/granite/holocene 同一输入 | `!IsIsthmus → operator_fee=0`(经门封装) |
| 缺失参数 | slot8 全零 | 参数槽空 → 0(首块回退语义) |
| fork 切换 | 同一 (slots, gas) 切 ecotone/fjord/isthmus/jovian/karst | 四端公式选择一致性(L1 公式切换点 ecotone→fjord 亦覆盖) |
| Jovian blob scalar | 非平凡 blobBaseFeeScalar × blob 项 | A 层补绝对锚定 |

**已裁剪(单独立案,FISCO 实现缺口,非测试缺口)**:bedrock/regolith fork、首块 Ecotone→Bedrock 回退、pre-Regolith +68。这些需要 FISCO 补 Bedrock L1 公式与首块检测实现,超出「只增测试」范围,单独 issue 跟踪。

### 四端 runner 结构

**FISCO runner**(`run_fisco`,C++,TESTS 域)
- 读网格;经 **`has_operator_fee` 门封装**(薄 wrapper 镜像 `OpTransition.cpp:395` 的三元门;纯 `computeOperatorCost` 也独立暴露,分别验证公式求值器与门两层)
- fork tag → `OpForkConfig`(`OpForkSchedule.h`);调 `computeL1Cost` / 门封装
- 独立可执行,`add_test(NAME DaMatrixFiscoCheck COMMAND run_fisco --check opgeth)` 挂 ctest(CI `TESTS=ON`)

**op-geth runner**(`run_opgeth/main.go`,Go)
- mock `StateGetter`(仅 `GetState(L1BlockAddr, slot)`,未知 slot 返回零;op-geth 自带 testStateGetter 同模式)
- fork tag → `params.ChainConfig`(前置 fork 时间戳全置 0,参照 OptimismTestConfig;jovian 必须 IsthmusTime+JovianTime 同时置 0)
- 调 `NewL1CostFunc`/`NewOperatorCostFunc`;envelope → `NewRollupCostData`

**op-revm runner**(`run_oprevm/`,Rust,依赖 op-revm 20.0.0)
- `OpSpecId::{ECOTONE..JOVIAN,KARST}`;调 `L1BlockInfo::operator_fee_charge` / `tx_cost`(Jovian×100 + DA footprint 在此)
- operator fee 参数经 `try_fetch` 从 slot 装载或手工构造 `L1BlockInfo` 字段

**Solidity 权威端**(GasPriceOracle.sol)
- `getOperatorFee` 逐项对照;L1 fee 端注意 **+68 / 未签名输入**口径差异,需换算(`getL1Fee` 吃未签名 tx + flz+68;其余端吃签名 envelope 无 +68)
- 可作为 forge 测试或 eth_call 形式对拍

**op-reth v1.1.5**:降级为 L1 fee 交叉参考(其 L1 cost 委托 revm;不做 operator/jovian 主参考)。**revm 19.4.0 弃用**(0x7F deposit 判定错误、无 JOVIAN/KARST)。

### 快照与 CI 集成

- 首次:四端各自产出 → 人工核对 → **提交 4 份快照**(`golden/{fisco,opgeth,oprevm,solidity}/`)入 repo
- **CI 门**(快):`run_fisco --check opgeth` → FISCO 输出 vs op-geth 快照逐位比(op-revm/solidity 快照同参数)
- **known_divergence 机制**:checker 读网格 `known_divergence` 字段,命中的行跳过并计数;已登记分歧:
  - `l1_fee_saturation`:FISCO L1 fee ≥2^256 饱和 2^256-1 vs op-geth big.Int 无界(网格 slot1/slot7 已约束 <2^256 避免触发;若未来放开,此标记生效)
  - `flz_zero_clamp`:flzLen==0 → FISCO 0 vs op-geth clamp 100(RollupCost.h 已文档化 DELIBERATE DIVERGENCE)
  - `karst_alias`:FISCO karstConfig 为 jovianConfig 别名(占位;输出「一致」是设计使然,非 karst 语义已验证)
- 分叉登记复用 `opstack-executor/tests/t8n/vectors/DIVERGENCES.md` 体系(等价/已知分叉/结构性差异)
- 深度检查(可选 workflow):实时四端对拍(Go/Rust/Solidity 构建重;FISCO runner 快照对拍为 CI 主门)

### A 层单测增补(锚定快照值,CI 内)

**`bcos-evm/test/opstack/OpFeeParamsTest.cpp`**(+3):
- `UnpacksMaxValueScalars` — 全 max 槽解包
- `PackedByteBleedIsolation` — [18,20)/[20,24)/[24,32) 相邻不串扰
- `MissingAllSlotsZero` — 4 槽全零 → 全参 0

**`bcos-evm/test/opstack/RollupCostTest.cpp`**(+6):
- `OperatorFeePreIsthmusZero` — fjord/granite/holocene → 0(**经门封装**)
- `OperatorFeeMissingParamsZero` — slot8 全零 → 0(经门封装)
- `OperatorFeeMaxValuesNoWrap` — u64max×u32max(×100)= 103 bits < 2^256
- `TotalRollupCostFjordIsthmusJovian` — L1+operator 求和,锚定 op-geth 常量(gas=1618)
- `OperatorFeeForkSwitchSameInput` — 同输入切 ecotone/fjord/isthmus/jovian/karst
- `JovianL1CostBlobScalarAnchor` — 非平凡 blob scalar 的 L1 cost 绝对锚定

**`opstack-executor/tests/`(新 target,+2)**:
- `ValidateJovianBlockShapeLenSelector` — 176/178B 长度 + selector 分派(0x098999be/0x3db6be2b)(锁定 `validateJovianBlockShape`)
- `ValidateJovianBlockShapeDaFootprintExtract` — calldata[176:178] da_footprint 提取
- (不新建「calldata 字段级解析」实现——避免触碰实现约束;shape 校验是现有函数)

### 实施顺序(每步可独立提交)

0. **前置 spike**:① FISCO `has_operator_fee` 门封装(薄 wrapper);② op-revm 20.0.0 本机构建可行性
1. 网格 JSON + FISCO runner(经门封装)→ 基线
2. op-geth runner → op-geth 快照 → 与 FISCO 对拍
3. op-revm runner + Solidity 端 → 各自快照 → 四端对拍
4. 裁决分叉(known_divergence 登记)+ 提交 4 份快照
5. A 层单测增补(锚定快照值)
6. C 层 t8n 向量(可选,jovian DA 极端值)
7. 全量回归:`opstack-executor` C++ 套件 + op-e2e(`run_all.sh`)无退步

## 约束

- **测试不可退步**:只增补测试与工具,不触碰 DA/operator fee 实现。网格/单测发现实现级分歧时 → 登记 `known_divergence` 或单独立案,不混入本方案修复。
- **单独立案跟踪(FISCO 实现缺口)**:pre-Ecotone L1 公式(含 +68)、首块 Ecotone→Bedrock 回退、Karst 真实适配(DIVERGENCES D-2 已标 🔴)、L1 fee 饱和语义。
- 单测与网格快照值必须逐位一致。
