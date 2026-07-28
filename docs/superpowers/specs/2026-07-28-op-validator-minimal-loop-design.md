# OP 验证者模式最小闭环设计(engine OP 化 + OpScheduler 组件 + ETH 头哈希层)

日期:2026-07-28
状态:**rev.2**——rev.1 经 4 视角并行审查(事实/协议/设计/测试,约 40 项发现全数采纳)
重写。rev.1 的三个核心假设被证伪并在本版修正:①":524 接缝+TODO 插槽"锚错位置
(该调用点在 OP 模式不可达的 buildPayload 路径);②"验证者 op-node 不发
payloadAttributes"不成立(派生/强制构块路径必发);③"向量语料金值基本够用"不成立
(33/33 全缺 blockHash/txRoot/extraData/excessBlobGas,deposit 无原字节)。
基座:`feat-evm-ledger-bridge` 之上开新分支
前置文档:`2026-07-27-real-ledger-bridge-design.md`(桥 spec)、
`2026-07-24-opstack-block-execution-port-design.md`(移植 spec)
用户指示锚点:OP 执行以 **scheduler_v1 + TransactionExecutor 形态的调度组件**承载
(OpSchedulerImpl),不在 engine 里散写执行逻辑。

## 1. 背景与现状(rev.2 修正后的如实描摹)

- `EngineServiceImpl`(engine/bcos-engine)四类型全模板(`:76-81`,SchedulerType 受
  C++20 concept `scheduler_v1::TransactionScheduler<..., vector<Transaction::Ptr>>`
  约束——**编译期无条件检查,与运行时可达性无关**);
- **`handleNewPayload` 现状(`:355-441`):对外部 payload 零执行、零语义验证
  (仅结构校验)、零落库**——parentKnown 判据是内存态(`m_forkchoiceState.headBlockHash`
  / `m_blockHashToPayloadId`),外部 payload 登记即 Valid。本设计的 OP 分支是
  **从零新增**执行+验证+落库能力,不是增量改判据;
- `m_scheduler.executeBlock` 唯一调用点 `:524` 与 stateRoot/txRoot 两处 TODO
  (`:528/:612`)都在 `buildPayload`(FCU-attributes 出块路径)——本期 OP 模式下
  **不可达**,不作为本设计的接缝资产;
- `handleNewPayload` 现存"持 `x_state` 锁跨 `co_await`"的 TODO 自认(`:427-429`,
  POSIX UB 风险)——OP 执行段将整体运行于该锁下,安全前提见 §4.4,该债记入合流台账;
- FCU 状态机:head 单调(+1 或持平校验哈希);**head 回退时静默返回 Valid 且不更新
  跟踪状态**(既非拒绝也非推进)——本设计沿用此语义并明示;
- 桥交付物(`Storage2Ledger`/`LedgerSeed`/泛型 `stateRootOf`/`processOpBlock`/
  `sealOpBlock`)33 向量 E-b gate 全绿;
- 向量语料实测:`env`+`_op_expected` 供 15 个头字段;**33/33 均无
  blockHash/transactionsRoot/extraData/excessBlobGas**;typed tx 有原字节(`_op_raw`),
  **deposit 仅结构化字段(`_op_deposit`)无原字节**;
- `.git/info/exclude` 被并行会话加了 `docs/superpowers/` 等排除(本 spec 以
  `git add -f` 入库)。

## 2. 已定决策(rev.2 增补)

| 决策点 | 结论 |
|---|---|
| 基座 | `feat-evm-ledger-bridge` 继续叠 |
| 组件形态 | OpSchedulerImpl **双签名**:OP 专用 `executeOpBlock(...)→OpExecuteBlockResult` + 满足 concept 的通用签名 `executeBlock(...)`(哑实现,调用即 throw)——不动 framework concept、不动通用调度器文件 |
| 接缝定位(修正) | OP 执行是 `handleNewPayload` OP 分支的**全新调用**(调 `executeOpBlock`);`:524`/两处 TODO 原样不动;**rev.1 的 seal 能力探测机制整段删除** |
| 目标版本 | newPayloadV4/getPayloadV4 + FCU V3(Isthmus/Jovian;FCU 无 V4,OP FCU 版本 Ecotone+ 即 V3) |
| 验收环境 | 金向量 ctest;**金值策略重写**:全部 33 条离线补算(§7.1),非"3 例" |
| 交易载体 | `bcos-framework/engine/Types.h` 的 `ExecutionPayload` 增**可选** `rawTransactions`(bytes 列表)+ OP 扩展字段 `withdrawalsRoot`;通用路径不填不读,行为零变化;EngineEndpoint RPC 解析层 OP 分支**本期豁免**(gate 直调 EngineServiceImpl),列 op-node 实连前置欠账 |
| V4 放宽门控 | 版本上界成员化(构造参数/模板策略),**仅 OP 组合根放宽至 V4**;通用组合根对 V4 行为与现状逐字节一致,配探针(§7.4) |
| 非目标 | JWT、devnet、attributes 构块(=getPayload OP 化,见 §6.4)、重组窗口、eth_* RPC 面、OP meta 回执 RPC、增量 stateRoot、RPC 层 OP 交易解析 |

