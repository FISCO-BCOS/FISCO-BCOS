# OP Stack 系统测试方案:RPC 接口矩阵 + 区块状态验证(v3)

> **日期**: 2026-08-12(v3,经两轮共 8 个 sub-agent 交叉审查)
> **前提**: B1 sequencer 已交付(真实节点 `eth_sendRawTransaction → seal → buildOpPayload → executeOpBlock → 提交 → eth_* 可查`,1930/1930)。
> **目标**: 真实节点上验证 ① opstack 路径 RPC 接口;② 区块状态正确性。
> **用户裁定(v3)**: RPC 覆盖 = 完整矩阵;**3 个 RPC 读路径 bug 纳入修复**;**实现 engine V4 端点**(阶段 0 前置);Part C 对比 = 砍 C.2,预算投 C.1(实际投 L0 txsRoot 补齐);向量 VALID 场景留 L1。

---

## 1. 分层与现有覆盖盘点

| 层 | 载体 | 现状 | 本方案 |
|---|---|---|---|
| L0 单元 | `OpT8nReplayTest.cpp` 等 | 127 向量回放 = **执行输出等价**(stateRoot/receiptsRoot/逐回执/OP 费用/postState) | **补 txsRoot 对比**(用 FISCO `computeOpTxRoot`),优先 16 个 pre-Isthmus golden(见 §5 C.1) |
| L1 engine 集成 | `OpNewPayloadRpcE2eTest.cpp` 81 用例 | `assertSevenFields` 已断言 **opHeaderHash==golden.blockHash、stateRoot/receiptsRoot/withdrawalsRoot/gasUsed/txsRoot/logsBloom、encodeOpHeader 21 字段字节相等**(63 单块 + 4 chained);getPayload 仅 mock 层 | 复杂 VALID 场景留此层;V4 端点实现后补真实 getPayload 路径 |
| L2 真实节点 E2E | B3 节点 + HTTP RPC | 手动验证:blockNumber/chainId/getBlock/getReceipt/sendRawTransaction | **本方案核心**:RPC 矩阵(修复 R1-R3 + V4 端点后)+ 状态断言脚本 |
| L3 RPC mock 层 | `bcos-rpc/test/unittests/rpc/` 22 文件 | 参数/错误码,状态来自 stub | 不作为状态正确性依据 |

**现有资产(事实修正)**:
- golden 根目录 = `opstack-executor/tests/t8n/golden/engine/`(**无 vectors 段**),chained 在 `golden/engine/chained/`(**子目录**)。engine golden 顶层键全集 = `{blockHash, encodedHeaderHex, excessBlobGas, extraData, rawTransactions, transactionsRoot}`,**无 `currentBaseFee`**(该值在向量 `env.currentBaseFee`)。**63+4 个 isthmus/jovian golden 已被 L1 `assertSevenFields` 消费**;未被 L1 覆盖的 = **16 个 pre-Isthmus golden**(engine 只收 Isthmus+,`-38005`)。
- 向量 `_op_expected` 无 txsRoot 字段;deposit tx 无 `_op_raw`(只有 `_op_deposit`+data);golden `rawTransactions` 含 deposit 完整 envelope(0x7e…)——**txsRoot 对比输入必须是 golden `rawTransactions`**。
- 参考 op-geth(唯一权威): `/Users/octopus/octo/code/blockchain-impl/op-geth` pin `e8800cff`(= v1.101702.2,与 golden 同源);`/code/op-geth`(v1.101701.0-rc.3 脏树)**不用**。
- B3 配置: `/tmp/op-spike/setup_b3.sh`(`produce_empty_blocks=true` + single_node_consensus;⚠ 每次 rm -rf,不能重启)。签名工具 `sign_secp.c` + `send_tx.py`(已禁代理)。

---

## 2. 阶段 0 前置(硬 gate,完成后才进阶段 1)

