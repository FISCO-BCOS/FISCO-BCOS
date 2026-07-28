# OP 验证者模式最小闭环设计(engine OP 化 + OpScheduler 接缝 + ETH 头哈希层)

日期:2026-07-28
状态:已评审(设计四节逐节用户确认)
基座:`feat-evm-ledger-bridge`(真账本桥已 READY 交付,156/131 双路全绿)之上开新分支
前置文档:`2026-07-27-real-ledger-bridge-design.md`(桥 spec,§4 桥契约/§10 边界)、
`2026-07-24-opstack-block-execution-port-design.md`(移植 spec)
用户指示锚点:**OP 执行适配在 `m_scheduler.executeBlock`(scheduler_v1 + TransactionExecutor)
接缝上**——不绕过调度器直调,engine 骨架的调用形状保持。

## 1. 背景与目标

仓内已有一套 ETH 口径 Engine API 骨架(`engine/bcos-engine/EngineServiceImpl` +
`bcos-rpc` `EngineEndpoint`):FCU V1-V3 完整状态机(head/safe/finalized 校验、tracked
head)、getPayload 缓存、newPayload 校验分档、mempool 出块经
`m_scheduler.executeBlock`(scheduler_v1)。缺口:执行核是 FISCO 通用语义(无 deposit/
L1 费)、stateRoot 是 FISCO 口径(`calculateStateRoot` 自带 TODO)、块哈希是 FISCO 哈希、
无 V4、无 OP payloadAttributes 字段。

本设计交付**验证者模式最小闭环**:op-node 语义下的 `engine_newPayloadV4` 导入 OP 块
(执行=真账本桥链路)+ FCU 推进,验收 = 33 向量金向量 gate 全 VALID。

### 1.1 关键既有资产(勘察核验)

- `EngineServiceImpl` 对 SchedulerType/ExecutorType/GlobalStateStorageType 全模板
  (鸭子类型)——同型 OpScheduler 可零侵入换型插入;
- 接缝签名(`SchedulerSerialImpl.h:34`):`executeBlock(Storage&, TransactionExecutor&,
  BlockHeader const&, input_range auto const&, LedgerConfig const&) →
  Task<vector<TransactionReceipt::Ptr>>`;engine 调用点 `EngineServiceImpl.h:524`,
  stateRoot/txRoot 两处 TODO 即预留插槽;
- 真账本桥交付物:`Storage2Ledger`(storage2 之上的 StateView,毒旗/写穿/一块一实例
  契约)、`LedgerSeed`、泛型 `stateRootOf`、`processOpBlock`/`sealOpBlock`(33 向量
  E-b gate 全绿);
- 向量语料:33 条 Isthmus/Jovian,含原始 signedTxEnvelope 与 deposit 原字节。

## 2. 已定决策

| 决策点 | 结论 |
|---|---|
| 基座 | `feat-evm-ledger-bridge` 继续叠(依赖零缺口;合流债沿既有台账机制记账) |
| 接入形态 | **方案 A:同型 OpScheduler 鸭子接缝**;engine 直调 processOpBlock(B)与改通用调度器本体(C)否决 |
| 目标版本 | **Engine V4 + Isthmus/Jovian**(与向量语料、OpForkSchedule 对齐;FCU 仍 V3,规范如此) |
| 验收环境 | **金向量 ctest**(33 向量包装 newPayloadV4 经完整 engine 服务层);JWT/devnet/op-node 实连下期 |
| 非目标 | JWT、devnet、出块(getPayload OP 化)、重组窗口(FCU 保持单调约束)、eth_* RPC 面、OP meta 进回执 RPC、增量 stateRoot(仍全量建根) |

## 3. 架构与模块布局

```
bcos-codec/bcos-codec/rlp/
└── EthBlockHeader.{h,cpp}        # ETH 头结构 + RLP 编码 + keccak 块哈希(ETH 通用件)

bcos-evm/bcos-evm/engine/         # 纯模板头,framework 依赖(同 Storage2Ledger 例)
├── OpSchedulerImpl.h             # 同型鸭子接缝 executeBlock + 带 seal 的扩展返回
└── OpReceiptMap.h                # OP 回执 → protocol::TransactionReceipt(账本存储面)

engine/bcos-engine/               # 既有骨架,增量改造(通用路径零变化)
├── EngineServiceImpl.h           # +V4;OP newPayload 分支;TODO 插槽换能力探测
└── (组合根)                      # 链配置开关(opstack.enabled,要求 executor.version>=1)
                                  #   选 OpSchedulerImpl 实例化;旧链 flag-OFF 零影响
```