## 3. 模块布局

```
bcos-codec/bcos-codec/rlp/
├── EthBlockHeader.{h,cpp}        # ETH/OP 头结构 + RLP + keccak 块哈希(§5)
└── OpDepositEncode.{h,cpp}       # deposit tx 0x7E envelope 编码器(§7.1,从结构化字段重建)

bcos-evm/bcos-evm/engine/         # 纯模板头(framework 依赖,同 Storage2Ledger 例)
├── OpSchedulerImpl.h             # 双签名调度组件(§4)
└── OpReceiptMap.h                # OP 回执 → protocol::TransactionReceipt

bcos-framework/bcos-framework/engine/Types.h   # ExecutionPayload 增 rawTransactions +
                                               # withdrawalsRoot(可选字段,通用路径零行为变化)

engine/bcos-engine/EngineServiceImpl.h         # newPayload OP 分支(§6,从零新增)
bcos-evm/test/opstack/
├── EngineNewPayloadGateTest.cpp  # 金向量 gate(§7)
└── t8n/golden/engine/            # 33 条离线金值表(blockHash/txRoot/extraData 选值,
                                  #   新目录,不触碰 vectors/)
```

依赖规则同 rev.1(OpScheduler 不反向进 engine;通用调度器文件零改动;bcos-framework
的 Types.h 改动限于新增可选字段)。

## 4. OpSchedulerImpl(调度组件)

### 4.1 双签名

```cpp
template <class Storage>
class OpSchedulerImpl {
public:
    // 满足 scheduler_v1::TransactionScheduler concept(类模板实例化需要);
    // OP 模式下不可达,调用即 throw(防误用,含明确错误消息)
    task::Task<std::vector<protocol::TransactionReceipt::Ptr>> executeBlock(
        Storage&, auto& executor, protocol::BlockHeader const&,
        ::ranges::input_range auto const&, ledger::LedgerConfig const&);

    // OP 专用入口(handleNewPayload OP 分支调用)
    task::Task<OpExecuteBlockResult> executeOpBlock(Storage& storage,
        OpBlockEnv const& env, ::ranges::input_range auto const& rawTxBytes);
};
struct OpExecuteBlockResult {
    std::vector<protocol::TransactionReceipt::Ptr> receipts;
    OpBlockSeal seal;   // 六字段,桥销毁前算毕
};
```

- `executeOpBlock` 为模板方法(`input_range auto`),接收原始字节 range;
- **vm 归属**:`evmc::VM`(evmone)为 OpSchedulerImpl 成员,每调度器一实例进程级复用;
  线程安全依据 = engine 执行段被 `x_state` 串行化(§4.4);
- **receipt factory 注入**:构造参数传入 `protocol::TransactionReceiptFactory::Ptr`
  (OpReceiptMap 消费)。

### 4.2 OpBlockEnv(rev.2 补 gasLimit)

`{ BlockHeader const& fiscoHeader; bytes32 parentHash; bytes32 prevRandao;
uint256 baseFeePerGas; address feeRecipient; bytes32 parentBeaconBlockRoot
(OP 语义:L1 origin 的 parent beacon root,EIP-4788 系统调用输入,参与 blockHash);
uint64 gasLimit(FISCO 头无此字段,payload 是唯一来源——执行 gas pool 上限必需);
bytes extraData; uint64 blobGasUsed; }`。chainId 读链配置键 `web3.chain_id`
(既有 web3 链配置通道;gate fixture 负责播种,§7.3 前置清单)。

### 4.3 执行六步

同 rev.1(分拣→桥→processOpBlock→毒旗→sealOpBlock+stateRootOf,错误分类
OpConsensusError/OpStorageError),仅修正:deposit 解码接收**结构化重建后的 envelope**
或原字节(gate 侧经 OpDepositEncode 重建,实连侧 payload 本就携带原字节)。

