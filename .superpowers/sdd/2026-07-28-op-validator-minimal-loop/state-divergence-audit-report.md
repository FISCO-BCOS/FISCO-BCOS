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
