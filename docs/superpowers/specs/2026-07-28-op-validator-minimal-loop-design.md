# OP 验证者模式最小闭环设计(engine OP 化 + OpScheduler 组件 + ETH 头哈希层)

日期:2026-07-28(rev.3.1 回填:2026-07-29)
状态:**rev.3.1**——实现完成后的**纯文档修订**(T7,不改任何已实现语义):①§7.3
latestValidHash 断言口径按裁定 I2 修订为"非 blockHash 桶**且已过 parentKnown**才断言
parentHash,step 2 静态校验阶段统一 null"(采纳实现口径,修 spec 而非改实现);②§6.1
补记 step 3b 父子块号连续性校验(T5b 实现已有、rev.3 漏载);③§6.4 欠账台账追加 5 条
实现期发现;④§8 逐条打钩并回填实测值。以下 rev.3 原文除这四处外**逐字保留**。

rev.3 原状态:spec-plan 成对审查(4 视角:事实/协议/设计/测试,30 项裁定全数
采纳)重写,裁定书:`.superpowers/sdd/validator-loop-rev3-directive.md`。rev.2→
rev.3 结构性改动:①`OpExecuteBlockResult` 拆为**六项比对面**(seal 3 字段原样不动
+ result 3 个独立成员 stateRoot/gasUsed/txRoot,替代 rev.2 对 seal 结构的误称);
②链式向量对改**离线专生成**(op-geth `GenerateChainWithGenesis` 块数 1→2,拼接法因
静态校验/金值/state-number 三重矛盾作废);③extraData 改**生成块原样发射**(人工
选值方案与生成器既有惯例冲突,作废);④链头进度表(current number)本期**不写**
(FCU 保持只读+内存态,推进列入 §6.4 欠账);⑤fork 阈值经 `OpForkTimestamps` 构造
参数注入(不动 SystemConfigs);⑥`engine_newPayloadV4`/`getPayloadV4` 的 RPC 端点
注册**整体列入 §6.4 欠账**,本期仅 `EngineServiceImpl` 直调。

rev.2 的历史更迭(供溯源):rev.1 经 4 视角并行审查(约 40 项发现全数采纳)重写,
修正三个被证伪的核心假设——①":524 接缝+TODO 插槽"锚错位置(该调用点在 OP 模式
不可达的 buildPayload 路径);②"验证者 op-node 不发 payloadAttributes"不成立
(派生/强制构块路径必发);③"向量语料金值基本够用"不成立(33/33 全缺
blockHash/txRoot/extraData/excessBlobGas,deposit 无原字节)。

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

## 2. 已定决策(rev.3 增补)

| 决策点 | 结论 |
|---|---|
| 基座 | `feat-evm-ledger-bridge` 继续叠 |
| 组件形态 | OpSchedulerImpl **双签名**:OP 专用 `executeOpBlock(...)→OpExecuteBlockResult` + 满足 concept 的通用签名 `executeBlock(...)`(哑实现,调用即 throw)——不动 framework concept、不动通用调度器文件;构造参数注入 chainId + `OpForkTimestamps`(rev.3,§4.2,裁定 A5) |
| 接缝定位(修正) | OP 执行是 `handleNewPayload` OP 分支的**全新调用**(调 `executeOpBlock`);`:524`/两处 TODO 原样不动;rev.1 的 seal 能力探测机制整段删除 |
| 目标版本 | newPayloadV4/getPayloadV4 + FCU V3(Isthmus/Jovian;FCU 无 V4,OP FCU 版本 Ecotone+ 即 V3);**本期语义层仅落在 `EngineServiceImpl`,RPC 端点注册不动**(rev.3,§6.3,裁定 A6) |
| 验收环境 | 金向量 ctest;金值策略重写:全部 33 条离线补算(§7.1)+ 1 对离线生成的链式向量(rev.3,裁定 A2),非"3 例" |
| 交易载体 | `bcos-framework/engine/Types.h` 的 `ExecutionPayload` 增**可选** `rawTransactions`(bytes 列表)+ OP 扩展字段 `withdrawalsRoot`;通用路径不填不读,行为零变化;**RPC 层(EngineEndpoint 解析/端点注册)本期整体豁免**(gate 与单测均直调 `EngineServiceImpl`),`newPayloadV4`/`getPayloadV4` 端点注册与 RPC 层 OP 交易解析一并列 op-node 实连前置欠账(rev.3 修正,裁定 A6) |
| V4 放宽门控 | 版本上界成员化(构造参数/模板策略),**仅 OP 组合根放宽至 V4**;通用组合根对 V4 行为与现状逐字节一致,配探针(§7.4);闸判定发生在 `EngineServiceImpl` 层,RPC 层未注册故不可达(rev.3) |
| 非目标 | JWT、devnet、attributes 构块(=getPayload OP 化,见 §6.4)、重组窗口、eth_* RPC 面、OP meta 回执 RPC、增量 stateRoot、RPC 层 OP 交易解析、**`engine_newPayloadV4`/`engine_getPayloadV4` RPC 端点注册**(rev.3,见 §6.4)、**链头进度表(current number)随 FCU head 推进写入**(rev.3,见 §6.4)、**Holocene EIP-1559 baseFee 父子一致性校验**(rev.3,见 §6.4) |

## 3. 模块布局

```
bcos-codec/bcos-codec/rlp/
├── EthBlockHeader.{h,cpp}        # ETH/OP 头结构 + RLP + keccak 块哈希(§5)
└── OpDepositEncode.{h,cpp}       # deposit tx 0x7E envelope 编码器(§7.1,从结构化字段重建)

bcos-evm/bcos-evm/engine/         # 纯模板头(framework 依赖,同 Storage2Ledger 例)
├── OpSchedulerImpl.h             # 双签名调度组件(§4)
└── OpReceiptMap.h                # OP 回执 → protocol::TransactionReceipt

bcos-evm/bcos-evm/opstack/OpForkSchedule.{h,cpp}   # 既有具名工厂(isthmusConfig()/
                                   # jovianConfig()/karstConfig());rev.3 新增
                                   # OpForkTimestamps + configAt(timestamp, ...)(§4.2)

bcos-framework/bcos-framework/engine/Types.h   # ExecutionPayload 增 rawTransactions +
                                               # withdrawalsRoot(可选字段,通用路径零行为变化)

engine/bcos-engine/EngineServiceImpl.h         # newPayload OP 分支(§6,从零新增)
bcos-evm/test/opstack/
├── EngineNewPayloadGateTest.cpp  # 金向量 gate(§7)
└── t8n/golden/engine/            # 33 条离线金值表(blockHash/txRoot/extraData 原样
                                  #   发射,新目录,不触碰 vectors/)
    └── chained/                 # rev.3 新增:1 对离线生成的链式向量(§7.1/§7.2)
```

依赖规则同 rev.1(OpScheduler 不反向进 engine;通用调度器文件零改动;bcos-framework
的 Types.h 改动限于新增可选字段)。

## 4. OpSchedulerImpl(调度组件)

