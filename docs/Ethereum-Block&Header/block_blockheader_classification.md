# FISCO-BCOS 中 Block / BlockHeader 相关代码分类

## 0. Ethereum 标准区块结构与 FISCO-BCOS 当前结构的主要区别

这一节先给出一个总览，后面再进入 FISCO 代码分类。

这里的“Ethereum 标准执行节点区块结构”，指的是 geth / op-geth 这类执行层客户端使用的标准 Ethereum `Header` / `Block` 结构。对 OP Stack 来说，`op-geth` 在执行层区块对象上本质延续的是 Ethereum 标准 block/header 形态。

本节以下对照，直接基于你放到文档目录里的 go-ethereum 实现代码：

- `/Users/wushichen/FISCO-BCOS/docs/Ethereum-Block&Header/block.go`

以及 FISCO-BCOS 当前结构定义：

- `/Users/wushichen/FISCO-BCOS/bcos-framework/bcos-framework/protocol/Block.h`
- `/Users/wushichen/FISCO-BCOS/bcos-framework/bcos-framework/protocol/BlockHeader.h`
- `/Users/wushichen/FISCO-BCOS/bcos-tars-protocol/bcos-tars-protocol/tars/Block.tars`

### 0.1 区块头字段层面的主要区别

FISCO-BCOS 当前 `BlockHeader` 的核心字段包括：

- `version`
- `parentInfo`
- `txsRoot`
- `receiptsRoot`
- `stateRoot`
- `blockNumber`
- `gasUsed`
- `timestamp`
- `sealer`
- `sealerList`
- `extraData`
- `consensusWeights`
- `signatureList`

而你放入的 geth `Header` 实际字段包括：

- `parentHash`
- `sha3Uncles`
- `miner` / `coinbase`
- `stateRoot`
- `transactionsRoot`
- `receiptsRoot`
- `logsBloom`
- `difficulty`
- `number`
- `gasLimit`
- `gasUsed`
- `timestamp`
- `extraData`
- `mixHash`
- `nonce`
- `baseFeePerGas`
- `withdrawalsRoot`
- `blobGasUsed`
- `excessBlobGas`
- `parentBeaconBlockRoot`
 - `requestsHash`
 - `blockAccessListHash`
 - `slotNumber`

如果按“结构与语义差异”来归纳，最关键的是：

1. `父块表达方式不同`
   - FISCO 用 `parentInfo`，是一个列表结构。
   - Ethereum 标准 header 用单一 `parentHash`。
   - 对 OP Stack / Ethereum 执行层来说，标准语义是单父块 hash。

2. `共识相关字段完全不同`
   - FISCO header 内直接带：
     - `sealer`
     - `sealerList`
     - `consensusWeights`
     - `signatureList`
   - Ethereum 标准 header 不包含这套 PBFT/联盟链式的验证人列表与签名列表字段。
   - 也就是说，FISCO 当前 header 里有一块明显是“内部共识元数据”，而不是标准 Ethereum block header 字段。

3. `Ethereum 需要的若干执行层标准字段，FISCO 当前没有`
   - FISCO 当前 header 没有标准的：
     - `sha3Uncles`
     - `logsBloom` 作为 header 字段
     - `difficulty`
     - `gasLimit`
     - `mixHash`
     - `nonce`
     - `baseFeePerGas`
     - `withdrawalsRoot`
      - `blobGasUsed`
      - `excessBlobGas`
      - `parentBeaconBlockRoot`
      - `requestsHash`
      - `blockAccessListHash`
      - `slotNumber`
   - 这些字段里，有些可以固定值/空值兼容，有些则需要真正持久化或引入新的来源。

4. `logsBloom 的位置不同`
   - 在 Ethereum 标准结构里，`logsBloom` 属于 header 字段。
   - 在 FISCO 当前抽象里，`logsBloom` 挂在 `Block` 上，而不是 `BlockHeader` 上。
   - 这是一个很实际的结构差异，因为标准 block hash 是否包含 bloom，要看它是否属于 header 编码的一部分。

5. `时间字段单位不同`
   - FISCO 当前 `timestamp` 是 `int64`，现有 RPC 代码里按毫秒处理，再除以 `1000` 输出秒。
   - geth `Header.Time` 是 `uint64` 秒级时间戳。
   - 因此 `timestamp` 不是直接拷贝，而是要做单位转换。

6. `gas 语义不完整`
   - FISCO 当前 header 有 `gasUsed`，但没有标准 Ethereum header 的 `gasLimit`。
   - 如果要输出标准 Ethereum block/header，就需要补齐 `gasLimit` 的来源与持久化策略。

7. `区块哈希计算输入不同`
   - FISCO header hash 是基于它自己的 header 数据结构计算的。
   - geth `Header.Hash()` 是对标准 Ethereum header 的 RLP 编码做 `keccak256`。
   - 因为字段集合和编码方式不同，所以两者的 block hash 不可能天然相同。

### 0.2 区块体层面的主要区别

它当前主要包含：

- `blockHeader`
- `transactions`
- `receipts`
- `transactionsMetaData`
- `receiptsHash`
- `nonceList`
- `transactionsMerkle`
- `receiptsMerkle`
- `logsBloom`

而 geth `Body` / `Block` 的主体是：

- `header`
- `transactions`
- `uncles`
- `withdrawals`

这里最关键的差异有：

1. `FISCO block 内直接包含 receipts，Ethereum block 不直接包含 receipts`
   - Ethereum 标准里，receipt 通常是链上派生对象，不是 block body 的一部分。
   - FISCO 当前 `Block` 抽象中，receipt 是 block 对象的一部分，并且很多内部逻辑依赖这一点。

2. `FISCO block 内含 transaction metadata / nonceList / merkle cache 等辅助数据`
   - `transactionsMetaData`
   - `nonceList`
   - `transactionsMerkle`
   - `receiptsMerkle`
   - 这些都不是标准 Ethereum block body 的核心组成部分。

3. `Ethereum block 需要 uncle / withdrawals 语义，FISCO 当前没有对应原生结构`
   - FISCO 当前没有标准的 `uncles` 列表结构
   - 也没有标准的 `withdrawals` 列表结构
   - 即便 RPC 层当前可以返回空 `uncles` / 空 `withdrawals`，从结构设计上看依然是缺位的

### 0.3 按字段分类：哪些可以直接映射，哪些可以填空，哪些目前拿不到

这一小节只回答“从 FISCO 当前区块数据出发，能不能构造出这些 Ethereum 标准字段”。

这里把字段分成三类：

1. `可以直接复制，或者稍作转换即可得到`
2. `可以先填空值、零值、固定常量`
3. `从现代 Ethereum / OP Stack 语义上看是需要的，但目前 FISCO 区块里没有现成来源`

#### 0.3.1 可以直接复制，或者稍作转换即可得到

- `parentHash`
  - 可从 `parentInfo[0].blockHash` 取出
  - 前提是按 FISCO 当前单父块语义处理

- `stateRoot`
  - 可直接用 FISCO 的 `stateRoot`
  - 你已经说明这部分计算由其他人处理，可视为两边一致

- `transactionsRoot`
  - 可直接用 FISCO 的 `txsRoot`
  - 但要注意：这代表“当前 FISCO 已算出的交易根”，并不自动等价于标准 Ethereum trie root 语义

- `receiptsRoot`
  - 可直接用 FISCO 的 `receiptsRoot`
  - 同样要区分“值可拿到”和“是否完全满足 Ethereum 标准计算语义”是两件事

- `logsBloom`
  - FISCO 当前在 `Block` 上已有 `logsBloom`
  - 需要做的不是“重新计算”，而是把它移动到 Ethereum header 语义里

- `number`
  - 可直接用 `blockNumber`

- `gasUsed`
  - 可直接用 `gasUsed`

- `timestamp`
  - 可从 FISCO `timestamp` 得到
  - 但需要从毫秒转换为秒，且类型从 `int64` 转为 `uint64`

- `extraData`
  - 可直接用 `extraData`

