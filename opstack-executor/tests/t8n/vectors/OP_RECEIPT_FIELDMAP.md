# OP_RECEIPT_FIELDMAP — op-geth 回执 OP 字段映射（isthmus/jovian）最终版

Phase 2 line C 最终字段映射文档。初版为 Task 0 探针产物（`--probe-receipt-fields <case.in.json>`，
`opt8n-ref`，`bcos-evm/test/opstack/t8n/generator/main.go` 自含 op-geth 管线 dump 发射面）；
Task 1 扩发射、Task 2 重生成、Task 3 全字段比对、Task 4 修复全绿后演进为本文件。
op-geth 以 PIN `e8800cffe53d459cde8a07c8e8f1de9d86e79e07` 为准。

**本文件是线 C 的权威字段映射规范**：每字段的存在性规则、值格式、FISCO meta 对应、比对结论。
引用本文件时，一律以本表为准，不复述探针。

---

## 1. 结论速览（最终比对字段集，Task 0-4 全绿）

| # | FISCO `_op_` 字段 | op-geth 源字段 | 发射条件 | 值格式 | 状态（线 C 终态） |
|---|---|---|---|---|---|
| 1 | `_op_deposit_nonce` | `Receipt.DepositNonce` | 仅 deposit 回执（Regolith+） | hex `0x…` | 已发射+已比对（全绿） |
| 2 | `_op_deposit_receipt_version` | `Receipt.DepositReceiptVersion` | 仅 deposit 回执（Canyon+） | hex `0x…` | 已发射+已比对（全绿） |
| 3 | `_op_l1_fee` | `Receipt.L1Fee` | 每笔非 deposit 回执 | hex `0x…` | 已发射+已比对（全绿） |
| 4 | `_op_operator_fee` | **FISCO 派生**（无 op-geth 字段） | 非 deposit ∧ operator scalar/constant 非零 | hex `0x…` | 已发射+已比对（全绿） |
| 5 | `_op_da_footprint` | **从 `Receipt.BlobGasUsed` 派生** | 非 deposit ∧ Jovian | hex `0x…` | 已发射+已比对（全绿） |
| 6 | `_op_l1_gas_price` | `Receipt.L1GasPrice` | 每笔非 deposit 回执 | hex `0x…` | 已发射+已比对（全绿） |
| 7 | `_op_l1_blob_base_fee` | `Receipt.L1BlobBaseFee` | 每笔非 deposit 回执 | hex `0x…` | 已发射+已比对（全绿） |
| 8 | `_op_l1_gas_used` | `Receipt.L1GasUsed` | **每笔非 deposit 回执（裁决：恒发射）** | hex `0x…` | 已发射+已比对（全绿，FISCO Task 4 补算） |
| 9 | `_op_l1_base_fee_scalar` | `Receipt.L1BaseFeeScalar` | 每笔非 deposit 回执 | hex `0x…` | 已发射+已比对（全绿） |
| 10 | `_op_l1_blob_base_fee_scalar` | `Receipt.L1BlobBaseFeeScalar` | 每笔非 deposit 回执 | hex `0x…` | 已发射+已比对（全绿） |
| 11 | `_op_operator_fee_scalar` | `Receipt.OperatorFeeScalar` | 非 deposit ∧ scalar/constant 非零 | hex `0x…` | 已发射+已比对（全绿） |
| 12 | `_op_operator_fee_constant` | `Receipt.OperatorFeeConstant` | 非 deposit ∧ scalar/constant 非零 | hex `0x…` | 已发射+已比对（全绿） |
| 13 | `_op_da_footprint_gas_scalar` | `Receipt.DAFootprintGasScalar` | 非 deposit ∧ Jovian | hex `0x…` | 已发射+已比对（全绿） |

`FeeScalar`（op-geth `Receipt.FeeScalar`, `*big.Float`）在 isthmus/jovian 下恒 absent（post-Ecotone 已废弃），
无 FISCO meta 对应，不列入 gate。

> 注：`_op_operator_fee`（#4）与 `_op_da_footprint`（#5）虽为 FISCO 侧派生/可派生字段，
> **两者都进值比对，不豁免**——见 §5.2/§5.3。

---

## 2. FeeScalar 映射（isthmus/jovian，spec C4）

- FISCO `l1_base_fee_scalar` ↔ op-geth `L1BaseFeeScalar`（直接值映射，无打包）
- FISCO `l1_blob_base_fee_scalar` ↔ op-geth `L1BlobBaseFeeScalar`（直接值映射）
- op-geth 打包 `FeeScalar` 仅 pre-Ecotone 发射；isthmus/jovian 不出现 → 无需解包
- Ecotone calldataGas 打包映射归线 A（另立）

