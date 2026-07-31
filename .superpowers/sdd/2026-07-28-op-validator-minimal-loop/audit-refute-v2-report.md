# 分歧审计对抗性复核 · V2 报告(只读)

- 复核对象:`state-divergence-audit-report.md` 表 1 的 #3-#8 + 表 2 的 #5-#8
- 工作目录:`/Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/ledger-bridge`,分支 `feat-op-validator-loop`,HEAD `7b7e0afb3`
- 基准:op-geth `e8800cffe53d459cde8a07c8e8f1de9d86e79e07`(本机核对 `git log -1` 一致)
- 方式:**纯只读**。未构建、未跑测试、未改任何源码/测试文件。需构建才能证实的标 `[需验证]`。
- 立场:证伪优先。每条走 A(我方代码)→ B(op-geth)→ C(可达性)→ D(触发构造)→ E(裁决)。

---

## 【复核 表1-#3】空块拒绝 / 首笔必须是 attrs deposit / deposit 不得后置(分歧 4-1、4-2、4-3)

### A. 我方代码

`bcos-evm/bcos-evm/opstack/OpBlockExecute.cpp` 逐行核对(**行号全部命中**):

- `:15-21` `isL1AttributesTx`:`dep.to.has_value() && *dep.to == OP_L1_BLOCK && dep.from == OP_DEPOSITOR`;
  `:17-19` 注释自陈 "stricter-than-spec (spec §6 decision point 2, user ruling) ... op-geth EL does not
  perform this validation (responsibility pushed down to the CL layer)"。
- `:36-37` 空块 `throw std::runtime_error("op block: missing L1 attributes deposit (empty block)")`。
- `:38-40` 首笔非 attrs deposit `throw`。
- `:54-55` `if (seenNonDeposit) throw ... "deposit after non-deposit tx"`。

审计读对了。**没有**任何上游检查提前挡住这三种输入(`validateOpNewPayloadRequest` 不看交易内容,
只做字段级静态校验;`decodeOneRawTx` 只管信封类型)。

### B. op-geth(e8800cffe)

审计称"op-geth EL 对 deposit 的顺序/位置/数量没有任何校验"。**这句话在 pre-Jovian 上成立,
在 Jovian 上不成立**——这是审计的实质性漏看:

`core/block_validator.go:119-134`(ValidateBody,在 `insertChain` → newPayload 路径上):

```go
if v.config.IsJovian(header.Time) {
    ...
    daFootprint, err := types.CalcDAFootprint(block.Transactions())
    if err != nil { return fmt.Errorf("failed to calculate DA footprint: %w", err) }
```

`core/types/rollup_cost.go:563-566`:

```go
func CalcDAFootprint(txs []*Transaction) (uint64, error) {
    if len(txs) == 0 || !txs[0].IsDepositTx() {
        return 0, errors.New("missing deposit transaction")
    }
```

再往下 `:569-577` 与 `ExtractDAFootprintGasScalar`(`:547-558`)要求 `txs[0].Data()` 要么恰好
`IsthmusL1AttributesLen = 176`(激活块分支),要么 `>= JovianL1AttributesLen = 178` 且
`data[0:4] == {0x3d,0xb6,0xbe,0x2b}`。

即 **op-geth 在每一个 Jovian 块上都隐含要求:块非空、首笔是 deposit、且首笔 calldata 是
176B(激活块)或 ≥178B 且带 Jovian 选择器**。这三条把 4-1 与 4-2 的绝大部分触发面在 Jovian 上堵死了。

pre-Jovian(我方 fork schedule 下即 Isthmus):`grep -rn "IsDepositTx\|DepositTxType"
core/block_validator.go consensus/beacon/consensus.go core/state_processor.go` 只命中
`state_processor.go:174`、`:217`(Regolith 回执字段),与顺序/位置无关。审计这半边读对了。

### C. 可达性 / D. 触发构造(逐条重算)