- `miner` / `coinbase`
  - 可由 `sealerList[sealer]` 进一步推导
  - FISCO 当前 RPC 已经有这段逻辑：公钥 -> keccak -> address
  - 所以它不是 header 里直接存好的字段，但“稍作转换即可得到”

- `transactions`
  - block body 中的交易列表可直接复用

#### 0.3.2 可以先填空值、零值、固定常量

下面这些字段，按 geth 当前结构，技术上可以先用空值/零值/固定常量占位：

- `sha3Uncles`
  - 如果 FISCO 没有 uncle 机制，可固定为“空 uncle 列表的哈希”

- `uncles`
  - 可直接返回空数组

- `withdrawals`
  - 如果暂不支持提现列表，可返回空数组

- `withdrawalsRoot`
  - 如果 `withdrawals` 固定为空，可填空 withdrawals root
  - 也可以在不启用该语义时保持 `nil`

- `difficulty`
  - FISCO 没有 PoW/PoS difficulty 语义
  - 若目标只是形成结构并对外返回，可先填 `0`

- `mixHash`
  - 可先填零值

- `nonce`
  - 可先填零值

- `blobGasUsed`
  - 若没有 blob transaction，可先填 `0` 或 `nil`

- `excessBlobGas`
  - 若没有 blob transaction，可先填 `0` 或 `nil`

- `parentBeaconBlockRoot`
  - 若没有 beacon root 来源，可先填 `nil` 或零值

- `requestsHash`
  - 若当前完全不支持该语义，可先填 `nil`

- `blockAccessListHash`
  - 若当前完全不支持该语义，可先填 `nil`

- `slotNumber`
  - 若当前完全不支持该语义，可先填 `nil`

这类字段的共同特点是：

- `从结构上能占位`
- `从 RPC 返回上能先兼容`
- `但不代表已经满足现代 Ethereum / OP Stack 的完整执行语义`

#### 0.3.3 从现代 Ethereum / OP Stack 语义上看需要，但目前 FISCO 区块里没有现成来源

这一类字段不是说“完全没法先塞个值”，而是说：

- 如果你希望最终形成稳定、可持续的 Ethereum 标准区块语义
- 或希望它真正参与标准 header hash / payload 语义
- 那么当前 FISCO block/header 里并没有天然来源，需要新增字段、配置来源、或新增计算逻辑

最典型的是：

- `gasLimit`
  - 当前 FISCO block/header 中没有这个字段
  - 现有 RPC 里是直接写死 `30000000`
  - 如果后面要形成稳定的标准 Ethereum header，这个字段最好不要继续只靠常量硬编码

- `baseFeePerGas`
  - 当前 FISCO block/header 中没有 fee market 对应字段
  - 从 geth 结构看它是可选指针，但从现代 Ethereum / OP Stack 语义上看，它通常不是一个可以长期缺失的字段
  - 这类字段需要明确新来源，而不是单纯从 FISCO block 里映射

- `withdrawalsRoot`
  - 如果只是“空 withdrawals”，可以给空根
  - 但如果后面真的要支持标准提现语义，那么 FISCO 当前区块里并没有原生 withdrawals 数据来源

- `parentBeaconBlockRoot`
  - 当前 FISCO 没有 beacon 链上下文
  - 若未来某些 fork/接口真的要求它语义成立，则需要新增来源

- `requestsHash`
  - FISCO 当前没有对应请求集合语义

- `blockAccessListHash`
  - FISCO 当前没有对应块级 access list 语义

- `slotNumber`
  - FISCO 当前没有对应 slot 概念

如果把问题进一步收窄到“你现在要做的三期目标”，那么最值得重点关注的是两类：

1. `gasLimit`
   - 因为它是 header 核心字段，而且当前 FISCO block 里没有

2. `baseFeePerGas`
   - 因为它在现代 Ethereum / OP Stack 执行层里非常敏感
   - 就算结构上能先空着，后面通常也绕不过去

### 0.4 对后续改造最重要的结论

如果把上面的字段分类直接用于你的三期改造，那么可以得出下面几个很实际的结论：

1. `第一批最容易落地的标准字段`
   - `parentHash`
   - `stateRoot`
   - `transactionsRoot`
   - `receiptsRoot`
   - `logsBloom`
   - `number`
   - `gasUsed`
   - `timestamp`
   - `extraData`
   - `miner`

2. `第二批可以先占位的字段`
   - `sha3Uncles`
   - `difficulty`
   - `mixHash`
   - `nonce`
   - `withdrawalsRoot`
   - `blobGasUsed`
   - `excessBlobGas`
   - `parentBeaconBlockRoot`
   - `requestsHash`
   - `blockAccessListHash`
   - `slotNumber`

3. `真正需要额外设计来源的字段`
   - `gasLimit`
   - `baseFeePerGas`
   - 以及未来若要支持真实提现语义时的 `withdrawals` / `withdrawalsRoot`

### 0.5 EthHeader 的标准 RLP 编码顺序，以及当前字段处理策略

这一节直接面向后续第一期/第二期实现：

- 如果要得到标准 Ethereum `block hash`
- 或者要让标准 `EthHeader` 真正具备可持续的对外语义

那么必须对 `EthHeader` 按标准顺序做 RLP 编码，再做 `keccak256`。

#### 0.5.1 复用现有 RLP 编码实现

这部分不建议重新实现一套新的 RLP 库，FISCO-BCOS 仓库里已经有现成实现，可以直接复用：

- `/Users/wushichen/FISCO-BCOS/bcos-codec/bcos-codec/rlp/RLPEncode.h`
- `/Users/wushichen/FISCO-BCOS/bcos-codec/bcos-codec/rlp/RLPDecode.h`
- `/Users/wushichen/FISCO-BCOS/bcos-codec/bcos-codec/rlp/Common.h`

现有代码里已经在使用这套 RLP：

- Web3 交易 RLP 编码
  - `/Users/wushichen/FISCO-BCOS/bcos-rpc/bcos-rpc/web3jsonrpc/model/Web3Transaction.cpp`
- MPT 节点 RLP 编码
  - `/Users/wushichen/FISCO-BCOS/bcos-ledger/bcos-ledger/mpt/NodeEncoder.cpp`
- 其他 Ethereum 兼容相关逻辑
  - `/Users/wushichen/FISCO-BCOS/bcos-crypto/bcos-crypto/ChecksumAddress.cpp`

所以后续实现 `EthHeader` 编码时，推荐做法是：

- 直接复用 `bcos::codec::rlp::encode(...)`
- 新增一个专门的 `encodeEthHeaderForHash(...)`
- 不要再引入新的第三方 RLP 库

#### 0.5.2 标准 EthHeader 的 RLP 字段顺序

基于你放入的 geth `block.go`，`Header.Hash()` 的输入是标准 Ethereum header 的 RLP 编码。

对你当前这条改造路线，建议 `EthHeader` 在 RLP 编码时严格按下面顺序输出字段：

1. `parentHash`
2. `sha3Uncles`
3. `miner` / `coinbase`
4. `stateRoot`
5. `transactionsRoot`
6. `receiptsRoot`
7. `logsBloom`
8. `difficulty`
9. `number`
10. `gasLimit`
11. `gasUsed`
12. `timestamp`
13. `extraData`
14. `mixHash`
15. `nonce`
16. `baseFeePerGas`
17. `withdrawalsRoot`
18. `blobGasUsed`
19. `excessBlobGas`
20. `parentBeaconBlockRoot`
21. `requestsHash`
22. `blockAccessListHash`
23. `slotNumber`

这里的核心原则是：

- 字段顺序不能按 FISCO 自己习惯调整
- 必须和目标标准 header 的字段顺序完全一致
- 哪怕某些字段是占位值，它们一旦被纳入当前 header 版本，也必须在正确位置参与 RLP

#### 0.5.3 当前阶段各字段在 RLP 中的处理建议

下面这张表更适合直接指导后续实现：