---

## 3. 探针方法

- 探针 4 case（`--write-cases` 生成 `.in.json` 后逐个跑）：
  `isthmus_transfer_basic`（普通）、`jovian_da_mix`（Jovian DA）、
  `isthmus_deposit_only`（纯 deposit）、`isthmus_fee_env_observer`（唯一 operator scalar 非零的 case）。
- 管线：genesis 构造 → tx 签名 → `GenerateChainWithGenesis` → `receiptsAll[0]`，
  与 `processBlockVector` 前半段逐行同源（探针为自含一次性工具，不重构 `processBlockVector`）。
- 数值字段统一 hex 打印；`FeeScalar` 用 `*big.Float.String()` 十进制。

## 4. 发射矩阵（探针实测）

### 4.1 deposit 回执（op-geth `receipt_opstack.go:36-38`：最后一笔 tx 是 deposit 则整体早退）

| 字段 | deposit_only | deposit 出现在其他 case |
|---|---|---|
| `DepositNonce` | `0x0` | `0x0` |
| `DepositReceiptVersion` | `0x1` | `0x1` |
| `L1GasPrice`/`L1BlobBaseFee`/`L1GasUsed`/`L1Fee`/`FeeScalar`/`L1BaseFeeScalar`/`L1BlobBaseFeeScalar`/`OperatorFeeScalar`/`OperatorFeeConstant`/`DAFootprintGasScalar`/`BlobGasUsed` | 全 absent | 全 absent |

### 4.2 非 deposit 回执（isthmus，`fee_env_observer` 实测）

| 字段 | 值 |
|---|---|
| `L1GasPrice` | `0x6fc23ac00`（30 gwei） |
| `L1BlobBaseFee` | `0xf4240`（1,000,000） |
| `L1GasUsed` | `0x640`（1,600） |
| `L1Fee` | `0xf4eb688f4` |
| `FeeScalar` | absent（post-Ecotone 恒 nil） |
| `L1BaseFeeScalar` | `0x558`（1,368） |
| `L1BlobBaseFeeScalar` | `0xc5fc5`（810,949） |
| `OperatorFeeScalar` | `0x1388`（5,000） |
| `OperatorFeeConstant` | `0x1e61`（7,777） |
| `DAFootprintGasScalar` | absent（isthmus） |
| `BlobGasUsed` | absent（0，isthmus） |

### 4.3 非 deposit 回执（jovian，`da_mix` 实测，DA scalar=400）

| 字段 | tx1 | tx2 | tx3 |
|---|---|---|---|
| `L1GasUsed` | `0x640` | `0x1308` | `0x438d` |
| `L1Fee` | `0xf4eb688f4` | `0x2e9ee82768` | `0xa572c76232` |
| `DAFootprintGasScalar` | `0x190` | `0x190` | `0x190` |
| `BlobGasUsed` | `0x9c40`（40,000） | `0x1db00`（121,600） | `0x69780`（432,000） |

`BlobGasUsed` 与向量 `_op_da_footprint` 逐笔一致（`0x9c40`/`0x1db00`/`0x69780`）——Jovian 载体确认。
`OperatorFeeScalar`/`OperatorFeeConstant` 在此 case absent（scalar=0）。

---

## 5. 关键裁决

### 5.1 `L1GasUsed` 裁决：恒发射（每笔非 deposit 回执）

op-geth `deriveOPStackFields`（`receipt_opstack.go:40`）对每笔非 deposit 回执赋值
`rs[i].L1Fee, rs[i].L1GasUsed = gasParams.costFunc(rcd)`——**非 deposit 回执的 `L1GasUsed` 恒非 nil**。
4 case 探针无一例外。→ Task 1 **保留**该字段，Task 3 **保留比对**；
FISCO 侧 `_op_l1_gas_used` 存在性断言**永不删除**（§7 Global Constraints）。

### 5.2 `_op_operator_fee`：FISCO 派生字段（op-geth 无 `OperatorFee` 字段）

op-geth 回执不发射聚合 operator fee；`buildExpectedReceipts`（`main.go:1570-1623`）用
`operatorFee()` 公式（scalar/constant × gasUsed，Isthmus `/1e6` vs Jovian `×100`）手算，
并与 op-geth `NewOperatorCostFunc` 交叉校验。**值比对照常做**（不豁免）。

### 5.3 `_op_da_footprint`：op-geth 可派生字段（载体 `BlobGasUsed`）

