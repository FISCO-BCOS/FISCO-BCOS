# opstack DA / Operator Fee 参数化矩阵测试 — 设计

> 日期:2026-08-17
> 范围:FISCO opstack Jovian DA gas / operator fee 参数化矩阵补齐(仅此专项)
> 参考:op-geth(`core/types/rollup_cost.go` + `rollup_cost_test.go`)与 op-reth(`crates/optimism/evm/src/l1.rs` `parse_l1_info_tx_*`)
> 前置核实:FISCO 公式已与 op-geth 逐位一致(`gas×scalar/1e6+constant` Isthmus、`gas×scalar×100+constant` Jovian、pre-Isthmus→0、参数槽空→0);缺口是**测试覆盖**,非实现分歧。

## 背景与缺口

FISCO 已有 DA 相关测试(`bcos-evm/test/opstack/RollupCostTest.cpp` 11 用例 + `OpFeeParamsTest.cpp` 3 用例),覆盖主路径(L1 cost Bedrock/Ecotone/Fjord、FLZ 压缩、DA size、operator cost Isthmus/Jovian、slot 解包 happy-path)。对照 op-geth `rollup_cost_test.go` 13 用例,缺:

1. **pre-Isthmus → operator fee 0**(op-geth `!IsOptimismIsthmus → 0`)
2. **OperatorFeeParamsSlot 空(零哈希)→ 0**(首块回退)
3. **TotalRollupCost(L1 + operator 求和)**
4. **Extract*GasParams 逐 fork 标量提取的极端值/边界**(calldata 解析层)
5. **溢出边界**(op-geth 注释「77 bits,panic」;u64max×u32max×100 < 2^256 验证)
6. **fork 切换同输入**(0 → Isthmus → Jovian → Karst)

## 方案 A:跨客户端三端对拍(已获批)

### 层次总览

```
┌─ A 层:CI 内单测(确定性,锚定快照)─────────────────────────┐
│  OpFeeParamsTest + RollupCostTest 增补 ~11 用例           │
│  新增 OpCalldataParseTest(op-reth parse_l1_info_tx_* 参照) │
├─ B 层:跨客户端三端对拍 harness(离线生成快照 + CI 对拍)────┤
│  JSON 网格(唯一输入源)                                    │
│    ├─ FISCO runner  (C++, 调 RollupCost.h)               │
│    ├─ op-geth runner(Go,  调 rollup_cost.go)             │
│    └─ op-reth runner(Rust,调 op-reth l1.rs / revm op)    │
│  三端输出逐位对拍 → 提交 3 份期望快照入 CI                 │
├─ C 层(相邻):jovian DA 极端值 t8n 向量(opt8n-ref,可选)─────┤
└──────────────────────────────────────────────────────────┘
```

### JSON 网格规格(`tools/da-matrix/da_matrix.json`,唯一输入源 = 规格)

输入(每行一用例):
```json
{
  "cases": [
    {
      "id": "isthmus_baseline",
      "slots": {"1": "0x..", "3": "0x..", "7": "0x..", "8": "0x.."},
      "envelope": "0x02..",
      "gas": 21000,
      "fork": "isthmus"
    }
  ]
}
```
- `slots`:OpFeeParams 4 个存储槽,L1Block 地址上的 1/3/7/8,32 字节 big-endian hex。
- `envelope`:签名交易信封(hex)——L1 data fee 需要(FISCO `computeL1Cost` 与 op-geth `L1CostFunc` 都吃 envelope/calldata;op-reth 经 revm 同样)。每个用例的 envelope 随用例给出,保证三端输入一致。
  - slot1 = l1_base_fee(整槽)
  - slot3 bytes[16,20) = base_fee_scalar,[20,24) = blob_base_fee_scalar
  - slot7 = blob_base_fee(整槽)
  - slot8 bytes[18,20) = da_footprint_gas_scalar,[20,24) = operator_fee_scalar,[24,32) = operator_fee_constant
- `gas`:交易 gas(十进制)。
- `fork` 枚举:`bedrock / regolith / ecotone / fjord / granite / isthmus / jovian / karst`。

输出(每 runner 各产一份,逐位对拍):
```json
{ "id": "isthmus_baseline", "l1_cost": "0x..", "operator_cost": "0x.." }
```
- `l1_cost`:L1 data fee;`operator_cost`:operator fee(未激活 fork 或参数空 → `0x0`)。

**网格覆盖 7 类**:
| 类 | 用例 | 目的 |
|---|---|---|
| 基准 | isthmus/jovian 各一,正常 scalar/constant | 主路径锚定(op-geth 参考常量) |
| 全 max | scalar=0xffffffff, constant=0xffffffffffffffff, da=0xffff | 解包+计算上限 |
| 溢出 | gas=u64max × scalar=u32max(×100 Jovian) | op-geth「77 bits,panic」边界 |
| pre-Isthmus | fjord/ecotone 同一输入 | `!IsIsthmus → operator_fee=0` |
| 缺失参数 | 4 槽全零 | OperatorFeeParamsSlot 空 → 0(首块回退) |
| 首块回退 | ecotone 首块,Bedrock 属性 | op-geth FirstBlockEcotone 语义 |
| fork 切换 | 同一 (slots, gas) 切 fjord/isthmus/jovian/karst | 三端公式选择一致性 |

