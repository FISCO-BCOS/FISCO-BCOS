# 状态分歧差分审计报告 — FISCO opstack ↔ op-geth

- 被审对象:`/Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/ledger-bridge`,分支 `feat-op-validator-loop`,HEAD `7b7e0afb3`
- 基准:op-geth `e8800cffe53d459cde8a07c8e8f1de9d86e79e07`(`/Users/octopus/octo/code/blockchain-impl/op-geth`)
- 方式:**只读**审计。未构建、未跑测试。凡需构建才能证实的,标 `[需验证]` 并给最小验证步骤。
- 已知且已记账的分歧(本报告引用而不重证):C1 RLP 长度前缀前导零、C2 7702 授权 yParity 位宽、C3 pre-Isthmus V3 闸、C4 baseFeePerGas 双向缺陷、批 9 零值槽(已修)。

> 撰写策略:**逐层落盘、逐层提交**。本文件按九层顺序增长,层写完即 `git add -f` 提交。

---

## 第 1 层:交易解码(EIP-2718 信封 → 交易对象)

### 我方解码路径(唯一入口)

OP 模式下 payload 里的 `rawTxBytes` 只经由一条路径解码:
`bcos-evm/bcos-evm/engine/OpSchedulerImpl.h:660` `detail::decodeOneRawTx()`
→ `decodeDepositTx`(:517)/ `decodeEip1559Tx`(:557)/ `decodeSetCodeTx`(:606)。

底层原语来自 `bcos-codec/bcos-codec/rlp/RLPDecode.h`(`decodeHeader`:33),外面包了一层
"规范性"壳:`readCanonicalScalar`(OpSchedulerImpl.h:300)、`readFixedWidth`(:322)、
`decodeBoolField`(:371)、`expectExhausted`(:434)。

注意:`bcos-rpc/bcos-rpc/web3jsonrpc/model/Web3Transaction.cpp:386` `decodeTransaction()` 是
**另一条**(eth_sendRawTransaction)路径,OP 块执行不走它。本层结论只针对 OpSchedulerImpl.h。

### 【分歧 1-1】只认 3 种交易类型;legacy / 0x01 / 0x03 / 0x7D 一律判块无效 ★最高危

```
我方:    bcos-evm/bcos-evm/engine/OpSchedulerImpl.h:660-676 —— decodeOneRawTx 只 dispatch
         0x7E(deposit)/0x02(eip1559)/0x04(set_code);其余首字节一律
         `throw OpConsensusError("unsupported tx type byte ...")`。
         OpSchedulerImpl.h:812-815 在 executeOpBlock 的 step 1 无条件对每条 raw tx 调它,
         抛出即整块 INVALID。
op-geth: core/types/transaction.go:191-210 UnmarshalBinary —— `b[0] > 0x7f` 走 legacy
         (LegacyTx);core/types/transaction.go:212-234 decodeTyped —— 支持
         AccessListTxType(0x01,:218)、DynamicFeeTxType(0x02,:220)、BlobTxType(0x03,:222)、
         SetCodeTxType(0x04,:224)、PostExecTxType(0x7D,:226)、DepositTxType(0x7E,:228)。
         beacon/engine/types.go:252-262 DecodeTransactions 对 payload 逐条调 UnmarshalBinary。
触发输入:任何 payload,其 `transactions[i]` 首字节 ≥ 0xc0(即一条普通 legacy 交易的 RLP list),
         或首字节 == 0x01(EIP-2930 access-list 交易)。两者在 OP L2 上都是完全合法的日常交易。
         最小字节:一条已签名 legacy 转账,`0xf8 0x6c 0x80 0x85 ...`。
分歧结果:VALID(op-geth 正常执行,产出 stateRoot/receiptsRoot)vs INVALID(我方直接拒块)。
         方向是"我方拒绝 op-geth 接受的块"——对验证者节点等于投错票。
可达性:  当前可构造(只要 payload 里出现一条 legacy 或 0x01 交易)。
置信度:  已读两侧源码确认。
```

补充:0x03(blob)在 OP L2 上 op-geth 侧另有拒绝路径(evmone 侧 `validate_transaction`
`bcos-evm/bcos-evm/eth/state/state.cpp:426-444` 也有 blob 分支,但我方解码器根本到不了那里),
所以 0x03 的方向性结论需要单独确认 `[需验证]`;**0x7D(PostExecTxType)是本 op-geth commit
新引入的类型**(`core/types/post_exec_tx.go:10`),是否受分叉门控、在 Isthmus/Jovian 上是否可
出现在块里,本次未查清 `[需验证]`。但 legacy 与 0x01 两项无需任何补充验证即成立。

**最小验证步骤(若要实测)**:在 `bcos-evm/test/opstack/OpSchedulerImplTest.cpp` 里补一条用
`0xf8...` legacy 信封喂 `detail::decodeOneRawTx` 的用例,断言它抛
`OpConsensusError`;再用 op-geth `rlp.DecodeBytes` 对同一串确认解码成功。

### 【分歧 1-2】`decodeOneRawTx` 对空 payload 的语义与 op-geth 的 `errShortTypedTx` 边界不同

```
我方:    OpSchedulerImpl.h:661-663 —— 仅 `rawEntry.empty()` 抛错。长度恰为 1(只有一个类型字节,
         如单字节 `0x02`)时不在此处拒绝,而是落到 decodeEip1559Tx:561
         `bcos::bytesRef body(rawEntry.data() + 1, rawEntry.size() - 1)` 得到空 body,
         再由 enterList → decodeHeader(RLPDecode.h:35-39)返回 InputTooShort 而拒绝。
op-geth: core/types/transaction.go:213-215 —— `if len(b) <= 1 { return nil, errShortTypedTx }`。
触发输入:单字节 payload 项 `0x02`。
分歧结果:两侧都是 INVALID,只是错误来源不同。**不构成状态分歧**,列此仅为覆盖边界记录。
可达性:  当前可构造。
置信度:  已读两侧源码确认。
```

### 【分歧 1-3】deposit 信封的 `mint` 被解成"存在且可能为 0",丢失 nil 语义

```
我方:    OpSchedulerImpl.h:535 —— `dep.mint = decodeU256Scalar(listBody)`,即使线上是空串
         (nil)也得到 `optional` 已赋值的 0。注释(:529-534)承认这是刻意选择。
op-geth: core/types/deposit_tx.go:35-36 —— `Mint *big.Int \`rlp:"nil"\``,空串解成 nil 指针。
         执行侧 core/state_transition.go 对 `msg.Mint != nil` 才 AddBalance。
触发输入:一条 mint 字段为空串(0x80)的 deposit。
分歧结果:两侧最终都是"给 from 加 0",**账面无差**;但 receipt/回执与 tx 结构的再编码若走
         我方解码结果会把 nil 写成 0(在 RLP 里仍是 0x80,同形)。故不构成根分歧。
         真正的风险在于:我方 txRoot 建在**原始线上字节**上(OpSchedulerImpl.h:909
         `computeOpTxRoot(rawTxBytes)`),不经再编码,所以这条 nil/0 的信息丢失不会影响 txRoot。
可达性:  当前可构造,但无分歧后果。
置信度:  已读两侧源码确认。
```

### 【关键事实】txRoot 建在原始线上字节,不是重编码 —— 与 op-geth 不同,靠"解码器拒绝一切非规范编码"补齐

```
我方:    OpSchedulerImpl.h:896-909 与 `bcos-evm/bcos-evm/engine/OpEngineSeam.h` 的
         `computeOpTxRoot` —— trie 的 value 是 **raw wire bytes 原样**。
op-geth: beacon/engine/types.go:347 `TxHash: types.DeriveSha(types.Transactions(txs),
         trie.NewStackTrie(nil))` —— `Transactions.EncodeIndex` 从**解析后的结构体重新规范编码**。
后果:   两者只在"输入本来就是规范编码"时相等。任何"我方接受但非规范"的字节串都会让
         txRoot 分歧(我方算原字节的根,op-geth 算重编码的根)。
         这就是 C1(长度前缀前导零)已实测出 txRoot 分歧的机理,也是本层其余规范性检查存在的理由。