### V1. 实现 engine V4 RPC 端点(用户裁定,约 2-3 天)
- **现状**: `EngineEndpoint.cpp` 的 `newPayloadV4`/`forkchoiceUpdatedV4`/`getPayloadV4` 是返回 `-38005 UnsupportedFork("not yet supported (Prague fork)")` 的桩,不转发到 engineService;OP 引擎只收 Isthmus+ 且要求 version==4 → 真实节点当前**无法通过 engine API 提交有效 OP 块**。
- **修复**: 三个 V4 端点从桩改为**转发到 engineService**(`handleNewPayload`/`handleForkchoiceUpdated`/`handleGetPayload` 的 V4 路径),`maxEngineVersion → 4`;`supportedOpCapabilities` 的"V4 已支持"注释与实际一致。
- **验证**: `engine_newPayloadV4`(合法 OP payload)→ VALID;`exchangeCapabilities` 能力列表与实际端点一致。

### V2. 修复 R1-R3 RPC 读路径 bug
| # | Bug | 修复 | 细化(二轮审查) |
|---|---|---|---|
| R1 | **块 hash 错**: `combineBlockResponse` 取 tars 头 `dataHash`(OP 头为空)→ 读路径 `BlockHeaderFactoryImpl` 自动回填 **FISCO tars 哈希**,≠ OP 块 hash | 对 OP 头调 `opHeaderHash(opHeaderConst())` | ① **OP 头判别式** = `blockHeader->withdrawalsRoot().has_value()`(+`baseFee().has_value()` 兜底),非 OP 链行为不变;② **`opHeaderConst` 依赖**: 在 `bcos::engine::detail`,bcos-rpc **不能 include engine** → rpc 侧本地构造常量(`c_emptyOmmersHash`/`c_posNonce` 与 combineBlockResponse 已硬编码值同源)或下沉到 `bcos-framework/protocol/BlockHeader.h`;③ **同源遗漏**: `getTransactionByBlockNumberAndIndex`(EthEndpoint.cpp:820)同一 TARS-hash bug,一并修 |
| R2 | **头字段硬编码**: `combineBlockResponse` 硬编码 `baseFeePerGas=0x0`/`gasLimit=30000000`/`withdrawalsRoot`/`blobGasUsed`/`excessBlobGas`/`parentBeaconBlockRoot` 全零、`mixHash`=零、`coinbase`=sealer pk 派生 | 从真实 header 读:baseFee/gasLimit/withdrawalsRoot/blobGasUsed/**mixHash=prevRandao**/**coinbase=feeRecipient** | optional 字段用 `.value_or` 回退(非 OP 块 baseFee/withdrawalsRoot 为 nullopt);gasLimit 断言比**提交值**非硬编码常量 |
| R3 | **余额/存储读空**: `eth_getBalance`/`eth_getStorageAt`/`eth_getTransactionCount` 都经 `ledger::getStorageAt`,读 OP 状态空;executeOpBlock 路径能读到 | **先三路对照坐实根因**(B3 打块后): 底层 RocksDB 直读 `/apps/<addr>:balance` / `m_stateStorage->asyncGetRow`(块表路径) / `getStateStorage()`(账户态路径)→ 定位是 `getStateStorage()`(新建 `StateStorage(m_stateStorage,true)`)读层还是 `Ledger::getStorageAt` 读层,再修 | **范围声明**: 只覆盖经 `getStorageAt` 的三个方法;`eth_getCode`/`eth_call` 走 `m_nodeService->scheduler()` 不经此函数,**单独验证** |

**修复验证 gate**(全部通过才进阶段 1):
`getBlockByNumber(n).hash == s_number_2_hash[n]`(RocksDB 交叉,非平凡回查)、`getTransactionByBlockNumberAndIndex.blockHash` 同上、`getBalance`/`getTransactionCount`/`getStorageAt` 经 RPC vs RocksDB 落盘值一致、`baseFeePerGas != 0x0`、`eth_getBalance(sender) == ledger 落盘值`。

