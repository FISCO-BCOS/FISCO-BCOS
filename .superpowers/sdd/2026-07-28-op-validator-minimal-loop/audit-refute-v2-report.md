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
