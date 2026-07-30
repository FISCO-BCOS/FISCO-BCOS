# 终审批 7:解码层共识分歧(C1 非规范长度前缀 + C2 授权项 yParity 位宽)

工作目录:`/Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/ledger-bridge`,分支 `feat-op-validator-loop`。
**不豁免编译与测试。** 回归基线:in-tree 239/239、standalone 131/131、`test-bcos-engine` 11 例、E-b 桥三腿 33×3。
基准:op-geth `e8800cffe`(`/Users/octopy/octo/code/blockchain-impl/op-geth` → 正确路径 `/Users/octopus/octo/code/blockchain-impl/op-geth`)。

**这两条都已实测确认,不是推测。** 你不需要重新证明缺陷存在,你要做的是修它、并配上会翻红的断言。

---

## C1 · RLP 长形态长度前缀的前导零未被拒绝

**位置**:`bcos-codec/bcos-codec/rlp/RLPDecode.h:92-113`(长列表分支)以及 `:65-84`(长字符串分支)。

**现状**:读 `lenOfLen` 个字节 → big-endian 转 `payloadSize` → 只检查 `payloadLength < 56`。**不检查长度字节自身的前导零。**

**op-geth 对照**:`rlp/decode.go` 的 `readUint`(约 `:1098-1114`):
```go
default:
    buffer := s.uintbuf[:8]
    clear(buffer)
    start := int(8 - size)
    if err := s.readFull(buffer[start:]); err != nil { return 0, err }
    if buffer[start] == 0 {
        // Note: readUint is also used to decode integer values.
        ...  // → ErrCanonSize
    }
```
即**长度前缀的最高位字节为 0 就是非规范**,直接拒。

**已实测的后果**(上一轮验证):`0xf9 0x00 LL` 形态被本实现接受;取一条真金值向量把 framing 改成非规范(payload 一字不动),**实算出两个不同的 txRoot**:`0xe7fe64f3…`(非规范字节建根)vs `0xd1c4b2f3…`(规范)。因为 `computeOpTxRoot`(`OpEngineSeam.h:172-185`)对**原始线上字节**建 trie,而 op-geth `DeriveSha` 的 `EncodeIndex` 是**从已解析结构体重新规范编码**。攻击者令 `payload.transactionsRoot` 与非规范字节自洽 → **本实现 VALID,op-geth INVALID。**

### 第一步(必做,先于任何代码):修复位置的爆炸半径调研

`RLPDecode.h` 是**共享**的以太坊 RLP 解码器,不只服务 OP 路径。两个候选:

- **(A) 改共享层 `decodeHeader`** —— 根治,一处修全链受益。**风险**:本闭环之外的既有以太坊消费者(Web3 交易解码、eth RPC、`EthBlockHeader`、可能还有别的)会一起变严。
- **(B) 在 OP 侧再加一层严格解码头** —— 不碰共享层。**代价**:规范性严格层已经在 `OpSchedulerImpl.h` 与 `EthBlockHeader.cpp` **被复制成两份**(全分支复审 P2 Imp-3 / P4 Minor),再加一份变三份,而"同一规则复制多份、只测其中一份"正是 `a47b00e78` yParity 事故的成因。

**你要查清并写进报告**:
1. `RLPDecode.h` 的 `decodeHeader` / `decode` 全仓有哪些调用点(逐个列出 `file:line` 与用途)?
2. 其中哪些处理**来自网络的不可信字节**、哪些只处理本节点自己刚编码出来的字节?后者变严无风险,前者才需要评估。
3. 有没有任何调用点**依赖当前的宽松行为**(即存在会被新检查拒掉的既有测试或既有数据)?**这一问决定 (A) 是"修正"还是"回归"。** 用 `rtk grep` + 跑现有测试判断,不要猜。
4. 若 (A) 可行,给出受影响的测试清单;若 (A) 不可行,说明是哪个调用点挡住的。