| 输入 | 我方 | op-geth Isthmus | op-geth Jovian |
|---|---|---|---|
| `transactions = []` | INVALID(:37) | **VALID** | **INVALID**(missing deposit transaction) |
| 首笔是 0x02 普通交易 | INVALID(:40) | **VALID** | **INVALID**(同上) |
| 首笔是 deposit,`from` 非 DEPOSITOR / `to` 非 L1Block,calldata 合规 | INVALID(:40) | **VALID** | **VALID**(op-geth 只看 calldata 形状,不看 from/to)→ **分歧仍在** |
| `[attrs, 0x02, deposit]` | INVALID(:55) | **VALID** | **VALID**(激活块分支 `txs[len-1].IsDepositTx()` 反而为真;非激活块分支根本不看顺序)→ **分歧仍在** |

D 的具体输入:上表第 3 行取任意一条金值向量的 attributes deposit,把 `from` 改成
`0x…0002`(非 DEPOSITOR),重算 sourceHash/txRoot/blockHash 使 payload 自洽 —— 我方 :40 抛,
op-geth 正常执行。第 4 行取 `jovian_*` 向量,在末尾追加一条 0x7E deposit。两者都零成本。

### E. 裁决

- **4-3(deposit 后置)**:**CONFIRMED**(两个分叉上都成立)。
- **4-2(首笔约束)**:**降级为"仅 from/to 语义面成立"**。审计列的触发输入 (a)(首笔是普通 0x02)
  在 Jovian 上被 op-geth 自己拒绝,不构成分歧;(b)(c)(from/to 不匹配)在两个分叉上都成立。
- **4-1(空块)**:**降级为"仅 pre-Jovian 成立"**。Jovian 上 op-geth 同样拒空块,无分歧。

### 附:派单要求的**定性判断**

**审计把 #3 放进"分歧表"是恰当的,但把它与 #1 并列排序是误导的**,理由三条:

1. **性质不同**:#1(只认三种交易类型)是**未声明的实现裁剪**——`OpSchedulerImpl.h:246-250` 的注释
   只说"matching the t8n corpus",没有任何"本期只支持三型"的裁定;而 #3 的三条规则在
   `OpBlockExecute.cpp:17-19` 有逐字的"stricter-than-spec + op-geth 不做此校验"自陈,是
   **已声明的刻意偏离**。
