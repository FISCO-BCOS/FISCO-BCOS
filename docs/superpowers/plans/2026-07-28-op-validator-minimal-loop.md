# OP 验证者模式最小闭环 Implementation Plan v2

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**状态**:v2——随 spec rev.3 同步重写(spec-plan 成对审查,裁定书
`.superpowers/sdd/validator-loop-rev3-directive.md`)。v1→v2 结构性改动:Task 5
拆分为 **Task 5a**(版本闸成员化 + opMode 编译期判据 + 通用组合根零漂移)+
**Task 5b**(newPayload OP 分支本体),任务总数 **8**(T1-T4, T5a, T5b, T6, T7);
Task 2 增产链式向量对(离线专生成,拼接法作废);Task 6 消费该向量对。

**Goal:** `engine_newPayloadV4` 导入 OP 块(执行走真账本桥链路,gate 与单测均
直调 `EngineServiceImpl`,RPC 端点本期不注册)+ FCU 推进,33 向量金向量 gate
全 VALID + 两块链式(专用向量对)因果闭环。

**Architecture:** OP 执行由双签名调度组件 `OpSchedulerImpl` 承载(OP 专用
`executeOpBlock` + 满足 concept 的哑通用签名,chainId/`OpForkTimestamps` 经
构造参数注入);`handleNewPayload` 新增 OP 分支(从零:校验/执行/**六项比对面**
分档/块登记;链头进度表推进与 RPC 端点注册本期整体豁免,列入欠账台账);ETH 头
哈希与 deposit 编码入 bcos-codec;判别力来自 pinned op-geth 离线金值(全量 33
条 + 1 对链式向量,extraData 原样发射)。

**Tech Stack:** C++20、GTest、bcos-codec RLP、storage2/MultiLayerStorage、
evmone(vendored)、pinned op-geth v1.101702.2(离线金值仪式)。

**Spec:** `docs/superpowers/specs/2026-07-28-op-validator-minimal-loop-design.md`
(rev.3)——**全部语义细节以 spec 为唯一依据**,本计划给结构/骨架/命令;每任务
brief 中引用的 spec 节号必须先读。

## Global Constraints

- 基座:`feat-evm-ledger-bridge` HEAD 开新分支 `feat-op-validator-loop`。
- `ports/`、`bcos-evm/test/opstack/t8n/vectors/` 逐字节零触碰;通用调度器文件(`transaction-scheduler/`)零改动;`bcos-framework` 改动仅限 `engine/Types.h` 新增可选字段。
- 命名空间:`bcos::evm::engine`(OpSchedulerImpl/OpReceiptMap)、`bcos::codec::rlp`(EthBlockHeader/OpDepositEncode 按 bcos-codec 既有惯例)、`bcos::evm::opstack`(OpForkTimestamps/configAt 扩展)。
- 库纯净:gtest/nlohmann 不入库目标;新测试全部进 `bcos-evm-opstack-tests` 的 framework 门控块。
- 协程上下文契约(spec §4.4):OP 执行链的嵌套 syncWait 仅在"底层同步完成 + x_state 串行"前提下许可;Task 1 完成契约文本修订前,后续任务不得开工。
- 错误码/分档(spec §6.1/§6.2 表)逐字执行:-38005/-38003/-32602/-32603、INVALID+null(blockHash 桶)、SYNCING 不入库。
- **RPC 解析层/端点注册本期豁免**(裁定 A6):`bcos-rpc`/`EngineEndpoint` 零改动;`engine_newPayloadV4`/`engine_getPayloadV4` 不注册 RPC 端点;gate 与全部单测直调 `EngineServiceImpl`。
- **chainId 注入口径**(裁定 D2):经构造参数注入(与 `OpForkTimestamps` 同通道);链配置键(`web3.chain_id`)读取责任在组合根(engine 初始化处),`OpSchedulerImpl`/`EngineServiceImpl` 本身不读配置;gate fixture 直接传值,不经配置读取路径。
- 文档提交需 `git add -f`(`.git/info/exclude` 含 `docs/superpowers/`);git 用 `rtk` 前缀;clang-format hook 归一化照**移植 spec(2026-07-24)§4.6(a)**口径记录(裁定 D1);禁后台命令(Bash timeout 600000)。
- 环境:`export FB=/Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/ledger-bridge`(worktree 根);in-tree 构建 `$FB/build`、standalone `$FB/bcos-evm/build` 均已配置;pinned op-geth 检出 `/Users/octopus/octo/code/blockchain-impl/op-geth`(@ v1.101702.2,金值仪式用)。

---

### Task 1: 协程契约条件式许可修订(spec §4.4 前置义务)

**Files:**
- Modify: `bcos-evm/bcos-evm/ledger/Storage2Ledger.h`(头注"许可例外"段)
- Modify: `docs/superpowers/specs/2026-07-27-real-ledger-bridge-design.md`(§10.1 对应条目)

**Interfaces:** Produces: 修订后的契约文本(后续任务的合法性来源)。

- [ ] **Step 1**: 把桥头注与桥 spec §10.1 中"仅此一层嵌套、仅此一个调用点"的例外,改写为**条件式许可**:安全前提(底层 storage2 任务线程内同步完成、执行段单线程串行、外层协程从不跨线程恢复)+ 失效判据(任何后端引入跨线程/事件循环异步完成即失效)+ 新增许可拓扑(engine newPayload → executeOpBlock → 桥读方法 → visitAccounts → code getter 的多层嵌套)。文字以本 spec §4.4 三条为准逐条落。
- [ ] **Step 2**: 重编译桥测试确认注释级改动零行为(`cmake --build $FB/build --target bcos-evm-opstack-tests -j8` + `ctest --test-dir $FB/build -R BcosEvmOpstackTests`,预期全过)。
- [ ] **Step 3**: Commit——`rtk git add -f docs/superpowers bcos-evm && rtk git commit -m "docs(bridge): 协程嵌套例外改写为条件式许可(验证者闭环 spec §4.4 前置义务)"`

---

### Task 2: 离线金值仪式(全量 33 条 + 1 对链式向量)

**Files:**
- Modify: opt8n-ref 发射段(pinned op-geth 检出内,沿 `t8n/generator/README.md` 仪式扩展;裁定 A3——定性为"扩展 opt8n-ref 发射段",非"从 env+txs 构头")
- Create: `bcos-evm/test/opstack/t8n/golden/engine/manifest.txt`、`<vectorId>.golden.json` ×33
- Create: `bcos-evm/test/opstack/t8n/golden/engine/chained/`(A/B 两块链式向量对:`chainA.golden.json`、`chainB.golden.json` + 各自 pre/post 状态种子)
- Create: `bcos-evm/test/opstack/t8n/golden/engine/README.md`(生成记录:op-geth pin SHA、命令;extraData 原样发射口径说明)

**Interfaces:** Produces:
- 33 条孤立向量金值:`{blockHash, transactionsRoot, extraData(原样发射,Isthmus 9B/Jovian 17B hex), excessBlobGas: "0x0", rawTransactions: [hex...], encodedHeaderHex}`(rawTransactions 含 deposit 的 0x7E 完整 envelope——同时作为 Task 3 OpDepositEncode 的字节级金值;`encodedHeaderHex` 供裁定 C3 的字段级定位断言);
- 链式向量对(裁定 A2):A/B 两块,各自完整 payload 字段 + golden,B 的 pre 状态 = A 的 post 状态(离线生成时经 `GenerateChainWithGenesis` 块数 1→2、`InsertChain` 头校验产出,非拼接)。

- [ ] **Step 1**: 在 pinned op-geth 检出内(沿 `t8n/generator/README.md` 仪式:pin 校验、干净树、用毕清理)**扩展 opt8n-ref 发射段**(emit 阶段追加输出 `block.Hash()/header.TxHash/header.Extra/tx.MarshalBinary()/encodedHeaderHex`),对 33 条既有向量逐条发射上述金值 JSON;extraData **不做人工选值,原样取生成块 `header.Extra`**(rev.1/rev.2 的人工选值方案作废)。
- [ ] **Step 2**: 用同一 pinned op-geth 检出,调用 `GenerateChainWithGenesis` 生成块数 1→2 的专用链式对(经 `InsertChain` 头校验),两块各按 Step 1 同法发射完整 payload 字段 + golden,产物落 `golden/engine/chained/`。
- [ ] **Step 3**: 自检(裁定 B4 扩充):(a) 每条 `_op_expected.header` 全部 7 个共有字段(receiptsRoot/gasUsed/logsBloom/withdrawalsRoot/blobGasUsed/stateRoot/requestsHash)与 golden 逐字段对账;(b) typed tx 断言 `golden.rawTransactions[i] == 向量._op_raw[i]` 逐字节(防生成器漂移;deposit 走 `OpDepositEncode` 交叉,Task 3 完成后补跑);(c) golden 每条已附 `encodedHeaderHex`(整头 RLP hex);(d) 33 个文件与 manifest 集合相等;(e) `git diff --stat -- bcos-evm/test/opstack/t8n/vectors/` 为空(孤立向量与链式对生成均不触碰 vectors/)。
- [ ] **Step 4**: Commit——`rtk git commit -m "test(bcos-evm): 验证者 gate 离线金值表(33 条 + 1 对链式向量,extraData 原样发射,pinned op-geth)"`(路径受 info/exclude 影响时 `add -f`)。

---

### Task 3: EthBlockHeader + OpDepositEncode(bcos-codec)+ 金值单测

**Files:**
- Create: `bcos-codec/bcos-codec/rlp/EthBlockHeader.{h,cpp}`、`OpDepositEncode.{h,cpp}`
- Modify: `bcos-codec/CMakeLists.txt`(源列表)
- Test: `bcos-evm/test/opstack/EthBlockHeaderTest.cpp`(framework 门控块;金值取自 Task 2 golden 文件,**不自算**;**GTest 套件名钉死为 `EthBlockHeader`/`OpDepositEncode` 两个独立套件**,裁定 C6)

**Interfaces:**
- Produces:
```cpp
namespace bcos::codec::rlp {
struct EthBlockHeader {   // spec §5.1 的 21 字段,类型:h256/u256/uint64_t/bytes/Address
    /* parentHash, ommersHash, feeRecipient, stateRoot, transactionsRoot,
       receiptsRoot, logsBloom(256B), difficulty, number, gasLimit, gasUsed,
       timestamp, extraData, prevRandao, nonce(8B), baseFeePerGas,
       withdrawalsRoot, blobGasUsed, excessBlobGas, parentBeaconBlockRoot,
       requestsHash */
    bcos::bytes encode() const;      // RLP,字段规范序
    bcos::h256 hash() const;         // keccak256(encode())
};
bcos::bytes encodeDepositEnvelope(/*结构化字段,签名对齐向量 _op_deposit*/);  // 0x7E‖RLP
}
```

- [ ] **Step 1(TDD)**: 写失败测试:任取 3 条 golden(Isthmus 单笔/Jovian 多笔/deposit-only),用向量 env+`_op_expected`+**golden.extraData(原样发射,Isthmus 9B/Jovian 17B)**组 `EthBlockHeader`,断言 `encode()==golden.encodedHeaderHex`(字段级,裁定 C3)且 `hash()==golden.blockHash`;deposit:`encodeDepositEnvelope(向量 _op_deposit 字段)==golden.rawTransactions[0]` 逐字节。跑确认编译失败。
- [ ] **Step 2**: 实现(RLP 用既有 `RLPEncode.h` 基元;deposit 字段序对齐 op-geth `core/types/deposit_tx.go`)。测试绿。
- [ ] **Step 3**: 33 条全量断言测试(循环 golden 目录,`encode()`/`hash()`/`rawTransactions` 全比;含 Task 2 Step 3(b) 的 deposit×`OpDepositEncode` 交叉补跑)。绿后双路零回归。
- [ ] **Step 4**: Commit——`rtk git commit -m "feat(bcos-codec): EthBlockHeader RLP/keccak + OP deposit envelope 编码(33 条 op-geth 金值锚定,extraData 原样发射)"`

---

### Task 4: Types.h 载体 + OpSchedulerImpl 双签名 + fork 阈值注入 + 单测

**Files:**
- Modify: `bcos-framework/bcos-framework/engine/Types.h`(ExecutionPayload 增可选 `rawTransactions`(vector<bytes>)与 `withdrawalsRoot`(optional<h256>);通用路径不填不读)
- Modify: `bcos-evm/bcos-evm/opstack/OpForkSchedule.{h,cpp}`(新增 `OpForkTimestamps` 结构 + `configAt(timestamp, OpForkTimestamps) → OpForkConfig`,自由函数或静态方法;-38005 判定复用同一函数,裁定 A5)
- Create: `bcos-evm/bcos-evm/engine/OpSchedulerImpl.h`、`OpReceiptMap.h`
- Test: `bcos-evm/test/opstack/OpSchedulerImplTest.cpp`

**Interfaces:**
- Consumes: 桥全套(`Storage2Ledger`/`stateRootOf`/`processOpBlock`/`sealOpBlock`)、Task 3 编码器(测试用)、`OpForkSchedule::configAt`(新)。
- Produces(spec §4.1/§4.2 逐字):
```cpp
namespace bcos::evm::opstack {
struct OpForkTimestamps { uint64_t isthmusTime; uint64_t jovianTime; };
const OpForkConfig& configAt(uint64_t timestamp, const OpForkTimestamps&);
}
namespace bcos::evm::engine {
struct OpBlockEnv { /* fiscoHeader&, parentHash, prevRandao, baseFeePerGas,
    feeRecipient, parentBeaconBlockRoot, gasLimit, extraData, blobGasUsed */ };
struct OpExecuteBlockResult {
    std::vector<protocol::TransactionReceipt::Ptr> receipts;
    bcos::evm::opstack::OpBlockSeal seal;   // 现有结构原样不动
    bcos::h256 stateRoot;   // 净新增,与 seal 并列
    uint64_t gasUsed;       // 净新增,与 seal 并列
    bcos::h256 txRoot;      // 净新增(对 rawTransactions 建 trie),与 seal 并列
};
template <class Storage> class OpSchedulerImpl {
public:
    OpSchedulerImpl(protocol::TransactionReceiptFactory::Ptr, uint64_t chainId,
        bcos::evm::opstack::OpForkTimestamps);
    task::Task<std::vector<protocol::TransactionReceipt::Ptr>> executeBlock(
        Storage&, auto&, protocol::BlockHeader const&,
        ::ranges::input_range auto const&, ledger::LedgerConfig const&);  // 哑:throw
    task::Task<OpExecuteBlockResult> executeOpBlock(Storage&, OpBlockEnv const&,
        ::ranges::input_range auto const& rawTxBytes);
private:
    evmc::VM m_vm;   // evmc_create_evmone(),每调度器一实例
};
struct OpConsensusError : std::runtime_error {...};
struct OpStorageError  : std::runtime_error {...};
}
```

- [ ] **Step 1(TDD)**: 失败测试五组:(a) 哑 `executeBlock` 调用即 throw(消息含"OP mode");(b) `executeOpBlock` 全链路——用一条最简向量的 pre 播种(LedgerSeed)+ golden rawTransactions,断言**六项比对面**(seal 三字段 + result.stateRoot/gasUsed/txRoot)== 向量 `_op_expected` + golden.transactionsRoot;(c) 首笔非 attributes deposit → `OpConsensusError`;(d) `ThrowingStorage` → `OpStorageError`;(e) **`OpForkSchedule::configAt` 阈值判定**:timestamp ∈ [isthmusTime, jovianTime) → Isthmus 配置、∈ [jovianTime, ∞) → Jovian 配置(裁定 A5)。
- [ ] **Step 2**: 实现(spec §4.3 六步;交易分拣、`OpForkSchedule::configAt` 定档、桥一块一实例、六项比对面桥销毁前算毕;OpReceiptMap 只映射 status/gasUsed/logs;chainId/`OpForkTimestamps` 经构造参数注入,组合根负责读配置——裁定 D2)。测试绿。
- [ ] **Step 3**: concept 自检编译期断言(**具名,裁定 B3**):
  ```cpp
  static_assert(scheduler_v1::TransactionScheduler<
      OpSchedulerImpl<MLS::ViewType>, MLS::ViewType, StubExecutor,
      std::vector<protocol::Transaction::Ptr>>);
  ```
  (Storage 实参必须是 `ViewType` 而非 `GlobalStateStorage`)进测试文件——双签名策略的机器判据。双路零回归。
- [ ] **Step 4**: Commit——`rtk git commit -m "feat(bcos-evm): OpSchedulerImpl 双签名调度组件(executeOpBlock 走真桥链路,六项比对面)+ Types.h OP 载体字段 + fork 阈值注入"`

---

### Task 5a: 版本闸成员化 + opMode 编译期判据 + 通用组合根零漂移

**Files:**
- Modify: `engine/bcos-engine/EngineServiceImpl.h`(`maxEngineVersion` 构造参数;`isVersionSupported` 上界改读该成员;opMode 编译期判据)
- Modify: `engine/bcos-engine/EngineServiceImpl.cpp`(`supportedCapabilities` V4 条目,按 opMode 编译期分支)
- Test: `bcos-evm/test/opstack/EngineVersionGateTest.cpp`(framework 门控块)

**Interfaces:**
- Produces:`EngineServiceImpl` 增构造参数 `maxEngineVersion`;**opMode 判据用
  `if constexpr (requires { scheduler.executeOpBlock(...); })` 编译期探测**(裁定
  B1——禁运行时 bool,与既有全模板风格一致)。

- [ ] **Step 1(TDD)**: 失败测试:(a) OP 组合根(`OpSchedulerImpl` 实例化)`isVersionSupported(4)==true`;(b) 通用组合根(`SchedulerSerialImpl` 实例化)`isVersionSupported(4)` 行为与现状逐字节一致(`UnsupportedEngineApiVersion`,零漂移——探针 C-2 的单测先例)。
- [ ] **Step 2**: 实现:`maxEngineVersion` 构造参数(OP 组合根传 4,通用组合根传现状值);`if constexpr (requires { scheduler.executeOpBlock(...); })` 探测 opMode,编译期分支选择 `supportedCapabilities` 条目与后续 Task 5b 的 OP 分支可达性。测试绿。
- [ ] **Step 3**: 双路零回归 + engine 既有 Boost.Test 套件零回归。
- [ ] **Step 4**: Commit——`rtk git commit -m "feat(engine): 版本闸成员化(maxEngineVersion)+ opMode 编译期判据(if constexpr requires)+ 通用组合根 V4 零漂移"`

---

### Task 5b: engine newPayload OP 分支(从零)

**Files:**
- Modify: `engine/bcos-engine/EngineServiceImpl.h`(newPayload OP 分支;`:524` 与两处 TODO **零触碰**)
- Test: `bcos-evm/test/opstack/EngineOpBranchTest.cpp`(单测层,不含 gate;fixture 闭包见下)

**Fixture 闭包(裁定 B2,Files/Interfaces 列全)**:`EngineServiceImpl` 直构五参——
MemPool 用本地 Stub(仿 `EngineServiceTest.cpp:147-204` 桩,须满足
`remove(view)`/`seal(limit, view, out)`);Executor 用 `StubExecutor` 本地复刻
(`EbT8nReplayTest.cpp:45-49` 复刻惯例);BlockFactory 用
`bcos::test::createBlockFactory(createNormalCryptoSuite())`(链 protocol-tars +
testutils);storage fixture 仿 `EbT8nReplayTest::Impl`。**警示**:勿抄
`EngineServiceTest.cpp:221-229` 的悬垂局部桩工厂模式,scheduler/executor 与
service 须同生命周期。CMake:engine 头 include 路径(`${CMAKE_SOURCE_DIR}` 级)与
`EngineServiceImpl.cpp` 编入测试源均放 `if(TARGET bcos-framework)` 门控块内;
链接可能需补 `ledger` 库;注释写明"编入与链 engine 库二选一"护栏(该 CMake 改动
随 Task 6 的 `bcos-evm/test/CMakeLists.txt` 一并落,本任务先在 brief 中钉死约束)。

**Interfaces:**
- Consumes: Task 3/4 全部;`EthBlockHeader`;Task 5a 的 `maxEngineVersion`/opMode 判据。
- Produces:OP 分支私有方法 `handleOpNewPayload(request, version) → Task<PayloadStatus>`。

- [ ] **Step 1(TDD)**: 失败测试(直构 `EngineServiceImpl<...,OpSchedulerImpl<...>>`,内存 storage2 fixture,五参见上):(a) 时间戳×版本闸:Isthmus payload 走 V3 → -38005 错误;(b) blockHash 篡改 → INVALID + `latestValidHash==null`;(c) parent 未知(不预登记)→ SYNCING 且无落库;(d) OP 模式 FCU 带 attributes → -38003 且 head 照常推进(断言两点);(e) 通用组合根(`SchedulerSerialImpl` 实例化)收 V4 → 行为与现状一致(`UnsupportedEngineApiVersion`,Task 5a 已覆盖,此处复用断言防回归);(f) **`OpConsensusError` → 引擎层 INVALID 映射**(非静态校验路径,裁定 C7——用一个静态校验放行但语义违约的向量,断言错误经执行层分类而非静态校验短路);(g) **blobGasUsed 校验**(Isthmus 非零 → INVALID,裁定 C7)。
- [ ] **Step 2**: 实现,严格按 spec §6.1 七步与 §6.2/§6.3:静态校验(重组头/withdrawals/blob hashes 在场且空/excessBlobGas/executionRequests/blobGasUsed)、parentKnown 走 `getBlockNumber(view, parentHash, fromStorage)`("parent 已验证"操作性定义见 spec §6.1.5)、执行调 `executeOpBlock`、**六项比对面**比对(validationError 点名字段)、错误分类(`OpConsensusError`→INVALID,`OpStorageError`→-32603)、块登记(表级清单:`SYS_HASH_2_NUMBER`/`SYS_NUMBER_2_HASH` 编码抄 `BaselineScheduler.h:207-220`;头 RLP 入新表 `s_eth_block_header`(表名常量放 `bcos-evm/bcos-evm/engine/` 头内,不动 `LedgerTypeDef.h`,裁定 B5)键=number;回执经既有通道;`pushView`)。**本期不写链头进度表**(裁定 A4——FCU 保持只读+内存态,推进责任列入 spec §6.4 欠账台账)。**不落地 RPC 端点注册**(裁定 A6——`newPayloadV4`/`getPayloadV4` 的 `bcos-rpc`/`EngineEndpoint` 注册整体列入 spec §6.4 欠账;`getPayloadV4` 仅在 `EngineServiceImpl::getPayload(id, 4)` 层对 OP 模式返回明确错误)。
- [ ] **Step 3**: 测试绿 + 双路零回归 + engine 既有 Boost.Test 套件(`--list_content` 捕获对照)零回归。
- [ ] **Step 4**: Commit——`rtk git commit -m "feat(engine): newPayload OP 分支(校验七步/执行/六项比对面分档/块登记,链头进度表与 RPC 端点本期整体豁免)"`

---

### Task 6: 金向量 gate + 两块链式(专用向量对)+ 变异矩阵(13 类 18 例)

**Files:**
- Create: `bcos-evm/test/opstack/EngineNewPayloadGateTest.cpp`(framework 门控块)
- Modify: `bcos-evm/test/CMakeLists.txt`(源列表 + 需要时编入 `EngineServiceImpl.cpp`——不链 engine 库,防重复符号;engine 头 include 路径与编入均放 `if(TARGET bcos-framework)` 门控块;CMake 注释写明"编入与链 engine 库二选一"护栏,裁定 B2)

**Interfaces:** Consumes: Task 2 golden(含 `chained/` 子目录)、Task 3-5b 全部、E-b fixture 先例(`EbT8nReplayTest.cpp`)。

- [ ] **Step 1**: 33 向量 gate:逐向量 pre 播种 → fixture 预登记 parent(storage 写入,编码同 spec §6.1.6)→ 包装 payload(头字段←向量+golden;rawTransactions←golden)→ `newPayload(request, 4)` → 断言 VALID、`latestValidHash==blockHash`、**`payload.blockHash==golden.blockHash` 与 `result.txRoot==golden.transactionsRoot`(六项比对面之一)交叉断言**;blockHash 断言前先断言 `EthBlockHeader::encode()==golden.encodedHeaderHex` 逐字节(裁定 C3,字段级定位),失败时 `RecordProperty` dump 21 字段 hex;`RecordProperty` 逐向量记录。预期 33/33。翻红按 DIVERGENCES 纪律归因,禁改向量/金值。
- [ ] **Step 2**: 两块链式用例(spec §7.2,**裁定 A2 修正**):消费 Task 2 `golden/engine/chained/` 的专用链式向量对——`newPayload(A)→VALID → FCU(head=A) → newPayload(B)`(B 的 pre 即 A 的 post,不重播种)断言 B 的 parent-known **经块登记自然满足**(B 的 parent 不做 fixture 预登记);跳过 FCU 投未知 parent → SYNCING。
- [ ] **Step 3**: 变异矩阵 **13 类 18 例**(spec §7.3 表逐类):#1-7 各 1 例、#8(**六项比对面**)展开 6 例(每例只改一字段,断言 `validationError` 点名)、#9 parent 未知、**#10 同 payload 重发**(SYNCING → 补登记 parent → 重发同 payload → VALID,裁定 C1 新增)、#11 attributes 拒绝(-38003)、#12 通用版本闸零漂移、#13 存储故障(-32603)。**非 blockHash 桶的 INVALID 用例同时断言 `latestValidHash==parentHash`**。全绿。
- [ ] **Step 4**: Commit——`rtk git commit -m "test(bcos-evm): 验证者金向量 gate 33/33 + 两块链式闭环(专用向量对)+ 13 类 18 例变异矩阵"`

---

### Task 7: 五探针留痕 + N0 验收 + 文档回填

**Files:**
- Create: `.superpowers/sdd/probe-op-validator-gate-report.md`
- Modify: spec(§8 实测回填)、`bcos-evm/README.md`(engine 闭环状态,措辞守 spec §10/§6.4 欠账台账)

- [ ] **Step 1**: 五探针逐个(spec §7.4):①blockHash 校验探针(篡改必红);②**六项比对面**比对探针;③错误分类探针(-32603 vs INVALID);④块登记接线探针(测试 M-1:注入跳过块登记写入 → 两块链式用例第二块必转 SYNCING);⑤通用版本闸探针(测试 C-2:通用组合根 V4 行为零漂移;**fixture:`SchedulerSerialImpl` + `MockExecutorSerial` 本地复刻,先例 `testSchedulerSerial.cpp:20-75`;V4 拒绝在版本闸完成,不触达 executor**,裁定 C4)。注入→翻红原文→回退→复绿→git status,全录留痕文件。
- [ ] **Step 2**: **N0 相对基线**(裁定 C5):时点 = **Task 6 结束、本步骤开始前**;engine Boost 用 `test-bcos-engine --list_content 2>&1 | sort > n0-engine.txt`(**输出走 stderr,需 `2>&1` 重定向,实测陷阱**);双路 gtest `--gtest_list_tests | sort` 各存一份;三份存档 `.superpowers/sdd/n0-*.txt`。合并后三份全过;新增名单入报告。
- [ ] **Step 3**: 验收清单(spec §8,与本文件末清单逐行同构,裁定 C6)逐项命令化执行:`--gtest_filter='EngineNewPayloadGate.*'` 33/33、金值交叉 33/33、变异矩阵 13 类 18 例、`--gtest_filter='EthBlockHeader.*:OpDepositEncode.*'` 绿、N0 三份对照全过、五探针留痕在案、`git diff --stat $(git merge-base HEAD feat-evm-ledger-bridge) -- ports/ bcos-evm/test/opstack/t8n/vectors/ transaction-scheduler/` 全空、库纯净 rg、Task 1 契约修订已落、RPC 端点整体豁免确认(`bcos-rpc`/`EngineEndpoint` 零改动)。
- [ ] **Step 4**: 文档回填 + Commit——`rtk git commit -m "docs(engine): 验证者最小闭环收尾——探针留痕/N0 验收/README 状态(欠账台账原文保留)"`

---

## 验收清单(spec §8 同口径,逐行同构,全过才算完;裁定 C6)

- [ ] 金向量 gate:`--gtest_filter='EngineNewPayloadGate.*'` 33/33 VALID,
      `latestValidHash==blockHash`;blockHash 与 `EthBlockHeader::encode()`/离线
      金值逐字段交叉断言 33/33;`result.txRoot==golden.transactionsRoot` 33/33
- [ ] 两块链式用例(专用向量对,`golden/engine/chained/`)绿(parent-known 经
      块登记因果成立)+ 未知 parent→SYNCING
- [ ] 变异矩阵(§7.3)13 类 18 例逐例分档正确(六项比对面 6 例各断言点名字段;
      非 blockHash 桶 INVALID 用例断言 latestValidHash==parentHash)
- [ ] `--gtest_filter='EthBlockHeader.*:OpDepositEncode.*'` 金值单测绿
- [ ] 通用组合根 V4 行为零漂移(探针⑤/Task 5a 单测)
- [ ] 基线零回归:N0 相对基线(**Task 6 结束、Task 7 探针前**捕获:
      `--gtest_list_tests` 双路各一份 + engine Boost.Test
      `test-bcos-engine --list_content 2>&1 | sort`),三份存档
      `.superpowers/sdd/n0-*.txt`,合并后全过;新增名单入报告
- [ ] 五探针翻红复绿留痕在案(`probe-op-validator-gate-report.md`)
- [ ] `git diff --stat $(git merge-base HEAD feat-evm-ledger-bridge) -- ports/
      bcos-evm/test/opstack/t8n/vectors/ transaction-scheduler/` 空
- [ ] 桥 spec §10.1 + `Storage2Ledger.h` 头注的条件式许可修订已落(§4.4 义务,Task 1)
- [ ] RPC 端点整体豁免确认:`bcos-rpc`/`EngineEndpoint` 零改动(裁定 A6)