### V3. 测试基础设施
- **`restart_b3.sh`**: kill `node.pid` 后**不删**存储,原 `$WORK` 重跑同 config(`setup_b3.sh` 每次 rm -rf,不能重启)。
- **`chain_driver.py`**(镜像 op-geth `setupBlocks`): 被动模式(签名流)= B3(`enable_single_node_consensus=true`),`注入确定性交易 → 等 `SINGLE_CONSENSUS Committed` 日志/`eth_blockNumber` 推进 → 断言块头/回执/余额 → 下一块`;主动模式(engine 矩阵)= **独立实例** `enable_single_node_consensus=false`,harness 自驱动 FCU→getPayload→newPayload(**规避双 FCU 竞争 + `produce_empty_blocks=true` 对 current_number 的污染**)。
- **`rpc_matrix.py`**: HTTP RPC 封装(禁代理)+ 各方法断言;**`state_verify.py`**: 直接读 RocksDB(`s_number_2_*` 表解码)做 B.1/B.2 链式校验。
- **不预编译 op-geth**(C.1 全离线消费已存在 golden,无需 op-geth 可执行文件)。

---

## 3. Part A — RPC 接口矩阵(真实节点,V1-V3 完成后)

驱动方式: **签名驱动**(eth_sendRawTransaction,主)+ **invalid 拒绝向量**(engine_newPayloadV4,链上下文无关)+ 复杂 VALID 场景留 L1。

### A.1 engine_* 端点(V4 端点实现后完整可跑)
| 方法 | 场景 | 断言 |
|---|---|---|
| `engine_newPayloadV4` | 合法 OP payload;invalid 篡改向量(错 stateRoot/extraData) | VALID;invalid 拒绝且**块未入库**(hash 查不到、current_number 不推进) |
| `engine_newPayloadV1/V2/V3` | 同 payload(时间戳落在 Isthmus+ 窗口) | 版本门返回 `-38005` |
| `engine_forkchoiceUpdatedV1-V4` | head=已提交块;带合法 attributes | V4 VALID + payloadId;V1-3 版本门错误 |
| `engine_getPayloadV1-V4` | FCU 后按 payloadId 取 | 返回 payload;blockHash 与后续 newPayload 一致(真实路径,L1 仅 mock) |
| `engine_exchangeCapabilities` | — | 返回字符串数组含 `engine_newPayloadV4` 等(⚠ 是 `exchangeCapabilities`,非 `exchangeTransitionConfigurationV1`) |

> **engine 未实现缺口(声明)**: `engine_exchangeTransitionConfigurationV1`/`getPayloadBodiesV1/V2`/`getClientVersionV1` out-of-scope。