2. **但"已声明"的声明**载体只有源码注释,**不在本分支的 spec 里**:注释引用的 "spec §6 decision
   point 1/2" 指向的是 bcos-evm-ref 时代的旧 spec,`docs/superpowers/specs/2026-07-28-op-validator-
   minimal-loop-design.md` 的 §6.4 欠账台账(a-t 共 20 条)**一条都没提这三条更严规则**。
   `git log -- OpBlockExecute.cpp` 只有两个 commit(`d60a773c3` 整层移植、`87fe0bf4f` 改命名空间),
   本分支从未复核过这个裁定。**建议:补进 §6.4 或另立"刻意更严"一节**——今天读 spec 的人看不出
   本实现会拒绝合法块。
3. **批次划分建议**:#3 不应与 #1/#2 同批。#1/#2 是"日常流量即触发的未声明缺陷",#3 是
   "已裁定的更严行为 + 一个文档缺口"。真要动 #3 的代码,前提是**重新裁定**(把"CL 被攻陷时的
   防御价值"与"验证者不得拒绝合法块"称一次重),不是修 bug。

---

## 【复核 表1-#4】`extraData` 的 OP 形状零校验 + 与 C4 复合(分歧 2-1)

### A. 我方代码

- `engine/bcos-engine/EngineServiceImpl.cpp:274-283`:唯一的 extraData 校验是
  `constexpr std::size_t c_maxExtraDataSize = 32; if (payload.extraData.size() > c_maxExtraDataSize)`。
  行号命中。
- `:309-311` 注释:"the payload verbatim (extraData included -- 原样, never re-derived or
  shape-checked, see §6.4's extraData 欠账)";`:331` `.extraData = payload.extraData` 直接进重建头。
- `bcos-evm/bcos-evm/engine/OpSchedulerImpl.h:242` `blk.extra_data = ...` 进 `BlockInfo`。

**审计漏说的一环(我方侧)**:`extra_data` 进了 `BlockInfo` 之后**没有任何消费者**。
`grep -rn "extra_data" bcos-evm/bcos-evm/{eth,opstack,engine,adapter}` 只命中
`eth/state/block.hpp:46` 的字段声明与上面那一行赋值。extraData 在我方**只参与 blockHash 重建**,
不参与任何执行语义。

### B. op-geth(e8800cffe)

- `consensus/beacon/consensus.go:240-245`:`if chain.Config().IsOptimism() &&
  !chain.Config().IsOptimismGenesisBlock(header.Number) { eip1559.ValidateOptimismExtraData(...) }`
  —— 在 verifyHeader 上,**用的是当前块自己的 `header.Time` 与 `header.Extra`**。审计读对了。
- `consensus/misc/eip1559/eip1559_optimism.go:22-31` 派发;`:195-206` `ValidateJovianExtraData`
  (恰好 17 字节 / `extra[0] == 0x01` / 8 字节部分两个非零);`:147-155` `ValidateHoloceneExtraData`
  (恰好 9 字节 / `extra[0] == 0x00` / 两个非零);`:133-141` `validateHoloceneExtraDataPart`。
  审计引的行号**逐个命中**。
- `eip1559.go:64,78` `CalcBaseFee` 里 `DecodeOptimismExtraData(config, parent.Time, parent.Extra)`
  —— 审计引的 `:76-79` 命中(实际 `:78`)。

### C. 可达性

当前可构造,且**比审计说的更宽**:审计只举了"17 字节但版本字节写错"。实际上
`ValidateOptimismExtraData` 在 Holocene 之后**要求 extraData 必须恰好 9/17 字节**,
所以 **`extraData = {}`(空)也是 op-geth 拒绝、我方接受**。空 extraData 是任何手工构造
payload 的默认值(`OpSchedulerImplTest.cpp:421` 就是 `.extraData = {}`)。金值向量本身是
合规的(`isthmus_*.golden.json` 用 9B `0x000000003200000006`,`jovian_*` 用 17B
`0x0100000032000000060000000000000000`),所以这条不会被现有测试暴露。

### D. 触发构造

任取一条 `isthmus_*` 向量,把 `extraData` 从 `0x000000003200000006` 改成 `0x`(或 8 字节、
或 `0x010000003200000006` 版本字节写错),按新 extraData 重算 `blockHash` 使 payload 自洽。
我方全线通过(extraData 只影响 blockHash,而 blockHash 是按同一份 extraData 重建的);
op-geth `verifyHeader` 直接 `invalid optimism extraData: holocene extraData should be 9 bytes, got 0`。

### E. 裁决

- **主论断(extraData OP 形状零校验 → 我方接受 op-geth 拒绝的块)**:**CONFIRMED**,
  且**加强**:触发面包含"空 extraData"这一最自然的默认值,不止畸形值。
- **复合论断("它是下一块 baseFee 的参数源,与 C4 复合后 baseFee 完全失守")**:**REFUTED**。
  证据:
  1. **我方从不计算 baseFee**。`grep -rn "calc_base_fee" --include='*.cpp' --include='*.h'
     --include='*.hpp' .` 全仓只有两处:`bcos-evm/bcos-evm/eth/state/block.cpp:13` 的定义与
     `block.hpp:69` 的声明,**零调用点**(含测试)。`baseFeePerGas` 从 payload 原样进
     `OpBlockEnv`(`EngineServiceImpl.cpp:334`)→ `BlockInfo.base_fee`
     (`OpSchedulerImpl.h:238`)。既然从不用 `DecodeOptimismExtraData` 那套参数,
     extraData 里的 denominator/elasticity/minBaseFee 对我方**全部是惰性字节**。
  2. 因此"denominator=0 会污染后续所有块 baseFee"在我方**不可能发生**——那是 op-geth 内部
     被 `ValidateOptimismExtraData` 挡在门外的除零风险,不是我方的攻击面。
  3. 两个缺口是**独立且效果重叠**的,不是相乘:C4 单独就已经让 `baseFeePerGas` 完全由 payload
     设定(op-geth 会 `header.BaseFee.Cmp(expectedBaseFee) != 0` 拒绝,我方不算)。
     extraData 形状缺失**不增加任何攻击者能力**,它只增加一条独立的"我方接受 op-geth 拒绝"面。
  **改法建议**:表 1 #4 的"后果"列应改为"放行畸形/空 extraData → 与网络分叉",删掉
  "baseFee 完全失守"的复合表述(该表述归 C4 独有)。
- **附:定性**。#4 与 #3 一样属于**已声明偏离**:spec §6.4 正文逐字列了 "extraData 形状校验"
  与 "Holocene EIP-1559 baseFee 父子一致性校验(裁定 A7)",测试
  `EngineOpBranchTest.cpp:405-407` 也有 "The engine does not shape-check extraData this cycle
  (§6.4 欠账)" 的注释。表 1 把它与 #1/#2 同列会让人误以为是新发现。

---

## 【复核 表1-#5】Jovian `daFootprint > gasLimit` 块级上限缺失(分歧 2-2)

### A. 我方代码

- `bcos-evm/bcos-evm/opstack/OpBlockSeal.cpp:72-81`:`if (cfg.has_da_footprint)` 时
  `footprint += txr->meta.da_footprint.value_or(0)`(只对 `OpTxReceipt` 变体累加,deposit 是
  另一个变体,与 op-geth "跳过 deposit" 等价),`seal.blobGasUsed = footprint`。**只求和,无上限**。
  审计引的 `:74-81` 命中。
- `engine/bcos-engine/EngineServiceImpl.h:1080-1086`:
  `commitments.blobGasUsed.has_value() && u256(*commitments.blobGasUsed) != *payload.blobGasUsed`
  → `mismatchedField = "blobGasUsed"`。**只比相等,不与 gasLimit 比**。审计引的 `:1078-1085` 命中(±2)。
- 全仓 `grep -rn "da_footprint\|daFootprint"` 非测试命中 12 处,**没有任何一处与 `gas_limit` 比较**。
- `OpBlockSeal.h:31-38` 的注释**明确自陈**:"op-geth's validation-side 'reject footprint > gasLimit'
  (block_validator.go:131) is a validation responsibility; this function only produces the value."
  —— 即"这里不做"已被写下,但**接手方(engine 比对面)并没有做**,声明落空。

### B. op-geth(e8800cffe)

`core/block_validator.go:119-134` 原文核对:`daFootprint > block.GasLimit()` →
`"DA footprint %d exceeds block gas limit %d"`。在 `ValidateBody` 内,newPayload 路径上。
审计引对了(具体行:上限检查在 `:131-133`)。

### C. 可达性

需要 (i) Jovian 时间戳,(ii) 状态里 L1Block 槽 8 的 `da_footprint_gas_scalar` 非零,
(iii) 足量 calldata。(ii) 由 attributes deposit 执行写入,不是 payload 直填 —— 所以构造者需要
控制 CL/sequencer 或直接构造创世状态。属"恶意/异常 CL"面,不是普通用户面。
**审计的"触发成本:一个 calldata 密集的 Jovian 块"略微低估**了 scalar 需从状态取这一约束,
但结论方向不变。

### D. 触发构造

`da_footprint = estimatedDaSizeFromFlz(flzLen) * scalar`(`OpReceiptMeta.cpp:27-31`)。
取 `scalar = 1000`、`gasLimit = 30_000_000`,则只需 Σ estimatedDASize > 30_000 字节,
约 30 笔 1KB calldata 的交易(执行 gas 远低于 30M,不撞 gas 池)。payload 的 `blobGasUsed`
填成同一个和即通过我方比对。我方 VALID,op-geth `INVALID: DA footprint 30000000+ exceeds
block gas limit 30000000`。

### E. 裁决

**CONFIRMED**(尝试证伪的三条路都失败:①非"另一层已堵" —— 全仓无 gasLimit 比较;
②非"路径不可达" —— Jovian 配置在测试与生产 fork schedule 上都可达;
③非"方向搞反" —— 我方接受 op-geth 拒绝,方向与审计一致)。
**补充一条审计没说的**:这条与 §6.4 的其他"我方更松"欠账**不同**,它有一条**已写下但没兑现**
的责任转移注释(`OpBlockSeal.h:36-37` 把责任推给 "validation" 层,而 validation 层没接),
属于比"未声明缺口"更容易被误读为"已处理"的类型 —— 建议单独在 §6.4 记一条。

---

## 【复核 表1-#6】Jovian attributes 的长度/选择器不校验(分歧 5-1(a))

### A. 我方代码

- `bcos-evm/bcos-evm/opstack/OpFeeParams.cpp:33` `.da_footprint_gas_scalar =
  static_cast<uint16_t>(readBE(slot8, 18, 2))` —— **行号精确命中**。`OpFeeParams.h:19-26` 的
  布局注释:slot 8 bytes[18,20) = daFootprintGasScalar、[20,24) = operatorFeeScalar、
  [24,32) = operatorFeeConstant。
- `loadOpFeeParams`(`:36-46`)读 L1Block 的槽 1/3/7/8;`OpBlockExecute.cpp:66-73` 在第一笔
  非 deposit 交易时惰性调一次。审计引的 `:43-45` 与 `:66-73` 命中。
- **证伪尝试**:`grep -rn "3db6be2b|0x3d, 0xb6|selector" bcos-evm/bcos-evm engine/bcos-engine`
  → **零命中**。我方从头到尾**没有任何一处读过 attributes 交易的 calldata**,遑论校验长度/选择器。
  没有上游检查堵住这条。

### B. op-geth(e8800cffe)

`core/types/rollup_cost.go`:
- `:46-47` `IsthmusL1AttributesLen = 176`、`JovianL1AttributesLen = 178`;`:64-65`
  `JovianL1AttributesSelector = []byte{0x3d, 0xb6, 0xbe, 0x2b}`。
- `:547-558` `ExtractDAFootprintGasScalar`:`len(data) < 178` → 报错;
  `!bytes.Equal(data[0:4], JovianL1AttributesSelector)` → 报错;取 `data[176:178]`。
- `:563-590` `CalcDAFootprint` 由 `core/block_validator.go:125` 调用,**返错即整块无效**。

审计引对了。注意一处细节修正:长度判据是 `<`(不是 `!=`),**大于 178 字节的 attributes
calldata op-geth 是接受的**,只要选择器对。

### C. 可达性 / D. 触发构造

审计的触发输入 (a) 属实:一个 Jovian 块,attributes calldata 长 178、选择器写
Isthmus 的 `0x098999be`。op-geth `ValidateBody` → `failed to calculate DA footprint:
L1 attributes transaction data does not have Jovian selector` → **整块 INVALID**。
我方对 calldata 一字节都不看,照读槽 8 算 footprint,payload 的 `blobGasUsed`
填成同一个和即通过 `EngineServiceImpl.h:1080-1086` 的比对 → **VALID**。

一个审计没说、但**加强**该结论的事实:选择器写错时,真实 L1Block 预部署会因无匹配函数而
revert,即 attributes deposit 变成失败回执(Regolith 语义,不是块错误),**槽 8 保持上一块的
值**——所以我方不但不拒,还会拿一个陈旧的 scalar 继续算。

### E. 裁决

**CONFIRMED**(触发面 (a))。补两条:
1. **长度判据修正**:op-geth 是 `len < 178` 而非 `!= 178`,审计写的"长度 178"过窄。
2. **(b) 分支(槽 8 [18:20] ↔ calldata[176:178] 是否恒等)仍是 `[需验证]`,且我给出了
   为什么现有语料给不出证据**:17 条 `jovian_*` 向量里,L1Block(`0x42..15`)的 `code`
   **全部是 `"0x"`(无字节码)**——包括名字叫 `jovian_system_contracts_real` 的那条(它的
   "real" 指 EIP-4788 的 `0x000f3df6...` 与 EIP-2935 的 `0x0000f908...`,与 L1Block 无关)。
   **语料里从来没有一条向量真正执行过 L1Block 的 setter**;`jovian_da_mix` 是把
   `slot8 = 0x…0190…`(bytes[18:20] = 400)与 attributes calldata `[176:178] = 0x0190`
   **由夹具作者手工写成一致的**。所以"两者恒等"在本仓**零证据**,只有"夹具作者相信它们相等"。
   最小验证步骤(仍是只读做不了的):取 Jovian 版 `L1Block.sol` 的真实 runtime 字节码,
   放进一条向量的 `pre`,用一条真 178B attributes calldata 跑一遍,断言执行后
   `slot8[18:20] == calldata[176:178]`。

---

## 【复核 表1-#7】Jovian 激活块"不得含用户交易"约束缺失(分歧 5-2)

### A. 我方代码

无对应检查(`grep` 全仓无激活块特判)。`OpBlockSeal.cpp:73-81` 无条件求和。

审计的推理"激活块槽 8 尚未写入 → scalar = 0 → footprint 恒 0"**需要一个它没给的前提**,
我补上:激活块的 attributes 用的是 176B Isthmus setter,而 slot 8 的 bytes[18,20) 这两个
字节在 Isthmus 版 L1Block 的存储布局里**不存在任何写者**(Isthmus 只打包
operatorFeeConstant[24,32) + operatorFeeScalar[20,24)),所以在规范链上激活块的
`slot8[18:20]` **必然是 0**,不是"多半为 0"。结论方向不变,但推理链是闭合的。

### B. op-geth

`core/types/rollup_cost.go:569-577`:

```go
data := txs[0].Data()
if len(data) == IsthmusL1AttributesLen {
    if !txs[len(txs)-1].IsDepositTx() {
        return 0, errors.New("unexpected non-deposit transactions in Jovian activation block")
    }
    return 0, nil
}
```

审计引对了(`:570-576`)。

### C. 可达性 / D. 触发构造

Jovian 激活块 = 时间戳恰好跨过 `jovianTime` 的第一个块,attributes 仍是 176B。
往块尾加一笔 0x02 用户交易:op-geth INVALID;我方 footprint = 0(scalar = 0)= payload 的 0
→ VALID。可构造,但只在跨激活边界的**那一个块**上可达,一条链上一生一次。

**证伪尝试(失败)**:我检查了三条可能堵住它的路——①`validateOpNewPayloadRequest` 的
`!jovianActive && *payload.blobGasUsed != 0`(`EngineServiceImpl.cpp:234-241`)只管
pre-Jovian,激活块 `jovianActive == true`,不拦;②`OpBlockExecute.cpp` 的三条准入规则不涉及
激活块;③seal 比对面只比相等。都不堵。

### E. 裁决

**CONFIRMED**,危害等级维持"仅激活块可达"。补一条**审计漏掉的反向观察**:op-geth 这段的
注释 "sufficient to check last transaction because deposits precede non-deposit txs" 说明
**op-geth 自己假定了"deposit 必须前置"**——这正是我方分歧 4-3 强制执行、而 op-geth EL
不强制的那条规则。即在激活块这一个点上,op-geth 的正确性依赖一条它不校验的假设,
而我方校验了它却在别处更松。这对"批次划分"有意义:4-3 与 5-2 是同一处协议假设的两面,
应放在同一批一起裁定,而不是一个进表 1 #3、一个进表 1 #7。

---

## 【复核 表1-#8】首块豁免 timestamp 单调性 + `configAt` 吞 pre-Isthmus(分歧 2-3 + 9-1)

### A. 我方代码(逐环节)

1. `engine/bcos-engine/EngineServiceImpl.h:861-867` 注释:"Missing parent header => SKIP,
   deliberately (this is the genesis/first-block case, and the behaviour is part of the contract,
   not an oversight)";`:868-891` timestamp 比较整段包在
   `if (auto parentHeaderEntry = ...; parentHeaderEntry.has_value())` 里。**审计读对了**。
2. `bcos-evm/bcos-evm/opstack/OpForkSchedule.cpp:102-110` `configAt`:
   `if (timestamp >= thresholds.jovianTime) return jovianConfig(); return isthmusConfig();`
   —— 低于 `isthmusTime` 也落 Isthmus。**审计读对了**(行号精确)。
3. `-38005` 闸(`EngineServiceImpl.h:692-701`):`isthmusActive != (version == 4)` 即抛。
   注意这是**双条件**:pre-Isthmus 时间戳走 V4 被拒,Isthmus+ 时间戳走非 V4 也被拒。
   所以 pre-Isthmus 时间戳的**唯一**入口是 V1/V2/V3。

### B. op-geth

- `eth/catalyst/api.go:888-891` 父块缺失 → `delayPayloadImport`(SYNCING);
  `consensus/beacon/consensus.go:253-256` `header.Time <= parent.Time` 无条件。审计引对了。
- `beacon/engine/types.go:319-347`:pre-Isthmus 时间戳 + V3(`requests == nil`)→
  `requestsHash = nil`;Isthmus 时间戳 → 强制要求 `WithdrawalsRoot != nil`、
  `requests` 必须是空数组。

### C. 可达性 —— **这里推翻审计的一半**

审计称残余缺口是"引导后的第一个块无父头 → timestamp 任取 → 走 V3 通道带一个 pre-Isthmus
时间戳进来 → 整块用错规则集 ⇒ stateRoot/receiptsRoot/gasUsed 全线分歧"。逐环节验证后,
**前半段(能进来)成立,后半段(状态分歧)不成立**:

1. **第二道闸**:`validateOpNewPayloadRequest`(`EngineServiceImpl.cpp:200-241`)**无条件**
   要求 `withdrawalsRoot` 在场(`:219-225`)、`withdrawals` 在场且空、`excessBlobGas == 0`、
   `blobGasUsed` 在场、`parentBeaconBlockRoot` 在场。一个**真实的** pre-Isthmus payload
   没有 `withdrawalsRoot`(那是 Isthmus 才加的 payload 扩展字段)→ 直接 INVALID。
   能进来的只有"Isthmus 形状 + pre-Isthmus 时间戳"的**人造** payload。
2. **第三道闸(审计完全没看)**:`rebuildOpEthHeader`(`EngineServiceImpl.cpp:304-341`)
   把 `.requestsHash = c_opEmptyRequestsHash` **无条件**钉死,`.withdrawalsRoot =
   payload.withdrawalsRoot.value()` 也无条件填;而 `bcos-codec/bcos-codec/rlp/
   EthBlockHeader.h:62,71` 的这两个字段是**非 optional 的 `h256`,永远参与 RLP 编码**。
   于是我方重建出的永远是 21 字段的 Isthmus/Prague 形状头。
   **op-geth 对同一份 pre-Isthmus payload 建出的头 `RequestsHash` 是 nil** → 两侧
   `blockHash` **必然不同**。攻击者只能二选一:让 payload.blockHash 匹配我方(op-geth 立刻
   `INVALID_BLOCK_HASH`),或匹配 op-geth(我方 `EngineServiceImpl.h:774` 的
   `ethHeader.hash() != payload.blockHash` 一票否决)。
   **⇒ 不存在任何一个 pre-Isthmus 时间戳的块能被两侧同时接受并产出不同状态。**
3. 2-3 的**另一半**(同一分叉内的首块 timestamp 任取)不受上面影响,仍然成立:引导后的第一个
   块可以带任意 timestamp(哪怕小于起点块),我方接受、op-geth 拒绝。但它**一个节点一生只有
   一次**(此后每个块都存了头),且是引导协议的可修项。

### D. 触发构造

- 可构造的:一条 Isthmus 形状、时间戳任取的首块 payload(现有任一 `isthmus_*` 向量改
  `timestamp` 再 reseal blockHash 即可),我方接受。**这是 2-3 的真实触发**。
- 不可构造的:一个能让两侧都产出结果、且结果不同的 pre-Isthmus 块(见 C.2/C.3)。

### E. 裁决

**降级**:
- **2-3(首块 timestamp 豁免)**:**CONFIRMED**,但危害面从"整块用错规则集"降到
  "引导后的第一个块可携带非单调 timestamp,我方接受 op-geth 拒绝",**且每个节点仅一次**。
- **9-1(`configAt` 吞 pre-Isthmus)**:**降级为"死路径上的正确性缺陷"**。它在代码上成立
  (`OpForkSchedule.cpp:107-109` 确实把 `timestamp < isthmusTime` 解析成 Isthmus),
  但**没有任何输入能让它产生与 op-geth 的状态分歧**——被 `-38005` 闸(V4)、
  `withdrawalsRoot` 必填(真实 pre-Isthmus payload)、`requestsHash` 无条件钉死
  (人造 payload 的 blockHash)三道独立地堵死。
- **两者的"合成"(表 1 #8 的写法)**:**REFUTED**。"引导后的第一个块可携带任意(含
  pre-Isthmus)时间戳,整块用错规则集"这个后果链在第三环断掉。表 1 #8 应拆成两条:
  2-3 留在表 1(降级),9-1 移出"当前可构造的分歧",改记为"防御性正确性欠账
  (今天不可达,但 `configAt` 的 fall-through 是一个等着被别处放宽后引爆的地雷)"。
