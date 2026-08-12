# OP-ized 块构建(sequencer 能力)设计

> **日期**: 2026-08-12
> **前置**: 真实节点 OP 块执行 + eth RPC 可查已交付(commit `54dfd06`);1930/1930 测试绿。
> **B3 实测结论**: OP 模式下 FCU+attributes → `UnsupportedOpPayloadAttributes`(-38003),getPayload → `OpPayloadBuildingUnsupported`。当前 OP 节点只能**接收**外部组好的块(newPayload 注入),不能**自己打包**。
> **目标**: 让 OP 节点具备 sequencer 能力——从 mempool 封签普通交易 → 构造 OP 块 → 执行 → 提交,对齐 op-geth sequencer 语义。这是"普通 web3 交易经 eth_sendRawTransaction 提交后节点自行打包出块"的前提。

---

## 1. 现状与差距

| 能力 | 现状 | 差距 |
|---|---|---|
| newPayload 注入执行 | ✅ 已通(commit 54dfd06) | — |
| FCU+attributes 构建 | ❌ `-38003`(EngineServiceImpl.h:383-398) | B1 解除 |
| getPayload | ❌ `OpPayloadBuildingUnsupported`(L543-555) | B1 解除 |
| mempool seal | ✅ 已实现(非 OP 分支 L414-417) | 产出 FISCO tx,需转 OP envelope |
| OP 执行 | ✅ `executeOpBlock`(返回 seal/stateRoot/txRoot/gasUsed) | 组块复用 |

## 2. 核心事实(设计依据)

1. **封签交易 → OP envelope 字节**: mempool `seal()` 产出 FISCO `Transaction::Ptr`;其 tars `extraTransactionBytes` 普通 tx 存的是 `encodeForSign()`(无签名),deposit 存完整 `encode()`。→ **需新增 tars 字段保存 eth_sendRawTransaction 的原始 EIP-2718 envelope**,封签时逐字节取用(用户裁定:改 tars 保原始字节)。
2. **OP 组块所需的全部 commitments 已在 `executeOpBlock` 输出**: `OpExecuteBlockResult{seal, stateRoot, gasUsed, txRoot}`,seal 含 receiptsRoot/logsBloom/withdrawalsRoot/requestsHash/blobGasUsed。组块只是"回填 header"。
3. **baseFee / extraData 是计算值**(非 attributes 提供): `calcOpBaseFee`(EngineServiceImpl.h:132,镜像 op-geth CalcBaseFee 含 Holocene extraData)。
4. **复用 seam**: `computeTxRoot`/`commitmentsOf`/`announcedCommitmentsOf`/`mismatchedFieldOf`/`executeOpBlock` 均已在 SchedulerType 上暴露,engine 可达。
5. **tars 向后兼容**: 新增 optional 字段(tag 16),旧节点忽略未知 tag。

## 3. 设计:buildOpPayload(engine 内 OP 专用构建)

**架构**: 在 `EngineServiceImpl` 的 `c_opMode` 分支新增 `buildOpPayload`(独立函数,不改现有 `buildPayload` 的 FISCO 逻辑)。

### 数据流(一次 build)

```
FCU(head, attributes)     getPayload(id)           newPayload(payload)
     │                        │                         │
     ▼                        ▼                         ▼
updateForkchoice ──▶ buildOpPayload(NEW) ──▶ 现有三阶段(execute→compare→commitBlock)
(解除 -38003)         封签→取raw→执行→组头→算hash    (比对天然一致,自己组自己提交)
```

**buildOpPayload 步骤**:
1. `m_memPool.get().seal(limit, view, ...)` → `sealedTxs`(FISCO `Transaction::Ptr`)
2. 每 tx 取新 tars 字段 `rawTransactionBytes` → `rawTransactions`(OP envelope 字节列表,逐字节 = eth_sendRawTransaction 收到的原始字节)
3. `view.newMutable()`;构造 OP 头骨架:`parentInfo`(父 = forkchoiceState.head)、`number`(head+1)、`timestamp`(attributes)、`coinbase`(attributes.suggestedFeeRecipient)、`prevRandao`(attributes)、`gasLimit`(ledger)
4. 调 `executeOpBlock(view, header骨架, rawTransactions)` → `OpExecuteBlockResult`
5. 用 result **回填 header**: stateRoot/receiptsRoot/txRoot/gasUsed/logsBloom/withdrawalsRoot/requestsHash/blobGasUsed;`baseFee` 用 `calcOpBaseFee`(父头);`extraData` 用 OP 格式(Holocene+ 9/17 字节)
6. `opHeaderHash(opHeaderConst())` 算 blockHash
7. 组装 `ExecutionPayload`(含 `rawTransactions`)→ 存 `PayloadEntry`(含 view,供 getPayload/newPayload)

**getPayload**: 解除 `OpPayloadBuildingUnsupported`,返回缓存的 OP payload(与 FISCO 分支同结构,`rawTransactions` 承载 envelope)。

**newPayload**: 现有三阶段不动。块是自己组的,execute→compare 天然一致;commitBlock 落库。

### 组件边界
- `buildOpPayload` = engine 内新函数(编排),复用 seam(computeTxRoot/commitmentsOf/executeOpBlock)
- tars 新字段 = `Transaction.tars` tag 16 + 写入点(EthEndpoint::sendRawTransaction 保原始 bytes → takeToTarsTransaction 填字段)
- SingleNodeConsensus **不改**(已用标准 FCU/getPayload/newPayload 闭环)

## 4. 错误处理

| 场景 | 处置 |
|---|---|
| mempool 空 | 与 FISCO 分支一致:返回空块或跳过(由 produceEmptyBlocks 决定) |
| 某 tx 无 `rawTransactionBytes`(如旧数据/非 web3 tx) | 跳过该 tx(与 opstackRegisterBlock 的"转换失败跳过"一致),或响亮失败——**设计选择:跳过**,块仍 VALID |
| executeOpBlock 抛 | 沿用现有分类:`OpConsensusError`→INVALID、`OpStorageError`→-32603 |
| commitments 回填后 newPayload 比对不一致 | 理论上不可能(同一函数);若发生→INVALID,表明内部不变量坏 |
| tars 解析旧数据(缺新字段) | optional 缺省,封签跳过该 tx |

## 5. 测试

**单元**: buildOpPayload 单测(封签 tx → OP 头 → 各字段正确;复用现有 OpNewPayloadRpcE2eTest 的存储 fixture)。
**集成(真实节点)**: 开 `enable_single_node_consensus` + sender alloc → `eth_sendRawTransaction` 提交签名普通转账 → 观察节点自动打包出块 → `eth_blockNumber`=1 / `eth_getTransactionReceipt` 可查。这是 B3 的逆——B3 确认拒绝,B1 后应成功。
**回归**: 1930 测试不退化(现有 newPayload 注入路径不动)。

## 6. 风险

1. **封签 tx 与 op-geth 组块结果一致性**: 组出的 blockHash/stateRoot 需与 op-geth 在同样交易下一致——用现有向量对账。
2. **mempool 封签语义**: OP 下封签的 nonce 处理需与 evmone 执行一致(非 OP 分支已处理,复用)。
3. **`opHeaderHash` 需要正确的 OP 头**: header 字段(尤其 extraData 格式、withdrawalsRoot 语义)必须与 op-geth 对齐,否则自组块 hash 错。