| 字段 | 当前建议 | 说明 |
|---|---|---|
| `parentHash` | 必须真实编码 | 直接来自 `parentInfo[0].blockHash` |
| `sha3Uncles` | 可先固定为空 uncle hash | 即 `keccak256(RLP([]))` |
| `miner` / `coinbase` | 建议真实编码 | 由 `sealerList[sealer]` 推导地址 |
| `stateRoot` | 必须真实编码 | 可直接复用现有值 |
| `transactionsRoot` | 必须真实编码 | 当前阶段可直接复用现有 `txsRoot` |
| `receiptsRoot` | 必须真实编码 | 当前阶段可直接复用现有 `receiptsRoot` |
| `logsBloom` | 建议真实编码 | 现有值在 `Block` 上，需迁移到 `EthHeader` 视角 |
| `difficulty` | 可先编码为 `0` | 当前无真实 PoW/PoS difficulty 来源 |
| `number` | 必须真实编码 | 直接复用 block number |
| `gasLimit` | 必须真实编码 | 当前没有天然来源，需要新增设计 |
| `gasUsed` | 必须真实编码 | 直接复用现有值 |
| `timestamp` | 必须真实编码 | 从毫秒转秒后编码 |
| `extraData` | 必须真实编码 | 直接复用现有值 |
| `mixHash` | 可先编码零值 | 当前无真实来源 |
| `nonce` | 可先编码零值 | 当前无真实来源 |
| `baseFeePerGas` | 建议尽快给出真实来源 | 现代 Ethereum / OP Stack 语义里较关键 |
| `withdrawalsRoot` | 可先编码空 root / nil 策略 | 当前若 `withdrawals=[]` 可先占位 |
| `blobGasUsed` | 可先编码 `0` / nil | 若不支持 blob 交易 |
| `excessBlobGas` | 可先编码 `0` / nil | 若不支持 blob 交易 |
| `parentBeaconBlockRoot` | 可先编码 nil / 零值 | 当前无 beacon 来源 |
| `requestsHash` | 可先编码 nil | 当前无对应语义 |
| `blockAccessListHash` | 可先编码 nil | 当前无对应语义 |
| `slotNumber` | 可先编码 nil | 当前无对应语义 |

#### 0.5.4 当前阶段“必须有真实来源”的最小字段集合

如果目标只是先把第一期/第二期推进下去，那么在 RLP 编码这一层，最小必须真实化的字段集合可以先收敛成：

- `parentHash`
- `stateRoot`
- `transactionsRoot`
- `receiptsRoot`
- `logsBloom`
- `number`
- `gasLimit`
- `gasUsed`
- `timestamp`
- `extraData`
- `miner`

其中最值得提前拍板的，是：

1. `gasLimit`
   - 因为它一定参与标准 header 编码
   - 但当前 FISCO 区块里没有天然来源

2. `baseFeePerGas`
   - 如果短期允许占位，可以先给固定值
   - 但从现代 Ethereum / OP Stack 语义看，后面通常还是要补真实来源

#### 0.5.5 实现建议

在具体编码实现上，建议不要做“自动反射式通用编码器”，而是直接手工实现一个专门函数，例如：

- `encodeEthHeaderForHash(const EthHeader& header) -> bytes`

函数内部：

1. 按标准顺序逐项准备字段
2. 复用 `bcos::codec::rlp::encode(...)`
3. 得到 RLP 字节串
4. 再做 `keccak256`

这样做的好处是：

- 和 geth 字段顺序对照最直接
- 方便调试每一个字段的实际编码内容
- 便于后续逐步从“占位字段”切换到“真实字段”

### 0.6 存储与读取模型上的主要区别

除了字段本身不一样，两者在“区块对象如何被系统使用”上也不同：

1. `FISCO 的 block 更像内部工作对象`
   - 它同时承载：
     - header
     - transactions
     - receipts
     - metadata
     - nonceList
     - logsBloom
   - 既服务执行，也服务存储拆分和读取重建。

2. `Ethereum 标准 block 更像对外共识/执行层标准对象`
   - header/body 边界更清晰。
   - header 决定 block hash。
   - receipt 不直接作为 block body 成员。

3. `FISCO 当前持久化的是“分表拆解后的逻辑区块”，不是一份标准 Ethereum block`
   - 所以后续如果要引入 Ethereum 标准区块，不能只改 RPC 输出，还要决定：
     - 哪些标准字段单独存
     - 哪些可以从现有 FISCO 数据映射
     - 哪些索引要重新按 Ethereum block hash 建立

如果只看结构差异，后续三期方案里真正绕不开的是下面四件事：

1. `必须新增一套标准 Ethereum header hash`
   - 因为 FISCO header hash 和 Ethereum header hash 的输入字段不同。

2. `必须新增一套标准 Ethereum header 持久化`
   - 因为 FISCO 当前 header 字段集合本身不等于标准 Ethereum header。

3. `交易和回执大概率可以共用，但 block/header 索引不能共用`
   - `txHash -> tx`
   - `txHash -> receipt`
   - 这两类数据通常可以共用。
   - 但 `blockNumber <-> blockHash`、`blockNumber -> header` 最好拆成 FISCO / Ethereum 两套。

4. `RPC 层如果只做“动态转换”，短期可行，长期会越来越难维护`
   - 因为标准 block hash、header 字段、日志 bloom、withdrawals 等语义，最终都要求有一套稳定的标准区块视图作为数据源。

本文档将 FISCO-BCOS 中与 `Block` / `BlockHeader` 相关的代码，按以下四类进行整理：

1. 基础数据类及其相关操作方法
2. 产生区块的相关链路
3. 存储区块的相关链路
4. 区块读取的相关链路

目的不是穷举所有零散引用点，而是先建立一份“主设计链路图”，方便后续评估如何将 FISCO-BCOS 改造成兼容 OP Stack / Ethereum block 结构的执行层。

## 1. 基础数据类及其相关操作方法

这一类代码负责定义 `Block` / `BlockHeader` 的抽象接口、默认实现、序列化格式、工厂创建方式，以及围绕区块头的通用复制和计算逻辑。

### 1.1 抽象接口定义

- `/Users/wushichen/FISCO-BCOS/bcos-framework/bcos-framework/protocol/Block.h`
  - `Block` 抽象接口定义。
  - 定义了区块的核心能力：`encode/decode`、`calculateTransactionRoot()`、`calculateReceiptRoot()`、`blockHeader()`、交易/回执/metadata 的 append/set/get、`nonceList()`、`logsBloom()` 等。
  - 从架构上看，`Block` 不只是“数据结构”，还是“区块对象操作接口”。

- `/Users/wushichen/FISCO-BCOS/bcos-framework/bcos-framework/protocol/BlockHeader.h`
  - `BlockHeader` 抽象接口定义。
  - 定义了 header 字段访问与修改接口：`hash()`、`calculateHash()`、`parentInfo()`、`txsRoot()`、`receiptsRoot()`、`stateRoot()`、`number()`、`gasUsed()`、`timestamp()`、`sealer()`、`sealerList()`、`signatureList()`、`consensusWeights()`、`extraData()` 等。
  - 也定义了若干通用逻辑，如：
    - `populateFromParents()`
    - `verifySignatureList()`
    - `populateEmptyBlock()`

### 1.2 工厂接口与复制逻辑

- `/Users/wushichen/FISCO-BCOS/bcos-framework/bcos-framework/protocol/BlockFactory.h`
  - `Block` 工厂接口。
  - 负责创建空 block，或从编码数据创建 block。

- `/Users/wushichen/FISCO-BCOS/bcos-framework/bcos-framework/protocol/BlockHeaderFactory.h`
  - `BlockHeader` 工厂接口。
  - 负责创建空 header、从编码数据创建 header、按 block number 创建 header。
  - 其中 `populateBlockHeader()` 很关键，它定义了“如何复制一个 header”：
    - `version`
    - `txsRoot`
    - `receiptsRoot`
    - `stateRoot`
    - `number`
    - `gasUsed`
    - `timestamp`
    - `sealer`
    - `sealerList`
    - `signatureList`
    - `consensusWeights`
    - `parentInfo`
    - `extraData`