### 4.4 协程上下文契约(rev.2 新增,Critical C1 落地)

本设计的执行链拓扑为**嵌套 syncWait**:外层驱动(gate 的 `task::syncWait(newPayload)`
或生产 `task::wait`)→ `executeOpBlock` 协程帧内 → 桥读方法 `syncWait` →
(stateRoot 段)`visitAccounts` syncWait → 惰性 code getter syncWait(三层)。
这超出桥 spec §10.1/I-3 的"仅一层、仅一个调用点"例外。**本 spec 显式扩许可**,
安全前提:
1. 桥对接的 storage2 后端全部 co_await **线程内同步完成**(内存 MultiLayerStorage;
   RocksDB 为线程内阻塞读),内层任务从不真正让出线程、外层协程从不跨线程恢复,
   嵌套 syncWait 退化为纯栈递归;
2. engine 执行段被 `x_state` 锁串行(单线程);
3. **失效判据**:任何后端引入跨线程/事件循环异步完成,本许可即失效,必须重新设计
   (该判据同时覆盖 §1 的"持锁跨 co_await"现存 TODO——同一前提)。

**同步修订义务(实施计划首批任务)**:桥 spec §10.1 与 `Storage2Ledger.h` 头注的
"仅一层嵌套"例外改写为上述**条件式许可**(安全前提 + 失效判据),否则文档与代码
事实矛盾。

## 5. EthBlockHeader 与 payload 结构

### 5.1 头字段(21 字段,rev.2 补注)

字段全集与固定值同 rev.1,rev.2 补三注:
- **extraData**:Holocene+ 编码 pin 为 **9 字节 = 0x00(version) ‖ uint32 denominator
  ‖ uint32 elasticity(大端)**;验证者对 extraData 做形状校验(长度 9/version=0/可解码)
  是 op-geth 行为,本期**不做**,记简化台账;
- **blobGasUsed**:Isthmus 应校验 =0;**Jovian 起该槽复用为 DA footprint**
  (`OpBlockSeal.h:31-38` 口径),由 seal 比对承接,不当常量处理;
- **requestsHash** = sha256("")(`OP_EMPTY_REQUESTS_HASH`,与向量值三方互证)。

### 5.2 ExecutionPayload 的 OP 结构扩展(rev.2 新增,协议 I1)

**OP Isthmus 扩展了 payload 结构:新增 `withdrawalsRoot` 字段**(= MessagePasser
storage root,无法从恒空 withdrawals 列表推得;op-geth NewPayloadV4 在 OP 链上强制
要求)。`Types.h` 的 ExecutionPayload 增该可选字段;blockHash 重建依赖它。
另注:newPayloadV4 的参数是 ExecutionPayloadV3 结构(+OP withdrawalsRoot 扩展)
+ 三个旁路参数——"ExecutionPayloadV4"并非独立结构名。

## 6. engine newPayload OP 分支(从零新增)

### 6.1 校验与执行流程

1. **时间戳×版本闸**(协议 I5):payload.timestamp 所属 fork 与方法版本不匹配 →
   JSON-RPC `-38005 Unsupported fork`(Isthmus+ 禁 V3,pre-Isthmus 禁 V4);
2. 静态校验:`EthBlockHeader` 重组头(含 withdrawalsRoot/extraData 原样)
   `hash()==payload.blockHash`,不等 → **INVALID + latestValidHash=null**
   (`INVALID_BLOCK_HASH` 状态自 Shanghai 已废弃,OP 分支不用;既有通用路径的
   InvalidBlockHash 枚举保留,偏离台账记一条);
   `withdrawals` 在场且空、`expectedBlobVersionedHashes` **在场且为空数组**
   (元数固定,缺参为 -32602)、`excessBlobGas=0`、`executionRequests` 在场且空
   (非空→INVALID+latestValidHash=null,归 blockHash 失配桶;畸形列表→-32602);
3. **parentKnown(rev.2 修正)**:OP 分支以 **storage 查询**为准
   (`getBlockNumber(view, parentHash, fromStorage)`),未知 → **SYNCING,不入库**
   (op-node 依赖此语义触发同步;内存 map 判据留给通用路径);
4. 执行:组 `OpBlockEnv` → `executeOpBlock` → `result.seal` 与 payload 头逐字段比对
   (六字段全比)→ 错误分类表(OpConsensusError→INVALID;OpStorageError→
   -32603 internal error,绝不 INVALID);
5. **INVALID 的 latestValidHash**:规范为"最近有效祖先";本期单调链下 = parent
   **当且仅当 parent 已验证有效**(成立条件明示);blockHash 失配桶恒 null;
