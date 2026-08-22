# 真实节点复杂 OP 块测试方案(向量驱动)

> **日期**: 2026-08-12
> **前提**: 真实节点 deposit-only 块已打通(commit `54dfd06`):engine_newPayloadV4 → VALID,eth_blockNumber / getBlockByNumber / getTransactionByHash / Receipt 可查,1930/1930 测试绿。
> **目标**: 用已有 131 个 op-geth 参考向量驱动真实节点,覆盖**多账户 / 多交易 / 多块链 / invalid 拒绝 / 状态连续性**,验证真实节点 RPC 路径与参考完全一致。

---

## 1. 核心洞察:向量即原料,无需重签名

现有 `opstack-executor/tests/t8n/vectors/*.json`(131 个,op-geth 参考生成,`generator=opt8n-ref`)天然适配真实节点:

| 向量字段 | 真实节点用途 |
|---|---|
| `block.transactions[i]._op_raw` | **已签名 EIP-2718 envelope** → 直接作为 `newPayload` 的 `rawTransactions`,无需私钥 |
| `_op_deposit` | deposit envelope 构造(现 build_payload.py 已做) |
| `env.*` | payload 的 header 字段(parentHash/baseFee/timestamp/gasLimit/...)— **第 2 块起 parentHash 需用上一块实测 blockHash** |
| `_op_expected.header.*` | receiptsRoot / gasUsed / logsBloom / withdrawalsRoot / requestsHash 期望值 → newPayload 的对应字段 |
| `postState` | 块执行后 stateRoot 期望(链式连续性验证) |
| `_op_expected` | 8 字段 commitments 比对基准 |

**为什么真实节点不用验签**:OP 块执行走 `decodeOneRawTx`(只解码不验签,对齐 op-geth state_processor 语义),`_op_raw` 的 sender 从 v/r/s 恢复。t8n 测试已验证这 131 个向量执行结果与参考一致——真实节点只需保证**同一条执行路径**。

---

## 2. 关键差距:pre 账户 seed 进 genesis

向量 `pre` 含执行所需的账户余额/L1Block storage,但真实节点 genesis 目前只有 2 个空 alloc(L1Block/MessagePasser,balance=0, nonce=1)。

**差距**:`isthmus_transfer_multi` 的 pre 需 3 个 sender(各 1 ETH 余额)+ L1Block 的 slot 数据(nonce=1 已在,但 slot 0x1/0x3/0x7 的 L1 属性值缺失)。

**解法**:配置生成脚本按向量 `pre` 自动生成 genesis `[alloc.N]` 段:
```
[alloc.0]  address=0x2b5ad5c4795c026514f8317c7a215e218dccd6cf  balance=0x56bc75e2d63100000  nonce=0
[alloc.1]  address=0x4200...0015  balance=0  nonce=1  storage=...
...
```
(genesis alloc 的 storage 段需确认 config 语法支持——见 §5 风险 2)

---

## 3. 测试场景(按复杂度分层)

### 阶段 1:多交易单块(transfer_multi / legacy_transfer / tx_reverted)
- **原料**: `isthmus_transfer_multi`(1 deposit + 5 签名转账,3 sender,interleaved nonce)、`isthmus_legacy_transfer`、`isthmus_tx_reverted`
- **构造**: genesis 含 pre 账户;payload 的 transactions = `[_op_deposit_envelope, _op_raw_1..5]`,header 字段取 `env` + `_op_expected.header`
- **断言**:
  - newPayload → VALID
  - `eth_getBlockByNumber(1).transactions` 长度 = 6,哈希顺序 == `_op_raw` 顺序
  - `eth_getTransactionReceipt(txHash).gasUsed/status/logs` == `_op_expected.receipts[i]`(逐笔比对)
  - `eth_getBalance(sender)` == pre.balance − 转账额 − gas 费(精确核对)
  - `eth_getTransactionByHash` 的 from == 向量 sender
- **对照**: op-geth 参考(向量即参考输出);t8n 已断言执行结果,真实节点只验证**读路径一致**

