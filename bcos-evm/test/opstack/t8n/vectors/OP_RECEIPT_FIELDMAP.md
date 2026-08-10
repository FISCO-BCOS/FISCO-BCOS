# OP_RECEIPT_FIELDMAP — op-geth 回执 OP 字段发射面（isthmus/jovian）

Phase 2 line C task 0 探针产出。探针 `--probe-receipt-fields <case.in.json>`（`opt8n-ref`，
`bcos-evm/test/opstack/t8n/generator/main.go`）驱动 op-geth 自含管线
（`GenerateChainWithGenesis` + 真实 `ApplyTransaction`/`FinalizeAndAssemble`），
dump 每笔回执的全部 OP 字段存在性 + hex 值。op-geth 以 PIN `e8800cffe53d459cde8a07c8e8f1de9d86e79e07` 为准。

**本文件是 Task 1/3 的字段发射面规范**：每字段的存在性规则、值格式、FISCO meta 对应。引用本文件时，
一律以本表为准，不复述探针。

---

## 1. 结论速览（Task 1/3 直接照此实现）

| # | FISCO `_op_` 字段 | op-geth 源字段 | 发射条件 | 值格式 | 阶段 |
|---|---|---|---|---|---|
| 1 | `_op_deposit_nonce` | `Receipt.DepositNonce` | 仅 deposit 回执（Regolith+） | hex `0x…` | 已发射 |
| 2 | `_op_deposit_receipt_version` | `Receipt.DepositReceiptVersion` | 仅 deposit 回执（Canyon+） | hex `0x…` | 已发射 |
| 3 | `_op_l1_fee` | `Receipt.L1Fee` | 每笔非 deposit 回执 | hex `0x…` | 已发射 |
| 4 | `_op_operator_fee` | **FISCO 派生**（无 op-geth 字段） | 非 deposit ∧ operator scalar/constant 非零 | hex `0x…` | 已发射 |
| 5 | `_op_da_footprint` | **从 `Receipt.BlobGasUsed` 派生** | 非 deposit ∧ Jovian | hex `0x…` | 已发射 |
| 6 | `_op_l1_gas_price` | `Receipt.L1GasPrice` | 每笔非 deposit 回执 | hex `0x…` | Task 1/3 |
| 7 | `_op_l1_blob_base_fee` | `Receipt.L1BlobBaseFee` | 每笔非 deposit 回执 | hex `0x…` | Task 1/3 |
| 8 | `_op_l1_gas_used` | `Receipt.L1GasUsed` | **每笔非 deposit 回执（裁决：恒发射）** | hex `0x…` | Task 1/3 |
| 9 | `_op_l1_base_fee_scalar` | `Receipt.L1BaseFeeScalar` | 每笔非 deposit 回执 | hex `0x…` | Task 1/3 |
| 10 | `_op_l1_blob_base_fee_scalar` | `Receipt.L1BlobBaseFeeScalar` | 每笔非 deposit 回执 | hex `0x…` | Task 1/3 |
| 11 | `_op_operator_fee_scalar` | `Receipt.OperatorFeeScalar` | 非 deposit ∧ scalar/constant 非零 | hex `0x…` | Task 1/3 |
| 12 | `_op_operator_fee_constant` | `Receipt.OperatorFeeConstant` | 非 deposit ∧ scalar/constant 非零 | hex `0x…` | Task 1/3 |
| 13 | `_op_da_footprint_gas_scalar` | `Receipt.DAFootprintGasScalar` | 非 deposit ∧ Jovian | hex `0x…` | Task 1/3 |

`FeeScalar`（op-geth `Receipt.FeeScalar`, `*big.Float`）在 isthmus/jovian 下恒 absent（post-Ecotone 已废弃），
无 FISCO meta 对应，不列入 gate。

> 注：`_op_operator_fee`（#4）与 `_op_da_footprint`（#5）虽为 FISCO 侧派生/可派生字段，
> **两者都进值比对，不豁免**——见 §4。

---

## 2. 探针方法

- 探针 4 case（`--write-cases` 生成 `.in.json` 后逐个跑）：
  `isthmus_transfer_basic`（普通）、`jovian_da_mix`（Jovian DA）、
  `isthmus_deposit_only`（纯 deposit）、`isthmus_fee_env_observer`（唯一 operator scalar 非零的 case）。
- 管线：genesis 构造 → tx 签名 → `GenerateChainWithGenesis` → `receiptsAll[0]`，
  与 `processBlockVector` 前半段逐行同源（探针为自含一次性工具，不重构 `processBlockVector`）。
- 数值字段统一 hex 打印；`FeeScalar` 用 `*big.Float.String()` 十进制。

