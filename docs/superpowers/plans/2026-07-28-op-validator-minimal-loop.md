# OP 验证者模式最小闭环 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `engine_newPayloadV4` 导入 OP 块(执行走真账本桥链路)+ FCU 推进,33 向量金向量 gate 全 VALID + 两块链式因果闭环。

**Architecture:** OP 执行由双签名调度组件 `OpSchedulerImpl` 承载(OP 专用 `executeOpBlock` + 满足 concept 的哑通用签名);`handleNewPayload` 新增 OP 分支(从零:校验/执行/分档/块登记);ETH 头哈希与 deposit 编码入 bcos-codec;判别力来自 pinned op-geth 离线金值(全量 33 条)。

**Tech Stack:** C++20、GTest、bcos-codec RLP、storage2/MultiLayerStorage、evmone(vendored)、pinned op-geth v1.101702.2(离线金值仪式)。

**Spec:** `docs/superpowers/specs/2026-07-28-op-validator-minimal-loop-design.md`(rev.2)——**全部语义细节以 spec 为唯一依据**,本计划给结构/骨架/命令;每任务 brief 中引用的 spec 节号必须先读。

## Global Constraints

- 基座:`feat-evm-ledger-bridge` HEAD 开新分支 `feat-op-validator-loop`。
- `ports/`、`bcos-evm/test/opstack/t8n/vectors/` 逐字节零触碰;通用调度器文件(`transaction-scheduler/`)零改动;`bcos-framework` 改动仅限 `engine/Types.h` 新增可选字段。
- 命名空间:`bcos::evm::engine`(OpSchedulerImpl/OpReceiptMap)、`bcos::codec::rlp`(EthBlockHeader/OpDepositEncode 按 bcos-codec 既有惯例)。
- 库纯净:gtest/nlohmann 不入库目标;新测试全部进 `bcos-evm-opstack-tests` 的 framework 门控块。
- 协程上下文契约(spec §4.4):OP 执行链的嵌套 syncWait 仅在"底层同步完成 + x_state 串行"前提下许可;Task 1 完成契约文本修订前,后续任务不得开工。
- 错误码/分档(spec §6.1/§6.2 表)逐字执行:-38005/-38003/-32602/-32603、INVALID+null(blockHash 桶)、SYNCING 不入库。
- 文档提交需 `git add -f`(`.git/info/exclude` 含 `docs/superpowers/`);git 用 `rtk` 前缀;clang-format hook 归一化照 §4.6(a) 口径记录;禁后台命令(Bash timeout 600000)。
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

### Task 2: 离线金值仪式(全量 33 条)

**Files:**
- Create: `bcos-evm/test/opstack/t8n/golden/engine/manifest.txt`、`<vectorId>.golden.json` ×33
- Create: `bcos-evm/test/opstack/t8n/golden/engine/README.md`(生成记录:op-geth pin SHA、命令、extraData 选值)

**Interfaces:** Produces: 每向量 `{blockHash, transactionsRoot, extraData(9 字节 hex,选值固定), excessBlobGas: "0x0", rawTransactions: [hex...]}`(rawTransactions 含 deposit 的 0x7E 完整 envelope——同时作为 Task 3 OpDepositEncode 的字节级金值)。

- [ ] **Step 1**: 在 pinned op-geth 检出内(沿 `t8n/generator/README.md` 仪式:pin 校验、干净树、用毕清理)扩展/新写离线工具:对每条向量,以其 env+txs 构造完整 OP 头,输出上述金值 JSON。extraData 选值:Isthmus/Jovian 统一 `0x00` ‖ denominator=250 ‖ elasticity=6(canyon 默认参数,大端 u32),写死进 README 与生成命令。
- [ ] **Step 2**: 产物落 `golden/engine/`;自检:`requestsHash` 相关字段与向量 `_op_expected` 已有值交叉一致;33 个文件与 manifest 集合相等;`git diff --stat -- bcos-evm/test/opstack/t8n/vectors/` 为空。
- [ ] **Step 3**: Commit——`rtk git commit -m "test(bcos-evm): 验证者 gate 离线金值表(33 条 blockHash/txRoot/raw envelopes,pinned op-geth)"`(路径受 info/exclude 影响时 `add -f`)。

---

### Task 3: EthBlockHeader + OpDepositEncode(bcos-codec)+ 金值单测

**Files:**
- Create: `bcos-codec/bcos-codec/rlp/EthBlockHeader.{h,cpp}`、`OpDepositEncode.{h,cpp}`
- Modify: `bcos-codec/CMakeLists.txt`(源列表)
- Test: `bcos-evm/test/opstack/EthBlockHeaderTest.cpp`(framework 门控块;金值取自 Task 2 golden 文件,**不自算**)

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