```

### 本层已核对、判定为**一致**的项

| 项 | 我方 | op-geth | 结论 |
|---|---|---|---|
| 整数前导零(payload 首字节 0) | `readCanonicalScalar` OpSchedulerImpl.h:311-314 | `rlp/decode.go:883-884` `buffer[0]==0 → ErrCanonInt` | 一致 |
| 单字节 <0x80 用 0x81 前缀 | `decodeHeader` RLPDecode.h:53-63 `NonCanonicalSize` | `rlp/raw.go:413 ErrCanonSize` | 一致 |
| 短串长度 <56 却用长形式 | RLPDecode.h:78-83 / :107-112 | `rlp/raw.go:362 ErrCanonSize` | 一致 |
| payloadLength 超出剩余输入 | RLPDecode.h:114-118 | `rlp` 的 `ErrValueTooLarge`/EOF | 一致(且我方有界,`enterList` 不会越界读) |
| uint64 超宽(>8 字节) | `readCanonicalScalar(in, 8, ...)` OpSchedulerImpl.h:308-310 | `rlp/decode.go:756 errUintOverflow` | 一致 |
| uint256 超宽(>32 字节) | `decodeU256Scalar` maxBytes=32,:339 | `rlp` big.Int/uint256 32 字节上限 | 一致 |
| address 必须恰好 20 字节 | `readFixedWidth` :322-335 | `rlp` 定长数组 `common.Address` | 一致 |
| hash / storage key 必须恰好 32 字节 | `decodeHashField` :392-398 | `common.Hash` 定长数组 | 一致 |
| bool 只认 空串 / 0x01 | `decodeBoolField` :371-379(显式拒 `0x00`) | `rlp: invalid boolean value` | 一致 |
| 信封尾随多余字节 | `expectExhausted` :434-439,deposit/1559/setcode 各调两次 | `rlp.DecodeBytes` 要求全消费 | 一致 |
| access list 条目尾随字节 | :453 | 同上 | 一致 |
| 授权元组尾随字节 | :480 | 同上 | 一致 |
| 外层签名 r,s ∈ [1,n-1] 且 s ≤ n/2(EIP-2) | `requireLowSSignature` :207-215 | `crypto/crypto.go:239-250` `ValidateSignatureValues(..., homestead=true)`,由 `londonSigner.Sender` 以 true 调用 | 一致 |
| 外层 yParity ∈ {0,1} | :583-584 / :630-631 | `ValidateSignatureValues` 末行 `(v == 0 \|\| v == 1)` | 一致 |
| chainId 与本链不符 → 拒 | :566-567 / :615-616 | `core/types/transaction_signing.go:284-285 ErrInvalidChainId` | 一致 |
| set_code 必须有 `to`、授权列表非空 | `eth/state/state.cpp:446-453`(`CREATE_SET_CODE_TX`/`EMPTY_AUTHORIZATION_LIST`) | `core/state_transition.go` `ErrSetCodeTxCreate`/`ErrEmptyAuthList` | 一致(路径不同:我方在 validate 阶段,op-geth 在 preCheck) |
| tip > feeCap → 拒 | `eth/state/state.cpp:466-467 TIP_GT_FEE_CAP` | `ErrTipAboveFeeCap` | 一致 |

### 本层**未覆盖**的面(划定边界)

- 0x03 blob 信封的双向性(我方解码器直接拒;op-geth 在 OP 上是否也在块级拒,未查)`[需验证]`。
- 0x7D PostExecTxType 的分叉门控与可达性,未查 `[需验证]`。
- legacy 的 EIP-155 / pre-155 双形态:因为我方**根本不解 legacy**,本层无从比对(见分歧 1-1)。

<!-- LAYER-1-END -->

---

## 第 2 层:块级前置校验

### 我方校验点分布

- `engine/bcos-engine/EngineServiceImpl.cpp:184-301` `validateOpNewPayloadRequest()` —— 静态(无父块)校验。
- `engine/bcos-engine/EngineServiceImpl.cpp:304-342` `rebuildOpEthHeader()` —— 21 字段重建 ETH 头,
  其中 `ommersHash`/`difficulty`/`nonce`/`requestsHash`/`excessBlobGas` 是**钉死的常量**,
  随后 `EngineServiceImpl.h:774` `ethHeader.hash() != payload.blockHash` 一票否决。
  这条等价于 op-geth `consensus/beacon/consensus.go:246-258` 的 nonce/uncleHash/difficulty 三项检查。
- `EngineServiceImpl.h:817-821` blockNumber == parent+1;`:886-890` timestamp > parent。
- `EngineServiceImpl.h:1044-1093` 执行后八项比对(六项比对面 + blobGasUsed + requestsHash)。

op-geth 对照面:`beacon/engine/types.go:286-372`(ExecutableDataToBlockNoHash)、
`consensus/beacon/consensus.go:235-311`(verifyHeader)、
`consensus/misc/eip1559/eip1559.go:33-58`(VerifyEIP1559Header)、
`consensus/misc/eip4844/eip4844.go` VerifyEIP4844Header、
`core/block_validator.go` ValidateBody、`eth/catalyst/api.go:855-905`。

### 【分歧 2-1】`extraData` 的 OP 形状完全不校验 —— 我方接受 op-geth 拒绝的块 ★

```
我方:    engine/bcos-engine/EngineServiceImpl.cpp:279-283 —— 只有一条 `extraData.size() > 32`
         的 ETH 通用长度界。:308-311 的注释自陈 extraData "原样,never re-derived or
         shape-checked",OP 形状检查挂在 spec §6.4 欠账。
op-geth: consensus/beacon/consensus.go:241-245 —— 对每个 OP 块(非创世)调
         `eip1559.ValidateOptimismExtraData`。
         consensus/misc/eip1559/eip1559_optimism.go:22-31 分叉派发;
         :195-204 ValidateJovianExtraData —— **必须恰好 17 字节**、
         **extra[0] 必须 == 0x01**(JovianExtraDataVersionByte)、
         :133-141 denominator 与 elasticity 都**必须非零**;
         :147-155 ValidateHoloceneExtraData —— 恰好 9 字节、版本字节 0x00、同样两个非零。
触发输入:一个 Jovian 时间戳的 payload,`extraData` = 17 字节但版本字节写 0x00(或 denominator
         写 0x00000000,或长度写成 9/16/18 字节)。payload 自己的 blockHash 按这份 extraData
         算好即可通过我方的 blockHash 检查。
分歧结果:VALID(我方)vs INVALID(op-geth `invalid optimism extraData`)。
         二阶后果更重:extraData 是**下一块 baseFee 的参数来源**
         (eip1559.go:76-79 `DecodeOptimismExtraData(config, parent.Time, parent.Extra)`),
         denominator==0 在 op-geth 里会走 `num.Div(num, denom.SetUint64(0))` —— 该分支被
         ValidateOptimismExtraData 挡在门外,而我方既不挡形状也不算 baseFee(C4),等于把
         一个能污染后续所有块 baseFee 的字段完全放行。
可达性:  当前可构造。
置信度:  已读两侧源码确认。
```

### 【分歧 2-2】Jovian:`daFootprint > gasLimit` 的块级上限未校验 ★

```
我方:    bcos-evm/bcos-evm/opstack/OpBlockSeal.cpp:74-81 —— `has_da_footprint` 时把各回执的
         `meta.da_footprint` 求和填进 `seal.blobGasUsed`,**只求和,不设上限**。
         engine/bcos-engine/EngineServiceImpl.h:1078-1085 —— 只把它和 `payload.blobGasUsed`
         比相等,没有任何与 gasLimit 的比较。
op-geth: core/block_validator.go(ValidateBody,Jovian 段)——
         `daFootprint, err := types.CalcDAFootprint(block.Transactions())`;
         `if blobGasUsed != daFootprint { ... }` **并且**
         `if daFootprint > block.GasLimit() { return fmt.Errorf("DA footprint %d exceeds block
         gas limit %d", ...) }`。
触发输入:Jovian 时间戳、gasLimit 取一个小值(例如 30_000_000)、块内塞入足够多 calldata
         使 DA footprint 之和超过 gasLimit 的一批交易(DA footprint 由 calldata 字节数驱动,
         与执行 gas 解耦,所以可以在不超 gas 池的前提下超 DA 上限)。payload 的
         `blobGasUsed` 填成同一个和即可通过我方比对。
分歧结果:VALID(我方)vs INVALID(op-geth "DA footprint exceeds block gas limit")。
可达性:  当前可构造(需要 Jovian 分叉配置 + 一个 calldata 密集的块)。
置信度:  已读两侧源码确认。`CalcDAFootprint` 与我方 `meta.da_footprint` 求和的**逐笔公式**
         是否一致属第 5 层,本条只针对**块级上限缺失**。
```

### 【分歧 2-3】首块(父头缺失)豁免 timestamp 单调性 —— 同时豁免了分叉选择器

```
我方:    EngineServiceImpl.h:868-895 —— timestamp 单调性检查包在
         `if (auto parentHeaderEntry = ...; parentHeaderEntry.has_value())` 里。
         :862-867 的注释明说:引导时只 seed `SYS_HASH_2_NUMBER`,不写父头,所以**本循环接受的
         第一个块没有父头可比,检查整段跳过**。
op-geth: eth/catalyst/api.go:888-891 —— `parent := GetBlock(block.ParentHash(), ...)`,
         `parent == nil` 走 `delayPayloadImport`(SYNCING),**绝不会在"有父块记录但无父头"
         的状态下放行**;consensus/beacon/consensus.go:253-256 `header.Time <= parent.Time`
         也是无条件的。
触发输入:一个刚 seed 过起点(只有 SYS_HASH_2_NUMBER)的节点,投喂第一个块,timestamp 取
         任意值 —— 包括**小于起点块的时间戳**,甚至小于 isthmusTime。
分歧结果:我方接受(且 `configAt(timestamp)` 会据此选一个更早的分叉配置,把整块的执行语义
         回滚到 Isthmus);op-geth 场景上不存在(它的起点必带头)。
         同时会与第 1 层的 -38005 版本闸交互(C3 已记账)。
可达性:  当前可构造(测试夹具与任何未来生产引导都走这条 seed 路径);属"生产接入才可达"的
         边界情形——引导协议若改为同时 seed 父头则消失。
置信度:  已读两侧源码确认。
```

### 【分歧 2-4】父头按**高度**读,op-geth 按**哈希**读

```
我方:    EngineServiceImpl.h:869-871 —— `StateKeyView{c_ethBlockHeaderTable, parentNumberStr}`。
op-geth: eth/catalyst/api.go:888 —— `GetBlock(block.ParentHash(), block.NumberU64()-1)`。
触发输入:同一高度存在两个块(重组窗口)。
分歧结果:我方会拿**错的父头**做 timestamp 比较。
可达性:  需未来改动 —— 今天被 step 3c(:951-965 非链尾父块直接 -32603)堵死,代码注释
         (:846-852)已自陈这是真依赖而非巧合。列此以标记其为"解除限制时必须同步修的项"。