### 4.1 双签名(rev.3 修正 A1:OpExecuteBlockResult 拆分为六项比对面)

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
    OpBlockSeal seal;      // 现有结构原样不动(receiptsRoot/logsBloom/
                            // withdrawalsRoot + requestsHash?/blobGasUsed?,
                            // OpBlockSeal.h:24-31),桥销毁前算毕
    bcos::h256 stateRoot;  // 净新增,与 seal 并列的独立成员
    uint64_t gasUsed;      // 净新增,与 seal 并列的独立成员
    bcos::h256 txRoot;     // 净新增(对 rawTransactions 建 trie,复用 vendored
                            // MPT/rlp),金值判据 golden.transactionsRoot;与 seal 并列
};
```

- `executeOpBlock` 为模板方法(`input_range auto`),接收原始字节 range;
- **六项比对面**术语约定(rev.3,替代 rev.2 对 seal 结构原字段合并计数的误称):`seal` 的
  receiptsRoot/logsBloom/withdrawalsRoot + `result` 的 stateRoot/gasUsed/txRoot,
  合计 6 项,全篇统一称呼(§6.1/§7.1/§7.3/§7.5 同口径);
- **vm 归属**:`evmc::VM`(evmone)为 OpSchedulerImpl 成员,每调度器一实例进程级复用;
  线程安全依据 = engine 执行段被 `x_state` 串行化(§4.4);
- **receipt factory 注入**:构造参数传入 `protocol::TransactionReceiptFactory::Ptr`
  (OpReceiptMap 消费)。

### 4.2 OpBlockEnv(rev.2 补 gasLimit;rev.3 补 fork 阈值注入与 chainId 口径)

`{ BlockHeader const& fiscoHeader; bytes32 parentHash; bytes32 prevRandao;
uint256 baseFeePerGas; address feeRecipient; bytes32 parentBeaconBlockRoot
(OP 语义:L1 origin 的 parent beacon root,EIP-4788 系统调用输入,参与 blockHash);
uint64 gasLimit(FISCO 头无此字段,payload 是唯一来源——执行 gas pool 上限必需);
bytes extraData; uint64 blobGasUsed; }`。

**chainId 注入口径(rev.3 修正,裁定 D2)**:经构造参数注入(与下述 fork 阈值同通道);
链配置键 `web3.chain_id` 的**读取责任在组合根**(engine 初始化处),`OpSchedulerImpl`
本身不读配置;gate fixture **直接传值**,不经配置读取路径。

**fork 阈值注入(rev.3 新增,裁定 A5)**:`OpForkSchedule::configAt` 现状无地基
(`bcos-evm/bcos-evm/opstack/OpForkSchedule.{h,cpp}` 现仅具名工厂
isthmusConfig()/jovianConfig()/karstConfig();SystemConfigs 无 op fork 键)。新增
`struct OpForkTimestamps { uint64_t isthmusTime; uint64_t jovianTime; }`,经
OpSchedulerImpl 构造参数注入(与 chainId 同通道,不动 SystemConfigs 枚举,守
framework 最小触碰);`OpForkSchedule::configAt(timestamp, OpForkTimestamps) →
OpForkConfig`(自由函数或静态方法,落 `bcos-evm/opstack`)按 timestamp 落
Isthmus/Jovian 分档;§6.1 第 1 步的 -38005 时间戳×版本闸判定**复用同一函数**,
不重复实现阈值比较逻辑;gate fixture 按向量 timestamp 播种两阈值(Isthmus 向量
∈ [isthmusTime, jovianTime)、Jovian 向量 ∈ [jovianTime, ∞))。

### 4.3 执行六步(rev.3 补 txRoot/gasUsed 产出)

同 rev.1(分拣→桥→processOpBlock→毒旗→sealOpBlock+stateRootOf,错误分类
OpConsensusError/OpStorageError),rev.2 修正:deposit 解码接收结构化重建后的
envelope 或原字节(gate 侧经 OpDepositEncode 重建,实连侧 payload 本就携带原字节)。
**rev.3 补**:第六步须同时产出 `txRoot`(对 rawTransactions 建 trie,复用 vendored
MPT/rlp)与 `gasUsed`(累计),连同 `stateRootOf` 结果一并填入
`OpExecuteBlockResult` 的三个独立成员(§4.1,**六项比对面**之三)。

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

### 5.1 头字段(21 字段,rev.3 修正 extraData 与 blobGasUsed 注)

字段全集与固定值同 rev.1,rev.2/rev.3 补三注:
- **extraData**:Holocene+ 编码 pin 为 **Isthmus 9 字节 = 0x00(version) ‖ uint32
  denominator ‖ uint32 elasticity(大端)**;**Jovian 17 字节**(在 Isthmus 9 字节
  基础上另含强制 minBaseFee,generator README:42/99-100 在案)。golden 的
  extraData 取**生成块 `header.Extra` 原样发射**,不做人工选值(rev.2 的人工选值
  方案作废——与生成器既有惯例冲突)。验证者对 extraData 做形状校验(长度/version=0/
  可解码)是 op-geth 行为,本期**不做**,记简化台账(§6.4);
- **blobGasUsed**:Isthmus 应校验 =0(变异矩阵独立用例,§7.3 "blobGasUsed≠0
  (Isthmus)"行);**Jovian 起该槽复用为 DA footprint**(`OpBlockSeal.h:31-38`
  口径),由 seal 比对承接(**六项比对面**之一),不当常量处理;
- **requestsHash** = sha256("")(`OP_EMPTY_REQUESTS_HASH`,与向量值三方互证)。

### 5.2 ExecutionPayload 的 OP 结构扩展(rev.2 新增,协议 I1)

**OP Isthmus 扩展了 payload 结构:新增 `withdrawalsRoot` 字段**(= MessagePasser
storage root,无法从恒空 withdrawals 列表推得;op-geth NewPayloadV4 在 OP 链上强制
要求)。`Types.h` 的 ExecutionPayload 增该可选字段;blockHash 重建依赖它。
另注:newPayloadV4 的参数是 ExecutionPayloadV3 结构(+OP withdrawalsRoot 扩展)
+ 三个旁路参数——"ExecutionPayloadV4"并非独立结构名。

## 6. engine newPayload OP 分支(从零新增)

### 6.1 校验与执行流程

1. **时间戳×版本闸**(协议 I5):payload.timestamp 所属 fork(经 §4.2 的
   `OpForkSchedule::configAt(timestamp, OpForkTimestamps)` 判定,同一函数,不
   重复实现阈值比较)与方法版本不匹配 → JSON-RPC `-38005 Unsupported fork`
   (Isthmus+ 禁 V3,pre-Isthmus 禁 V4);
2. 静态校验:`EthBlockHeader` 重组头(含 withdrawalsRoot/extraData 原样)
   `hash()==payload.blockHash`,不等 → **INVALID + latestValidHash=null**
   (`INVALID_BLOCK_HASH` 状态自 Shanghai 已废弃,OP 分支不用;既有通用路径的
   InvalidBlockHash 枚举保留,偏离台账记一条);
   `withdrawals` 在场且空、`expectedBlobVersionedHashes` **在场且为空数组**
   (元数固定,缺参为 -32602)、`excessBlobGas=0`、`blobGasUsed`(Isthmus 应=0,
   §5.1)、`executionRequests` 在场且空(非空→INVALID+latestValidHash=null,归
   blockHash 失配桶;畸形列表→-32602);
3. **parentKnown(rev.2 修正)**:OP 分支以 **storage 查询**为准
   (`getBlockNumber(view, parentHash, fromStorage)`),未知 → **SYNCING,不入库**
   (op-node 依赖此语义触发同步;内存 map 判据留给通用路径);
   **3b. 父子块号连续性(rev.3.1 补记,2026-07-29 T7 落地;语义源自 T5b 实现 + T5b 审查 I2,
   rev.3 原文漏载)**:parentKnown 成功后立即校验 `payload.blockNumber ==
   *parentBlockNumber + 1`,不等 → **INVALID + latestValidHash=parentHash**(parent 此时
   已被 step 3 确立为已验证祖先,故归"已过 parentKnown"档,与 step 4/5 同分类)。
   **它不是可选的加分项**:step 6 的两张登记索引(`SYS_NUMBER_2_HASH` 与 ETH 头表)都**以
   块号为键**,缺此校验时一个"parent 合法但 blockNumber 任意"的 payload 会静默覆写既有高度
   的索引条目——后果是链索引损坏,而非仅仅放行一个坏块;
4. 执行:组 `OpBlockEnv` → `executeOpBlock` → `result` 与 payload 头逐字段比对
   (**六项比对面**全比:seal 的 receiptsRoot/logsBloom/withdrawalsRoot + result
   的 stateRoot/gasUsed/txRoot)→ 错误分类表(OpConsensusError→INVALID;
   OpStorageError→ -32603 internal error,绝不 INVALID);
5. **INVALID 的 latestValidHash**:规范为"最近有效祖先";本期单调链下 = parent
   **当且仅当 parent 已验证有效**。**"parent 已验证"操作性定义(rev.3 新增,
   裁定 C2)**:⟺ parent hash 存在于 `SYS_HASH_2_NUMBER`(仅 VALID 分支写入,
   存在即已验证);fixture 预登记 = 对该不变式的显式豁免(等价可信创世前提),
   仅测试合法。块登记表**不随 FCU head 回退回滚**。blockHash 失配桶恒 null;
6. **块登记(表级清单,rev.2 补;rev.3 修正 A4)**:VALID 后写入同一 mutable 层:
   `SYS_HASH_2_NUMBER` + `SYS_NUMBER_2_HASH`(键=hash 原始 32 字节、值=number 十进制
   串,编码逐字节抄 `BaselineScheduler.h:207-220` 生产先例)、ETH 头 RLP(新表
   `s_eth_block_header`,键=number)、OP 回执(经 OpReceiptMap,复用既有回执表通道)
   → `pushView`;**本期链头进度表不写**(rev.2 的"FCU head 推进时写"作废——全新
   只读路径引入落库,原子性/写放大/测试面全未定义,且本期无消费者;FCU 保持现状
   只读+内存态,该表的推进列入 §6.4 欠账台账,编排接入时定);merge 时机:FCU 经
   `fork()` 读,登记数据须 `pushView` 后即可见(MultiLayerStorage 层内可见性,
   无需提前 merge)。

### 6.2 FCU OP 语义(rev.2 修正,协议 C1)

**理由改写**:op-node 验证者在 L1 派生路径(consolidation 失败或无 unsafe 块)**必发**
带 OP 扩展 attributes 的 FCU(`noTxPool=true` + 派生交易)——"验证者不发 attributes"
不成立。本期**不支持 attributes 构块**(与 getPayload OP 化同属一条路,列 op-node
实连前置欠账):OP 模式收到 attributes → JSON-RPC **`-38003 Invalid payload
attributes`,且 forkchoiceState 更新不回滚**(head 照常推进,仅不开启 build,规范语义)。
attributes 的 OP 扩展字段归属(transactions/noTxPool/gasLimit 自 Bedrock,
eip1559Params 自 Holocene,均挂在 attributes 对象、不引入新 FCU 版本)记录在案。

### 6.3 版本闸与端点(rev.3 修正 A6)

`isVersionSupported` 上界成员化(构造参数),仅 OP 组合根放宽至 V4;通用组合根
对 V4 请求行为与现状逐字节一致(探针 §7.4)。**RPC 端点整体豁免**:本期不动
`bcos-rpc`——`engine_newPayloadV4`/`engine_getPayloadV4` 的 RPC 端点注册
(EngineEndpoint 层转发)整体列入 §6.4 欠账;V4 支持只在 `EngineServiceImpl` 层
生效,gate 与单测均**直调** `EngineServiceImpl`,不经 RPC 解析/分发路径。
`getPayloadV4` 行为改述为:`EngineServiceImpl::getPayload(id, 4)` 在 OP 模式下
返回明确错误(出块未 OP 化),单测直调断言该错误;响应结构含 executionRequests
的完整 OP 化随 attributes 构块欠账一并交付。

### 6.4 op-node 实连前置欠账台账(本期明确不做,防"绿灯=可实连"误读)

attributes 构块(FCU attrs + getPayloadV4)、**`engine_newPayloadV4`/
`engine_getPayloadV4` RPC 端点注册**(EngineEndpoint 层转发,本期仅
`EngineServiceImpl` 直调,§6.3,裁定 A6)、RPC 解析层 OP 交易载体、**`SYS_CURRENT_STATE`
的 current number 随 FCU head 推进写入**(本期 FCU 保持只读+内存态,§6.1 第 6 步,
裁定 A4)、SYNCING 完整语义(缓存回填/侧链 ACCEPTED)、JWT、extraData 形状校验、
**Holocene EIP-1559 baseFee 父子一致性校验**(用父块 extraData 参数重算子块
baseFee 并比对——真实 op-geth 会拒绝的 baseFee 错块本验证者放行,与 extraData
形状校验并列,不得被掩盖,裁定 A7)、INVALID_BLOCK_HASH 枚举清理、重组窗口。

**rev.3.1 追加(2026-07-29 T7 落地,实现期发现,逐条不得被"33/33 全绿"掩盖)**:

| # | 欠账 | 事实与后果 |
|---|---|---|
| a | **`mergeBackStorage()` 永不调用** | OP 分支每接受一个块只 `pushView`,不 merge——`MultiLayerStorage` 的不可变层单调增长(**层数无界**),且每次 `fork()` 后的读要穿越自进程启动以来累积的全部层(**读放大随已接受块数线性增长**)。何时 merge、与重组窗口如何交互,属于编排层职责,与下一条同时落地。本期最小闭环块数量级小,这是**伸缩性**问题而非正确性问题——但生产接入前必须解决(源于 T5b 审查 I3,`EngineServiceImpl.h` step 6 注释同文在案) |
| b | **`SYS_CURRENT_STATE` 的 current number 随 FCU head 推进写入** | 本期 FCU 保持只读 + 内存态(裁定 A4,§6.1 step 6),链头进度表不写。与 (a) 同属编排层,一并落地 |
| c | **`executionRequests` 校验未实现** | `NewPayloadRequest`(`bcos-framework/engine/Types.h`)**没有 `executionRequests` 成员**,§6.1 step 2 的"在场且空"约束因此当前是**真空成立**——不是"检查通过",是"根本没有可检查的载体"。变异矩阵 #7 用 `requestsHash` 位移作代理复现线上后果,并以 `static_assert(!requires(NewPayloadRequest r){ r.executionRequests; })` 钉住:载体一旦被加进来这条断言立刻翻红,强制补真检查与真用例(源于 T5b/T6 偏离台账 ②) |
| d | **`engine_newPayloadV4` / `engine_getPayloadV4` 的 RPC 端点注册** | 与上文同条,此处重述以并列成表:本期 `bcos-rpc`/`EngineEndpoint` **零改动**(裁定 A6),V4 只在 `EngineServiceImpl` 层生效;OP 异常类型(`UnsupportedFork`/`UnsupportedOpPayloadAttributes`/`OpExecutionInternalError` 等)携带的 -38005/-38003/-32603 **是意图文档,不是线上 JSON-RPC 码**——异常类型→错误码的映射在本仓任何位置都尚未实现(既有通用异常同样如此)。测试断言的是**异常类型**,不是线上码,不得被读作"错误码已验证"(T6 审查 M4) |
| e | **Holocene EIP-1559 baseFee 父子一致性校验** | 与上文同条,并列备查:真实 op-geth 会拒绝的 baseFee 错块,本验证者放行(裁定 A7) |

**rev.3.2 追加(2026-07-29,整分支终审批 3 落地;来源=终审视角 3/4 的变异实验与文档诚实性审查,
逐条不得被"224/224 全绿"掩盖)**:

| # | 欠账 | 事实与后果 |
|---|---|---|
| f | **交易本体:写 OP 专用 `s_eth_hash_2_rawtx`,通用 `SYS_HASH_2_TX` 刻意不写**(2026-07-29 终审批 6 实现,原"从不写入"条目改写) | `registerOpBlock` 现写**五**张表,新增 `s_eth_hash_2_rawtx`(表名常量在 `bcos-evm/bcos-evm/engine/OpEngineSeam.h`,裁定 B5):键 = `keccak(raw envelope)`(= 以太坊交易哈希,**与 `SYS_HASH_2_RECEIPT` 同键**,一个键同时解出交易与回执),值 = 原始 EIP-2718 envelope 逐字节。**通用 `SYS_HASH_2_TX` 仍不写,这是刻意规避而非遗漏**:①该表存 tars 编码的 `bcos::protocol::Transaction`,其读侧(`Ledger.cpp:1440-1443`、`LedgerMethods.h:235-239`、lightnode、storage-tool)把字节直接交给 `createTransaction(..., checkSig=false, checkHash=false)`;把以太坊 envelope 放进去**不会响亮失败**——`bcostars::Transaction` 每个字段都是 `optional`,tars 标签扫描器的 `catch` 是空的,于是解出一个全默认对象,工厂再给它盖一个**自洽的新哈希**,消费者拿到的是**非空、看似合法、但 `hash() != key` 而无人核对**的假交易,并会流到 `eth_getTransactionByHash` 响应与 txpool `requestMissedTxs`(共识提案验证);②改为"映射成真 Transaction"也不可行:唯一映射器 `Web3Transaction::takeToTarsTransaction()` 硬拒 `0x04`/`0x7E`,tars IDL 无处安放 `sourceHash`/`mint`/`authorizationList`(`isSystemTransaction` 更是运行时非序列化 atomic),且 `Transaction::verify` 会对**无签名**的 deposit 做 ecrecover **伪造出一个发送者**。**结论:查不到是明确缺失,假交易是静默错误答案且已进共识路径——后者糟糕一个数量级。** **已知边界(不遮掩)**:让该行缺失本身在一条路径上也不干净——`LedgerMethods.h:233-235` 未判 `has_value()` 即解引用(见条目 r);但那是既有缺陷、今天无 OP 消费者,且写假交易只会把**可发现的崩溃换成不可发现的错答案**。**通用"按哈希查交易"接口对 OP 块仍无数据**,OP 块的交易检索面是 envelope 语义的专用面 |
| g | **缺参 `rawTransactions` 判 INVALID 而非 -32602** | §6.1 步骤 2 提到"缺参为 -32602"的约定,但 `validateOpNewPayloadRequest`(`EngineServiceImpl.cpp:200-206`)对缺失的 `rawTransactions` 返回 **INVALID + 点名字段**。理由:`NewPayloadRequest` 对象层**无法区分"缺参"与"空数组"**——那是 RPC 解析层的区分,而 RPC 端点本期整体豁免(条目 d)。此前该偏离只在未入库的 `task-5b-report.md` §7 交代过,读者从 spec 看不出(终审视角 4 Imp-3) |
| h | **`OpConsensusError → INVALID` 的覆盖不如 `OpStorageError → -32603` 对称** | `StorageError → -32603` 有真调度器 + 真桥的端到端覆盖(探针③、`EngineNewPayloadGateTest` 的存储布局故障注入);`OpConsensusError → INVALID` 在 engine 层有一条真调度器用例(`EngineOpBranch.ConsensusErrorFromExecutionMapsToInvalid`,坏类型字节 → 解码期抛),但 **`OpSchedulerImpl.h` 的 `catch(...)` 重抛路径**(即 T4 修复真正针对的场景:`processOpBlock` 内部逃逸的非 typed 异常被兜底重分类为 `OpConsensusError` 后抵达 engine)engine 侧此前**无用例**。**该缺口已于终审批 3 复审(I-1)闭合**:`EngineNewPayloadGate.ConsensusErrorViaCatchAllReclassificationIsInvalid` 用真调度器 + 真桥走这条腿(取一条真金值向量、删掉首笔 L1 attributes deposit 使 `OpBlockExecute.cpp:40` 抛出、`resealBlockHash` 保持 payload 自洽),断言 INVALID + `latestValidHash == parentHash` + 正例标识 `typed catch bypassed` + 反例标识(不得含比对桶前缀);翻红自验:把 `catch(...)` 改成 `catch(int)` → 该例翻红,把其分类改成 `OpStorageError` → 该例同样翻红。**保留本条的原因**:断言精度受条目 (j) 限制(断得到腿、断不到具体 throw)|
| i | **零散记账(此前仅存于 `progress.md` 一行)** | ①`computeOpTxRoot` 对同一批 raw 字节**算两次**(engine step 2 一次、`executeOpBlock` step 6 一次),且形参声明为 `input_range` 而语义要求可重复遍历,**应为 `forward_range`**——当前调用点全是 `std::vector`,故未爆;②`c_opMode` 只探测 `executeOpBlock` **一个成员名**,签名漂移会静默退化为通用分支(护栏只有三个测试里的 `static_assert`);③三个 engine 测试 TU 的匿名命名空间含**同名类型**,当前无 ODR 问题,但 `UNITY_BUILD` 打开后是硬冲突(`engine` target 已 `UNITY_BUILD ON`,测试 target 未开);④`${CMAKE_SOURCE_DIR}` 进 include 路径**作用于整个测试 target 的 20+ 个源文件**,不止三个 engine 测试——实测无遮蔽(仓库根无无扩展名文件,同名目录项被跳过),但 CMake 注释里"暴露面有界"的表述弱化了实际范围 |
| j | **`catch(...)` 重分类丢弃 `e.what()`,四类块级拒绝共用一条泛化 `validationError`** | `OpSchedulerImpl.h` 的 `catch(...)`(RTTI 变通,`fe2a40c` 引入)无法从被捕获对象上取回原始消息,只能抛一条固定文本的 `OpConsensusError`。后果:`OpBlockExecute.cpp` 的**四处**块级 throw——空块(:37)、首笔非 L1 attributes deposit(:40)、deposit 排在非 deposit 之后(:55)、非 deposit 交易校验失败——抵达 engine 后**共用同一条 `validationError`**,节点运维**无法区分是哪一类拒绝**。这是 RTTI 变通的既有后果(非终审批 3 引入),此前不在本台账上。它同时**限定了测试的断言精度**:`EngineNewPayloadGate.ConsensusErrorViaCatchAllReclassificationIsInvalid`(批 3 review I-1)只能断到"走了 `catch(...)` 这条腿"(消息含 `typed catch bypassed`),断不到"是四处 throw 中的哪一处"。真正的修法是消除 RTTI 变通本身,不是在这一层拼消息。机理的**本分支内**载体是 `OpSchedulerImpl.h` 该 `catch(...)` 子句的注释与 `bcos-evm/test/opstack/T8nReplayHarness.h` 的两处同类兜底;**原始排查报告不在本分支**——它写在无关分支 `feat-evm-mb1-block-execution`(`d0937e8a1`)的 `bcos-evm-ref/docs/audits/2026-07-12-typed-catch-rtti-investigation.md`,未随移植带入 |

**rev.3.3 追加(2026-07-29,整分支终审批 4 落地;来源=终审批 4 实施与复审。前两条是
op-node 实连的最高优先级前置——它们是本清单里唯二"op-geth 能正常处理、本实现硬拒或误答"
的路径,其余条目都是"本实现放行了 op-geth 会拒的东西"或伸缩性/可观测性问题)**:

| # | 欠账 | 事实与后果 |
|---|---|---|
| **k**(置顶) | **INVALID 判决不留痕迹 → INVALID↔SYNCING 判决倒挂** | `EngineServiceImpl.h` step 3:parent 不在 `SYS_HASH_2_NUMBER` 即答 SYNCING,而 **INVALID 判决不写任何记录**。于是块 N 被判 INVALID 后紧接着投块 N+1(parent 是 N):op-geth 返回 **INVALID + `latestValidHash` 回溯到最近有效祖先**(`api.go:914-917` 写 `invalidBlocksHits`/`invalidTipsets`、`:878-880` `checkInvalidAncestor`、`:946`),op-node 据此**立即放弃该分支**;本实现返回 **SYNCING**,op-node 会**持续尝试同步一条永远不会成立的链**。修法:有界的 invalid-ancestor 缓存(LRU/定容 map),newPayload 判 INVALID 时记入,后续 payload 的 parent 命中即答 INVALID + 最近有效祖先。**本条不在既有台账内**:条目 (a)-(j) 与正文 park 的"SYNCING 完整语义(缓存回填/侧链 ACCEPTED)"逐条核对过,**没有任何一条覆盖"INVALID 判决不留痕迹"**(终审视角 1 I4;批 4 列为可选项 B4-5,未实施) |
| **l**(置顶) | **合法重组被非链尾拒绝挡下** | §6.1 step 3c(批 2 B2-2(b))在 parent 不是当前链尾时抛 `OpExecutionInternalError`。它防的是"用错基态执行出必然失配的 INVALID",但代价是:首块被判坏后 CL 投递**同高度的竞争块**——op-geth 能正常处理——在本实现得到**硬拒绝**。与 (k) 同类且相邻:一个把"该放弃的分支"拖住,一个把"该接受的分支"挡死。真正的修法同为"任意-parent 基态"(需要 `blockHash → MLS 层`映射),与 (k) 一并落地(批 2 报告记账,rev.3.3 收入台账) |
| m | **`static_assert(c_opMode)` 不在生产组合根旁** | 批 4 B4-3 已让通用分支对 `version >= 4` 直接拒绝,堵住了 `c_opMode` 静默塌成 false 时的**橡皮图章**后果链(通用静态校验接受 OP payload → `parentKnown` 由 op-node 刚设的内存 head 满足 → 返回 VALID + payload 自报 blockHash、**全程不执行区块**;自验实测:去掉护栏后该请求 `throws nothing`)。但那是**第二道**防线;**第一道**——编译期 `static_assert(OpEngineService::c_opMode)`——目前只住在三个测试文件里,生产组合根尚不存在。**接入生产组合根时必须在同一处落这条静态断言**,否则签名漂移仍会静默降级、只是不再能被误答为 VALID |
| n | **`computeOpTxRoot` ↔ op-geth `DeriveSha` 的等价性依赖解码器严格性(跨文件隐式契约)** | `computeOpTxRoot`(`OpEngineSeam.h`)对**原始线上字节**建 trie,而 op-geth `header.TxHash` 来自 `DeriveSha`,其 `EncodeIndex` 是**从已解析结构体重新规范编码**。二者**仅在规范输入下等价**;非规范输入下对同一批 payload 字节算出**不同 transactionsRoot → 不同 blockHash**。使等价成立的**不是该函数,而是批 4 B4-2 加在 `OpSchedulerImpl.h` 解码器上的规范性严格层**(禁前导零 / u64 ≤8 字节 / address·hash 等长 / bool 仅空串与 `0x01`)——非规范输入在 step 1 即被拒,能到达建根的输入必定规范。**后果**:任何以"提高兼容性"为由放宽该解码层的改动,都会**无声破坏** blockHash 一致性(不会有测试直接翻红,因为放宽后非规范输入才刚开始能进来)。两处注释(`OpEngineSeam.h` 的 `computeOpTxRoot`、`OpSchedulerImpl.h` step 6)已互指,此处入台账以免注释被后续重写时一并抹掉 |
| o | **B4-4 `visitAccountsImpl` 未命中结果不入缓存的不变式** | 批 4 B4-4 让 `visitAccountsImpl` 走写穿缓存,并在**未命中**时以 `computeHasStorage=false` 读取(跳过每账户一次全表 range)。**该结果刻意不写回 `m_accountCache`**:它缺 `has_storage`,而 `Account::has_storage` 默认 false、`eth/state/state.cpp` 的 `has_initial_storage` 直接取它,进而影响 **EIP-161/7610 空账户清除与 CREATE 碰撞判定**。守护断言:`Storage2Ledger.VisitAccountsMissDoesNotPoisonAccountCache`(批 4 复审 I-1;反证自验:把 miss 结果写进缓存 → 该例翻红)。今天的窗口窄(`executeOpBlock` 在 `visitAccounts` 之后不再 `get_account`),窄窗口正是最容易在后续重排中被悄悄拉宽的那种 |
| p | **`s_eth_block_header` 读路按块号取,op-geth 按 hash 取** | 批 4 B4-1 打通读路时以 `s_eth_block_header[parentNumber]` 取父头,而 op-geth 按 `block.ParentHash()` 取(`api.go:887`)。二者**仅在 number → header 单射时等价**,而单射性今天由条目 (l) 的非链尾拒绝保证(每个高度至多登记一个块)。**(l) 一旦放开(即重组窗口启用),本读路会取到错误的父头**——必须在同一改动里改为按 hash 取(需要一个尚不存在的 `hash → header` 索引)。`EngineServiceImpl.h` step 3a 注释已点明该依赖 |

**rev.3.4 追加(2026-07-29,终审批 6 落地)。条目 q 置于 k/l 之前**——k/l 是"判决语义到不了
正确结果",q 是"请求根本进不来":**入口不通,后面的判决语义再对也无从发生**:

| # | 欠账 | 事实与后果 |
|---|---|---|
| **q**(**最置顶**) | **OP 路径在生产上没有可用的 RPC 入口,且现有入口的解析是错的** | 比"`newPayloadV4` 端点未注册"(条目 d)更精确也更严重的表述:①`ExecutionPayload::rawTransactions`——OP 路径**唯一**的交易载体——的非测试赋值点只有 `Types.h`/`OpDepositEncode.h`/`EngineServiceImpl.{h,cpp}`,**RPC 层从未赋值**,即生产上没有任何路径能把 OP payload 送进本闭环;②更糟的是现有 `bcos-rpc/.../endpoints/EngineEndpoint.cpp:164` 的 newPayload 解析走 `transactionFactory.decodeTransaction(ref(txData))`,即**把以太坊 RLP envelope 喂给 tars 反序列化器**——与条目 (f) 描述的是同一类编码契约错误,只是发生在**入口**而非出口,失败形态同样是"静默解出一个全默认对象"而非报错。**本期不做**:`bcos-rpc` 属裁定 A6 明确 park 的范围且是本闭环的零触碰硬约束(终审批 6 调研发现,控制器核实) |
| r | **`LedgerMethods.h:233-235` 缺行未判 `has_value()` 即解引用** | `getBlockData` 的 storage2 重载取 `SYS_HASH_2_TX` 行后直接 `txEntry->get()`,**缺行是 UB 而非错误**。**与 OP 无关的既有缺陷**(任何"块有交易元数据但该表缺行"的情形都会触发),但**会先由 OP 块暴露**——因为条目 (f) 裁定 OP 块不写该表。今天该路径无 OP 消费者(`BaselineScheduler.h:742` 只传 `HEADER` flag) |
| s | **解码异常导致双重回调 → 同一 coroutine handle `resume()` 两次** | `Ledger.cpp:1417-1465` 的解码 lambda **无 try/catch**,而 `RocksDBStorage.cpp:228-233` 的**成功回调在 `try` 内部**:lambda 抛出 → 存储层接住 → **第二次**调用同一 `_callback`(带 error)。沿链 `asyncBatchGetTransactions` → `asyncGetBatchTxsByHashList` → `LedgerMethods.cpp:558-568` 对同一 `coroutine_handle` **`resume()` 两次**,即 UB/崩溃而非干净的 RPC 错误。**同为与 OP 无关的既有缺陷**,但它是条目 (f) 中"写错编码"危害链的最后一环,一并记录 |

## 7. 金向量 gate 与测试(rev.3 重写)

### 7.1 金值策略(事实 Critical 落地,rev.3 修正 A2/A3)

- 33/33 向量无 blockHash/txRoot/extraData/excessBlobGas;deposit 无原字节——
  这是结构性盲区,不是边缘缺口;
- **离线金值仪式**(沿 t8n generator 先例,pinned op-geth v1.101702.2):定性为
  "**扩展 opt8n-ref 发射段**"(裁定 A3)——emit 阶段追加输出
  `block.Hash()/header.TxHash/header.Extra/tx.MarshalBinary()/encodedHeaderHex`,
  **非**"从 env+txs 构头"。为全部 33 条补算 `blockHash`/`transactionsRoot`,
  **extraData 取生成块 `header.Extra` 原样发射,不做人工选值**(rev.2 的人工选值
  方案作废——与生成器既有惯例冲突,且 Jovian extraData 是 17 字节含强制
  minBaseFee)与 `excessBlobGas=0`;产物入 `test/opstack/t8n/golden/engine/`
  (新目录,vectors/ 逐字节不动);deposit envelope 由 `OpDepositEncode` 从
  结构化字段重建(编码器自身以金值锚定:重建字节参与 txRoot,txRoot 对上 op-geth
  金值即编码器正确性的判据);
- **链式向量对**(rev.3 新增,裁定 A2):33 条孤立向量之外,**离线专生成**一对
  链式向量——op-geth `GenerateChainWithGenesis` 生成块数 1→2(经 InsertChain
  头校验),两块各发射完整 payload 字段 + golden,产物入
  `golden/engine/chained/`(vectors/ 依旧零触碰)。rev.2 的"A 的 blockHash 设为
  B 的 parentHash"拼接法作废(三重破产:静态校验先挡路/金值失效/state 与
  number 不符);
- 金值样本结构覆盖:Isthmus/Jovian 两时代、单笔/多笔(非平凡 trie)、
  deposit-only、setcode——由 33 条全量覆盖天然满足;链式对补覆盖"parent-known
  经块登记因果成立"这一时序维度;
- 诚实口径:blockHash/txRoot 的判别力来自离线金值(op-geth 背书),非
  "自己算自己验"的自洽比对;gate 断言 `payload.blockHash == golden.blockHash`
  且 `result.txRoot == golden.transactionsRoot`(**六项比对面**之一,§4.1),
  两侧独立来源交叉。

### 7.2 gate 流程(rev.3 修正 A2)

同 rev.1 骨架(pre 播种→包装→newPayloadV4→VALID→FCU 推进),rev.2 增、rev.3
改写链式部分:
- 两块链式用例(测试 I-2)消费 **§7.1 离线生成的专用链式向量对**(`golden/engine/
  chained/`):`newPayload(A)→VALID → FCU(head=A) → newPayload(B)`(B 的 pre 即
  A 的 post,不重播种)断言 B 的 parent-known **经块登记自然满足**(非 fixture
  预注册);跳过 FCU 直接投未知 parent 的块 → SYNCING。这是"闭环"成立的因果证据;
- 33 条孤立向量的 parent 仍由 fixture 预登记(storage 写入,`SYS_HASH_2_NUMBER`
  编码同 §6.1.6)。

### 7.3 变异分档矩阵(逐分支,测试 I-1;rev.3 扩为 13 类 18 例,裁定 C1)

**latestValidHash 断言口径(rev.3.1 修订,2026-07-29 T7 落地;裁定 I2)**:非 blockHash 桶
**且已过 parentKnown(§6.1 step 3)**的 INVALID 用例才**同时断言
`latestValidHash==parentHash`**——即下表的 #8.1–#8.6 与父子块号连续性一类;**静态校验阶段
(§6.1 step 2)被拒的全部用例统一断言 null**,含 blockHash 失配桶(#2/#7)与 #3–#6。

修订理由(采纳实现口径、修 spec 而非改实现):step 2 尚未查过 parent,此时声称"最近有效祖先
= parent"是一句未经验证的断言;Engine API 允许 null;工程上更诚实。rev.3 原文"非 blockHash
桶的 INVALID 用例同时断言 parentHash"按字面读会要求 #3–#6 断言 parentHash,与实现不符——
该字面偏离由本次修订消除(来龙去脉见 `task-6-report.md` §4/§8b 的 I1/I2 记录)。

| # | §6 校验分支 | 变异用例 |
|---|---|---|
| 1 | 时间戳×版本闸 | Isthmus payload 走 V3 → -38005 |
| 2 | blockHash 重组 | 篡改一字节 → INVALID + latestValidHash=null |
| 3 | withdrawals 非空 | → INVALID |
| 4 | expectedBlobVersionedHashes 非空 | → INVALID(独立用例) |
| 5 | excessBlobGas ≠0 | → INVALID(独立用例) |
| 6 | **blobGasUsed ≠0(Isthmus,rev.3 新增)** | → INVALID(独立用例,呼应 §5.1 blobGasUsed 注) |
| 7 | executionRequests 非空 | → INVALID + null |
| 8.1–8.6 | **六项比对面**(rev.3 展开为 6 个独立用例) | stateRoot/gasUsed/receiptsRoot/logsBloom/withdrawalsRoot/txRoot **各一例**篡改 → INVALID + validationError 点名该字段 |
| 9 | parent 未知 | → SYNCING |
| 10 | **同 payload 重发(rev.3 新增)** | SYNCING → 补登记 parent → 重发同 payload → VALID |
| 11 | attributes 拒绝 | OP 模式 FCU 带 attrs → -38003 且 head 照常推进(断言两点) |
| 12 | 通用版本闸 | 通用组合根收 V4 → 行为与现状一致 |
| 13 | 存储故障 | ThrowingStorage → -32603 而非 INVALID |

计数:13 类(#1-7、#8 六项比对面合记 1 类、#9-13)、18 例(#8 展开 6 例 +
其余 12 类各 1 例)。

### 7.4 探针(五个,留痕文件 `.superpowers/sdd/probe-op-validator-gate-report.md`)

①blockHash 校验探针(篡改必红);②**六项比对面**比对探针;③错误分类探针
(-32603 vs INVALID);④**块登记接线探针**(测试 M-1:注入跳过块登记写入 → 两块
链式用例第二块必转 SYNCING);⑤**通用版本闸探针**(测试 C-2:通用组合根 V4 行为
零漂移;**fixture:`SchedulerSerialImpl` + `MockExecutorSerial` 本地复刻,先例
`testSchedulerSerial.cpp:20-75`;V4 拒绝在版本闸完成,不触达 executor**,裁定 C4)。
留痕纪律同桥项目(注入 diff/翻红原文/回退/复绿/git status)。

### 7.5 单测层

EthBlockHeader(金值来自 §7.1 离线表而非自算,含 Isthmus 9B/Jovian 17B extraData
两编码,原样发射非人工选值;RLP 边界;`encode()==golden.encodedHeaderHex` 字段级
断言先于 `hash()` 断言,裁定 C3);OpDepositEncode(重建字节 vs 金值 txRoot);
OpSchedulerImpl(双签名:通用签名调用即 throw;分拣/首笔违约/毒旗分类/**六项
比对面**完整性——seal 三字段 + result 三独立成员);engine OP 分支单测
(版本闸/attributes 语义/latestValidHash 取值)。

**rev.3.2 补(终审批 3 落地的单测层增量)**:

- **step 5 的比对是 8 条,不是 6 条**。§4.1 的"六项比对面"之外还有 §5.1 的两条
  (`blobGasUsed` 的 Jovian DA footprint、`requestsHash` 的常量漂移),终审视角 3 的变异实验
  把这两条同时短路为恒假,**50 例 engine 测试全绿**——即它们此前**从未**被任何用例触碰过。
  `EngineOpBranch.EachComparisonSurfaceFieldMismatchIsNamed` 的 mismatch 表因此从 6 行扩到
  **8 行**;§4.1/§5.1 的分类是文档区分,不是覆盖豁免;
- **块登记的写侧需要真实块的证据**,不能只有桩:gate 的每条向量现在逐笔断言
  `SYS_HASH_2_RECEIPT` 的键 = `keccak(raw EIP-2718 envelope)`,并断言**存储回执的 gasUsed 之和
  等于该块的 golden `gasUsed`**(`mapOpReceipt` 存的是每笔自身的 `gas_used`,故和恰为头字段);
- **收据数护栏**(数量不等 → 拒绝、空收据 → 拒绝,而非 `std::min` 截断 + 跳过)由两条用例固定
  ——此前该设计辩论的结论无任何测试守护;
- **`number`/`timestamp`/`baseFeePerGas` 的唯一支点**是链式对(33 条向量这三字段全常量),
  链式用例内已就此加显式断言并置于所有 fatal `ASSERT_*` 之前,注明不得删除;
- **金值 provenance 机内钉死**:`golden/engine/SHA256SUMS`(标准 shasum 格式,39 个文件)+ 每条
  `vectors/*.json` 的 `_op_test_vectors.generator_commit == e8800cff…`,由
  `EngineNewPayloadGate.GoldenCorpusProvenanceIsPinned` 校验。它挡的是"有人用**本实现**重新生成
  金值"——那会让 gate 静默退化成同义反复且全绿。它**不能**证明重新生成者用的是 op-geth,只能让
  重新生成成为一次**必须显式修改 SHA256SUMS 的、可审查的动作**;
- **`validationError` 断言一律精确匹配或前缀锚定**(§11 检查单末条),子串匹配已全部移除。

## 8. 验收清单(N0 相对基线 + 命令化,rev.3 修正 C5/C6)

**实测回填口径(2026-07-29,T7,分支 `feat-op-validator-loop` @ `fe2a40c29`)**:全部条目
逐条命令化执行完毕,勾选处的数字为**实跑输出**而非计划值;逐条命令与原始输出见
`.superpowers/sdd/2026-07-28-op-validator-minimal-loop/task-7-report.md`。

- [x] 金向量 gate:`--gtest_filter='EngineNewPayloadGate.*'` **5 TEST / 33 向量全绿**
      (`AllThirtyThreeGoldenVectors` 内断言 `ids.size()==33`,gtest XML 留痕
      `v_<id>_{status,block_hash,tx_root,tx_count}` 各 **33** 条),
      `latestValidHash==blockHash`;blockHash 与 `EthBlockHeader::encode()`/离线
      金值逐字段交叉断言 33/33;`result.txRoot==golden.transactionsRoot` 33/33
- [x] 两块链式用例(专用向量对,`golden/engine/chained/`)绿(parent-known 经
      块登记因果成立)+ 未知 parent→SYNCING —— `ChainedPairParentKnownThroughBlockRegistration`
      OK,因果性另由探针④反证(见下)
- [x] 变异矩阵(§7.3)13 类 18 例逐例分档正确
      (`--gtest_filter='EngineNewPayloadMutation.*'` **18/18**;六项比对面 6 例各断言点名字段;
      非 blockHash 桶**且已过 parentKnown** 的 INVALID 用例断言 latestValidHash==parentHash,
      **step 2 静态校验阶段一律 null**——口径同 §7.3 rev.3.1 修订)
- [x] `--gtest_filter='EthBlockHeader.*:OpDepositEncode.*'` 金值单测绿(**8/8**)
- [x] 通用组合根 V4 行为零漂移(探针⑤ + `EngineVersionGate.*` 2/2 + 变异 #12)
- [x] 基线零回归:N0 相对基线(**Task 6 结束、Task 7 探针前**捕获:
      `--gtest_list_tests` 双路各一份 + engine Boost.Test
      `test-bcos-engine --list_content 2>&1 | sort`),三份存档
      `.superpowers/sdd/2026-07-28-op-validator-minimal-loop/n0-*.txt`,合并后全过;
      新增名单入报告 —— **实测:in-tree `bcos-evm-opstack-tests` 206/206、standalone 131/131、
      engine Boost 11/11(“No errors detected”);本闭环新增 50 例(相对 merge-base
      `42e62fcef` 的 156 例),零删除、零改名;standalone 名单与 merge-base 逐条相同
      (50 例全部落在 `if(TARGET bcos-framework)` 守卫内),即通用件改动零外溢**
- [x] 五探针翻红复绿留痕(`.superpowers/sdd/probe-op-validator-gate-report.md`)
- [x] `git diff --stat $(git merge-base HEAD feat-evm-ledger-bridge) -- ports/
      bcos-evm/test/opstack/t8n/vectors/ transaction-scheduler/` 空(实测输出为空)
- [x] 桥 spec §10.1 + `Storage2Ledger.h` 头注的条件式许可修订已落(§4.4 义务,Task 1)
- [x] RPC 端点整体豁免确认:`bcos-rpc`/`EngineEndpoint` 零改动(裁定 A6;
      `git diff --stat <merge-base> -- bcos-rpc/` 为空)
- [x] 库目标纯净:`grep -rl "nlohmann\|gtest" bcos-evm/bcos-evm/ | grep -v statetest.hpp`
      为空;`bcos-codec/bcos-codec/rlp/`、`engine/bcos-engine/` 同法为空

**终审后回填(2026-07-29,批 1/2/3 落地后)**:上表的实测数字是 T7 收尾时(`fe2a40c29`)的快照。
整分支终审的三批修复之后,in-tree `bcos-evm-opstack-tests` **225/225**,本闭环新增 **58** 例
(相对 merge-base `42e62fcef` 的 156 例;T7 时为 50 例,批 1 +11、批 2 +2、批 3 +5),
`test-bcos-engine` 仍"No errors detected"。standalone 名单**未重新测量**:批 1/2/3 的测试改动
全部落在 `if(TARGET bcos-framework)` 守卫内,standalone 侧源列表逐字未动。

### 8.1 报告归档标准(rev.3.2 新增,终审视角 4 Imp-5 / 批 3 B3-12)

此前 8 份 `task-N-report.md` 只归档 3 份,无成文标准,并因此产生过一处悬空引用(§7.3 曾引用
未入库的 `task-6-report.md`)。**标准(自本次起适用,采用"全归档"口径)**:

- `.superpowers/sdd/` 下的 **`task-N-report.md`、`final-batch-N-report.md`、`n0-*.txt`、
  **探针报告**(`probe-*.md`)、**控制器裁定书/决策记录**(`*-directive.md` 一类,即 spec 会
  援引为权威出处的文件)一律入库(`git add -f`——该目录受 `.git/info/exclude` 覆盖,
  不加 `-f` 会被静默跳过,这正是此前漏归档的直接原因);
- `*-brief.md`(控制器下发的任务书)与 `review-*.diff`(中间产物)**不入库**;
- **spec / README 只允许引用已入库的文件**。引用未入库文件即悬空引用,视同文档缺陷——
  这条是上面那条的存在理由,不是附注。判定方式是**全量扫描**而非逐条点名:
  对 spec 与 README 抽出所有形如 `.superpowers/…`、`docs/…` 的引用,逐个比对 `git ls-files`。
  批 3 首轮只修了被点名的一处,复审即发现第二处(`probe-ledger-bridge-report.md`),
  我的扫描又发现第三处(`validator-loop-rev3-directive.md`)——**点名式修补不闭合**。
  但"扫描才闭合"这句话本身也**被证伪**了:那一轮 fix commit 在报告里写下"复扫结果:悬空引用
  0 处",而**同一个 commit 自己引入了第四处**——新写的条目 (j) 引用了一条本分支不存在的
  `docs/audits/…` 排查报告路径(该文件只存在于无关分支 `feat-evm-mb1-block-execution`,
  且带 `bcos-evm-ref/` 前缀,见条目 (j) 现文)。一次性扫描只能证明
  "扫描那一刻干净",管不住此后写下的每一行。**结论:人工纪律不足以闭合,应做成 CI 检查**
  ——抽 spec / README / **代码注释**里所有 `.superpowers/…`、`docs/…` 形状的路径,比对
  `git ls-files`,不匹配即失败(代码注释同样纳入:第四处之外还并存着三处同源的注释悬空引用,
  分别在 `OpSchedulerImpl.h` 与 `T8nReplayHarness.h`,均于本轮一并修正)。

## 9. 风险与预案

| 风险 | 预案 |
|---|---|
| 离线金值仪式依赖 pinned op-geth 环境(**rev.3 增:含链式向量对生成**) | 沿 t8n generator README 仪式;金值表带生成记录(pin SHA/命令),可复算;链式对生成复用同一 `GenerateChainWithGenesis` 仪式 |
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

## 11. 审查检查单:兜底断言与假绿(rev.3.2 新增,批 3 B3-13a)

同一类假绿在本分支的终审修复中**连续三轮**成为问题来源。这不是三次巧合,是一个可复现的
机制:**只断言异常类型的测试,在"两条不同防线抛同一类型"时无法区分谁在起作用**。三次实证:

1. **批 1**:`processOpBlock` 的 `catch(...)` 兜底把不同的块级拒绝抹成同一异常**类型**。
   只断言类型的用例在被测修复被删除后**仍然通过**——兜底把逃逸接住了,类型没变。
   → 必须改用**消息子串**断言。
2. **批 2 自验**:屏障消息与执行期兜底消息**共有** `unclassified exception` 子串。
   只禁用执行期兜底时,逃逸被外层屏障接住,原断言照样通过。
   → 子串断言本身不够,必须收紧为"**必须含 A 且不得含 B**"。
3. **批 2 复审**:删除透传分支(`catch (const OpExecutionInternalError&) { throw; }`)后,
   屏障会用**自己的消息重新构造**新异常,把内层原始消息**覆盖**掉——于是"删掉内层防线"
   这件事在只看内层消息的断言下依然不可见。

**规则(适用于本仓任何新增 `catch(...)` 兜底,或任何会重写/包装异常消息的屏障)**:

- 该防线的测试必须同时给**正例标识**(消息必须含本层的标识串)与**反例标识**
  (消息不得含相邻层的标识串)。缺任何一半,相邻防线都能顶替它而测试不红。
- **每条防线要有独立的翻红实验**:逐条注释掉/删除该防线的代码,确认**恰好**是为它写的
  用例翻红。若删掉防线 X 时红的是为防线 Y 写的用例,说明两者没被区分开。
- 同一条规则的自然推论,适用于**所有** `validationError` 断言而不只是异常:子串匹配
  (`find("blobGasUsed") != npos`)会同时命中静态校验桶与比对桶两条**语义不同**的消息,
  两者的 `latestValidHash` 取值还相反。→ **用精确匹配或前缀锚定**,让消息同一性把桶钉死
  (批 3 B3-6 已按此改写 engine 两个测试文件的全部 15 处)。

**rev.3.3 追加(批 4 复审落地,两条通则——它们不限于 `catch(...)`,适用于本仓任何修复)**:

- **报告里被称为"风险点"的东西,必须当场配一条会翻红的断言;否则那句话只是自我安慰。**
  实证(批 4 B4-4 / 复审 I-1):实施者把"`visitAccountsImpl` 未命中的结果决不能写进
  `m_accountCache`(它缺 `has_storage`)"写进了代码注释,并在报告里明确称其为"本条唯一
  正确性风险点"——但没有任何断言守护它。审查者把 `m_accountCache.emplace(addr, miss)` 加
  回去做反证,**237/237 全部照绿**。也就是说:被点名的风险点当时处于零守护状态,而报告的
  措辞让它读起来像是已被处理。**判据**:写下"风险/不变式/契约"这类词时,同时给出"哪条用例
  会因它被破坏而翻红";答不出来,就是还没做完。守护断言落地后应在 §6.4 对应条目里**指名**
  (见条目 o 的写法)。
- **自验三步「还原 + 重建 + 复绿」,在多构建目录的仓库里必须对每个目录各做一次。**
  本仓有两个:in-tree(仓库根 `build/`,含 engine 测试)与 standalone(`bcos-evm/build/`,
  不含 engine 测试,数量不同)。实证(批 4 复审):实施者的三步只在 in-tree 做,交付时
  standalone 目录是陈旧的——审查者首次构建它时 `OpT8nReplayTest.cpp.o` 等被重新编译,即
  "复绿"从未在该目录发生过。叠加批 3 已记的"只还原不重建会用带变异的二进制跑出假结果",
  完整判据是:**每个构建目录,还原后都要重新构建再复跑**,并在报告里分别写出两个目录的
  数字(本仓当前:in-tree 238/238、standalone 131/131)。