- [ ] **Step 1(TDD)**: 写失败测试:任取 3 条 golden(Isthmus 单笔/Jovian 多笔/deposit-only),用向量 env+`_op_expected`+golden.extraData 组 `EthBlockHeader`,断言 `hash()==golden.blockHash`;deposit:`encodeDepositEnvelope(向量 _op_deposit 字段)==golden.rawTransactions[0]` 逐字节。跑确认编译失败。
- [ ] **Step 2**: 实现(RLP 用既有 `RLPEncode.h` 基元;deposit 字段序对齐 op-geth `core/types/deposit_tx.go`)。测试绿。
- [ ] **Step 3**: 33 条全量断言测试(循环 golden 目录,hash 与 rawTransactions 全比)。绿后双路零回归。
- [ ] **Step 4**: Commit——`rtk git commit -m "feat(bcos-codec): EthBlockHeader RLP/keccak + OP deposit envelope 编码(33 条 op-geth 金值锚定)"`

---

### Task 4: Types.h 载体 + OpSchedulerImpl 双签名 + 单测

**Files:**
- Modify: `bcos-framework/bcos-framework/engine/Types.h`(ExecutionPayload 增可选 `rawTransactions`(vector<bytes>)与 `withdrawalsRoot`(optional<h256>);通用路径不填不读)
- Create: `bcos-evm/bcos-evm/engine/OpSchedulerImpl.h`、`OpReceiptMap.h`
- Test: `bcos-evm/test/opstack/OpSchedulerImplTest.cpp`