### A.2 eth_* 端点
| 类别 | 方法 | 断言 |
|---|---|---|
| 链 | `eth_blockNumber` | == 最新已提交块号 |
| | `eth_chainId` | == genesis `[web3] chain_id`,且 == `net_version`(双读源一致) |
| | `eth_gasPrice`/`eth_maxPriorityFeePerGas` | == `0x0`(FISCO 硬编码/读系统配置,**不是**与 baseFee 的关系) |
| | `eth_syncing` | false |
| 块 | `eth_getBlockByNumber(tag, full)` | R1/R2 修复后:number/parentHash/stateRoot/txsRoot/receiptsRoot/extraData/gasUsed/logsBloom/baseFeePerGas/gasLimit/mixHash/coinbase(==feeRecipient)与提交一致;timestamp **按秒比较**(存储 ms/1000) |
| | `eth_getBlockByHash` | 查到的块 `.hash == s_number_2_hash[n]`;未知返 null |
| | `eth_getBlockTransactionCountByNumber/Hash` | == 块内 tx 数(含 deposit) |
| 交易 | `eth_getTransactionByHash` | from/to/value/nonce/gas/input/type 与发送一致;blockNumber/blockHash/transactionIndex 填对 |
| | `eth_getTransactionByBlockNumberAndIndex` | index=0 deposit、index=1 用户 tx(**签名驱动块**);blockHash 正确(R1 修复后) |
| | `eth_getTransactionReceipt` | status/gasUsed/cumulativeGasUsed/effectiveGasPrice/logs;OP 扩展字段(`l1GasPrice/l1GasUsed/l1Fee/l1BlobBaseFee/l1BaseFeeScalar/l1BlobBaseFeeScalar/operatorFee*/daFootprintGasScalar/blobGasUsed=da_footprint/depositNonce/depositReceiptVersion/operatorFee`)按 fork 存在;空则不输出 |
| | `eth_getTransactionCount(address)` | == 已提交 nonce(**R3 同读路径**,修复后;tag 忽略恒 latest) |
| 账户 | `eth_getBalance(address)` | R3 修复后:精确核对 pre − Σ(value + gasUsed×effectiveGasPrice)(仅 head) |
| | `eth_getCode(address)` | 合约创建后 == 字节码;EOA 返 `0x`(**走 scheduler,不经 getStorageAt**,单独验证) |
| | `eth_getStorageAt(address, slot)` | R3 修复后 == 期望 slot(tag 忽略恒 latest) |
| | `eth_getProof(address, slots, tag)` | **conditional**:Merkle 半程 honors blockTag,flat 半程仍 latest;**若 B3 未接 `mptNodeReader`(-32603)则标 out-of-scope** |
| 执行 | `eth_call` | honors blockTag(可历史查询);对 B.3 已部署合约+输入,与 L0 同部署同状态对照 |
| | `eth_estimateGas` | 返回合理 gas 下限,与真实执行 gasUsed 同量级 |
| 事件 | `eth_getLogs` | topic/data/address/blockNumber 正确;按块号过滤生效 |

### A.3 web3 / net
| 方法 | 断言 |
|---|---|
| `web3_clientVersion` / `web3_sha3` | 版本串;`web3_sha3` == keccak256 |
| `net_version` | == chain_id 且 == `eth_chainId` |
| `net_peerCount` | **数值 == 0**(实现返回 JSON number 非 QUANTITY) |
| `net_listening` | true |

### A.4 in/out-of-scope 总表
- **in-scope**: A.1-A.3;filter 家族可选(`eth_getLogs` 主路径已覆盖)。
- **out-of-scope(已实现但排除/桩)**: `eth_coinbase`/`eth_mining`/`eth_hashrate`/`eth_accounts`/`eth_sendTransaction`/`eth_sign`/`eth_signTransaction`(桩返 0x00)/`eth_getUncleCountByBlock*`/`eth_getUncleByBlock*`(固定返)/`eth_subscribe`。
- **out-of-scope(未实现,声明缺口)**: `eth_feeHistory`(0 命中)、`engine_exchangeTransitionConfigurationV1`/`getPayloadBodiesV1/V2`/`getClientVersionV1`、`eth_protocolVersion`(注册但抛 not implemented)、debug/trace。

---

## 4. Part B — 区块状态验证

> **断言介质原则**: hash 重算、baseFee 链式、ms 级 timestamp 走 `state_verify.py`(RocksDB 解码真实头);RPC 断言只用于 V2 修复后可用字段。

### B.1 块表一致性(**7 张表**,RocksDB 直读)
- `s_number_2_hash[n] == s_hash_2_number[hash]`(表级双向可逆,不依赖 RPC 修复)
- 解码 `s_number_2_header[n]` → `header.opHeaderHash(opHeaderConst()) == s_number_2_hash[n]`(header 无 blockHash 字段,tars `dataHash` 空)
- `s_number_2_txs[n]` 序 == 块内 transactions 序(deposit 首位;畸形 envelope 跳过 `s_hash_2_tx` 行为已证例外,测试语料均合法)
- `s_hash_2_tx`/`s_hash_2_receipt` 可回溯(可经 RPC)
- `s_current_state[current_number]` == 最新块号;其余 3 键 OP 路径不维护,勿断言