6. **块登记(表级清单,rev.2 补)**:VALID 后写入同一 mutable 层:
   `SYS_HASH_2_NUMBER` + `SYS_NUMBER_2_HASH`(键=hash 原始 32 字节、值=number 十进制
   串,编码逐字节抄 `BaselineScheduler.h:207-220` 生产先例)、ETH 头 RLP(新表
   `s_eth_block_header`,键=number)、OP 回执(经 OpReceiptMap,复用既有回执表通道)
   → `pushView`;`SYS_CURRENT_STATE` 的 current number 在 **FCU head 推进时**写
   (newPayload 不动 head,规范语义);merge 时机:FCU 经 `fork()` 读,登记数据须
   `pushView` 后即可见(MultiLayerStorage 层内可见性,无需提前 merge)。

### 6.2 FCU OP 语义(rev.2 修正,协议 C1)

**理由改写**:op-node 验证者在 L1 派生路径(consolidation 失败或无 unsafe 块)**必发**
带 OP 扩展 attributes 的 FCU(`noTxPool=true` + 派生交易)——"验证者不发 attributes"
不成立。本期**不支持 attributes 构块**(与 getPayload OP 化同属一条路,列 op-node
实连前置欠账):OP 模式收到 attributes → JSON-RPC **`-38003 Invalid payload
attributes`,且 forkchoiceState 更新不回滚**(head 照常推进,仅不开启 build,规范语义)。
attributes 的 OP 扩展字段归属(транsactions/noTxPool/gasLimit 自 Bedrock,
eip1559Params 自 Holocene,均挂在 attributes 对象、不引入新 FCU 版本)记录在案。

### 6.3 版本闸与端点

`isVersionSupported` 上界**成员化**(构造参数),仅 OP 组合根放宽至 V4;通用组合根
对 V4 请求行为与现状逐字节一致(探针 §7.4)。`engine_getPayloadV4` 端点:OP 模式下
**返回错误**(出块未 OP 化,明确错误消息),响应结构含 executionRequests 的完整
OP 化随 attributes 构块欠账一并交付。

### 6.4 op-node 实连前置欠账台账(本期明确不做,防"绿灯=可实连"误读)

attributes 构块(FCU attrs + getPayloadV4)、RPC 解析层 OP 交易载体、SYNCING 完整
语义(缓存回填/侧链 ACCEPTED)、JWT、extraData 形状校验、INVALID_BLOCK_HASH 枚举
清理、重组窗口。

## 7. 金向量 gate 与测试(rev.2 重写)

### 7.1 金值策略(事实 Critical 落地)

- **33/33 向量无 blockHash/txRoot/extraData/excessBlobGas;deposit 无原字节**——
  这是结构性盲区,不是边缘缺口;
- **离线金值仪式**(沿 t8n generator 先例,pinned op-geth v1.101702.2):为**全部
  33 条**补算 `blockHash`/`transactionsRoot`,同时固定 `extraData` 选值(1559 params,
  否则金值不可复算)与 `excessBlobGas=0`;产物入 `test/opstack/t8n/golden/engine/`
  (新目录,**vectors/ 逐字节不动**);deposit envelope 由 `OpDepositEncode` 从
  结构化字段重建(编码器自身以金值锚定:重建字节参与 txRoot,txRoot 对上 op-geth
  金值即编码器正确性的判据);
- **金值样本结构覆盖**:Isthmus/Jovian 两时代、单笔/多笔(非平凡 trie)、
  deposit-only、setcode——由 33 条全量覆盖天然满足;
- **诚实口径**:blockHash/txRoot 的判别力来自离线金值(op-geth 背书),**非**
  "自己算自己验"的自洽比对;gate 断言 `payload.blockHash == golden.blockHash`
  且 `seal.txRoot == golden.txRoot`,两侧独立来源交叉。

### 7.2 gate 流程

同 rev.1 骨架(pre 播种→包装→newPayloadV4→VALID→FCU 推进),rev.2 增:
- **两块链式用例**(测试 I-2):取 2-3 条向量拼接(A 的 blockHash 设为 B 的
  parentHash):`newPayload(A)→VALID → FCU(head=A) → newPayload(B)` 断言 B 的
  parent-known **经块登记自然满足**(非 fixture 预注册);跳过 FCU 直接投未知
  parent 的块 → SYNCING。这是"闭环"成立的因果证据;
- 33 条孤立向量的 parent 仍由 fixture 预登记(storage 写入,`SYS_HASH_2_NUMBER`
  编码同 §6.1.6)。

