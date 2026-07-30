# 视角 2 · 正确性:交易解码、区块执行与编解码

**先读** `.superpowers/sdd/2026-07-28-op-validator-minimal-loop/wb-review-context.md`。
**报告写** `.superpowers/sdd/2026-07-28-op-validator-minimal-loop/wb-review-p2-report.md`。

## 你的范围

- `bcos-evm/bcos-evm/engine/OpSchedulerImpl.h`(+937)—— 主战场
- `bcos-evm/bcos-evm/engine/OpReceiptMap.h`(+83)
- `bcos-codec/bcos-codec/rlp/EthBlockHeader.{h,cpp}`(+322)
- `bcos-codec/bcos-codec/rlp/OpDepositEncode.{h,cpp}`(+133)
- `bcos-evm/bcos-evm/opstack/OpForkSchedule.{h,cpp}`
- 参考(不在改动范围但是执行主体):`bcos-evm/bcos-evm/opstack/OpBlockExecute.cpp`

## 你的问题

你负责回答"**同一串字节,本实现解出来的交易/块头,和 op-geth 解出来的是不是同一个东西**"。

对照物 op-geth `e8800cffe`,重点:`core/types/transaction_marshalling.go` 与各 `tx_*.go` 的 `DecodeRLP`、`rlp/decode.go`(规范整数 `ErrCanonInt`、`errUintOverflow`、`ErrCanonSize`、bool 只认 0/1)、`core/types/deposit_tx.go`、`core/types/tx_setcode.go`、`core/state_processor.go`。

逐项核对:

1. **解码器严格性**:逐个交易类型(legacy / 0x01 / 0x02 / 0x03 / 0x04 setcode / 0x7E deposit)对照 op-geth 的 RLP 解码规则。**非规范编码(前导零、超长整数、非最小长度前缀、bool 非 0/1、多余尾随字节)必须被拒绝**——共识实现里"宽容解码"等价于共识分歧,因为同一笔交易会有两个哈希。逐条给出:本实现拒绝吗?op-geth 拒绝吗?
2. **签名校验**:低 s(EIP-2)、`r,s ∈ [1, n-1]`、`yParity ∈ {0,1}`、chainId 匹配(以及 legacy 的 EIP-155 与 pre-155 双形态)。注意本仓有过一处"同一规则在两个**复制粘贴**的地方各实现一遍"的历史(测了一处不能说明另一处)——查还有没有同类复制。
3. **`0x04` (EIP-7702) 与 `0x7E` (deposit) 的字段完整性**:`authorizationList` 的每个授权项(chainId / address / nonce / yParity / r / s)、deposit 的 `sourceHash` / `mint` / `isSystemTx` / `to == nil` 的语义。有没有字段被解码后丢弃、或用默认值静默填充?
4. **块头 21 字段的 encode/decode 往返**:`EthBlockHeader.cpp` 的 decode 是新加的(打通 `s_eth_block_header` 读路)。找**"读反 + 写反"互相抵消**的假绿可能:如果 encode 和 decode 用了同一个错误的字段顺序,往返测试照样绿而与 op-geth 不兼容。核对字段顺序与可选字段(`withdrawalsRoot` / `blobGasUsed` / `excessBlobGas` / `parentBeaconBlockRoot` / `requestsHash`)的分叉门控是否与 op-geth `core/types/block.go` 一致。
5. **`executeOpBlock` 的异常边界**:哪些步骤在 `try` 内、哪些在外。step 1 解码循环、step 5 `sealOpBlock`/`stateRootOf`、step 6 `computeOpTxRoot`/`mapOpReceipt` 各自抛出时会被谁接住、重分类成什么。
6. **gas 记账**:块 gas 池的窄化(`static_cast<int64_t>`)、`blockGasLeft` 递减、`cumulative_gas_used` 累加、以及 deposit 的 gas 语义(deposit 不付费但计入 gas used)是否与 op-geth `state_transition.go` 一致。溢出/负值路径。
7. **收据映射** `OpReceiptMap.h`:status/logs/bloom/`cumulativeGasUsed`/`depositNonce`/`depositReceiptVersion` 逐字段。deposit 收据在 Regolith 之后带 `depositNonce`,Canyon 之后带 `depositReceiptVersion`——本实现的分叉门控对不对?
8. **`OpForkSchedule`**:分叉激活的比较是 `>=` 还是 `>`,边界时间戳的归属。以及已记账的"Karst 只是 Jovian 的占位别名"这件事在代码里是否有防止被误当作"已适配"的护栏。

## 交付

按共享上下文的报告格式。**每条 Critical 给出:具体字节串或交易字段值 → 本实现的解码结果 → op-geth 的结果 → 哈希/状态分歧点。**