### B.2 头字段链式一致(RocksDB 解码真实头)
- `parentHash == 前块 opHeaderHash`;`number == 前块+1`
- `timestamp` **ms 级严格递增**(RPC 秒级只能断言非递减)
- `baseFee` 按 `calcOpBaseFee` 重算(输入: parent.baseFee/gasUsed/**blobGasUsed**/gasLimit/**extraData(denominator/elasticity/minBaseFee)**/parentIsJovian/parentIsIsthmus;Jovian `gasMetered=max(gasUsed,blobGasUsed)` + minBaseFee floor);**Python 移植 calcOpBaseFee 列为阶段 2 显式交付物**
- `extraData` == Jovian 17B / Isthmus 9B
- **`coinbase` == feeRecipient;`withdrawalsRoot` == 空列表根 `0x1dcc…`;`blobGasUsed` == Jovian da_footprint**(R2 修了必须有断言覆盖)
- `gasUsed` == 回执 gasUsed 之和;cumulative 单调;`txsRoot` 用块内 rawTransactions 本地重算(`computeOpTxRoot`);stateRoot **不可本地重算**(全局 MPT),断言 == newPayload 提交值

### B.3 状态语义(签名交易流)
确定性序列(S 转账多接收者 + 合约创建 + **REVERT 合约**作失败交易;gas< intrinsic 在 mempool 被拒,无法入块):
1. 转账: S 余额精确 = pre − Σ(value + gasUsed×effectiveGasPrice);接收者 +value;nonce 递增(R3 修复后经 eth_getBalance,或 eth_call helper/RocksDB 兜底)
2. 合约创建: `eth_getCode` == 字节码;`eth_call` 返回正确
3. 失败交易(REVERT): status=0x0,余额只扣 gas 不扣 value,nonce 仍递增
4. OP 扩展回执: 字段按 fork 存在且自洽(l1_fee 公式需实测确认 Holocene 后含 scalar 折算)

### B.4 持久化(restart_b3.sh)
- 前置: `restart_b3.sh`(不删存储);⚠ 重启后驱动立即出空块 → 断言 **blockNumber ≥ 重启前**,且重启前块的 getBlockByNumber/getTransactionReceipt/getBalance 值不变

---

## 5. Part C — FISCO vs op-geth 对比(修订 v3)

> 参考 op-geth 一律 `blockchain-impl/op-geth` pin `e8800cff`。C.1 主体实际投 **L0 txsRoot 补齐**。

### C.1 执行等价性:L0 补 txsRoot(核心增量)
- **已有**: 127 向量回放比对执行输出;L1 `assertSevenFields` 已断言 isthmus/jovian 的 opHeaderHash==blockHash、txsRoot、encodeOpHeader 21 字段字节相等。
- **净增缺口 = 16 个 pre-Isthmus golden**(engine 只收 Isthmus+,L1 够不到): `ecotone_*`/`fjord_*`/`granite_*`/`isthmus_legacy_transfer`/`isthmus_upgrade_jovian_activation`/`jovian_legacy_transfer`。
- **txsRoot 对比**: 在 `OpT8nReplayTest`(C++ 进程内)加载 golden `rawTransactions` → 调 FISCO `computeOpTxRoot` → 断言 == golden `transactionsRoot`。**必须用 `computeOpTxRoot`**(经 receiptsRoot 回放门已证与 op-geth StackTrie 等价),**绝不用脚本重实现 MPT**(已实测 Python MPT 复现不出,叶子路径剥离 branch 索引 nibble 等编码细节)。
- baseFee/extraData 派生对比**不新增**: 已被 L1 覆盖(仅 2 对 chained 数据点,runChainedPair 已断言)。

### C.2 砍掉(记录原因)
op-geth `--dev` 是纯以太坊 dev 链(无 OptimismConfig/OP 预部署/deposit、chainId 1337、gas 目标差 260 倍、baseFee 走 burn)→ effectiveGasPrice/余额/块结构/回执 OP 字段全依赖链配置对齐,唯一可比 gasUsed 已 pin 死。砍掉。