依赖规则:
1. `OpSchedulerImpl.h` 只依赖 `bcosevm` + bcos-framework 头,不反向进 engine;
2. `EthBlockHeader` 放 bcos-codec(engine/rpc/测试共同消费);
3. 通用调度器(SchedulerSerialImpl/Parallel)零改动;OP/通用切换在组合根模板实例化。

**交易载体**:利用鸭子接缝的元素类型自由度——OP 模式下 engine 传 payload 的**原始交易
字节 range**(`bytes_view` 序列),不强塞 `protocol::Transaction`;0x7E deposit 与
typed envelope 原字节天然保真(G-1/L1 cost 计费输入)。通用路径的既有调用不受影响。

## 4. OpSchedulerImpl 内部流程

**接缝形状的诚实说明**:"同型"指返回形状与调用位形;OP 版扩展两个参数类型——交易范围
为原始字节 range,第三参为 `OpBlockEnv`(FISCO `BlockHeader` 引用 + OP 独有字段:
`prevRandao`/`baseFeePerGas`/`feeRecipient`/`parentBeaconBlockRoot`/`parentHash`,
由 engine OP 分支从 payload 组装)。

```
① cfg = OpForkSchedule::configAt(env.timestamp);chainId ← 链配置
② Storage2Ledger bridge{storage}          // 一块一实例;storage 即 engine fork 的 mutable 层
③ 交易分拣(逐条原始字节):0x7E → OpDepositTx;否则 typed envelope → evmone
   Transaction + 原 envelope 挂 props;首笔必须 L1-attributes deposit,违约 →
   OpConsensusError
④ blockInfo ← env;BlockHashes 由 env.parentHash 提供 number-1;
   processOpBlock(bridge, blockInfo, hashes, txs, cfg, vm, chainId,
                  apply = bridge.applyDiff)         // E-b 验证过的原链路
⑤ bridge.poisoned() → OpStorageError               // engine 转 RPC error,绝不 INVALID
⑥ sealOpBlock(result, cfg, messagePasserStorage←bridge) + stateRootOf(bridge)
                                                    // 桥销毁前算完,守一块一实例契约
```

**返回类型**:

```cpp
struct OpExecuteBlockResult {
    std::vector<protocol::TransactionReceipt::Ptr> receipts;  // 账本存储面
    OpBlockSeal seal;  // stateRoot/receiptsRoot/txRoot/logsBloom/gasUsed/withdrawalsRoot
};
```

stateRoot 必须在桥实例销毁前计算(桥 spec §4.2 一块一实例契约),不留给 engine 事后
另调;engine 的 stateRoot/txRoot 两处 TODO 在 OP 模式下整体废弃,以
`if constexpr (requires { result.seal; })` 探测——scheduler 返回带 seal 的结果即直取,
否则走旧路径,通用调度器零感知。

**回执映射(OpReceiptMap)**:只映射账本存储必需面(status/gasUsed/logs);
receiptsRoot/logsBloom **不从 FISCO 回执重算**,直接采 `seal`(op-geth 口径,
`encodeReceiptForRoot` 已有)——两套口径不混。OP meta 不进 protocol 回执(非目标)。

**错误分类表**:

| 错误 | 类型 | engine 动作 |
|---|---|---|
| 解码失败/首笔违约/排序错 | `OpConsensusError` | INVALID(latestValidHash=parent) |
| seal 与 payload 头不匹配 | engine 比对得出 | INVALID + validationError=字段名 |
| 毒旗/存储异常 | `OpStorageError` | JSON-RPC internal error(绝不 INVALID) |

## 5. ETH 头哈希层与 engine 改造

### 5.1 EthBlockHeader(bcos-codec/rlp)

- OP Isthmus 头字段全集(21 字段):parentHash/ommersHash(恒空叔块哈希)/feeRecipient/
  stateRoot/transactionsRoot/receiptsRoot/logsBloom/difficulty(=0)/number/gasLimit/
  gasUsed/timestamp/extraData(Holocene+ 携 eip1559Params,验证者模式原样取自 payload)/
  prevRandao/nonce(=0)/baseFeePerGas/withdrawalsRoot/blobGasUsed/excessBlobGas/
  parentBeaconBlockRoot/requestsHash(Isthmus = sha256(""),`OP_EMPTY_REQUESTS_HASH`);
- `encode()→RLP`、`hash()=keccak256(RLP)`;
- 金值判据:优先取 33 向量已有头字段/blockHash;不足则 pinned op-geth 离线算 3 例
  **硬编码进单测**,绝不改向量文件。

### 5.2 engine 骨架改造清单(增量,通用路径零变化)