### 1.3 Tars 序列化结构定义

- `/Users/wushichen/FISCO-BCOS/bcos-tars-protocol/bcos-tars-protocol/tars/Block.tars`
  - 定义了 Tars 层面的 `BlockHeaderData`、`BlockHeader`、`Block`。
  - 当前 FISCO 的 block/header 持久化与跨模块编解码，默认都是建立在这套 Tars 结构之上。
  - 其中可直接看到 FISCO block/header 当前包含的核心字段：
    - `parentInfo`
    - `txsRoot`
    - `receiptRoot`
    - `stateRoot`
    - `blockNumber`
    - `gasUsed`
    - `timestamp`
    - `sealer`
    - `sealerList`
    - `extraData`
    - `consensusWeights`
    - `signatureList`
    - `nonceList`
    - `logsBloom`

### 1.4 默认实现类

- `/Users/wushichen/FISCO-BCOS/bcos-tars-protocol/bcos-tars-protocol/protocol/BlockImpl.h`
  - `Block` 的默认实现类。
  - 对应 Tars `Block` 结构，负责：
    - block 编解码
    - 获取/设置 header
    - 交易、回执、metadata 的组织
    - 计算交易根、回执根
    - `nonceList`、`logsBloom` 等字段操作

- `/Users/wushichen/FISCO-BCOS/bcos-tars-protocol/bcos-tars-protocol/protocol/BlockHeaderImpl.h`
  - `BlockHeader` 的默认实现类。
  - 对应 Tars `BlockHeader` 结构，负责：
    - header 编解码
    - 访问与设置 header 字段
    - `calculateHash()`
    - 字段变化时清理 `dataHash`

### 1.5 默认工厂实现

- `/Users/wushichen/FISCO-BCOS/bcos-tars-protocol/bcos-tars-protocol/protocol/BlockFactoryImpl.cpp`
  - `BlockFactory` 默认实现。
  - 支持：
    - 创建空 block
    - 从编码数据解码 block
    - 如果 header 里缺少 hash，则补算 hash

- `/Users/wushichen/FISCO-BCOS/bcos-tars-protocol/bcos-tars-protocol/protocol/BlockHeaderFactoryImpl.cpp`
  - `BlockHeaderFactory` 默认实现。
  - 支持：
    - 创建空 header
    - 从编码数据解码 header
    - 如果 `dataHash` 为空，则重新计算 header hash

### 1.6 协议初始化注入

- `/Users/wushichen/FISCO-BCOS/libinitializer/ProtocolInitializer.cpp`
  - 在节点初始化时装配 `BlockFactory`、`BlockHeaderFactory` 等协议对象。
  - 这是 block/header 默认实现真正被系统其他模块注入使用的入口。

## 2. 产生区块的相关链路

这一类代码关注的是：什么时候创建 block、什么时候创建 blockHeader、什么时候补齐 roots/hash、什么时候形成最终可提交的 header。

整体上，主链路可以概括为：

`Sealer 触发出块` -> `SealingManager 创建 block/header 草稿` -> `Scheduler / BlockExecutive 执行交易` -> `生成最终执行后 header` -> `提交给后续 commit / ledger`

### 2.1 Sealer 触发生成 proposal

- `/Users/wushichen/FISCO-BCOS/bcos-sealer/bcos-sealer/Sealer.cpp`
  - `Sealer` 周期性尝试发起 seal 流程。
  - 会调用 `SealingManager::generateProposal()` 生成待执行区块提案。

### 2.2 创建 block 和 blockHeader 的第一现场

- `/Users/wushichen/FISCO-BCOS/bcos-sealer/bcos-sealer/SealingManager.cpp`
  - `generateProposal()` 是区块对象真正被创建的关键入口。
  - 在这里：
    - `blockFactory()->createBlock()` 创建 block
    - `blockHeaderFactory()->createBlockHeader()` 创建 header
    - 设置：
      - `number`
      - `timestamp`
    - 调用 `calculateHash()` 先形成一个初始 header hash
    - 将 header 挂到 block 上
    - 然后向 block 中填充交易 metadata / 系统交易
  - 这里得到的 block 还不是“最终执行完成后的区块”，更像是待执行 proposal。

### 2.3 调度器中的临时 block 构造

- `/Users/wushichen/FISCO-BCOS/bcos-scheduler/src/SchedulerImpl.cpp`
  - 在 `call()` 这类只读执行/模拟执行场景中，也会临时构造一个 block：
    - `createBlock()`
    - 设置 `blockHeader()->setNumber()`
    - `calculateHash()`
    - `appendTransaction()`
    - 设置 `version`
  - 这说明 block/header 不只用于正式出块，也用于执行环境中的临时上下文。

### 2.4 执行完成后生成最终 header

- `/Users/wushichen/FISCO-BCOS/bcos-scheduler/src/BlockExecutive.cpp`
  - 这是区块“执行后定稿”的关键位置。
  - 在执行结束后：
    - 用 `populateBlockHeader(m_block->blockHeader())` 复制原始 header
    - 设置执行结果：
      - `stateRoot`
      - `gasUsed`
      - `txsRoot`
      - `receiptsRoot`
    - 再次 `calculateHash()`
  - 这里产生的是最终可提交的 `executedBlockHeader`。
  - 因此如果你未来要引入 Ethereum 风格 header，这里是最关键的改造点之一。

### 2.5 提交区块头进入 commit 流程

- `/Users/wushichen/FISCO-BCOS/bcos-scheduler/src/SchedulerManager.cpp`
  - `commitBlock()` 接收执行完成后的 header，继续推动后续提交流程。

- `/Users/wushichen/FISCO-BCOS/bcos-scheduler/src/SchedulerImpl.cpp`
  - 负责实际的 commit 调度控制。
  - 这条链路说明：在 FISCO 中，最终提交时，`header` 本身是主线对象之一，而不只是 block 附属字段。

### 2.6 初始化阶段也会构造 block/header

- `/Users/wushichen/FISCO-BCOS/libinitializer/Initializer.cpp`
  - 系统合约初始化时，也会手工创建 block/header，执行并提交。
  - 这属于 bootstrap 出块链路，不属于日常 sealer 出块，但同样依赖 block/header 结构。

## 3. 存储区块的相关链路

这一类代码关注：执行完成后的区块是如何被拆解、如何映射成多张表、如何分别保存 header、tx hashes、交易、回执、nonce 等数据。

整体上，FISCO 不是把“完整 block blob”直接塞进一个表，而是拆分存储。

### 3.1 Ledger 存储主入口

- `/Users/wushichen/FISCO-BCOS/bcos-ledger/bcos-ledger/Ledger.cpp`
  - `asyncPrewriteBlock()` 是区块元数据预写入的核心入口。
  - `storeTransactionsAndReceipts()` 是交易和回执落库的重要入口。
  - `asyncPreStoreBlockTxs()` 负责预存储尚未写入后端的交易。

### 3.2 存储编排包装层

- `/Users/wushichen/FISCO-BCOS/bcos-ledger/bcos-ledger/LedgerMethods.h`
  - 定义了协程风格的存储入口：
    - `prewriteBlock()`
    - `prewriteBlockToBuffer()`
    - `storeTransactionsAndReceipts()`
  - 这里把 ledger 的 callback 接口封装成统一任务接口，方便其他模块调用。

- `/Users/wushichen/FISCO-BCOS/bcos-ledger/bcos-ledger/LedgerMethods.cpp`
  - 是上述协程包装接口的实现。

### 3.3 Block 被拆成哪些内容写入 DB

在 `/Users/wushichen/FISCO-BCOS/bcos-ledger/bcos-ledger/Ledger.cpp` 的 `asyncPrewriteBlock()` 中，block/header 会被拆成多份数据分别写表：

- `SYS_NUMBER_2_HASH`
  - `blockNumber -> blockHash`