## 3. 发射矩阵（探针实测）

### 3.1 deposit 回执（op-geth `receipt_opstack.go:36-38`：最后一笔 tx 是 deposit 则整体早退）

| 字段 | deposit_only | deposit 出现在其他 case |
|---|---|---|
| `DepositNonce` | `0x0` | `0x0` |
| `DepositReceiptVersion` | `0x1` | `0x1` |
| `L1GasPrice`/`L1BlobBaseFee`/`L1GasUsed`/`L1Fee`/`FeeScalar`/`L1BaseFeeScalar`/`L1BlobBaseFeeScalar`/`OperatorFeeScalar`/`OperatorFeeConstant`/`DAFootprintGasScalar`/`BlobGasUsed` | 全 absent | 全 absent |

### 3.2 非 deposit 回执（isthmus，`fee_env_observer` 实测）

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

### 3.3 非 deposit 回执（jovian，`da_mix` 实测，DA scalar=400）

| 字段 | tx1 | tx2 | tx3 |
|---|---|---|---|
| `L1GasUsed` | `0x640` | `0x1308` | `0x438d` |
| `L1Fee` | `0xf4eb688f4` | `0x2e9ee82768` | `0xa572c76232` |
| `DAFootprintGasScalar` | `0x190` | `0x190` | `0x190` |
| `BlobGasUsed` | `0x9c40`（40,000） | `0x1db00`（121,600） | `0x69780`（432,000） |

`BlobGasUsed` 与向量 `_op_da_footprint` 逐笔一致（`0x9c40`/`0x1db00`/`0x69780`）——Jovian 载体确认。
`OperatorFeeScalar`/`OperatorFeeConstant` 在此 case absent（scalar=0）。

---

## 4. 关键裁决

### 4.1 `L1GasUsed` 裁决：恒发射（每笔非 deposit 回执）

op-geth `deriveOPStackFields`（`receipt_opstack.go:40`）对每笔非 deposit 回执赋值
`rs[i].L1Fee, rs[i].L1GasUsed = gasParams.costFunc(rcd)`——**非 deposit 回执的 `L1GasUsed` 恒非 nil**。
4 case 探针无一例外。→ Task 1 **保留**该字段，Task 3 **保留比对**；
FISCO 侧 `_op_l1_gas_used` 存在性断言**永不删除**（Global Constraints）。

### 4.2 `_op_operator_fee`：FISCO 派生字段（op-geth 无 `OperatorFee` 字段）

op-geth 回执不发射聚合 operator fee；`buildExpectedReceipts`（`main.go:1570-1623`）用
`operatorFee()` 公式（scalar/constant × gasUsed，Isthmus `/1e6` vs Jovian `×100`）手算，
并与 op-geth `NewOperatorCostFunc` 交叉校验。**值比对照常做**（不豁免）。

### 4.3 `_op_da_footprint`：op-geth 可派生字段（载体 `BlobGasUsed`）

Jovian 下 `deriveOPStackFields` 令 `BlobGasUsed = daFootprintGasScalar × EstimatedDASize`。
`_op_da_footprint` 即 `r.BlobGasUsed` 的 hex。**值比对照常做**（不豁免）。

### 4.4 Operator scalar/constant：仅非零发射

`receipt_opstack.go:44`：`operatorFeeScalar != 0 || operatorFeeConstant != 0` 才写回执。
`transfer_basic`/`da_mix`（scalar=0）absent；`fee_env_observer`（5000/7777）present。
生成器 `buildExpectedReceipts` 以 `emitOpFee := opScalar != 0 || opConstant != 0` 同源镜像。

---

## 5. 全局约束（Global Constraints）

1. `_op_l1_gas_used` 的 FISCO 侧存在性断言永不删除（§4.1）。
2. 探针 4 case 覆盖普通/Jovian/deposit/operator-fee 四分支；任何一支缺失即 fieldmap 失效。
3. op-geth 侧字段以 PIN `e8800cff` 为准；字段名冲突时 `grep -nE "BlobGasUsed|OperatorFeeScalar|L1GasUsed" <op-geth>/core/types/receipt.go` 核对。
4. `types.Receipt` 无 `DAFootprint` 字段——DA footprint 的载体是 `BlobGasUsed`，禁直接引用 `DAFootprint`。

---

## 6. 探针原始输出（4 case 全量）

### 6.1 isthmus_transfer_basic（deposit + 1 transfer）

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

### 6.2 jovian_da_mix（deposit + 3 transfers）

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

### 6.3 isthmus_deposit_only（纯 deposit）

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

### 6.4 isthmus_fee_env_observer（deposit + 1 transfer，operator scalar 非零）

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