置信度:  已读两侧源码确认。
```

### 本层已核对、判定为**一致**的项

| 项 | 我方 | op-geth | 结论 |
|---|---|---|---|
| `extraData` ≤ 32 字节 | EngineServiceImpl.cpp:279-283 | beacon/engine/types.go:292-294;consensus.go:237-239 | 一致 |
| `gasLimit` ≤ 2^63-1 | EngineServiceImpl.cpp:269-273 | consensus.go:261-263 `params.MaxGasLimit` | 一致 |
| **OP 链 gasLimit 对父块可瞬时调整**(不设变化界) | 两侧都不做 | eip1559.go:39-43 `if !config.IsOptimism()` 才 `VerifyGaslimit` | **一致**(我方"漏"的这一条 op-geth 对 OP 本就不做) |
| `gasUsed ≤ gasLimit` | 无显式静态检查,但 `gasUsed` 由执行产出并与 payload 逐值比对(EngineServiceImpl.h:1069-1072),而执行侧 `blockGasLeft` 起始即 `block.gas_limit`(OpBlockExecute.cpp:44)且 `validate_transaction` 有 `tx.gas_limit > block_gas_left → GAS_LIMIT_REACHED`(state.cpp:483-484) | consensus.go:265-267 | 等价(deposit 路径的 gas 语义另见第 5 层) |
| `blockNumber == parent+1` | EngineServiceImpl.h:817-821 | consensus.go:269-271 | 一致 |
| `timestamp > parent`(有父头时,严格 `>`) | EngineServiceImpl.h:886-890 `<=` 即拒 | consensus.go:253-256 `header.Time <= parent.Time`;api.go:889 同 | 一致(边界方向逐字相同) |
| `excessBlobGas` 必须存在且为 0 | EngineServiceImpl.cpp:226-229 | eip4844.go `CalcExcessBlobGas` 对 `config.IsOptimism()` 短路 `return 0`,`VerifyEIP4844Header` 再要求相等 | 一致 |
| `blobGasUsed` 不受"blob 数倍数/上限"约束 | 我方无此检查 | `VerifyEIP4844Header` 的两条 blob 检查被 `if !config.IsOptimism()` 跳过 | 一致 |
| 块体不得含 blob | 我方在解码层就拒 0x03(分歧 1-1) | block_validator.go `if blobs > 0 { return errors.New("data blobs present in block body") }`(OP 走 else 分支) | 一致(路径不同,结论相同) |
| `withdrawals` 必须在场且为空(Isthmus+) | EngineServiceImpl.cpp:207-210 | beacon/engine/types.go:317-324;block_validator.go `IsOptimismIsthmus` 段 | 一致 |
| `withdrawalsRoot` 必须在场(Isthmus+) | EngineServiceImpl.cpp:216-221 | beacon/engine/types.go:318-320 | 一致 |
| `expectedBlobVersionedHashes` 必须为空 | EngineServiceImpl.cpp:211-214 | beacon/engine/types.go:302-312 | 一致 |
| `parentBeaconBlockRoot` 必须在场 | EngineServiceImpl.cpp:215-218 + `:589-601` 的版本闸 | consensus.go:296-298 | 一致 |
| `requestsHash` = OP 空请求常量 | 钉死在 rebuildOpEthHeader:340,由 blockHash 检查兜底 + :1086-1092 二次比对 | beacon/engine/types.go:333-341 | 一致 |
| 已知块短路 VALID | EngineServiceImpl.h:918-919 | api.go:871-876 | 一致 |
| 父块未知 → SYNCING | EngineServiceImpl.h:797-800 | api.go:888-890 `delayPayloadImport` | 一致 |
| `blockNumber` 不得为负 | EngineServiceImpl.cpp:252-255 | Go 侧 `uint64`,不可能为负 | 我方更严但无接受面差 |

### 本层的已知/已记账分歧(引用,不重证)

- **C4 `baseFeePerGas` 双向缺陷** —— 对应 op-geth `consensus/misc/eip1559/eip1559.go:52-57`
  `header.BaseFee.Cmp(expectedBaseFee) != 0`。我方 `validateOpNewPayloadRequest` 完全不算父子公式,
  `baseFeePerGas` 只是原样进 header 与 BlockEnv。**与分歧 2-1 复合**:extraData 形状不校验 +
  baseFee 公式不校验 = 攻击者对 baseFee 有完全自由度。
- **C3 pre-Isthmus V3 闸** —— `EngineServiceImpl.h:682-702`。

### 本层**未覆盖**的面(划定边界)

- EIP-7934 块 RLP 大小上限(`ErrBlockOversized`,Osaka 才生效)—— 与 Isthmus/Jovian 无关,未查。
- `checkInvalidAncestor`(op-geth api.go:877-879 的"坏块后代恒拒")—— 我方无对应机制,
  但它属于缓存/优化语义,不影响单块的 VALID/INVALID 判定,未展开 `[需验证]`。
- op-geth `verifyHeader` 的 `SlotNumber`(Amsterdam)—— 分叉未及,未查。

<!-- LAYER-2-END -->

---

## 第 3 层:块前系统调用(EIP-4788 / EIP-2935)

我方入口:`bcos-evm/bcos-evm/opstack/OpBlockExecute.cpp:31-32` ——
`applyDiff(sanitizeStateDiff(view, system_call_block_start(view, block, hashes, cfg.rev, vm)))`,
实现在 `bcos-evm/bcos-evm/eth/state/system_contracts.cpp:85-107`。
op-geth 对照:`core/state_processor.go:90-95`(`ProcessBeaconBlockRoot` / `ProcessParentBlockHash`),
实现在 `:262-282` / `:286-308`。

### 【分歧 3-1】系统调用 REVERT / OOG 时,我方**不回滚**,op-geth 回滚

```
我方:    bcos-evm/bcos-evm/eth/state/system_contracts.cpp:66-82 execute_system_call ——
         直接 `vm.execute(host, rev, msg, code.data(), code.size())`,**绕过了
         `Host::call`**。回滚只发生在 `Host::call` 里
         (bcos-evm/bcos-evm/eth/state/host.cpp:383-397:`state_checkpoint = m_state.checkpoint()`
         → 非 SUCCESS 时 `m_state.rollback(state_checkpoint)`)。顶层帧没有任何人做 checkpoint,
         因此系统合约在中途 SSTORE 后 REVERT/OOG,**已写入的槽会留在 `state` 里**,随后被
         :106 `state.build_diff(rev)` 收集并 applyDiff 落账。
         :103 只有一句 `assert(res.status_code == EVMC_SUCCESS)` —— NDEBUG 下被编译掉。
op-geth: core/state_processor.go:262-282 —— `evm.Call(msg.From, *msg.To, msg.Data, 30_000_000,
         common.U2560)`。geth 的 `EVM.Call` 内部 `snapshot := evm.StateDB.Snapshot()`,
         出错即 `RevertToSnapshot(snapshot)` —— 顶层 REVERT 被完整回滚。
         2935 侧另有 `:301-303 if err != nil { panic(err) }`(节点崩溃,不是判块无效)。
触发输入:`0x000F3df6D732807Ef1319fB7B8bB8522d0Beac02`(BEACON_ROOTS)或
         `0x0000F90827F1C53a10cb7A02335B175320002935`(HISTORY_STORAGE)上的代码在
         SSTORE 之后 REVERT(或耗尽 30M gas)。规范预部署代码不会,但这是**账本状态**决定的,
         不是协议常量 —— 任何把这两个地址写成别的代码的创世/升级都能触发。
分歧结果:stateRoot 直接分歧(我方多出被 revert 掉的槽写入)。
可达性:  需生产账本(取决于预部署代码);当前 t8n 语料用规范预部署,恒不触发。
置信度:  已读两侧源码确认回滚位置的存在与缺失;"规范预部署不会 revert"未逐字节验证 `[需验证]`。
最小验证步骤:在 `OpBlockExecuteTest.cpp` 里给 BEACON_ROOTS 装一段 `PUSH1 1 PUSH1 0 SSTORE
         PUSH1 0 PUSH1 0 REVERT` 的代码,断言执行后该槽在 diff 里 —— 若在,分歧成立。
```

### 【分歧 3-2】`assert` 承担了唯一的失败处理,Release 构建下失败被静默吞掉

```
我方:    system_contracts.cpp:103 `assert(res.status_code == EVMC_SUCCESS);` —— 生产构建
         (NDEBUG)下该行不存在,失败的系统调用与成功的系统调用**在控制流上无法区分**。
op-geth: 4788 侧同样忽略错误(`_, _, _ = evm.Call`,state_processor.go:281),
         **但**由上面 3-1 的 snapshot 保证"失败 ⇒ 无状态变化";2935 侧 `panic(err)`。
分歧结果:与 3-1 同源。单列是因为它决定了 3-1 在 Debug 构建下表现为**进程 abort**、
         在 Release 构建下表现为**静默状态分歧** —— 两种都不是"判块 INVALID"。