- `SYS_HASH_2_NUMBER`
  - `blockHash -> blockNumber`

- `SYS_NUMBER_2_BLOCK_HEADER`
  - `blockNumber -> encoded blockHeader`

- `SYS_BLOCK_NUMBER_2_NONCES`
  - `blockNumber -> nonceList`
  - 这里会额外构造一个只装 `nonceList` 的临时 block，然后 encode 后写入

- `SYS_CURRENT_STATE`
  - 更新当前最新块高、总交易数、失败交易数等状态

- `SYS_NUMBER_2_TXS`
  - `blockNumber -> 交易 metadata/hash 列表`
  - 这里不会直接把完整交易列表嵌进主 block 保存，而是构造一个只带 `TransactionMetaData` 的 block 再编码入库

- `SYS_HASH_2_RECEIPT`
  - `txHash -> encoded receipt`

- `SYS_HASH_2_TX`
  - `txHash -> encoded transaction`

### 3.4 为什么说它不是“存整块”

从存储设计上看，FISCO 的 block 数据被拆成了几部分：

1. `header`
2. `number/hash` 双向索引
3. `交易 hash / metadata 列表`
4. `完整交易内容`
5. `回执`
6. `nonceList`

也就是说，block 在库里的存在形式更像“多表拼出来的逻辑区块”，而不是单表完整对象。

### 3.5 Scheduler 到 Ledger 的落库调用链

- `/Users/wushichen/FISCO-BCOS/bcos-scheduler/src/BlockExecutive.cpp`
  - 在 commit 阶段调用 ledger 的预写入与交易/回执存储接口。

- `/Users/wushichen/FISCO-BCOS/bcos-scheduler/src/SchedulerImpl.cpp`
  - 负责组织 commit 时机。

- `/Users/wushichen/FISCO-BCOS/bcos-scheduler/src/SchedulerManager.cpp`
  - 负责将执行后 header 推进到 commit 流程。

## 4. 区块读取的相关链路

这一类代码关注：哪里需要获取 block 内容、从哪里取、如何从多张表重建 block/header，并最终返回给 RPC、同步模块、过滤器等调用方。

整体上，主逻辑是：

`调用方请求 block` -> `Ledger 查询 header / tx hash 列表 / tx / receipt` -> `内存中重建 Block 对象` -> `返回给 RPC 或其他模块`

### 4.1 Ledger 读块主入口

- `/Users/wushichen/FISCO-BCOS/bcos-ledger/bcos-ledger/Ledger.cpp`
  - `asyncGetBlockDataByNumber()` 是 callback 风格的核心入口。
  - 负责按 flag 决定读取哪些部分：
    - `HEADER`
    - `TRANSACTIONS`
    - `RECEIPTS`
    - `TRANSACTIONS_HASH`

- `/Users/wushichen/FISCO-BCOS/bcos-ledger/bcos-ledger/LedgerMethods.h`
  - `getBlockData()` 的协程版本入口。

- `/Users/wushichen/FISCO-BCOS/bcos-ledger/bcos-ledger/LedgerMethods.cpp`
  - `getBlockData()` 协程封装实现。

### 4.2 Ledger 如何重建一个完整 block

在 `/Users/wushichen/FISCO-BCOS/bcos-ledger/bcos-ledger/LedgerMethods.h` 的 `getBlockData()` 中，读取过程大致是：

1. 创建一个空 `block`
2. 如果请求 `HEADER`
   - 从 `SYS_NUMBER_2_BLOCK_HEADER` 读取编码后的 header
   - 通过 `blockHeaderFactory()->createBlockHeader()` 解码并挂到 block 上
3. 如果请求交易或回执或交易 hash
   - 从 `SYS_NUMBER_2_TXS` 读取交易 metadata/hash 列表
   - 先还原出 tx hashes
4. 如果请求 `TRANSACTIONS`
   - 按 hash 从 `SYS_HASH_2_TX` 批量读取完整交易
   - append 到 block
5. 如果请求 `RECEIPTS`
   - 按 hash 从 `SYS_HASH_2_RECEIPT` 批量读取回执
   - append 到 block
6. 如果请求 `TRANSACTIONS_HASH`
   - 直接用 hash 构造 `TransactionMetaData`
   - append 到 block

这说明：

- “完整 block”是读取时在内存里组装出来的
- DB 中默认没有一份完整 block 的统一持久化对象

### 4.3 对外 RPC 读取链路

- `/Users/wushichen/FISCO-BCOS/bcos-rpc/bcos-rpc/web3jsonrpc/endpoints/EthEndpoint.cpp`
  - `eth_getBlockByHash`
  - `eth_getBlockByNumber`
  - `eth_getTransactionByBlockHashAndIndex`
  - `eth_getTransactionByBlockNumberAndIndex`
  - `eth_getTransactionReceipt`
  - 这些接口都会调用 ledger 的 `getBlockData()` / `getBlockHash()` / `getBlockNumber()` 等接口来读取 block 相关数据。

- `/Users/wushichen/FISCO-BCOS/bcos-rpc/bcos-rpc/web3jsonrpc/model/BlockResponse.cpp`
  - 负责把内部 `Block` / `BlockHeader` 转成 Web3 风格 JSON 返回值。
  - 这是当前“内部 block/header”与“对外以太坊风格 block JSON”之间最直接的映射层。

### 4.4 过滤器 / 事件系统读取 block

- `/Users/wushichen/FISCO-BCOS/bcos-rpc/bcos-rpc/filter/FilterSystem.cpp`
  - 获取日志和 filter 结果时，会按 block number / hash 去取 block 数据。

- `/Users/wushichen/FISCO-BCOS/bcos-rpc/bcos-rpc/filter/LogMatcher.cpp`
  - 使用 block hash、交易和 receipt 来构造日志匹配结果。

- `/Users/wushichen/FISCO-BCOS/bcos-rpc/bcos-rpc/event/EventSub.cpp`
  - 事件订阅流程中也会读取 block 数据。

### 4.5 同步模块读取 block

- `/Users/wushichen/FISCO-BCOS/bcos-sync/bcos-sync/BlockSync.cpp`
  - 在区块同步、校验归档块、向其他节点发送块数据时，会通过 ledger 获取 block/header。

### 4.6 Engine / 执行层外部接口读取 block 索引

- `/Users/wushichen/FISCO-BCOS/engine/bcos-engine/EngineServiceImpl.h`
  - Engine API 场景下会读取 head/safe/finalized block number。
  - 这说明 block number/hash 索引不仅服务于 RPC，也服务于 execution engine 对外状态接口。

## 5. 四类分类后的总体结论

如果从架构角度总结，FISCO-BCOS 中 `Block` / `BlockHeader` 的设计可以理解为：

1. **基础层**
   - 定义了 block/header 的抽象接口、默认实现、工厂、序列化格式

2. **生成层**
   - sealer 创建 proposal block/header
   - scheduler/executive 在执行后补齐 root/hash，形成最终 header

3. **存储层**
   - ledger 将 block 拆成 header、索引、tx metadata、tx、receipt、nonce 等多份数据分表存储

4. **读取层**
   - ledger 再从这些分表数据中重建 block，对外提供给 rpc/filter/sync/engine 等模块

这也是后续改造时最重要的一点：

**FISCO 当前的 block 不是“单一对象直接存取”的设计，而是“统一接口 + 分表存储 + 按需重建”的设计。**

因此如果你后面要并行维护一套 FISCO 原生 block 和一套 Ethereum/OP Stack block，这四层都要分别考虑对应的落点：

- 基础数据结构是否双轨
- 出块时何时生成第二套 header
- 存储时是否双表/双索引
- 读取时如何根据调用方返回不同 block 视图

## 6. 面向 Ethereum 标准区块结构的三期实现路线

本节基于如下约束来规划修改范围：