### 阶段 2:多块链(链式 golden)
- **原料**: `golden/engine/chained/chainA.golden.json` + `chainB.golden.json`(off-line chained pair,block 1/2,`_op_expected` 已含 blockHash)
- **构造**: 第一块按 chainA 发;第二块的 `parentHash` = **chainA 实测 blockHash**(注意:向量 env.parentHash 是链 A 的期望父 hash,需与实测一致才不触发 step-3c 拒绝),transactions 取 chainB,stateRoot/receiptsRoot 取 chainB._op_expected
- **断言**:
  - 两块均 VALID
  - `eth_blockNumber` 推进到 `0x2`
  - `eth_getBlockByNumber(1)` 的 stateRoot == chainA.postState 根;块 2 的 parentHash == 块 1 实测 hash
  - 块 2 执行读到的状态含块 1 的写入(转账余额跨块连续)
  - 重启节点 → 块仍可查(持久化)
- **对照**: chainA/chainB 是 op-geth 同链连续执行产物,`_op_expected.header.stateRoot` 即权威期望

### 阶段 3:invalid 拒绝(6 类)
- **原料**: `invalid_isthmus_transfer_basic_{stateRoot,parentHash,gasUsed,receiptsRoot,extraData,blockHash}.json` + 静态族(static_1..12)
- **构造**: 逐字段篡改 payload(stateRoot 改一个字节 / parentHash 指向不存在 / gasUsed 少 1 / receiptsRoot 错 / extraData 错 / blockHash 不匹配),其余字段保持正确
- **断言**:
  - 每类 → newPayload 返回 INVALID(或对应错误码),且带正确 `latestValidHash`
  - **块未入库**: 篡改的块 hash 查不到、current_number 不推进(不污染链)
  - 错误分类:`-38005`(pre-Isthmus)/ `-38003`(payload attributes)按需覆盖
- **对照**: op-geth 对同篡改返回 INVALID;此处验证 FISCO 判 INVALID 且不污染状态

### 阶段 4(可选):合约 / Jovian
- **原料**: `isthmus_contract_create`、`isthmus_setcode_7702`、`jovian_*`、`invalid_isthmus_blob`
- **断言**: 合约 create 后 `eth_getCode` 可查;setcode 7702 的 authorization 生效;Jovian DA footprint
- **对照**: 同 t8n 参考

---

## 4. 实施步骤(基础设施)

1. **`genesis-from-vector.py`**: 读向量 `pre` → 生成 config.genesis 的 `[alloc.N]` 段(+ L1Block storage,若语法支持)
2. **`build_payload_from_vector.py`**(升级现 build_payload.py):
   - 输入: 向量文件 + 已提交的上一块 blockHash + stateRoot
   - 输出: 完整 ExecutionPayload(transactions = deposit envelope + `_op_raw` 列表)
   - 关键: 复用 `op-payload-builder` 计算 transactionsRoot + blockHash(已与向量 golden 逐字对齐)
3. **`chain_driver.py`**: 按 golden 的块序连续发 newPayload + FCU,每块记录实测 blockHash/stateRoot 供下一块 parentHash
4. **断言脚本**: 对照 `_op_expected` 逐字段比对 receipts/gasUsed;`postState` 比对 stateRoot;eth RPC 读路径断言
5. **回归门**: 全部场景脚本化进 CI/ctest(或保留为集成测试脚本),保证 1930 测试不退化

---

## 5. 风险与待确认

1. **genesis alloc 的 storage 语法**: 向量 pre 的 L1Block slot 数据是否能在 config.genesis `[alloc.N]` 表达,需验证 config 解析器(若不支持,降级:跳过 L1Block storage,deposit 会重写 slot,仅影响 deposit 块之前的只读引用——transfer_multi 的 deposit 是第一个 tx 且会设 L1 属性,可能自愈)
2. **第二块 parentHash 的 stateRoot 基**: 向量链的 pre 是统一的,但真实节点第一块会写 L1Block;第二块若 pre 不含第一块写入,需确认 `_op_raw` 的执行结果与链式 golden 一致(t8n chained 已验证,风险低)
3. **invalid 静态族(static_1..12)**: 这些是"静态篡改"向量,需确认篡改点对应真实节点的哪个校验(有的是 blockHash 静态错,有的是 stateRoot 静态错),映射到断言
4. **Jovian/合约阶段**: 范围大,建议作为阶段 4 可选,先交付阶段 1-3

---

## 6. 交付物

- 3 个脚本(`genesis-from-vector.py` / `build_payload_from_vector.py` / `chain_driver.py`)
- 断言脚本或 ctest 门
- 阶段 1-3 的实测记录(每场景: 构造 → 发送 → 断言结果)
- 若发现真实节点执行与参考不一致 → 记录为 bug(参照 fisco-review-process-lessons 的"技术声明实测")