可达性:  同 3-1。
置信度:  已读两侧源码确认。
```

### 本层已核对、判定为**一致**的项

| 项 | 我方 | op-geth | 结论 |
|---|---|---|---|
| EIP-4788 激活条件 | `system_contracts.cpp:35` `EVMC_CANCUN`;Isthmus/Jovian 的 `rev = EVMC_PRAGUE`(OpForkSchedule.cpp:62/77)≥ CANCUN | `state_processor.go:90` `block.BeaconRoot() != nil`,而头校验保证 Cancun 起必在场 | 一致 |
| EIP-2935 激活条件 | `system_contracts.cpp:39` `EVMC_PRAGUE` | `state_processor.go:93` `IsPrague \|\| IsVerkle`(OP 的 Isthmus ≡ Prague) | 一致 |
| 4788 输入 | `block.parent_beacon_block_root`(:36-38) | `*block.BeaconRoot()`(:91) | 一致 |
| 2935 输入 | `block_hashes.get_block_hash(block.number - 1)`(:40-42),由 `OpSchedulerImpl.h:221-230` `ParentOnlyBlockHashes` 应答 `env.parentHash` | `block.ParentHash()`(:94) | 一致 |
| 调用者 / gas | `SYSTEM_ADDRESS`,`gas = 30'000'000`(:71-76) | `params.SystemAddress`,`GasLimit: 30_000_000`(:268-275) | 一致 |
| 合约代码缺失时静默跳过 | `:96-98` `if (code.empty()) continue;` | `evm.Call` 对不存在账户 + value==0 直接早返回,不落任何状态 | 一致(净效果) |
| 两个系统调用的**先后顺序**(4788 先、2935 后) | `STORAGE_SYSTEM_CONTRACTS` 数组序 + `static_assert(is_sorted by_rev)`(:59-63) | `:90-95` 先 4788 后 2935 | 一致 |
| 块后 requests 系统调用(EIP-7002/7251/6110)**不执行** | `processOpBlock` 从不调 `system_call_block_end`;`OpForkConfig::disable_prague_requests = true`(OpForkSchedule.cpp:64/79) | `state_processor.go:141` `IsPrague(...) && !IsIsthmus(block.Time())` —— Isthmus 起整段跳过 | 一致 |
| 系统调用产生的 diff 也过 `sanitizeStateDiff` | OpBlockExecute.cpp:31 | —— | 见第 6 层 |

### 本层**未覆盖**的面

- `misc.EnsureCreate2Deployer`(op-geth `state_processor.go:81`)—— OP 在 **Canyon 激活块**注入
  Create2Deployer 代码的一次性块前状态写入。本循环的 `configAt` 只解析 Isthmus/Jovian
  (OpForkSchedule.cpp:102-110),Canyon 激活块不可能出现,故不适用;但如果未来支持从更早的
  分叉重放历史块,**这是一条我方完全没有的块前状态写入** `[需验证]`。
- `evm.StateDB.Finalise(true)`(op-geth 在两个系统调用后各调一次)与我方 `build_diff` 的
  空账户清除时机差异 —— 归到第 6 层一并处理。

<!-- LAYER-3-END -->

---

## 第 4 层:交易顺序与准入规则

三条准入规则全部集中在 `bcos-evm/bcos-evm/opstack/OpBlockExecute.cpp:36-55`。
它们**都是"更严"方向**,即拒绝 op-geth 会接受的块。源码注释(:17-19)自陈这一点:
"op-geth EL does not perform this validation (responsibility pushed down to the CL layer)"。
本报告仍然逐条列出——审计判据要求两个方向同等对待,而对验证者节点而言"拒绝一个合法块
等于投错票"。

对照面确认:op-geth 的 EL 侧对 deposit 的**顺序/位置/数量**没有任何校验。
`core/block_validator.go` 的 `ValidateBody`、`consensus/beacon/consensus.go` 的 `verifyHeader`
里没有出现 `IsDepositTx`/`DepositTxType`(逐文件 grep 确认);
`core/state_processor.go` 中仅有的两处 `IsDepositTx`(:174、:217)都是 Regolith 回执字段逻辑,
与顺序无关;`core/state_processor.go:97-116` 的交易循环对每笔一视同仁地
`TransactionToMessage` → `ApplyTransactionWithEVM`。

### 【分歧 4-1】空块被我方判 INVALID,op-geth 正常执行

```
我方:    OpBlockExecute.cpp:36-37 —— `if (txs.empty()) throw std::runtime_error(
         "op block: missing L1 attributes deposit (empty block)")`;
         经 OpSchedulerImpl.h:839-844 归类为 OpConsensusError → 引擎 :996-999 判 INVALID。
op-geth: core/state_processor.go:97 的 `for i, tx := range block.Transactions()` 对空切片直接
         跳过;`ValidateBody` 无最小交易数要求。空块产出空 txRoot/receiptsRoot,VALID。
触发输入:`executionPayload.transactions = []`。
分歧结果:VALID vs INVALID。
可达性:  当前可构造。
置信度:  已读两侧源码确认。
```

### 【分歧 4-2】首笔必须是 L1 attributes deposit,且 `to`/`from` 必须精确匹配

```
我方:    OpBlockExecute.cpp:15-21 `isL1AttributesTx` —— 要求
         `dep.to.has_value() && *dep.to == OP_L1_BLOCK && dep.from == OP_DEPOSITOR`;
         :38-40 首笔不满足即 throw。
op-geth: 无对应检查(见上)。
触发输入:(a) 首笔是普通 0x02 交易的块;(b) 首笔是 deposit 但 `from` 不是
         `0xDeaDDEaDDeAdDeAdDEAdDEaddeAddEAdDEAd0001`;(c) 首笔 deposit 的 `to` 不是
         `0x4200000000000000000000000000000000000015`(L1Block)。
         三者在 op-geth 上都会被正常执行。
分歧结果:VALID vs INVALID。
可达性:  当前可构造。
置信度:  已读两侧源码确认(且我方源码注释自陈"stricter-than-spec")。
风险评估:这条是**刻意的更严**,防御价值真实(CL 被攻陷时能挡住伪造 L1 属性);但作为
         验证者它会在 CL 侧任何合法的 attributes 变体上误判 INVALID。若 OP 未来允许
         attributes deposit 的 `to` 变更(例如新的 L1Block 预部署地址),我方会在分叉当天
         拒绝整条链。
```

### 【分歧 4-3】deposit 不得出现在任何非 deposit 之后

```
我方:    OpBlockExecute.cpp:54-55 —— `if (seenNonDeposit) throw std::runtime_error(
         "op block: deposit after non-deposit tx")`。
op-geth: 无对应检查。一笔位于块中部的 deposit 会被**正常执行并铸币**。
触发输入:交易序列 `[attrs-deposit, 0x02 转账, 0x7E deposit]`。
分歧结果:VALID(op-geth,且第三笔的 mint 真实入账)vs INVALID(我方)。
可达性:  当前可构造。
置信度:  已读两侧源码确认。
```

### 本层已核对、判定为**一致**的项

| 项 | 我方 | op-geth | 结论 |
|---|---|---|---|
| 普通(非 deposit)交易校验失败 ⇒ **整块无效**,不产生失败回执 | OpBlockExecute.cpp:76-81 —— `opValidate` 返回 `error_code` 即 `throw` | `state_processor.go:108-111` —— `ApplyTransactionWithEVM` 返错即 `return nil, fmt.Errorf("could not apply tx %d ...")`,整个 `Process` 失败 | 一致 |
| deposit 校验失败 ⇒ **产生失败回执并继续**(Regolith 语义) | OpDepositTx.cpp:93-113 | `state_transition.go:483-508` `execute()` 的 failed-deposit 分支 | 一致(细节见第 5 层) |
| `is_system_tx` 的 deposit ⇒ **整块无效**(非失败回执) | OpDepositTx.cpp:63-64 throw | `state_transition.go:353-357` 返回 `ErrSystemTxNotSupported`,且 `:486` 的 failed-deposit 分支用 `!errors.Is(err, ErrSystemTxNotSupported)` **显式排除**它 | 一致 |
| deposit 撞块 gas 池 ⇒ **整块无效**(非失败回执) | OpDepositTx.cpp:95-96 —— `GAS_LIMIT_REACHED` 单独 throw | `state_transition.go:360` `st.gp.SubGas(GasLimit)` 返回 `ErrGasLimitReached`,`:486` 同样**显式排除** | 一致(两边都把它划到"块级"而非"交易级") |
| 交易执行顺序 = payload 给出的顺序 | OpBlockExecute.cpp:50 顺序 for | `state_processor.go:97` 顺序 for | 一致 |
| `cumulative_gas_used` 逐笔累加 | OpBlockExecute.cpp:59-60 / :86-87 | `ApplyTransactionWithEVM` 内 `receipt.CumulativeGasUsed = usedGas` | 一致 |

### 本层**未覆盖**的面

- op-geth 是否在 `miner`/`op-node` 侧另有 deposit 顺序约束 —— 与 EL 的块**验证**语义无关,未查。
- 一个块里出现**两笔** L1 attributes deposit:我方 :54-55 只挡"deposit 在非 deposit 之后",
  连续两笔 attributes deposit 会被放行并**执行两次**;op-geth 同样放行。**两边一致**,
  但两边都依赖 CL 不产生这种块。

<!-- LAYER-4-END -->

---

## 第 5 层:单笔执行

我方:`OpValidate.cpp`(准入+费用预算)、`OpTransition.cpp`(普通交易)、`OpDepositTx.cpp`(deposit)、
`OpExecCommon.cpp`(共用消息执行/退款/floor)、`RollupCost.cpp` + `OpFeeParams.cpp`(费用公式与参数)。
op-geth:`core/state_transition.go`、`core/types/rollup_cost.go`、`core/state_processor.go`。

### 【分歧 5-1】Jovian DA footprint 的 `daFootprintGasScalar` **取自不同的源** ★★

```
我方:    OpFeeParams.cpp:33 —— `.da_footprint_gas_scalar = readBE(slot8, 18, 2)`,
         即 L1Block 合约(0x42..15)**存储槽 8 的第 18-19 字节**;
         :43-45 `loadOpFeeParams` 在本块第一笔非 deposit 交易时读一次(OpBlockExecute.cpp:66-73);
         OpReceiptMeta.cpp:27-32 用它算 `da_footprint = estimatedDaSizeFromFlz(flzLen) * scalar`。