**控制器倾向 (A)** —— Go 的行为就是这一条,对既有以太坊消费者而言变严是**修正而非回归**;且它同时消掉 P2 Imp-3 的一半。**但若第 3 问查出真实的破坏点,报我裁定,不要自行降级到 (B)。**

### 第二步:修 + 测

- 长字符串与长列表**两个分支都要修**(`:65-84` 与 `:92-113`)。只修一个是本仓的经典失败模式。
- 测试至少覆盖:长字符串前导零、长列表前导零、`lenOfLen` 为 1 时的边界(`0xb8 0x00` / `0xf8 0x00`,op-geth 走 `case 1` 不查前导零但会被 `size < 56` 拦住——**核对我们的行为与它一致**)、以及一条**规范输入仍然通过**的正例(防止改过头)。
- **另加一条 txRoot 层的端到端断言**:取一条真金值向量,把 framing 改成非规范 → 断言在**解码期**被拒(而不是靠 txRoot 失配才被拦)。这条钉的是"防线在正确的层"。

---

## C2 · EIP-7702 授权项 `yParity` 位宽错

**位置**:`bcos-evm/bcos-evm/engine/OpSchedulerImpl.h:477` —— `auth.v = decodeU256Scalar(entryBody)`(`transaction.hpp:30` 确认目标类型是 `intx::uint256`)。

**op-geth 对照**:`core/types/tx_setcode.go` 的授权项 `V` 是 **`uint8`**。`yParity` 编成 `0x82 0x01 0x00`(2 字节)时,op-geth 报 `rlp: input string too long for uint8`,**整笔交易解不出 → 块 INVALID**;本实现接受,然后在 `OpTransition.cpp:67` 走 `continue` **跳过该授权项**,块可判 VALID。

**已实测**:`auth.v = 256` 被接受。

**修法**:授权项的 `yParity` 按 `uint8` 语义解 —— 超过 1 字节即拒(**注意与"值 > 1 即拒"是两条不同的检查**:前者是编码宽度,后者是取值范围。批 1 已加了后者,现在缺的是前者。两条都要在,且要有各自的用例)。

**注意本仓的复制陷阱**:批 1 修 `yParity > 1` 时发现该校验在**两个复制粘贴的位置**(`:502`、`:548`)各写一遍,测了一处不能说明另一处。**本次务必先确认授权项解码只有一处**,若有多处则全改并各配用例。

---

## 交付要求

1. **第一步的爆炸半径调研先交报告,不要直接开写 C1** —— 这是设计决策不是补丁。C2 可以先做,不必等裁定。
2. 每项配"注释掉修复 → 重建 → 必须翻红 → 还原 → **重建** → 复绿"自验(六步缺一不可)。
3. **红绿见证必须点名目录**:`OpSchedulerImplTest` 与 engine 系测试都在 `if(TARGET bcos-framework)` 门控内,**standalone 对它们是依赖图无边的空真**,131/131 不得当证据。但 `RLPDecode.h` 的改动可能有 standalone 侧的消费者——若有,那 standalone 就**真的**构成见证,请如实区分。
4. 通用组合根零漂移;OP 依赖名不得进成员函数签名;`ports/` `vectors/` `golden/` `transaction-scheduler/` `bcos-rpc/` 零触碰。
5. **文档同步修正**(发现方当场落地,不得留报告):
   - `OpEngineSeam.h:156-163` 的"解码器已拒绝 Go 会拒的一切非规范编码"—— **已被 C1 实测证伪**,改写为修复后的实际状态;
   - spec §6.4 (n) 同源断言同改;
   - §6.4 新增/更新条目记录 C1/C2 的修复与残留边界。
6. **分段提交**:调研结论一出先提交,C2 一到绿先提交,C1 一到绿再提交,最后动文档。本闭环已有两次 watchdog 停滞 + 一次 API 断流,**三次都发生在攒了一大段没提交的时候**。
7. 报告写 `.superpowers/sdd/2026-07-28-op-validator-minimal-loop/final-batch7-report.md`;`rtk git` 精确路径提交。

返回四行 STATUS / COMMITS / TESTS / CONCERNS。