### C.3 修订(原"blockHash 对不上"论断已不成立)
`opHeaderHash(opHeaderConst())` = keccak(eth-RLP 21 字段头)已实现且 == golden.blockHash(L1 assertSevenFields 断言 63+4 通过)→ "除非 FISCO 引入 eth 头哈希"前提已满足。同 payload blockHash 对比已在 L1 覆盖;真实节点读路径的 blockHash 由 **R1 修复**(读路径调 opHeaderHash)补齐。无需新做。

---

## 6. 实施步骤(阶段化)

### 阶段 0:V1(V4 端点)+ V2(R1-R3)+ V3(基础设施)
- V1: 实现 `newPayloadV4`/`forkchoiceUpdatedV4`/`getPayloadV4` 转发 + `maxEngineVersion→4`(2-3 天)
- V2: R1(判别式/opHeaderConst 依赖/两处调用点)/ R2(mixHash 等)/ R3(三路对照坐实后修)+ 修复验证 gate
- V3: `restart_b3.sh`/`chain_driver.py`(被动+主动双实例)/`rpc_matrix.py`/`state_verify.py`

### 阶段 1:Part A(先 A.1 主动实例,后 A.2/A.3 被动实例)
- A.1 engine 矩阵(主动实例 `enable_single_node_consensus=false`)
- A.2 eth_*、A.3 web3/net(被动实例 B3)
- A.4 in/out-of-scope 核对

### 阶段 2:Part B
- B.1 表一致性(7 张,RocksDB)/ B.2 头链式(解码真实头,含 calcOpBaseFee Python 移植)/ B.3 语义(确定性交易序列)/ B.4 持久化(restart_b3.sh)

### 阶段 3:Part C(L0 txsRoot 补齐)
- 在 OpT8nReplayTest 加 golden 伴随 pass:16 个 pre-Isthmus golden 的 txsRoot 对比(computeOpTxRoot)

### 阶段 4:回归门
- 脚本化挂 ctest(fixture:起 B3 → run_all.sh → 断言退出码);纳入回归门 = 既有 1930(含 OpNewPayloadRpcE2eTest 81 例 + OpT8nReplayTest 127 向量),保证不退化

---

## 7. 交付物

- V1: V4 端点实现;V2: R1-R3 修复
- `tools/op-e2e/chain_driver.py`(核心驱动)、`rpc_matrix.py`、`state_verify.py`(含 calcOpBaseFee Python 移植)、`restart_b3.sh`、`run_all.sh`
- C.1: OpT8nReplayTest 的 golden txsRoot 伴随 pass
- 实测记录 + 发现的 bug 记录(fisco-review-process-lessons"技术声明实测")

---

## 8. 风险与前置

1. **V4 端点实现是阶段 0 最大项**(2-3 天),A.1 完整矩阵依赖它。
2. **R3 根因未坐实**(探查型,三路对照): 失败概率最高;兜底 = RocksDB 直读断言(方案已列)。
3. **R1 修复须 OP/非 OP 判别门控**: 对非 OP 头调 opHeaderHash 会抛 bad_optional_access,破坏 1930 → 判别式(withdrawalsRoot().has_value())必须先实现。
4. **主动/被动实例分离**: 主动模式必须独立实例 `enable_single_node_consensus=false`(规避双 FCU 竞争 + produce_empty_blocks 污染);`chain_driver` 要按实例管理节点起停。
5. **`eth_gasPrice`/`maxPriorityFeePerGas` 返回 0x0 是既定实现**,断言形状校验,不追为回归。
6. **golden 路径/字段**: 修正为 `tests/t8n/golden/engine/`(无 vectors 段);engine golden 无 `currentBaseFee`(在向量 env);txsRoot 输入 = golden `rawTransactions`(deposit 无 `_op_raw`)。
7. **eth_getProof conditional**: B3 未接 MPT 则 out-of-scope。
8. **断言介质纪律**: hash/baseFee/ms 时间戳走 RocksDB;RPC 断言只用于 V2 修复后字段;脚本断言走 RPC 返回不经 cerr。