Jovian 下 `deriveOPStackFields` 令 `BlobGasUsed = daFootprintGasScalar × EstimatedDASize`。
`_op_da_footprint` 即 `r.BlobGasUsed` 的 hex。**值比对照常做**（不豁免）。

### 5.4 Operator scalar/constant：仅非零发射

`receipt_opstack.go:44`：`operatorFeeScalar != 0 || operatorFeeConstant != 0` 才写回执。
`transfer_basic`/`da_mix`（scalar=0）absent；`fee_env_observer`（5000/7777）present。
生成器 `buildExpectedReceipts` 以 `emitOpFee := opScalar != 0 || opConstant != 0` 同源镜像。

---

## 6. Task 4 结果：`_op_l1_gas_used` FISCO 补算 → gate 全绿

§5.1 裁决 op-geth 对每笔非 deposit 回执恒发射 `L1GasUsed`；Task 3 重放器升级为全字段比对
（commit f8c1751b3）后暴露 FISCO 侧 `_op_l1_gas_used` 恒 nullopt —— **168 行 DIVERGE**
（want=`0x…` got=`<absent>`，存在性不对称，category 2），零回归（7 新增 + 5 既有字段）。

**修复（Task 4，commit 729d68bdc，`OpTransition.cpp:223`）**——FISCO 侧补算 Fjord L1 calldata gas：

```cpp
m.l1_gas_used = static_cast<uint64_t>(estimatedDaSizeScaled(props.flz_len) * 16 / 1'000'000);
```

- 公式对照 op-geth `core/types/rollup_cost.go:623-624`（`NewL1CostFuncFjord`）：
  `L1GasUsed = estimatedDASizeScaled(fastLzSize) * TxDataNonZeroGasEIP2028(16) / 1e6`。
- 用 `estimatedDaSizeScaled`（`RollupCost.h:17`）而非 `estimatedDaSizeFromFlz`——后者 flz==0 → 0 分叉
  且先除后乘低报线性支。
- 发射规则对照 op-geth `core/types/receipt_opstack.go` `deriveOPStackFields`（:40, :59-63）：
  非 deposit 恒设；FISCO `deriveOpReceiptMeta` 仅经 `opTransition`（非 deposit）可达，
  `runDeposit` 直接构造 deposit 字段不受影响。
- 数值回放：Task 3 want 值 5 个 distinct（`0x640`/`0x7e6`/`0x1141`/`0x1308`/`0x438d`）全部精确复现。

**Gate 结果**：`bcos-evm-opstack-tests --run_test=OpT8nReplay` → **exit 0，0 DIVERGE**（原 168）；
全量 suite 无回归；`DIVERGENCES.md` diff 空、无 ALLOWLIST 新增——`l1_gas_used` 是 **FIXED 非豁免**。

> **Ecotone latent divergence（归线 A，gate 不可见）**：`deriveOpReceiptMeta` 不取 `cfg`，
> Ecotone 下 `props.flz_len == 0` → 补算 `estimatedDaSizeScaled(0)*16/1e6 = 1600`，
> 而 op-geth Ecotone `L1GasUsed` 为 `bedrockCalldataGasUsed`（zeros*4 + nonzeros*16）。
> 34 向量全为 isthmus/jovian（Fjord+），gate 覆盖不到；未来若开 Ecotone 回执 gate 需 cfg-aware 分支。

---

## 7. 全局约束（Global Constraints）

1. `_op_l1_gas_used` 的 FISCO 侧存在性断言永不删除（§5.1）。
2. 探针 4 case 覆盖普通/Jovian/deposit/operator-fee 四分支；任何一支缺失即 fieldmap 失效。
3. op-geth 侧字段以 PIN `e8800cff` 为准；字段名冲突时 `grep -nE "BlobGasUsed|OperatorFeeScalar|L1GasUsed" <op-geth>/core/types/receipt.go` 核对。
4. `types.Receipt` 无 `DAFootprint` 字段——DA footprint 的载体是 `BlobGasUsed`，禁直接引用 `DAFootprint`。

---

## 8. 探针原始输出（4 case 全量）

### 8.1 isthmus_transfer_basic（deposit + 1 transfer）