op-geth: core/types/rollup_cost.go:563-590 `CalcDAFootprint(txs)` ——
         `daFootprintGasScalar, err := ExtractDAFootprintGasScalar(txs[0].Data())`,
         即**第一笔(L1 attributes deposit)交易的 calldata**,:547-558:
           - `len(data) < JovianL1AttributesLen(178)` → **报错,整块无效**;
           - `data[0:4] != JovianL1AttributesSelector(0x3db6be2b)` → **报错,整块无效**;
           - 取 `binary.BigEndian.Uint16(data[176:178])`。
触发输入:(a) 一个 Jovian 块,其 L1 attributes 交易 calldata 长度 178 但**选择器不是
         0x3db6be2b**(例如仍用 Isthmus 的 0x098999be 再补 2 字节):op-geth 判整块 INVALID;
         我方照读槽 8、算出 footprint、与 payload 比对通过 → VALID。
         (b) 任何"槽 8 的 [18:20] 与 attributes calldata[176:178] 不一致"的状态 —— 二者是否
         恒等取决于 **L1Block 预部署合约的存储布局**,不是协议常量。
分歧结果:(a) VALID vs INVALID;(b) `blobGasUsed`(Jovian 头字段)不同 → 我方判 INVALID
         而 op-geth 判 VALID(反向)。
可达性:  当前可构造(a);(b) 需生产账本。
置信度:  已读两侧源码确认取值位置不同;"槽 8 [18:20] 恒等于 calldata[176:178]"**未验证** `[需验证]`。
最小验证步骤:反编译/查阅 Jovian 版 L1Block.sol 的 `setL1BlockValuesJovian`,确认它把
         `_daFootprintGasScalar` 写进 `operatorFeeParams` 槽的哪两个字节;或在
         `OpFeeParamsTest.cpp` 里用一条真实 Jovian attributes calldata 跑一遍预部署字节码,
         比对槽 8 与 calldata[176:178]。
```

### 【分歧 5-2】Jovian 激活块的"不得含用户交易"约束缺失

```
我方:    无对应检查。`loadOpFeeParams` 会读到一个(激活块尚未写入的)槽 8,scalar 多半为 0,
         于是 `da_footprint` 恒为 0,seal.blobGasUsed = 0 —— 与 op-geth 的 `return 0, nil`
         结果相同,**但前提条件的校验没有做**。
op-geth: rollup_cost.go:569-577 —— 若 `len(txs[0].Data()) == IsthmusL1AttributesLen(176)`
         (即激活块仍带 Isthmus 版 attributes),则**要求最后一笔也是 deposit**
         (`if !txs[len(txs)-1].IsDepositTx() { return 0, errors.New("unexpected non-deposit
         transactions in Jovian activation block") }`),否则整块无效。
触发输入:Jovian 激活块,attributes 为 176 字节 Isthmus 格式,块内再放一笔 0x02 用户交易。
分歧结果:VALID(我方,footprint=0 与 payload 的 0 相符)vs INVALID(op-geth)。
可达性:  当前可构造(需构造一个跨 Jovian 激活边界的块)。
置信度:  已读两侧源码确认。
```

### 【分歧 5-3】`opValidate` 的"块 gas 池"检查发生在 `loadOpFeeParams` 之后,但费用参数只加载一次;
op-geth 的 `L1CostFunc`/`OperatorCostFunc` 闭包按 **blockTime** 缓存 —— 同一块内两者行为一致,
**跨块复用同一 scheduler 实例时**我方每块新建 `OpFeeParams`(局部变量),无跨块污染。**判定一致**,
但记录于此,因为它是"每块缓存一次 vs 每笔读"这一问题的直接答案:

```
我方:    OpBlockExecute.cpp:47-48 `bool feeLoaded=false; OpFeeParams fee{};` 是 processOpBlock 的
         局部变量,:66-73 惰性加载一次,块结束即销毁。
op-geth: rollup_cost.go:243-251 —— `func(gas, blockTime) { if forBlock != blockTime {
         forBlock = blockTime; cachedFunc = selectFunc(blockTime) } ... }`,闭包随
         `NewEVMBlockContext` 每块新建(state_processor.go:87-88)。