- 保留 FISCO-BCOS 现有区块、区块头、共识、执行逻辑不变
- 新增一套 Ethereum 标准 block / header 结构
- 允许在出块和存储时并行生成、并行保存这套标准区块数据
- RPC 对外优先返回 Ethereum 标准字段
- 不要求内部共识、执行阶段改为依赖 Ethereum 标准区块
- `stateRoot` 等字段可视为与现有实现共用同一套计算结果

### 6.1 第一期：添加基础的 Ethereum 标准区块结构、工厂类、处理函数

这一期的目标是把“第二套 block/header 类型系统”搭起来，但还不接入现有出块、存储、RPC 主链路。

#### 需要修改的核心代码

- `/Users/wushichen/FISCO-BCOS/bcos-framework/bcos-framework/protocol/Block.h`
  - 如果你希望两类区块都走统一抽象，需要评估是否扩展现有 `Block` 抽象接口。
  - 如果不希望影响现有接口，建议不要直接改这个文件，而是新增独立的 Ethereum block 类型接口。

- `/Users/wushichen/FISCO-BCOS/bcos-framework/bcos-framework/protocol/BlockHeader.h`
  - 同上。
  - 如果 Ethereum header 与 FISCO header 差异较大，建议不要把所有字段直接塞进现有 `BlockHeader` 抽象，而是新增一套独立协议对象。

- `/Users/wushichen/FISCO-BCOS/bcos-framework/bcos-framework/protocol/BlockFactory.h`
  - 需要评估是否新增 Ethereum block factory 接口，或者扩展现有 factory 的创建能力。

- `/Users/wushichen/FISCO-BCOS/bcos-framework/bcos-framework/protocol/BlockHeaderFactory.h`
  - 需要新增或扩展 Ethereum header 工厂逻辑。

- `/Users/wushichen/FISCO-BCOS/bcos-tars-protocol/bcos-tars-protocol/tars/Block.tars`
  - 如果你准备继续沿用 Tars 做标准区块结构的编解码，需要新增 Ethereum block/header 的 Tars 结构定义。
  - 如果不想污染现有 `Block.tars`，也可以新建单独的 `EthBlock.tars`。

- `/Users/wushichen/FISCO-BCOS/bcos-tars-protocol/bcos-tars-protocol/protocol/BlockImpl.h`
  - 如果继续复用当前实现层，需要评估是否拆出新的 `EthBlockImpl`。
  - 更推荐新增独立实现文件，而不是把两套结构强行揉进一个 `BlockImpl`。

- `/Users/wushichen/FISCO-BCOS/bcos-tars-protocol/bcos-tars-protocol/protocol/BlockHeaderImpl.h`
  - 同上，建议新增 `EthBlockHeaderImpl`。

- `/Users/wushichen/FISCO-BCOS/bcos-tars-protocol/bcos-tars-protocol/protocol/BlockFactoryImpl.cpp`
  - 需要新增 Ethereum block 默认工厂实现，负责创建/解码标准 block。

- `/Users/wushichen/FISCO-BCOS/bcos-tars-protocol/bcos-tars-protocol/protocol/BlockHeaderFactoryImpl.cpp`
  - 需要新增 Ethereum header 默认工厂实现，负责创建/解码标准 header。

- `/Users/wushichen/FISCO-BCOS/libinitializer/ProtocolInitializer.cpp`
  - 需要把新增的 Ethereum block/header factory 注入到系统初始化流程中，供后续出块、存储、RPC 使用。

#### 第一期建议新增的代码组织方式

从可维护性上，更建议“新增并行类型”，而不是直接污染现有 FISCO `Block` / `BlockHeader`：

- 新增一套 Ethereum block/header 协议对象
- 新增一套 Ethereum block/header factory
- 新增一套 encode/decode / hash / helper 函数
- 先不改动现有共识、执行、ledger 主逻辑

如果按目录建议，第一期最可能新增的文件会集中在：

- `bcos-framework/.../protocol/`
- `bcos-tars-protocol/.../tars/`
- `bcos-tars-protocol/.../protocol/`
- `libinitializer/`

### 6.2 第二期：在现有区块生成与存储链路中并行添加 Ethereum 标准区块

这一期是整个方案里最关键的一期。

目标是：

- 现有 FISCO block/header 继续照常生成
- 在相同出块时机，并行生成 Ethereum 标准 block/header
- 按与现有类似的方式把 Ethereum 标准区块也保存下来
- 能共用的表尽量共用，不能共用的部分新增独立表

#### 第二期必须介入的主链路代码

- `/Users/wushichen/FISCO-BCOS/bcos-sealer/bcos-sealer/SealingManager.cpp`
  - 当前 FISCO proposal block/header 的创建入口。
  - 这里需要决定：
    - Ethereum block/header 是不是在 proposal 阶段就创建草稿
    - 哪些字段在 proposal 阶段可确定
    - 哪些字段要等执行结束后再补

- `/Users/wushichen/FISCO-BCOS/bcos-scheduler/src/BlockExecutive.cpp`
  - 当前执行完成后生成最终 FISCO header 的关键位置。
  - 第二期最重要的改造点之一。
  - 这里需要并行生成最终 Ethereum header，并补齐：
    - `transactionsRoot`
    - `receiptsRoot`
    - `stateRoot`
    - `gasUsed`
    - `logsBloom`
    - `parentHash`
    - `timestamp`
    - `number`
    - 以及 Ethereum 标准要求的额外字段

- `/Users/wushichen/FISCO-BCOS/bcos-scheduler/src/SchedulerImpl.cpp`
  - commit 流程中需要把第二套 header / block 上下文继续往下传。
  - 如果 commit 接口只接收 FISCO header，需要在这里评估附带传递 Ethereum block 数据的方式。

- `/Users/wushichen/FISCO-BCOS/bcos-scheduler/src/SchedulerManager.cpp`
  - 需要跟进 commit 编排，使第二套 block/header 在提交阶段可见。

- `/Users/wushichen/FISCO-BCOS/libinitializer/Initializer.cpp`
  - bootstrap/sys contract 初始化如需对外可见 Ethereum block，也要补齐第二套 block 的生成和保存。

#### 第二期必须介入的存储代码

- `/Users/wushichen/FISCO-BCOS/bcos-framework/bcos-framework/ledger/LedgerTypeDef.h`
  - 需要新增 Ethereum block 持久化表名常量。
  - 例如：
    - Ethereum `number -> header`
    - Ethereum `hash -> number`
    - Ethereum `number -> hash`
    - 如有必要，Ethereum `number -> tx hash list`
  - 这是所有新表定义的中心位置。

- `/Users/wushichen/FISCO-BCOS/bcos-ledger/bcos-ledger/Ledger.cpp`
  - `asyncPrewriteBlock()`
    - 需要并行写入 Ethereum header 与 Ethereum block 索引
  - `storeTransactionsAndReceipts()`
    - 大概率可以继续共用现有交易和回执存储，不需要新增第二套 `txHash -> tx`、`txHash -> receipt`
  - `asyncPreStoreBlockTxs()`
    - 一般不需要单独复制逻辑，但要确认 Ethereum block 侧不会引入新的交易内容对象

- `/Users/wushichen/FISCO-BCOS/bcos-ledger/bcos-ledger/LedgerMethods.h`
  - `prewriteBlock()` / `prewriteBlockToBuffer()` / `getBlockData()` 等协程封装需要扩展
  - 需要新增“获取 Ethereum block 数据”的对应封装，或者给现有接口增加 block kind / view 参数

- `/Users/wushichen/FISCO-BCOS/bcos-ledger/bcos-ledger/LedgerMethods.cpp`
  - 跟随上面的接口变化进行实现修改

- `/Users/wushichen/FISCO-BCOS/bcos-ledger/bcos-ledger/LedgerImpl.h`
  - 这里包含同步风格/模板化的读写辅助逻辑
  - 如果你希望 storage2 路径也支持 Ethereum block，需要同步补齐

#### 第二期哪些表建议新增，哪些表可以共用

建议新增独立表的部分：

- FISCO `blockHeader` 与 Ethereum `blockHeader` 不应共表
  - 需要新增 Ethereum `number -> header`

