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