### 7.3 变异分档矩阵(逐分支,测试 I-1)

| §6 校验分支 | 变异用例 |
|---|---|
| 时间戳×版本闸 | Isthmus payload 走 V3 → -38005 |
| blockHash 重组 | 篡改一字节 → INVALID + latestValidHash=null |
| withdrawals 非空 | → INVALID |
| expectedBlobVersionedHashes 非空 | → INVALID(独立用例) |
| excessBlobGas ≠0 | → INVALID(独立用例) |
| executionRequests 非空 | → INVALID + null |
| seal 六字段 | stateRoot/gasUsed/receiptsRoot/logsBloom/withdrawalsRoot/txRoot **各一例**篡改 → INVALID + validationError 点名字段 |
| parent 未知 | → SYNCING |
| attributes 拒绝 | OP 模式 FCU 带 attrs → -38003 且 head 照常推进(断言两点) |
| 通用版本闸 | 通用组合根收 V4 → 行为与现状一致 |
| 存储故障 | ThrowingStorage → -32603 而非 INVALID |

### 7.4 探针(五个,留痕文件 `.superpowers/sdd/probe-op-validator-gate-report.md`)

1. blockHash 校验探针(篡改必红);2. seal 比对探针;3. 错误分类探针(-32603 vs
INVALID);4. **块登记接线探针**(测试 M-1:注入跳过块登记写入 → 两块链式用例第二块
必转 SYNCING);5. **通用版本闸探针**(测试 C-2:通用组合根 V4 行为零漂移)。
留痕纪律同桥项目(注入 diff/翻红原文/回退/复绿/git status)。

### 7.5 单测层

EthBlockHeader(**金值来自 §7.1 离线表而非自算**,含 Isthmus/Jovian extraData 两编码;
RLP 边界);OpDepositEncode(重建字节 vs 金值 txRoot);OpSchedulerImpl(双签名:
通用签名调用即 throw;分拣/首笔违约/毒旗分类/seal 完整性);engine OP 分支单测
(版本闸/attributes 语义/latestValidHash 取值)。

## 8. 验收清单(N0 相对基线 + 命令化)

- [ ] 金向量 gate:`--gtest_filter='EngineNewPayloadGate.*'` 33/33 VALID,
      `latestValidHash==blockHash`;**blockHash/txRoot 与离线金值交叉断言 33/33**
- [ ] 两块链式用例绿(parent-known 经块登记因果成立)+ 未知 parent→SYNCING
- [ ] 变异矩阵(§7.3)11 行逐行分档正确
- [ ] `--gtest_filter='EthBlockHeader.*:OpDepositEncode.*'` 金值单测绿
- [ ] 基线零回归:合并前捕获 N0(`--gtest_list_tests` 双路各一份 +
      engine Boost.Test `--list_content`),合并后 N0 全过;新增名单入报告
- [ ] 五探针翻红复绿留痕(`probe-op-validator-gate-report.md`)
- [ ] `git diff --stat <基座> -- ports/ bcos-evm/test/opstack/t8n/vectors/` 空;
      通用调度器文件(`transaction-scheduler/`)零改动
- [ ] 桥 spec §10.1 + `Storage2Ledger.h` 头注的条件式许可修订已落(§4.4 义务)

## 9. 风险与预案

| 风险 | 预案 |
|---|---|
| 离线金值仪式依赖 pinned op-geth 环境 | 沿 t8n generator README 仪式;金值表带生成记录(pin SHA/命令),可复算 |
| deposit 编码器与 op-geth 字节不一致 | txRoot 金值即判据;逐字段对齐 op-geth types/deposit_tx.go |
| Types.h 通用件改动外溢 | 仅新增可选字段;通用路径不填不读;N0 基线回归兜底 |
| concept 哑签名被误调 | throw + 明确消息;单测钉住 |
| 嵌套 syncWait 前提失效(未来异步后端) | §4.4 失效判据明文;头注同步修订 |
| x_state 跨 co_await 现存 UB TODO | 同一前提覆盖;债入合流台账 |
| 33 金值生成工作量 | 一次性仪式,产物入库;计划中列为独立任务并给退出条件 |

## 10. 边界重申

本闭环交付**验证者模式的 engine 层等价证据**(金向量驱动 + 因果闭环用例);
§6.4 欠账清单未清前,不构成与真实 op-node 互操作的宣称。E-b park 其余层
(编排完整接入、生产可用、Karst 真适配)边界不变。
