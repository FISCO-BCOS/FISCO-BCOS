# 视角 1 · 正确性:Engine API 共识语义与状态机

**先读** `.superpowers/sdd/2026-07-28-op-validator-minimal-loop/wb-review-context.md`。
**报告写** `.superpowers/sdd/2026-07-28-op-validator-minimal-loop/wb-review-p1-report.md`。

## 你的范围

- `engine/bcos-engine/EngineServiceImpl.h`(+926/-113)
- `engine/bcos-engine/EngineServiceImpl.cpp`(+219)
- `bcos-framework/bcos-framework/engine/Types.h`

## 你的问题

你是唯一负责回答"**这个节点会不会对一个 op-geth 会接受的块投反对票,或者对一个 op-geth 会拒绝的块投赞成票**"的人。

对照物:`/Users/octopus/octo/code/blockchain-impl/op-geth`(pin `e8800cffe`),重点文件 `eth/catalyst/api.go`(newPayload/FCU 主流程)、`beacon/engine/types.go`(payload → block 转换与静态校验)、`consensus/beacon/consensus.go`(头校验)。

逐项核对:

1. **状态桶的完整性**:VALID / INVALID / SYNCING / ACCEPTED 四种返回,本实现覆盖了哪些、遗漏了哪些、把哪些合并了?每个 `latestValidHash` 的取值是否符合 Engine API 规范(INVALID 时应指向最后一个已知有效祖先;当 parent 未知时应为 SYNCING 而非 INVALID)?
2. **`newPayload` 的前置校验顺序**:静态校验(step 2)→ 时间戳单调(3a)→ 已知块短路(3b)→ 非链尾 parent 拒绝(3c)→ 执行 → 六项比对(step 5)→ 登记(step 6)。**逐步对照 op-geth 的顺序**,找顺序差异带来的语义差异(例如:某个校验在 op-geth 里发生在执行前,这里发生在执行后,会不会把"应该 INVALID"变成"内部错误",反之亦然)。
3. **step 5 的比对面**:六项 seal/result 比对 + `blobGasUsed` / `requestsHash` 等额外比对。清点**payload 里有而比对面里没有**的字段——每个漏比的字段都是一个"本节点会接受 op-geth 会拒绝的块"的洞。特别看 `parentBeaconBlockRoot`、`withdrawals`、`excessBlobGas`、`extraData` 的 OP 形状。
4. **错误分类纪律**:存储/内部故障必须是 `-32603`(`OpExecutionInternalError`),**绝不能报 INVALID**(那等于让本节点对一个可能合法的块投反对票)。反过来,块级拒绝必须是 INVALID 而不是内部错误。追每一条 `throw` / `catch` 路径,找分类错位的。
5. **`forkchoiceUpdated`**:head/safe/finalized 三个哈希的处理、`payloadAttributes` 非空时的行为(验证者模式下应当拒绝还是忽略?op-geth 怎么做的)、以及 FCU 与 newPayload 之间共享的内存状态(forkchoice head)与持久状态(`SYS_HASH_2_NUMBER` 等表)之间**是否可能不一致**。
6. **`c_opMode` 探针**:`requires { &SchedulerType::template executeOpBlock<...>; }`。若签名漂移导致探针变假,OP payload 会落进通用分支——通用分支做了什么?它会不会返回 VALID 而**根本没执行区块**(橡皮图章)?现在有护栏了吗,护栏本身能不能被绕过?
7. **`registerOpBlock` 的写入原子性**:现写五张表(`SYS_HASH_2_NUMBER`、`SYS_NUMBER_2_HASH`、`s_eth_block_header`、`SYS_HASH_2_RECEIPT`、`s_eth_hash_2_rawtx`)。中途某张表写失败会怎样?会不会留下"块号已登记但头没写"的半登记状态,使**下一个块的 parentKnown 检查通过但读头失败**?

## 交付

按共享上下文的报告格式。**每条 Critical 必须给出:输入 payload 的具体形态 → 本实现的返回 → op-geth 的返回 → 分歧后果。**