### 三端 runner 结构(各为薄壳)

**FISCO runner**(`tools/da-matrix/run_fisco.cpp`,C++17)
- 读 `da_matrix.json`;对每行构造 `OpFeeParams`(slot hex → 字段),按 fork tag 选 `OpForkConfig`(复用 `OpForkSchedule.h` / `OpForkConfig`)
- 调 `computeL1Cost(params, envelope, cfg)` 与 `computeOperatorCost(params, gas, cfg)`
- 输出 `out_fisco.json`;独立可执行,链接 bcos-evm 静态库 + evmc + intx + evmone

**op-geth runner**(`tools/da-matrix/run_opgeth/main.go`,Go)
- 读网格;对每行构造 mock `StateGetter`(仅 `GetState(L1BlockAddr, slot)` 返回网格槽值;其余地址返回空)
- fork tag → `params.ChainConfig`(对应 fork 时间戳置 0,参考 `OptimismTestConfig`)
- 调 `NewL1CostFunc(config, statedb)` 与 `NewOperatorCostFunc(config, statedb)`(`core/types/rollup_cost.go` 导出);L1 数据 fee 用网格的 `envelope` 构造 `RollupCostData`
- 输出 `out_opgeth.json`

**op-reth runner**(`tools/da-matrix/run_opreth/`,Rust)
- 依赖本地 `op-reth/crates/optimism/evm`(`l1.rs` 的 `L1BlockInfo` 扩展 `l1_tx_data_fee`)或 revm 19.4.0 `op` 模块(本地有 revm checkout)
- fork tag → `OpSpecId`(revm);operator fee 位置实现时确认(候选:revm `op` 模块的 operator fee,或 op-reth `optimism/evm`/`consensus` 层——两者都在本机可查)
- 输出 `out_opreth.json`

> 三端对网格字段语义必须一致(槽值 big-endian hex、gas 十进制、fork 枚举、envelope hex)。实现时以「FISCO 先产出 → 另两端对拍」为对齐手段。

### 快照与 CI 集成

- 首次:三端各自产出 → 人工核对一致 → **提交 3 份快照**(`tools/da-matrix/golden/{fisco,opgeth,opreth}/`)
- CI 门(快):`run_fisco --check opgeth` / `--check opreth` → 逐位比对快照
- 深度检查(可选 target):实时三端对拍(CI 需 Go/Rust)
- op-geth 与 op-reth 若分叉 → 记为「已知双端分叉」,快照以 op-geth 为准(OP 规范参考),在 `tools/da-matrix/README.md` 记录

### A 层单测增补(锚定快照值,CI 内)

**`OpFeeParamsTest.cpp`**(+3):
- `UnpacksMaxValueScalars` — 全 max 槽解包
- `PackedByteBleedIsolation` — [18,20)/[20,24)/[24,32) 相邻不串扰
- `MissingAllSlotsZero` — 4 槽全零 → 全参 0

**`RollupCostTest.cpp`**(+5):
- `OperatorFeePreIsthmusZero` — fjord/ecotone → 0
- `OperatorFeeMissingParamsZero` — 槽全零 → 0(首块回退)
- `OperatorFeeMaxValuesNoWrap` — u64max×u32max(×100)< 2^256,无回绕
- `TotalRollupCostFjordIsthmusJovian` — L1+operator 求和,锚定 op-geth 常量(ithmusOperatorFee=1256417826611659930 / jovianOperatorFee=1256650673615173860 / fjordFee=3203000)
- `OperatorFeeForkSwitchSameInput` — 同输入切 4 fork

**新增 `OpCalldataParseTest.cpp`**(参照 op-reth `parse_l1_info_tx_*`,+3):
- `ParseIsthmusCalldata176` — 176B 布局字段提取
- `ParseJovianCalldata178` — 178B 布局 + selector 分派(0x098999be / 0x3db6be2b)
- `ParseTruncatedRejects` — 过短/缺字段 calldata 拒绝

### 实施顺序(每步可独立提交)

1. 网格 JSON + FISCO runner — 先建,立即跑现有实现得基线(若实现有 bug,此步暴露)
2. op-geth runner → op-geth 快照 → 与 FISCO 对拍
3. op-reth runner → op-reth 快照 → 三端对拍
4. 裁决分叉 + 提交快照
5. A 层单测增补(锚定快照值)
6. C 层 t8n 向量(可选,jovian DA 极端值)
7. 全量回归:`opstack-executor` C++ 套件 + op-e2e(`run_all.sh`)无退步

## 约束

- **测试不可退步**:任何修改后已通过测试集合不得变红;本方案只增补测试与工具,不触碰 DA/operator fee 实现(除非三端对拍暴露真实分歧,届时单独立案)。
- 单测与网格快照值必须逐位一致(FISCO 单测锚定已提交快照,防止快照与单测漂移)。