- FISCO `number -> hash` 与 Ethereum `number -> hash` 不应共表
  - 因为两类 header hash 计算方式不同

- FISCO `hash -> number` 与 Ethereum `hash -> number` 不应共表
  - 同理，区块哈希空间不同

- 如你希望按 Ethereum block 视角返回交易哈希列表，建议新增 Ethereum `number -> txs/meta`
  - 尤其当交易根计算、交易列表编码方式、交易类型字段将来可能变化时

建议直接共用的部分：

- `SYS_HASH_2_TX`
  - `txHash -> transaction`
  - 交易内容本身如果不变，可直接共用

- `SYS_HASH_2_RECEIPT`
  - `txHash -> receipt`
  - 回执内容如仍基于同一执行结果，可直接共用

- `SYS_BLOCK_NUMBER_2_NONCES`
  - 一般可以继续共用
  - 因为 nonce 列表本质对应的是同一批交易

有条件共用的部分：

- `SYS_NUMBER_2_TXS`
  - 如果 Ethereum block 只是复用同一批交易顺序和同一批 tx hash，可以考虑共用
  - 但从长期演进角度，更稳妥的是单独增加 Ethereum `number -> tx hash list`
  - 原因是将来 OP Stack / Ethereum 兼容性增强后，交易展示、编码或附加字段可能分化

#### 第二期还需要比对的字段差异

这一期真正的难点不只是“多一张表”，而是要根据 FISCO header 与 Ethereum header 的语义差异，决定哪些字段：

- 可以直接从现有 header 映射
- 可以直接共用现有执行结果
- 需要新增独立保存
- 只在 RPC 输出时动态补

这个字段差异分析，会直接决定第二期要新增哪些 header 字段、哪些索引表、哪些 helper 函数。

### 6.3 第三期：在 RPC 链路里返回 Ethereum 标准区块字段

这一期的目标是：

- 内部仍然保留 FISCO 原生 block/header 作为共识与执行主对象
- 对外 `web3jsonrpc` 获取 block 时，优先返回 Ethereum 标准 block 视图
- 当前阶段只改 `web3jsonrpc` 链路中的区块返回结构
- 当前阶段只重点改造 `eth_getBlockByHash`、`eth_getBlockByNumber`
- 传统 `jsonrpc` 链路保持原样
- 订阅链路保持原样
- filter / event / subscribe 相关功能暂不纳入第三期范围

#### Web3 RPC 主链路

- `/Users/wushichen/FISCO-BCOS/bcos-rpc/bcos-rpc/web3jsonrpc/endpoints/EthEndpoint.cpp`
  - `getBlockByHash()`
  - `getBlockByNumber()`
  - 当前阶段重点改这两个接口的区块读取与返回结构
  - 其他交易/回执接口本期不作为必须改造项

- `/Users/wushichen/FISCO-BCOS/bcos-rpc/bcos-rpc/web3jsonrpc/model/BlockResponse.cpp`
  - 这是第三期最核心的展示层改造点。
  - 需要把当前基于 FISCO header 的字段拼装，切换为基于 Ethereum 标准 header/block 的字段拼装。
  - 如果第二期已经持久化了标准 block/header，这里应该直接消费标准对象，而不是临时从 FISCO block 做大量动态转换。

- `/Users/wushichen/FISCO-BCOS/bcos-rpc/bcos-rpc/web3jsonrpc/model/BlockResponse.h`
  - 需要同步调整函数签名或引入新的标准 block response 组合函数。

#### 当前明确不改的链路

- `/Users/wushichen/FISCO-BCOS/bcos-rpc/bcos-rpc/jsonrpc/JsonRpcImpl_2_0.cpp`
  - 传统 `jsonrpc` 区块接口保持原样

- `/Users/wushichen/FISCO-BCOS/bcos-rpc/bcos-rpc/jsonrpc/JsonRpcImpl_2_0.h`
  - 保持原样

- `/Users/wushichen/FISCO-BCOS/bcos-rpc/bcos-rpc/jsonrpc/JsonRpcInterface.h`
  - 保持原样

- `/Users/wushichen/FISCO-BCOS/bcos-rpc/bcos-rpc/filter/FilterSystem.cpp`
  - 本期不改

- `/Users/wushichen/FISCO-BCOS/bcos-rpc/bcos-rpc/filter/LogMatcher.cpp`
  - 本期不改

- `/Users/wushichen/FISCO-BCOS/bcos-rpc/bcos-rpc/event/EventSub.cpp`
  - 本期不改

- `/Users/wushichen/FISCO-BCOS/bcos-rpc/bcos-rpc/web3jsonrpc/Web3Subscribe.cpp`
  - 本期不改

### 6.4 三期整体修改范围总结

如果按“最可能需要修改的代码文件”汇总，优先级可以概括成下面这组：

#### 第一期优先文件

- `/Users/wushichen/FISCO-BCOS/bcos-framework/bcos-framework/protocol/BlockFactory.h`
- `/Users/wushichen/FISCO-BCOS/bcos-framework/bcos-framework/protocol/BlockHeaderFactory.h`
- `/Users/wushichen/FISCO-BCOS/bcos-tars-protocol/bcos-tars-protocol/tars/Block.tars`
- `/Users/wushichen/FISCO-BCOS/bcos-tars-protocol/bcos-tars-protocol/protocol/BlockFactoryImpl.cpp`
- `/Users/wushichen/FISCO-BCOS/bcos-tars-protocol/bcos-tars-protocol/protocol/BlockHeaderFactoryImpl.cpp`
- `/Users/wushichen/FISCO-BCOS/libinitializer/ProtocolInitializer.cpp`

#### 第二期优先文件

- `/Users/wushichen/FISCO-BCOS/bcos-sealer/bcos-sealer/SealingManager.cpp`
- `/Users/wushichen/FISCO-BCOS/bcos-scheduler/src/BlockExecutive.cpp`
- `/Users/wushichen/FISCO-BCOS/bcos-scheduler/src/SchedulerImpl.cpp`
- `/Users/wushichen/FISCO-BCOS/bcos-scheduler/src/SchedulerManager.cpp`
- `/Users/wushichen/FISCO-BCOS/bcos-framework/bcos-framework/ledger/LedgerTypeDef.h`
- `/Users/wushichen/FISCO-BCOS/bcos-ledger/bcos-ledger/Ledger.cpp`
- `/Users/wushichen/FISCO-BCOS/bcos-ledger/bcos-ledger/LedgerMethods.h`
- `/Users/wushichen/FISCO-BCOS/bcos-ledger/bcos-ledger/LedgerMethods.cpp`
- `/Users/wushichen/FISCO-BCOS/bcos-ledger/bcos-ledger/LedgerImpl.h`
- `/Users/wushichen/FISCO-BCOS/libinitializer/Initializer.cpp`

#### 第三期优先文件

- `/Users/wushichen/FISCO-BCOS/bcos-rpc/bcos-rpc/web3jsonrpc/endpoints/EthEndpoint.cpp`
- `/Users/wushichen/FISCO-BCOS/bcos-rpc/bcos-rpc/web3jsonrpc/model/BlockResponse.cpp`
- `/Users/wushichen/FISCO-BCOS/bcos-rpc/bcos-rpc/web3jsonrpc/model/BlockResponse.h`

### 6.5 实现顺序建议

从实施风险上，建议按下面顺序推进：

1. 先完成第一期，独立把 Ethereum 标准 block/header 类型系统搭出来
2. 第二期优先打通“执行完成后并行生成标准 header + 持久化存储”
3. 第三期最后再切 RPC 输出

原因是：

- 如果第二期没打通，第三期 RPC 只能靠临时转换，后面会越来越难维护
- 第二期一旦打通，第三期其实就主要是“读哪套 block、返回哪套字段”的问题
- 你的总体目标是“保留现有功能不动，同时对外提供标准区块”，这最适合走“先并行生成并行存储，再切对外视图”的路线

## 7. 建议拆分的 8 个子任务与粗排期