结论:    一致(都是"本块第一笔非 deposit 交易时读一次 L1Block 槽")。
```

### 本层已核对、判定为**一致**的项(逐条对齐源码)

| 项 | 我方 | op-geth | 结论 |
|---|---|---|---|
| Fjord+ L1 费公式 `estimatedSize*(l1BaseFee*16*baseScalar + blobBaseFee*blobScalar)/1e12` | RollupCost.cpp:159-172 | rollup_cost.go:608-630 `NewL1CostFuncFjord` | 一致(常量 `-42585600`/`836500`/`1e12`/`16` 逐值相同,RollupCost.cpp:12-16 ↔ rollup_cost.go:92-93、:66-71) |
| `estimatedSize = max(100e6, intercept + coef*fastlz)` | RollupCost.cpp:127-132 | rollup_cost.go:632-641 `estimatedDASizeScaled` | 一致 |
| FastLZ 压缩长度算法 | RollupCost.cpp:26-119(逐行移植) | `FlzCompressLen` | 一致(结构对应;**未逐字节 differential fuzz** `[需验证]`) |
| L1 费的输入字节 = 完整签名信封 | `signedTxEnvelope`(OpBlockExecute.cpp:75,来自 `OpBlockTx::signedEnvelope` = 原始线上字节) | `tx.MarshalBinary()`(`RollupCostData()`) | 一致(前提:解码器拒绝非规范编码,见第 1 层) |
| Operator fee(Isthmus)`gas*scalar/1e6 + constant` | RollupCost.cpp:205-207 | rollup_cost.go:254-268 `newOperatorCostFuncIsthmus` | 一致(含运算顺序:先乘后整除再加) |
| Operator fee(Jovian)`gas*scalar*100 + constant` | RollupCost.cpp:199-204 | rollup_cost.go:272-286 `newOperatorCostFuncOperatorFeeFix`(由 `IsJovian` 选中,:236-239) | 一致 |
| operator fee 参数槽 = L1Block 槽 8,scalar=[20:24],constant=[24:32] | OpFeeParams.cpp:31-32 | rollup_cost.go:82-84 `OperatorFeeParamsSlot=8`;:656-659 `ExtractOperatorFeeParams` 取 `[20:24]`/`[24:32]` | 一致 |
| l1BaseFee=槽1、feeScalars=槽3([16:20]/[20:24])、blobBaseFee=槽7 | OpFeeParams.cpp:27-30、:43-45 | rollup_cost.go:70-80 | 一致 |
| operator fee 按 **gasLimit** 预扣、按 **gasUsed** 退差 | OpTransition.cpp:171-172(扣)/ :189-193(退+入库) | state_transition.go `buyGas` 的 `OperatorCostFunc(st.msg.GasLimit,...)`;`refundIsthmusOperatorCost()` 用 `OperatorCostFunc(st.gasUsed(),...)` 退差 | 一致 |
| 四个金库地址 | OpPredeploys.h:19-25 —— BaseFee `0x42..19`、L1Fee `0x42..1a`、Operator `0x42..1b`、MessagePasser `0x42..16` | params/protocol_params.go:25-34 —— 逐字相同 | 一致 |
| coinbase 小费 = `gasUsed * min(tip, feeCap-baseFee)` | OpTransition.cpp:161-163、:181 | state_transition.go `effectiveTip = GasPrice - BaseFee`,`GasPrice = min(tip+baseFee, feeCap)`;`AddBalance(Coinbase, gasUsed*effectiveTip)` | 一致 |
| baseFee 不销毁,入 BaseFeeVault | OpTransition.cpp:186-187 | state_transition.go:713-719 `AddBalance(OptimismBaseFeeRecipient, gasUsed*BaseFee)` | 一致 |
| deposit **不**付 coinbase / 不入任何金库 / 不计 L1 费 | OpDepositTx.cpp 全文无金库写入 | state_transition.go:681-688 Regolith deposit 提前 return;:713 `&& !st.msg.IsDepositTx` | 一致 |
| deposit 的 gas 语义:失败时 `gasUsed = gasLimit` 全额计入 | OpDepositTx.cpp:101、:112 | state_transition.go:495-505 `gasUsed := st.msg.GasLimit`;`st.gp.ReturnGas(0, gasUsed)` | 一致 |
| deposit 失败时**保留 mint、强制 nonce+1、回滚其余** | OpDepositTx.cpp:69-70(先 mint)、:99/:110(nonce=preNonce+1)、失败分支根本没执行 | state_transition.go:474-481(先 mint)、`snap` 在 mint **之后**、`RevertToSnapshot(snap)`、`SetNonce(from, GetNonce(from)+1)` | 一致 |
| deposit 余额不足以支付 `value` ⇒ 失败回执(非块级) | OpDepositTx.cpp:106-113 | `innerExecute` clause 6 → `execute()` 的 failed-deposit 分支 | 一致 |
| 块 gas 池净消耗 = 逐笔 `gasUsed` 之和 | OpBlockExecute.cpp:58/:85 `blockGasLeft -= gas_used`;:97 `result.gasUsed = cumulative` | `gp.SubGas(GasLimit)` + `gp.ReturnGas(gasRemaining, gasUsed)`;`gp.Used() = initial - remaining` = Σ gasUsed(core/gaspool.go:40-68、:82-88) | 一致 |
| `cumulativeGasUsed` 来源 | OpBlockExecute.cpp:59-60/:86-87 累加 | `gp.CumulativeUsed()`(state_processor.go:195) | 一致 |
| 退款上限 `gasUsed/5`(London+) | OpExecCommon.cpp:65-68 | `calcRefund` 用 `RefundQuotientEIP3529` | 一致 |
| EIP-7702 每条授权的退款 12500 | OpTransition.cpp:30-32、:104-109(25000-12500) | `params.CallNewAccountGas=25000`、`params.TxAuthTupleGas=12500` | 一致 |
| EIP-7623 floor:退款后再取 `max(gasUsed, floor)` | OpExecCommon.cpp:71 | state_transition.go:648-660(先 `gasRemaining += calcRefund()`,再 Prague floor) | 一致 |
| 7702 顺序:先 sender nonce+1,再处理授权列表 | OpTransition.cpp:151-156 | `innerExecute` 中 `SetNonce(msg.From, +1)` 后 `applyAuthorization` 循环 | 一致 |
| 7702 单条授权失败 ⇒ **跳过该条**,不废整笔 | OpTransition.cpp:57/61/67/71/79/91/96 全是 `continue` | geth 注释 "errors are ignored, we simply skip invalid authorizations here" | 一致 |
| 7702 授权的 `chain_id ∈ {0, chainId}`、`s ≤ n/2`、`v ≤ 1`、`nonce != NonceMax` | OpTransition.cpp:57-72 | 同 EIP-7702 规范,geth `applyAuthorization` | 一致(**除 C2 已记账的 yParity 位宽**) |
| `to == 0x00..0` 的授权 ⇒ 清空委托代码 | OpTransition.cpp:113-121 | EIP-7702 特例 | 一致 |
| 委托代码 `0xef0100 \|\| addr`、执行前解析委托目标并 warm | OpTransition.cpp:126;OpExecCommon.cpp:51-59 | state_transition.go:615-620 `ParseDelegation` + `AddAddressToAccessList` | 一致 |
| access list 预热 + Shanghai 起 coinbase 预热 | OpExecCommon.cpp:38-48 | `st.state.Prepare(rules, from, coinbase, to, precompiles, accessList)` | 一致 |
| EIP-3607(sender 必须是 EOA 或已委托) | state.cpp:494-496 | `preCheck` 的 `ErrSenderNoEOA` | 一致 |
| nonce too high / too low / EIP-2681 上限 | state.cpp:498-505 | `preCheck` 的三个分支 | 一致 |
| 余额充足性:`gasLimit*maxFeePerGas + value + l1Cost + operatorCost` | OpValidate.cpp:36-41(叠加在 evmone 的 `gasLimit*maxFee+value` 之上) | `buyGas` 的 `balanceCheck`(用 `GasFeeCap`) | 一致 |
| 实际扣款:`gasLimit*effectiveGasPrice + l1Cost + operatorCost(gasLimit)` | OpTransition.cpp:166-172 | `buyGas` 的 `mgval`(用 `GasPrice`) | 一致 |
| blob 交易在 OP 上不可执行 | OpValidate.cpp:12-13 返回 `not_supported`(**死代码**:解码层已先拒,见分歧 1-1) | —— | 一致(结论相同) |

### 本层的已知/已记账分歧(引用)

- **C2 7702 授权 `yParity` 位宽** —— `OpSchedulerImpl.h:477` `auth.v = decodeU256Scalar` vs
  op-geth `SetCodeAuthorization.V uint8`。
- **C4** 的一个下游后果:`block.base_fee` 由 payload 直接给定(`OpSchedulerImpl.h:238`),
  而 `OpTransition.cpp:158-163` 的 `effective_gas_price` 完全建立在它之上 —— baseFee 不校验
  意味着**每一笔交易的实际 gas 价格、coinbase 小费、BaseFeeVault 入账都可被 payload 任意设定**。

### 本层**未覆盖**的面

- Ecotone L1 费公式(`RollupCost.cpp:180-189`)—— `has_ecotone_l1_formula` 在 Isthmus/Jovian
  下恒为 false(OpForkSchedule.cpp:68/83),本循环不可达,未与 `newL1CostFuncEcotone` 逐值比对。
- Bedrock L1 费(`bedrockCalldataGasUsed`)—— 同上,只被 Ecotone 分支调用,不可达。
- `compute_tx_intrinsic_cost`(evmone)与 geth `IntrinsicGas` 的**逐项** gas 常量比对
  (calldata 零/非零字节、创建、access list 条目、授权元组)未做 `[需验证]`;
  这是 evmone upstream 面,风险低但不为零。
- 预编译差异(`OpPrecompiles.cpp` 的 Fjord/Granite/Isthmus/Jovian override)未比对。

<!-- LAYER-5-END -->

---

## 第 6 层:状态写入语义(含批 9 修复后的复核)

我方:`bcos-evm/bcos-evm/ledger/Storage2Ledger.h`(读桥 + 写回)、
`bcos-evm/bcos-evm/adapter/StateRootCompute.h/.cpp`(建根)、
`bcos-evm/bcos-evm/adapter/StateDiffSanitize.h`、`bcos-evm/bcos-evm/eth/state/state.cpp`(`build_diff`)。

### 批 9「零值槽」修复的复核 —— **结论:修复到位,且与生产写路径兼容**

逐条核对提示词点名的四个判据:

| 判据 | 位置 | 行为 | 判定 |
|---|---|---|---|
| `fetchStorage` / `get_storage` | Storage2Ledger.h:893-913 | 行不存在 → 全零;行存在且值为 32 字节全零 → 原样返回全零。两者不可区分 | ✅ 与以太坊语义一致 |
| `probeHasStorage`(→ `has_storage`) | :850-873 | 只有"存活(非墓碑)且 `!isZeroSlotValue`"的 32 字节键才判 true | ✅ |
| `fetchAllStorage`(→ 建根 map) | :618-671 | 墓碑跳过(:636-637)+ 零值跳过(:665-666),两层**叠加** | ✅ |
| `accountStorageRoot` / `opStorageRoot` | StateRootCompute.cpp:22-30 / OpBlockSeal.cpp:19-25 | `is_zero → continue` | ✅(此时已是防御性冗余) |
| 写回不留零值行 | :441-478 | 零值 → `storage2::removeOne`,并在删后 `existsOne` 回读守护 | ✅ |

**与生产写路径的兼容性(批 9 论证的关键前提)已独立验证成立**:
`bcos-framework/bcos-framework/ledger/EVMAccount.h:213-220` 的 `setStorage` 写的是
`concepts::bytebuffer::toView(value.bytes)` —— **恒为 32 字节**,零值也写满 32 个 0。
因此生产账本里的零值槽行必然满足 `isZeroSlotValue` 的
"`size() == 32 && 全零`"判据(:585-589),会被三处读判据一致地跳过,不会毒旗。
**这条是批 9 方案 A 成立与否的支点,此前的记账里没有见到它被独立核实过。**

对应的 EIP-7610 语义也随之对齐:

```
我方:    Storage2Ledger.h:850-873 has_storage → state.cpp:259 `.has_initial_storage`
         → host.cpp:81-92 `is_create_collision`(nonce!=0 / codeHash!=EMPTY / has_initial_storage)。
op-geth: core/vm/evm.go 的 `create()` —— `GetNonce(address) != 0`
         || `contractHash != {} && != EmptyCodeHash`
         || `storageRoot != {} && != EmptyRootHash`。
         注意 op-geth 用的是**已提交的 storage root**,而 trie 天然不含零值槽 ——
         这正是批 9 把零值槽排除出 has_storage 之后两边等价的原因。
判定:    ✅ 一致(批 9 之前是分歧,现已消除)。
```

### 【分歧 6-1】stateRoot 收录**每一个**存活账户,不做 EIP-161 空账户过滤 ★★(生产接入才可达)

```
我方:    StateRootCompute.h:82-93 `stateRootOf<Ledger>` —— `visitAccounts` 回调里对**每个**
         账户无条件 `trie.insert(keccak256(addr), rlp(nonce, balance, storageRoot, codeHash))`,
         没有任何 "nonce==0 && balance==0 && codeHash==EMPTY 则跳过" 的判据;
         Storage2Ledger.h:680-720 `visitAccountsImpl` 的唯一过滤是"墓碑行"与"/apps/ 前缀"。
op-geth: 空账户不在 trie 里 —— `ApplyTransactionWithEVM` 每笔之后
         `evm.StateDB.Finalise(true)`(core/state_processor.go:184-186,
         `deleteEmptyObjects=true`),EIP-158/161 语义下被 touch 过的空账户当场删除;
         从未被 touch 的空账户在规范链上根本不可能存在于 trie 中。
触发输入:账本里存在一个 `nonce=0, balance=0, 无 code, 无非零槽` 的 `/apps/<addr>` 表标记行,
         而这一块的执行**没有 touch 它**(因此 evmone 的 `build_diff` 不会为它产出
         `deleted_accounts` 项,`sanitizeStateDiff` 也无从介入)。
分歧结果:stateRoot 直接不同 → 我方对每一个块都判 INVALID(payload 的 stateRoot 永远对不上)。
可达性:  **需生产账本**。在本仓的测试世界里,桥是唯一写者且从不留下空账户,所以 33 条金值
         向量结构上碰不到这条。接生产账本后必然可达:通用执行器
         (`transaction-executor` / `bcos-ledger` 创世 alloc)没有 EIP-158 空账户清除语义,
         `EVMAccount::create()` 建的标记行不会因"字段全默认"而被回收。
置信度:  已读两侧源码确认;"生产账本必然含空账户"依赖对通用执行器的推断,
         **未实测** `[需验证]`。