1. **V4**:`isVersionSupported` 放宽至 V4;`engine_newPayloadV4`/`engine_getPayloadV4`
   端点;请求 `executionRequests[]` OP Isthmus 恒空,非空→INVALID;FCU 仍 V3。
2. **newPayload OP 分支**:
   - 静态校验:`EthBlockHeader` 重组头 `hash()==payload.blockHash` 不等→
     `InvalidBlockHash`;OP 约束:`withdrawals=[]`、`expectedBlobVersionedHashes` 必空、
     `excessBlobGas=0`;
   - 执行:组 `OpBlockEnv` → `OpSchedulerImpl::executeBlock` → `result.seal` 与
     payload 头逐字段比对 → 按 §4 错误分类表定 VALID/INVALID/error;
   - **块登记**:VALID 后 ETH `blockHash→number` 索引、头 RLP、FISCO 回执写入同一
     mutable 层再 `pushView`;`getBlockNumber(view, hash)` 既有查询在 OP 模式下
     即以 ETH 哈希为键成立,FCU 三标签校验不改。
3. **FCU OP 语义**:验证者模式 op-node 不发 payloadAttributes;OP 模式收到 attributes
   → 显式返回错误(防静默走通用 mempool 路径产出非 OP 块)。

## 6. 金向量 engine gate 与测试

**gate 落位**:`bcos-evm/test/opstack/EngineNewPayloadGateTest.cpp`,进
`bcos-evm-opstack-tests` 的 framework 门控块(`EngineServiceImpl` 模板头直接 include,
`EngineServiceImpl.cpp` 编入测试源);engine 既有 Boost.Test 套件不动。

**gate 流程(33 向量逐条)**:pre 经 `LedgerSeed` 播内存 storage2(E-b 同款 fixture)
→ fixture 预登记 parent(blockHash→number)→ 向量包装 ExecutionPayloadV4(头字段←
向量 expected,transactions←向量原始 envelope 字节)→ `newPayload(request, V4)` →
断言 VALID + `latestValidHash==blockHash` → FCU{head=该块} → 断言三标签推进。

**变异分档判别**(测试内存中变异,不动向量文件):篡改 blockHash 一字节→
`InvalidBlockHash`;篡改 stateRoot/gasUsed→INVALID 且 validationError 点名字段;
executionRequests/withdrawals 非空→INVALID;注入 ThrowingStorage→RPC error 而非
INVALID(错误分类判别器)。

**单测层**:EthBlockHeader(金值 hash/RLP 边界);OpSchedulerImpl(分拣与首笔违约、
毒旗分类、seal 完整性);engine OP 分支(V4 版本闸、attributes 拒绝、通用模式零回归)。

**探针留痕**(沿既有纪律,注入→翻红→复绿入报告):blockHash 校验探针、seal 比对探针、
错误分类探针。

## 7. 验收清单

- [ ] 金向量 gate:33/33 `newPayloadV4` VALID + `latestValidHash` 正确 + FCU 三标签推进
- [ ] 变异分档:四类变异全部按错误分类表分档正确
- [ ] EthBlockHeader 金值 hash 断言绿
- [ ] engine 既有 Boost.Test 套件零回归;桥双路(156/131 基线)零回归
- [ ] 三探针翻红并复绿,留痕在案
- [ ] `ports/`、`vectors/` 零触碰;通用调度器文件零改动

## 8. 风险与预案

| 风险 | 预案 |
|---|---|
| 向量缺完整头字段/blockHash,payload 包装不齐 | 先做向量字段盘点(计划首任务);缺则 pinned op-geth 离线补金值硬编码,不动向量 |
| FISCO BlockHeader 无法承载 OP 字段 | `OpBlockEnv` 扩展参数承载,不改 FISCO 头结构 |
| ETH 哈希索引与 FISCO 哈希索引混淆 | OP 模式下账本索引键统一 ETH 哈希;spec 明文,gate 的 FCU 推进即判别器 |
| engine 骨架并发模型与桥单线程契约冲突 | newPayload/FCU 已持 x_state 锁序列化关键段;OP 执行段全程单线程,桥一块一实例 |
| 合流债(基座为桥分支) | 沿台账机制记账;本设计不触 opstack 源文件与护栏,合流冲突面限于新增文件 |

## 9. 边界重申

本闭环交付**验证者模式的 engine 层等价证据**(金向量驱动);不构成与真实 op-node
互操作的宣称——JWT/devnet/出块/重组/eth_* RPC 面均为下期。E-b park 的其余层
(编排完整接入、生产可用宣称、Karst 真适配)维持既有边界不变。