这一节基于当前明确后的范围来拆解：

- 第一期：补齐 Ethereum 标准 block/header 基础结构与工厂
- 第二期：并行生成、并行持久化
- 第三期：只改 `web3jsonrpc` 的 `eth_getBlockByHash` / `eth_getBlockByNumber`

这里的排期先按“单人主导实现”的粗粒度估算，单位按“工作日”理解更合适。真实排期会受到你们对 `gasLimit` / `baseFeePerGas` 设计决策影响。

### 7.1 子任务 1：字段映射与缺口设计

- 实现内容：
  - 把 Ethereum 标准 header/body 字段逐项映射到 FISCO 当前结构
  - 明确：
    - 哪些字段直接映射
    - 哪些字段填空/固定值
    - 哪些字段需要新增来源
  - 重点做出：
    - `gasLimit` 设计
    - `baseFeePerGas` 策略
    - `withdrawalsRoot` 策略
- 产出物：
  - 一份字段映射表
  - 第二期存储设计输入
- 预计排期：
  - `2-3` 个工作日

### 7.2 子任务 2：新增 Ethereum 标准 block/header 类型与编解码

- 实现内容：
  - 新增 Ethereum block/header 数据结构
  - 新增对应 factory
  - 新增 encode/decode、hash、copy/helper 函数
  - 尽量不污染现有 FISCO `Block` / `BlockHeader`
- 重点代码：
  - `/Users/wushichen/FISCO-BCOS/bcos-framework/bcos-framework/protocol/`
  - `/Users/wushichen/FISCO-BCOS/bcos-tars-protocol/bcos-tars-protocol/tars/`
  - `/Users/wushichen/FISCO-BCOS/bcos-tars-protocol/bcos-tars-protocol/protocol/`
  - `/Users/wushichen/FISCO-BCOS/libinitializer/ProtocolInitializer.cpp`
- 预计排期：
  - `3-5` 个工作日

### 7.3 子任务 3：定义 Ethereum block 的存储表与索引模型

- 实现内容：
  - 在 ledger 常量中新增 Ethereum block 相关系统表
  - 明确哪些表独立、哪些表共用
  - 设计：
    - Ethereum `number -> header`
    - Ethereum `number -> hash`
    - Ethereum `hash -> number`
    - 是否新增 Ethereum `number -> tx hash list`
- 重点代码：
  - `/Users/wushichen/FISCO-BCOS/bcos-framework/bcos-framework/ledger/LedgerTypeDef.h`
  - `/Users/wushichen/FISCO-BCOS/bcos-ledger/bcos-ledger/Ledger.cpp`
  - `/Users/wushichen/FISCO-BCOS/bcos-ledger/bcos-ledger/LedgerMethods.h`
  - `/Users/wushichen/FISCO-BCOS/bcos-ledger/bcos-ledger/LedgerImpl.h`
- 预计排期：
  - `2-3` 个工作日

### 7.4 子任务 4：在执行完成后并行生成 Ethereum 标准 header

- 实现内容：
  - 在 FISCO 区块执行完成后，基于执行结果并行构造标准 Ethereum header
  - 补齐：
    - `parentHash`
    - `stateRoot`
    - `transactionsRoot`
    - `receiptsRoot`
    - `logsBloom`
    - `number`
    - `gasUsed`
    - `timestamp`
    - `extraData`
    - `miner`
    - 以及占位字段
- 重点代码：
  - `/Users/wushichen/FISCO-BCOS/bcos-sealer/bcos-sealer/SealingManager.cpp`
  - `/Users/wushichen/FISCO-BCOS/bcos-scheduler/src/BlockExecutive.cpp`
- 预计排期：
  - `4-6` 个工作日

### 7.5 子任务 5：将 Ethereum 标准 block/header 并行写入 ledger

- 实现内容：
  - 扩展 commit / ledger 预写入流程
  - 把标准 Ethereum header 和其索引并行落库
  - 交易、回执、nonce 尽量继续复用现有表
- 重点代码：
  - `/Users/wushichen/FISCO-BCOS/bcos-scheduler/src/SchedulerImpl.cpp`
  - `/Users/wushichen/FISCO-BCOS/bcos-scheduler/src/SchedulerManager.cpp`
  - `/Users/wushichen/FISCO-BCOS/bcos-ledger/bcos-ledger/Ledger.cpp`
  - `/Users/wushichen/FISCO-BCOS/bcos-ledger/bcos-ledger/LedgerMethods.cpp`
  - `/Users/wushichen/FISCO-BCOS/libinitializer/Initializer.cpp`
- 预计排期：
  - `4-6` 个工作日

### 7.6 子任务 6：新增 Ethereum block 读取接口

- 实现内容：
  - 在 ledger 侧新增“按 Ethereum block 视图读取”的接口
  - 支持：
    - 按 Ethereum block number 读
    - 按 Ethereum block hash 读
    - 重建 Ethereum header / block 视图
  - 尽量不要影响现有 FISCO block 读取接口
- 重点代码：
  - `/Users/wushichen/FISCO-BCOS/bcos-ledger/bcos-ledger/Ledger.cpp`
  - `/Users/wushichen/FISCO-BCOS/bcos-ledger/bcos-ledger/LedgerMethods.h`
  - `/Users/wushichen/FISCO-BCOS/bcos-ledger/bcos-ledger/LedgerMethods.cpp`
  - `/Users/wushichen/FISCO-BCOS/bcos-ledger/bcos-ledger/LedgerImpl.h`
- 预计排期：
  - `3-4` 个工作日

### 7.7 子任务 7：改造 web3jsonrpc 的区块返回结构

- 实现内容：
  - 只改 `web3jsonrpc`
  - 只改：
    - `eth_getBlockByHash`
    - `eth_getBlockByNumber`
  - 让这两个接口返回标准 Ethereum block 字段
  - 不改传统 `jsonrpc`
  - 不改订阅
  - 不改 filter / event
- 重点代码：
  - `/Users/wushichen/FISCO-BCOS/bcos-rpc/bcos-rpc/web3jsonrpc/endpoints/EthEndpoint.cpp`
  - `/Users/wushichen/FISCO-BCOS/bcos-rpc/bcos-rpc/web3jsonrpc/model/BlockResponse.cpp`
  - `/Users/wushichen/FISCO-BCOS/bcos-rpc/bcos-rpc/web3jsonrpc/model/BlockResponse.h`
- 预计排期：
  - `2-3` 个工作日

### 7.8 子任务 8：测试与兼容性回归

- 实现内容：
  - 覆盖以下验证：
    - FISCO 原生区块主流程不回归
    - Ethereum block 能生成并落库
    - `eth_getBlockByHash`
    - `eth_getBlockByNumber`
    - 标准 block hash 与 FISCO block hash 分离后读取正确
    - 空字段/占位字段返回符合预期
  - 补充单测、集成测试、必要时加调试工具
- 重点代码：
  - `bcos-ledger` 测试
  - `bcos-rpc` 测试
  - `bcos-tars-protocol` 测试
  - 如有必要补充 `docs/OP-stack/` 下联调脚本
- 预计排期：
  - `3-5` 个工作日

### 7.9 粗排期汇总

如果按顺序推进、并留少量联调缓冲，粗略可以估成：

1. 子任务 1：`2-3` 天
2. 子任务 2：`3-5` 天
3. 子任务 3：`2-3` 天
4. 子任务 4：`4-6` 天
5. 子任务 5：`4-6` 天
6. 子任务 6：`3-4` 天
7. 子任务 7：`2-3` 天
8. 子任务 8：`3-5` 天

总计大约：

- `23-35` 个工作日

如果中间 `gasLimit` / `baseFeePerGas` 的方案能够快速定下来，而且第二期的读取接口与存储表设计一次成型，实际也有机会压缩到：

- `18-25` 个工作日

如果要进一步降低实现风险，最适合先做的里程碑是：

1. 先完成子任务 1、2、3
2. 再打通子任务 4、5
3. 最后再做子任务 6、7、8