最小验证步骤:用 `MemoryLedger`/`Storage2Ledger` 手工塞一个只有 SYS_TABLES 标记行、
         无 BALANCE/NONCE/CODE_HASH 行的账户,调 `stateRootOf`,与不含该账户时的根比较 ——
         若不同,分歧成立。
```

### 【分歧 6-2】命中 `c_systemTxsAddress` 的地址会让节点**永久无法处理该块**(-32603)★★

```
我方:    Storage2Ledger.h:740-754 `accountTableName` —— 地址的 40 字符小写 hex 若命中
         `bcos::precompiled::c_systemTxsAddress` 集合,返回 false;
         读路四个方法据此 `poison(...)`(:127-131 等),写路
         `applyModifiedEntry`:409-413 / `applyDeletedEntry`:496-500 直接 `throw`。
         毒旗 → `OpSchedulerImpl.h:874-875` → `OpStorageError` → 引擎 `:1000-1007` 答 **-32603**。
         该集合(bcos-framework/bcos-framework/executor/PrecompiledTypeDef.h:143-149)含 8 个
         真实 20 字节地址:`0x…1000`(SYS_CONFIG)、`0x…1003`(CONSENSUS)、
         `0x…1005`(AUTH_MANAGER)、AUTH_COMMITTEE、WORKING_SEALER_MGR、SHARDING、
         `0x…10003`(ACCOUNT_MGR)、`0x…10004`(ACCOUNT)。
op-geth: 这 8 个地址在以太坊/OP 语义下是**完全普通的账户地址**(不是预编译,evmone 的预编译
         表也不含它们),可以收款、可以部署合约。
触发输入:一笔向 `0x0000000000000000000000000000000000001000` 转 1 wei 的普通 0x02 交易。
         `Host::call` 会 `get_or_insert` 该地址 → `build_diff` 产出 modified 项 →
         `applyModifiedEntry` 抛错 → 毒旗 → -32603。
分歧结果:op-geth VALID;我方既不是 VALID 也不是 INVALID,而是**内部错误**,且由于该块会被
         op-node 反复重投,节点在这条链上**永久卡死**。这是"拒绝合法块"的更坏形式 ——
         活性故障而非投票故障。
可达性:  当前可构造(只要放行普通交易类型;今天 0x02 就够)。
置信度:  已读两侧源码确认;"evmone 预编译表不含 0x1000" 未逐表核对 `[需验证]`
         (但即便含,行为也只会更奇怪而不会更一致)。
```

### 【分歧 6-3】`applyModifiedEntry` 对**每个** modified 项无条件 `create()` 账户表标记行

```
我方:    Storage2Ledger.h:421-422 —— `if (!co_await account.exists()) co_await account.create();`
         注释(:418-420)明说这是刻意的、"不得优化为'无字段可写则跳过'"。
op-geth: `Finalise(true)` 之后,一个被 touch 但仍为空的账户**不会**留在 trie 里。
分析:    两者并不直接冲突 —— evmone 的 `build_diff`(state.cpp:214-219)已经把
         `erase_if_empty && is_empty()` 的账户改投 `deleted_accounts`,所以正常路径上不会有
         "空的 modified 项"送进来。**但 `erase_if_empty` 不是所有插入路径都会置位**
         (account.hpp:71 默认 false;只有 `touch()`(state.cpp:291-294)、
         `access_account`(host.cpp:446)、7702 authority(state.cpp:122)会置)。
         任何以默认 `get_or_insert` 插入、最终仍为空的账户,会作为 modified 项落账,
         在我方账本里留下一行,并因分歧 6-1 进入 stateRoot。
触发输入:需要找到一条"以默认 `get_or_insert` 插入且最终为空"的路径。`opTransition.cpp:151`
         的 sender 与 `OpDepositTx.cpp:67` 的 `dep.from` 都属此类,但两者的 nonce 在所有分支
         上都会 +1,因而非空 —— **今天没有已知的可达触发输入**。
分歧结果:潜在的 stateRoot 分歧。
可达性:  需未来改动(新增一条默认 `get_or_insert` 且不 bump nonce 的路径即可达)。
置信度:  推理未验证。最小验证步骤:在 evmone 侧加断言"进入 applyModifiedEntry 的 entry
         不得是 EIP-161 空账户",跑全量 33 向量 + 250 用例看是否翻红。
```

### 本层已核对、判定为**一致**的项

| 项 | 我方 | op-geth | 结论 |
|---|---|---|---|
| 零值槽 ≡ 槽不存在(读侧三判据 + 建根 + 写回) | 见上表 | trie 删零值槽 | 一致(批 9 已修) |
| EIP-7610 CREATE 碰撞判据(nonce / codeHash / 初始 storage 三选一) | host.cpp:81-92 + `has_initial_storage` | core/vm/evm.go `create()` 三条件 | 一致 |
| EIP-6780:非同笔创建的 SELFDESTRUCT 只转余额、不销毁账户 | host.cpp:137-148 | geth `SelfDestruct6780` | 一致 |
| EIP-6780:同笔 create+selfdestruct 不留痕 | host.cpp:150-158 `destructed` → build_diff → `deleted_accounts` → `sanitizeStateDiff` 剥离(view 中不存在) | geth 同 | 一致(但见"文档失实"一节对 sanitize 注释的更正) |
| EIP-161 touch-delete(既存空账户被 touch 后删除) | state.cpp:214-219 + `sanitizeStateDiff` **保留** view 中存在者的删除项(StateDiffSanitize.h:10-14) | `Finalise(true)` | 一致 |
| 账户叶编码 `rlp(nonce, balance, storageRoot, codeHash)`,键 `keccak256(addr)` | StateRootCompute.h:87-89 | geth 的 `types.StateAccount` 同序 | 一致 |
| 槽叶编码 `rlp(trim(value))`,键 `keccak256(slot)` | StateRootCompute.cpp:27-28 | geth secure trie 同 | 一致 |
| "账户存在但字段全默认" ≠ `nullopt`(KEEP 契约) | Storage2Ledger.h:765-772(`existsOne` 判存在,再逐字段填默认) | geth `Exist()` vs `Empty()` 两分 | 一致 |
| code 读取:`CODE_HASH` → `SYS_CODE_BINARY`,无 code 时 `codeHash = keccak(空)` | :877-891 / :772 | geth `types.EmptyCodeHash` | 一致 |
| 块内逻辑删除的账户/槽不得"还魂"进 stateRoot | `liveContent` 墓碑判别,三处同源(:551-562、:636-637、:698-699、:864) | geth 无对应问题(内存 statedb) | 一致 |
| CREATE2 同址重生:删除账户后三张读缓存全量失效 | :538-541 | —— | 一致(桥内部正确性) |
| 系统调用 / 每笔 / finalize 的 diff **都**过 `sanitizeStateDiff` | OpBlockExecute.cpp:31、OpTransition.cpp:196、OpDepositTx.cpp:130、OpBlockFinalize.cpp:12 | —— | 一致(无遗漏) |

### 本层**未覆盖**的面

- `visitAccountsImpl` 只扫 `/apps/`(Storage2Ledger.h:682-694)。生产账本里 `/sys/` 下的
  FISCO 系统表**完全不进 stateRoot**。这在"OP 状态与 FISCO 系统状态分属两个命名空间"的
  前提下是自洽的,但该前提本身没有被任何检查守护 `[需验证]`。
- `evmone` 的 `MPT` 实现与 geth `StackTrie`/`SecureTrie` 的**逐字节等价性**未验证
  (33 条金值向量提供了间接证据,但语料里没有极端 trie 形状,例如超长公共前缀、
  17 分支满叉、单叶根)。
- 同笔内"既存余额账户被 CREATE 进去再 SELFDESTRUCT"的余额归属,两边未逐行比对。

<!-- LAYER-6-END -->

---

## 第 7 层:回执与根

我方:`OpReceiptEncode.cpp`(叶编码)、`OpBlockSeal.cpp`(receiptsRoot / logsBloom)、
`OpEngineSeam.h::computeOpTxRoot`(txRoot)。
op-geth:`core/types/receipt.go` `Receipts.EncodeIndex` / `depositReceiptRLP`、
`core/block_validator.go` `ValidateState`。

### 本层已核对、判定为**一致**的项

| 项 | 我方 | op-geth | 结论 |
|---|---|---|---|
| 普通交易回执叶 = `typeByte \|\| rlp([status, cumGas, bloom, logs])` | OpReceiptEncode.cpp:20-23 → `evmone::state::rlp_encode(receipt)` | receipt.go `EncodeIndex` 的 `AccessListTxType/DynamicFeeTxType/BlobTxType/SetCodeTxType` 分支 | 一致 |
| deposit 回执叶 = `0x7E \|\| rlp([status, cumGas, bloom, logs, depositNonce, depositReceiptVersion])` | OpReceiptEncode.cpp:14-17 | receipt.go `EncodeIndex` 的 `DepositTxType` + `DepositReceiptVersion != nil` 分支;`depositReceiptRLP` 结构体字段序逐一对应 | 一致 |
| `status` 编码:成功 `0x01`、失败空串 | `encode_tuple(bool)` | `r.statusEncoding()` 返回 `[]byte{0x01}` / `[]byte{}` | 一致 |
| `depositReceiptVersion` 恒为 1(Canyon 起) | OpDepositTx.cpp:131 `OpDepositReceipt{…, preNonce, 1}` | receipt.go / state_processor.go:217-224 `IsOptimismCanyon` → `CanyonDepositReceiptVersion` | 一致(Isthmus/Jovian 必然 post-Canyon) |
| `depositNonce` = **执行前**的 from nonce | OpDepositTx.cpp:68 `preNonce`(mint 之前读) | state_processor.go:173-176 —— `nonce = statedb.GetNonce(msg.From)`,位置在 `ApplyMessage` **之前** | 一致 |
| `cumulativeGasUsed` | OpBlockExecute.cpp:60/:87 | `gp.CumulativeUsed()` | 一致 |
| receiptsRoot:键 = `rlp(index)`,值 = 上述叶字节 | OpBlockSeal.cpp:39-42 | `types.DeriveSha(res.Receipts, trie.NewStackTrie(nil))` | 一致 |
| 块级 `logsBloom` = 各回执 bloom 的按位或 | OpBlockSeal.cpp:44-56 | `types.MergeBloom(res.Receipts)`(block_validator.go `ValidateState`) | 一致 |
| 空块 receiptsRoot | 空 MPT → `EMPTY_MPT_HASH` | `DeriveSha` 空列表 → 同值 | 一致(但空块在我方被第 4 层拒绝,见分歧 4-1) |
| txRoot:键 = `rlp(index)` | `computeOpTxRoot` | `DeriveSha(types.Transactions(txs))` | 键一致;**值不同**,见第 1 层的"关键事实" |

### 本层**未覆盖**的面

- `contractAddress` / `logs[].address` 等**不进 receiptsRoot** 的回执字段(它们只影响 RPC),
  未与 `mapOpReceipt`(`bcos-evm/bcos-evm/engine/OpReceiptMap.h`)逐字段比对。
- `evmone::state::compute_bloom_filter` 与 geth `types.CreateBloom` 的逐位等价性,
  只由 33 条金值向量间接见证,未做独立差分。

---

## 第 8 层:块后 finalize

### 本层已核对、判定为**一致**的项

| 项 | 我方 | op-geth | 结论 |
|---|---|---|---|
| 块尾 finalize 是**空操作**(无出块奖励、无叔块、提现列表为空) | OpBlockFinalize.cpp:12-13 → `evmone::state::finalize(view, rev, coinbase, std::nullopt, {}, {})`,state.cpp:534-559 在三者皆空时只返回一个空 diff | `p.chain.Engine().Finalize(...)`(beacon 引擎,合并后无奖励);OP Isthmus 的 withdrawals 恒空 | 一致 |
| 块尾 requests 系统调用不执行 | `disable_prague_requests = true`,`finalizeOpBlock` 对 false 直接 throw | `state_processor.go:141` `IsPrague && !IsIsthmus` | 一致 |
| Isthmus `withdrawalsRoot` = MessagePasser 的**存储根** | OpSchedulerImpl.h:879-891 取快照 → OpBlockSeal.cpp:60-64 `opStorageRoot` | block_validator.go:190-197 `statedb.GetStorageRoot(params.OptimismL2ToL1MessagePasser)` | 一致 |
| MessagePasser 地址 | OpPredeploys.h:31-32 `0x42..16` | `params.OptimismL2ToL1MessagePasser` 同值 | 一致 |
| 快照时机 = finalize **之后**、seal 之前 | OpSchedulerImpl.h:877-891(注释"桥销毁前算毕") | `ValidateState` 在 `Process` 全部完成后 | 一致 |

### 【分歧 8-1】MessagePasser 账户**不存在**时,两边给出不同的 withdrawalsRoot

```
我方:    OpSchedulerImpl.h:879-887 —— `messagePasserStorage` 初始为空 map,只有 visitAccounts
         命中 `OP_L2_TO_L1_MESSAGE_PASSER` 时才被填。账户不存在 ⇒ 保持空 ⇒
         OpBlockSeal.cpp:62 `opStorageRoot({})` = 空 MPT 根
         `0x56e81f171bcc55a6ff8345e692c0f86e5b48e01b996cadc001622fb5e363b421`。