```
tx[0] type=0x7e
  DepositNonce	0x0
  DepositReceiptVersion	0x1
  L1GasPrice	absent
  L1BlobBaseFee	absent
  L1GasUsed	absent
  L1Fee	absent
  FeeScalar	absent
  L1BaseFeeScalar	absent
  L1BlobBaseFeeScalar	absent
  OperatorFeeScalar	absent
  OperatorFeeConstant	absent
  DAFootprintGasScalar	absent
  BlobGasUsed	absent
tx[1] type=0x02
  DepositNonce	absent
  DepositReceiptVersion	absent
  L1GasPrice	0x6fc23ac00
  L1BlobBaseFee	0xf4240
  L1GasUsed	0x640
  L1Fee	0xf4eb688f4
  FeeScalar	absent
  L1BaseFeeScalar	0x558
  L1BlobBaseFeeScalar	0xc5fc5
  OperatorFeeScalar	absent
  OperatorFeeConstant	absent
  DAFootprintGasScalar	absent
  BlobGasUsed	absent
```

### 8.2 jovian_da_mix（deposit + 3 transfers）

```
tx[0] type=0x7e
  DepositNonce	0x0
  DepositReceiptVersion	0x1
  L1GasPrice	absent
  L1BlobBaseFee	absent
  L1GasUsed	absent
  L1Fee	absent
  FeeScalar	absent
  L1BaseFeeScalar	absent
  L1BlobBaseFeeScalar	absent
  OperatorFeeScalar	absent
  OperatorFeeConstant	absent
  DAFootprintGasScalar	absent
  BlobGasUsed	absent
tx[1] type=0x02
  DepositNonce	absent
  DepositReceiptVersion	absent
  L1GasPrice	0x6fc23ac00
  L1BlobBaseFee	0xf4240
  L1GasUsed	0x640
  L1Fee	0xf4eb688f4
  FeeScalar	absent
  L1BaseFeeScalar	0x558
  L1BlobBaseFeeScalar	0xc5fc5
  OperatorFeeScalar	absent
  OperatorFeeConstant	absent
  DAFootprintGasScalar	0x190
  BlobGasUsed	0x9c40
tx[2] type=0x02
  DepositNonce	absent
  DepositReceiptVersion	absent
  L1GasPrice	0x6fc23ac00
  L1BlobBaseFee	0xf4240
  L1GasUsed	0x1308
  L1Fee	0x2e9ee82768
  FeeScalar	absent
  L1BaseFeeScalar	0x558
  L1BlobBaseFeeScalar	0xc5fc5
  OperatorFeeScalar	absent
  OperatorFeeConstant	absent
  DAFootprintGasScalar	0x190
  BlobGasUsed	0x1db00
tx[3] type=0x02
  DepositNonce	absent
  DepositReceiptVersion	absent
  L1GasPrice	0x6fc23ac00
  L1BlobBaseFee	0xf4240
  L1GasUsed	0x438d
  L1Fee	0xa572c76232
  FeeScalar	absent
  L1BaseFeeScalar	0x558
  L1BlobBaseFeeScalar	0xc5fc5
  OperatorFeeScalar	absent
  OperatorFeeConstant	absent
  DAFootprintGasScalar	0x190
  BlobGasUsed	0x69780
```

### 8.3 isthmus_deposit_only（纯 deposit）

```
tx[0] type=0x7e
  DepositNonce	0x0
  DepositReceiptVersion	0x1
  L1GasPrice	absent
  L1BlobBaseFee	absent
  L1GasUsed	absent
  L1Fee	absent
  FeeScalar	absent
  L1BaseFeeScalar	absent
  L1BlobBaseFeeScalar	absent
  OperatorFeeScalar	absent
  OperatorFeeConstant	absent
  DAFootprintGasScalar	absent
  BlobGasUsed	absent
```

### 8.4 isthmus_fee_env_observer（deposit + 1 transfer，operator scalar 非零）

```
tx[0] type=0x7e
  DepositNonce	0x0
  DepositReceiptVersion	0x1
  L1GasPrice	absent
  L1BlobBaseFee	absent
  L1GasUsed	absent
  L1Fee	absent
  FeeScalar	absent
  L1BaseFeeScalar	absent
  L1BlobBaseFeeScalar	absent
  OperatorFeeScalar	absent
  OperatorFeeConstant	absent
  DAFootprintGasScalar	absent
  BlobGasUsed	absent
tx[1] type=0x02
  DepositNonce	absent
  DepositReceiptVersion	absent
  L1GasPrice	0x6fc23ac00
  L1BlobBaseFee	0xf4240
  L1GasUsed	0x640
  L1Fee	0xf4eb688f4
  FeeScalar	absent
  L1BaseFeeScalar	0x558
  L1BlobBaseFeeScalar	0xc5fc5
  OperatorFeeScalar	0x1388
  OperatorFeeConstant	0x1e61
  DAFootprintGasScalar	absent
  BlobGasUsed	absent
```