**Interfaces:**
- Consumes: 桥全套(`Storage2Ledger`/`stateRootOf`/`processOpBlock`/`sealOpBlock`)、Task 3 编码器(测试用)。
- Produces(spec §4.1/§4.2 逐字):
```cpp
namespace bcos::evm::engine {
struct OpBlockEnv { /* fiscoHeader&, parentHash, prevRandao, baseFeePerGas,
    feeRecipient, parentBeaconBlockRoot, gasLimit, extraData, blobGasUsed */ };
struct OpExecuteBlockResult {
    std::vector<protocol::TransactionReceipt::Ptr> receipts;
    bcos::evm::opstack::OpBlockSeal seal;
};
template <class Storage> class OpSchedulerImpl {
public:
    OpSchedulerImpl(protocol::TransactionReceiptFactory::Ptr, uint64_t chainId);
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

- [ ] **Step 1(TDD)**: 失败测试四组:(a) 哑 executeBlock 调用即 throw(消息含"OP mode");(b) executeOpBlock 全链路——用一条最简向量的 pre 播种(LedgerSeed)+ golden rawTransactions,断言 seal 六字段 == 向量 `_op_expected` + golden.txRoot;(c) 首笔非 attributes deposit → OpConsensusError;(d) ThrowingStorage → OpStorageError。
- [ ] **Step 2**: 实现(spec §4.3 六步;交易分拣、OpForkSchedule::configAt、桥一块一实例、seal 桥销毁前算毕;OpReceiptMap 只映射 status/gasUsed/logs)。测试绿。
- [ ] **Step 3**: concept 自检编译期断言:`static_assert(scheduler_v1::TransactionScheduler<OpSchedulerImpl<...>, ...>);` 进测试文件——双签名策略的机器判据。双路零回归。
- [ ] **Step 4**: Commit——`rtk git commit -m "feat(bcos-evm): OpSchedulerImpl 双签名调度组件(executeOpBlock 走真桥链路)+ Types.h OP 载体字段"`

---

### Task 5: engine newPayload OP 分支(从零)+ 版本闸成员化

**Files:**
- Modify: `engine/bcos-engine/EngineServiceImpl.h`(OP 分支;版本上界成员化;`:524` 与两处 TODO **零触碰**)
- Modify: `engine/bcos-engine/EngineServiceImpl.cpp`(supportedCapabilities V4 条目,OP 组合根条件)
- Test: `bcos-evm/test/opstack/EngineOpBranchTest.cpp`(单测层,不含 gate)

**Interfaces:**
- Consumes: Task 3/4 全部;`EthBlockHeader`。
- Produces: `EngineServiceImpl` 增构造参数 `opMode`(bool,或以 SchedulerType 特征推导——实现选一种并在报告记录)与 `maxEngineVersion`;OP 分支私有方法 `handleOpNewPayload(request, version) → Task<PayloadStatus>`。

- [ ] **Step 1(TDD)**: 失败测试(直构 EngineServiceImpl<...,OpSchedulerImpl<...>>,内存 storage2 fixture):(a) 时间戳×版本闸:Isthmus payload 走 V3 → -38005 错误;(b) blockHash 篡改 → INVALID + latestValidHash==null;(c) parent 未知(不预登记)→ SYNCING 且无落库;(d) OP 模式 FCU 带 attributes → -38003 且 head 照常推进(断言两点);(e) 通用组合根(SchedulerSerialImpl 实例化)收 V4 → 行为与现状一致(UnsupportedEngineApiVersion)。
- [ ] **Step 2**: 实现,严格按 spec §6.1 七步与 §6.2/§6.3:静态校验(重组头/withdrawals/blob hashes 在场且空/excessBlobGas/executionRequests)、parentKnown 走 `getBlockNumber(view, parentHash, fromStorage)`、执行调 `executeOpBlock`、seal 六字段比对(validationError 点名字段)、错误分类(OpStorageError→-32603)、块登记(表级清单:SYS_HASH_2_NUMBER/SYS_NUMBER_2_HASH 编码抄 `BaselineScheduler.h:207-220`;头 RLP 入新表 `s_eth_block_header` 键=number;回执经既有通道;pushView;SYS_CURRENT_STATE 由 FCU head 推进时写)。
- [ ] **Step 2b**: 端点层(spec §6.3):`bcos-rpc` `EngineEndpoint` 注册 `engine_newPayloadV4`/`engine_getPayloadV4` 方法(薄转发 version=4,照 V3 方法样式);`getPayloadV4` 在 OP 模式返回明确错误(出块未 OP 化);单测断言该错误消息。
- [ ] **Step 3**: 测试绿 + 双路零回归 + engine 既有 Boost.Test 套件(`--list_content` 捕获 N0 后全跑)零回归。
- [ ] **Step 4**: Commit——`rtk git commit -m "feat(engine): newPayload OP 分支(校验/执行/分档/块登记)+ 版本闸成员化(仅 OP 组合根 V4)"`

---

### Task 6: 金向量 gate + 两块链式 + 变异矩阵

**Files:**
- Create: `bcos-evm/test/opstack/EngineNewPayloadGateTest.cpp`(framework 门控块)
- Modify: `bcos-evm/test/CMakeLists.txt`(源列表 + 需要时编入 `EngineServiceImpl.cpp`——不链 engine 库,防重复符号;CMake 注释写明护栏)

**Interfaces:** Consumes: Task 2 golden、Task 3-5 全部、E-b fixture 先例(`EbT8nReplayTest.cpp`)。

- [ ] **Step 1**: 33 向量 gate:逐向量 pre 播种 → fixture 预登记 parent(storage 写入,编码同 Task 5)→ 包装 payload(头字段←向量+golden;rawTransactions←golden)→ `newPayload(request, 4)` → 断言 VALID、`latestValidHash==blockHash`、**`payload.blockHash==golden.blockHash` 与 `seal.txRoot==golden.transactionsRoot` 交叉断言**;`RecordProperty` 逐向量记录。预期 33/33。翻红按 DIVERGENCES 纪律归因,禁改向量/金值。
- [ ] **Step 2**: 两块链式用例(spec §7.2):`newPayload(A)→VALID → FCU(head=A) → newPayload(B, parentHash=hash(A))` 断言 B 的 parent-known **经块登记自然满足**(B 的 parent 不做 fixture 预登记);跳过 FCU 投未知 parent → SYNCING。
- [ ] **Step 3**: 变异矩阵 11 行(spec §7.3 表逐行,含 seal 六字段各一例、-38005/-38003/-32603 三码)。全绿。
- [ ] **Step 4**: Commit——`rtk git commit -m "test(bcos-evm): 验证者金向量 gate 33/33 + 两块链式闭环 + 11 行变异矩阵"`

---

### Task 7: 五探针留痕 + N0 验收 + 文档回填

**Files:**
- Create: `.superpowers/sdd/probe-op-validator-gate-report.md`
- Modify: spec(§8 实测回填)、`bcos-evm/README.md`(engine 闭环状态,措辞守 spec §10/§6.4 欠账台账)

- [ ] **Step 1**: 五探针逐个(spec §7.4):blockHash 校验/seal 比对/错误分类/**块登记接线**(注入跳过登记→两块链式第二块必 SYNCING)/**通用版本闸**(通用组合根 V4 行为零漂移)。注入→翻红原文→回退→复绿→git status,全录留痕文件。
- [ ] **Step 2**: 验收清单(spec §8)逐项命令化执行:gate filter、金值交叉 33/33、N0 相对基线(双路 gtest_list_tests + engine Boost --list_content 三份对照)、`git diff --stat <基座> -- ports/ bcos-evm/test/opstack/t8n/vectors/ transaction-scheduler/` 全空、库纯净 rg。
- [ ] **Step 3**: 文档回填 + Commit——`rtk git add -f ... && rtk git commit -m "docs(engine): 验证者最小闭环收尾——探针留痕/N0 验收/README 状态(欠账台账原文保留)"`

---

## 验收清单(spec §8 同口径,全过才算完)

- [ ] `--gtest_filter='EngineNewPayloadGate.*'` 33/33 VALID + 金值交叉断言 33/33
- [ ] 两块链式绿 + 未知 parent→SYNCING
- [ ] 变异矩阵 11/11;`--gtest_filter='EthBlockHeader.*:OpSchedulerImpl.*:EngineOpBranch.*'` 全绿
- [ ] N0 三份基线(双路 + engine Boost)全过,新增名单入报告
- [ ] 五探针留痕在案;`ports/`/`vectors/`/`transaction-scheduler/` diff 空
- [ ] Task 1 契约修订已落(桥 spec + 头注)