op-geth: `statedb.GetStorageRoot(addr)` 对不存在的账户返回 **`common.Hash{}`(全零)**,
         不是 EmptyRootHash;随后 `*header.WithdrawalsHash != root` 判块无效。
触发输入:一条创世里没有部署 L2ToL1MessagePasser 的 OP 链(或该账户被 SELFDESTRUCT 清除)。
分歧结果:同一状态下,op-geth 期望 `0x00…00`,我方算出空 MPT 根 ⇒ 我方判 INVALID
         而 op-geth 判 VALID(如果 payload 给的是全零),或反之。
可达性:  需生产账本 / 需未来改动(规范 OP 链的创世必含该预部署)。
置信度:  已读两侧源码确认。
```

---

## 第 9 层:分叉门控

### 【分歧 9-1】`configAt` 把**所有** pre-Isthmus 时间戳解析成 Isthmus 配置

```
我方:    OpForkSchedule.cpp:102-110 —— `if (timestamp >= jovianTime) return jovianConfig();
         return isthmusConfig();`。低于 `isthmusTime` 的时间戳**也**落到 Isthmus。
         头文件注释(OpForkSchedule.h:51-59)自陈这是 decision A5 的刻意选择。
op-geth: 每个分叉独立判定(`IsEcotone`/`IsFjord`/`IsGranite`/`IsHolocene`/`IsIsthmus`/`IsJovian`),
         pre-Isthmus 的块用 Ecotone/Fjord/… 的规则执行(EVM revision、L1 费公式、
         operator fee 有无、回执版本全都不同)。
触发输入:任何 `timestamp < isthmusTime` 的块。
分歧结果:整块用错规则集 ⇒ stateRoot/receiptsRoot/gasUsed 全线分歧。
可达性:  当前可构造,但**被 C3(pre-Isthmus V3 闸)部分挡住** —— 引擎的
         `-38005` 闸(EngineServiceImpl.h:692-702)在 V4 上会拒绝 pre-Isthmus 时间戳。
         残余缺口是**第 2 层分歧 2-3 的首块豁免**:引导后的第一个块无父头可比,
         timestamp 可任意取,若走 V3 通道即可携带一个 pre-Isthmus 时间戳进来。
置信度:  已读两侧源码确认。
```

### 【观察 9-2】七个 `OpForkConfig` 里只有两个可达;`karstConfig()` 是**完全的死代码**

```
我方:    OpForkSchedule.h:8-17 定义了 7 个 OpFork 枚举值,:33-39 声明 7 个 config 工厂;
         但 `OpForkTimestamps`(:45-49)只有 `isthmusTime`/`jovianTime` 两个阈值,
         `configAt` 只可能返回 `isthmusConfig()` 或 `jovianConfig()`。
         `ecotoneConfig/fjordConfig/graniteConfig/holoceneConfig/karstConfig` 在生产路径上
         **一次也不会被调用**。
影响:    不是分歧,是**覆盖假象**:代码结构看上去支持 Ecotone→Karst 七个分叉,实际只有两个。
         对应地,第 5 层"未覆盖"里列出的 Ecotone/Bedrock L1 费公式也就永远不会被验证。
```

### 【复核 9-3】Karst 别名在**当前 pin 上**无害,但依赖一条会失效的前提

```
我方:    OpForkSchedule.cpp:92-100 —— `karstConfig()` 由 `jovianConfig()` 派生,只改 fork tag。
op-geth: 本 pin(e8800cffe)上 `IsKarst`(params/config.go:1024-1026)与
         `IsOptimismKarst`(:1087-1089)**没有任何执行侧调用方** —— 逐目录 grep
         (params/core/consensus/eth/miner/beacon)只命中声明、配置解析与启动横幅。
结论:    在本 pin 上"Karst ≡ Jovian"是**正确的**,不只是占位。
         但这条正确性完全依赖"op-geth 尚未给 Karst 任何行为"这一外部事实;
         op-geth 一旦给 Karst 加语义,我方的别名会**静默**给出旧行为(且 `configAt` 根本不返回它)。
         MEMORY 里"Karst 未适配(占位别名)"的记账保持有效,此处只是补上"当前无害"的证据。
```

### 本层已核对、判定为**一致**的项

| 项 | 我方 | op-geth | 结论 |
|---|---|---|---|
| 分叉激活比较是 `>=`(边界时间戳归**新**分叉) | `configAt`:107 `timestamp >= jovianTime`;`isIsthmusActiveAt`/`isJovianActiveAt`(OpSchedulerImpl.h:745-758)同 | `isTimestampForked(s, head) { return *s <= head }`(params/config.go:1516-1521) | 一致(逐字方向相同) |
| Isthmus ↔ EVM revision Prague | OpForkSchedule.cpp:62 `EVMC_PRAGUE` | `IsPrague` 对 OP 由 Isthmus 驱动 | 一致 |
| Jovian ↔ EVM revision Prague(不升级 revision) | OpForkSchedule.cpp:77 | Jovian 不改 EVM revision | 一致 |
| Jovian 起启用 DA footprint + operator-fee-fix 公式 | `has_da_footprint`/`has_jovian_operator_formula`(:81-82) | `CalcDAFootprint` 由 `IsJovian` 门控;`newOperatorCostFuncOperatorFeeFix` 由 `config.IsJovian(blockTime)` 选中(rollup_cost.go:236-239) | 一致 |
| Isthmus 起启用 operator fee | `has_operator_fee = true`(:65) | `NewOperatorCostFunc` 的 `!config.IsOptimismIsthmus(blockTime)` → 恒 0(rollup_cost.go:224-227) | 一致 |
| Isthmus 起 requests 全禁 | `disable_prague_requests` | `state_processor.go:141` `&& !config.IsIsthmus(...)` | 一致 |

<!-- LAYER-9-END -->
