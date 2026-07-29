# Task 5b 报告 — engine newPayload OP 分支本体

**未编译验证**:按用户指令,开发期跳过 FISCO 编译/ctest。本报告用 (1) API 先例映射表
(每个调用在仓内找已编译先例)+ (2) 关键路径静态走查 替代构建验收。

---

## 0. 交付物

| 文件 | 性质 | 说明 |
|---|---|---|
| `engine/bcos-engine/EngineServiceImpl.h` | 改 | 4 个 OP 异常类型;`detail` 4 个 OP 声明;`updateForkchoice` 两处 `if constexpr`;`handleGetPayload` OP 分支;`handleNewPayload` 编译期分派;新增 `handleOpNewPayload` / `registerOpBlock` |
| `engine/bcos-engine/EngineServiceImpl.cpp` | 改 | 3 个 ETH 头协议常量 + `narrowU256ToU64` / `toEthLogsBloom` / `validateOpNewPayloadRequest` / `rebuildOpEthHeader` |
| `bcos-evm/bcos-evm/engine/OpEngineSeam.h` | **新** | 接缝发布面:`SYS_ETH_BLOCK_HEADER` 表名常量、`OpBlockCommitments`+`commitmentsOf`、`computeOpTxRoot`、`detail::toBcosH256`/`toBcosBloom` |
| `bcos-evm/bcos-evm/engine/OpSchedulerImpl.h` | 改(加性) | 引入 OpEngineSeam.h;公开 5 个关联名(`BlockEnv`/`ExecuteResult`/`ConsensusError`/`StorageError`/`c_ethBlockHeaderTable`)+ 3 个静态/常量成员函数(`commitmentsOf`/`computeTxRoot`/`isIsthmusActiveAt`/`isJovianActiveAt`);step 6 txRoot 改调 `computeOpTxRoot`;`toBcosH256` 定义搬到 seam 头 |
| `bcos-evm/test/opstack/EngineOpBranchTest.cpp` | **新** | 套件 `EngineOpBranch`,8 例(brief a–h) |

**未触碰**:`ports/`、`vectors/`、`transaction-scheduler/`、`bcos-rpc/`、
`bcos-framework/.../LedgerTypeDef.h`、`bcos-framework/engine/Types.h`、
`bcos-evm/test/CMakeLists.txt`(CMake 接入随 T6)、`EngineServiceImpl.h:524` 与两处
既有 TODO。

---

## 1. 架构裁定:为什么 OP 特有类型经 `SchedulerType` 关联名到达

T5a 已把"engine 不得依赖 bcos-evm"(库纯净)钉死在 `c_opMode` 注释里:`engine` CMake
目标只链 `bcos-framework/bcos-task/bcos-utilities/ledger`。但 OP 分支确实需要
`OpBlockEnv`、`OpConsensusError`/`OpStorageError`、txRoot 推导、头表表名、以及
bcos:: 类型表述的比对面。

唯一不引入 include 的通道是 **依赖名**:`typename SchedulerType::BlockEnv`、
`SchedulerType::computeTxRoot(...)` 等在 `if constexpr (c_opMode)` 内于实例化点查找,
而实例化点所在 TU 必然已 include `OpSchedulerImpl.h`。定义放新头 `OpEngineSeam.h`
(位置满足 brief"表名常量放 `bcos-evm/bcos-evm/engine/` 头内",裁定 B5),
`OpSchedulerImpl` 只做再发布。

> **偏离记账**:brief 的 Files 只列了 EngineServiceImpl.h + 测试文件。本任务额外对
> T4 的 `OpSchedulerImpl.h` 做了**纯加性**修改并新增一个 bcos-evm 头。理由如上——
> 在库纯净约束下没有第二条路径;所有新增成员皆为别名/静态投影,`executeOpBlock`
> 语义零变化(唯一实体改动是把 step 6 的 MPT 建根抽成 `computeOpTxRoot`,同一实现、
> 两个调用点)。

---

## 2. §6.1 六步逐条落位

| spec §6.1 | 落位 | 关键点 |
|---|---|---|
| **1. 时间戳×版本闸 -38005** | `handleOpNewPayload` Step 1 | `isthmusActive != (version == 4)` 即抛 `UnsupportedFork`。判据用 `scheduler.isIsthmusActiveAt(ts)`(阈值比较留在 bcos-evm 侧,与 `configAt` 同源同一个 `m_forkTimestamps.isthmusTime`)。**M4 落实**:不用 `configAt`——它把 sub-isthmusTime 也解析成 Isthmus 配置,答不了"是否 pre-Isthmus";测试 (a) 对这一方向有独立断言 |
| **2. 静态校验 + 重组头 blockHash 比对** | Step 2 + `detail::validateOpNewPayloadRequest` + `detail::rebuildOpEthHeader` | 四个 OP 约束:withdrawals 在场且空 / expectedBlobVersionedHashes 空 / excessBlobGas 在场且=0 / blobGasUsed 在场且(pre-Jovian)=0;另加 rawTransactions、withdrawalsRoot、parentBeaconBlockRoot 在场性与四个 u256→u64 范围检查。重组 21 字段头,`hash() != blockHash` → **INVALID + latestValidHash=null**,不用已废弃的 `InvalidBlockHash` |
| **3. parentKnown 走 storage** | Step 3 | `ledger::getBlockNumber(view, parentHash, fromStorage)`(读 `SYS_HASH_2_NUMBER`),`nullopt` → **SYNCING**,该路径不 `newMutable`、不写、不 `pushView` |
| **4. 执行 + 六项比对面 + 错误分类** | Step 4/5 | `executeOpBlock(view, blockEnv, *rawTransactions)`;`catch (ConsensusError)` → INVALID,`catch (StorageError)` → 抛 `OpExecutionInternalError`(-32603,绝不 INVALID)。六项:receiptsRoot / logsBloom / withdrawalsRoot(seal)+ stateRoot / gasUsed / txRoot(result);外加 §5.1 的两项(Jovian `blobGasUsed` DA footprint、`requestsHash` 漂移哨兵),注释明确它们**不计入"六项"** |
| **5. INVALID 的 latestValidHash** | Step 3 之后的 `latestValidHash` 常量 | 步骤 3 通过 ⟺ parent ∈ `SYS_HASH_2_NUMBER` ⟺ "parent 已验证"(裁定 C2 的操作性定义),故步骤 4/5 的 INVALID 一律带 parentHash;步骤 2 的桶恒 null |
| **6. 块登记(表级清单)+ pushView** | `registerOpBlock` + Step 6 | 见 §3;**不写链头进度表**(裁定 A4),不 `mergeBackStorage`(§6.1"merge 时机") |

§6.2(FCU):`updateForkchoice` 中 OP 模式**跳过**开头的 `validatePayloadAttributes`
早退(否则会在 head 推进前返回 Invalid),在"生成 payload 之前"抛
`UnsupportedOpPayloadAttributes`(-38003)——此时 storage 查询、单调性检查、
`m_forkchoiceState`/`m_trackedHeadBlock`/safe/finalized 更新**均已完成且保留**。

§6.3:`getPayload` 在 OP 模式无条件抛 `OpPayloadBuildingUnsupported`(比 V4-only
更严:OP 模式根本不建块,cache 恒空,否则会返回误导性的 `UnknownPayload`);RPC 端点
零触碰。

---

## 3. 块登记键值编码

| 表 | 键 | 值 | 先例 |
|---|---|---|---|
| `s_number_2_hash` | blockNumber 十进制串 | blockHash 原始 32 字节(`asBytes()`) | `BaselineScheduler.h:207-220` 逐字节 |
| `s_hash_2_number` | blockHash 原始 32 字节(`concepts::bytebuffer::toView`) | blockNumber 十进制串 | 同上;**必须**如此,否则步骤 3 的 `getBlockNumber(fromStorage)` 找不到 |
| `s_eth_block_header`(新) | blockNumber 十进制串(同 `s_number_2_hash` 口径) | `EthBlockHeader::encode()` 原始字节(21 字段 RLP) | 表名常量 `bcos::evm::engine::SYS_ETH_BLOCK_HEADER`,裁定 B5 |
| `s_hash_2_receipt` | tx hash 原始 32 字节 | `TransactionReceipt::encode()` | `bcos-ledger/LedgerMethods.h:106-119` `prewriteBlockToBuffer` |

tx hash = keccak256(原始 EIP-2718 envelope)——ETH 交易哈希定义,也是此处唯一可得的
(OP 路径携带的是原字节而非 `protocol::Transaction`)。**因此 OP 组合根的 BlockFactory
必须用 keccak256 crypto suite**(注释已写)。四张表全写进 Step 4 开的同一 mutable 层,
由单次 `pushView` 发布。

---

## 4. API 先例映射表(替代编译验证)

| 调用 | 仓内已编译先例 |
|---|---|
| `ledger::getBlockNumber(view, hash, fromStorage)` | `EngineServiceImpl.h` `updateForkchoice`(本文件既有,3 处);定义 `bcos-ledger/LedgerMethods.h:429-444` |
| `storage2::writeOne(view, StateKey{table, key}, Entry)` | `BaselineScheduler.h:210-220`;`LedgerMethods.h:113-119` |
| `StateKey{std::string_view, std::string_view}` | `StateKey.h:24`;调用形 `BaselineScheduler.h:212` |
| `concepts::bytebuffer::toView(h256)` | `BaselineScheduler.h:218`;`LedgerMethods.h:117` |
| `Entry::set(bytes)` / `Entry::set(std::string)` | `BaselineScheduler.h:210/215`(两种重载各一) |
| `FixedBytes::asBytes()` | `BaselineScheduler.h:210` |
| `MLS::fork()` / `newMutable()` / `pushView()` | `EngineServiceImpl.h` 既有(`updateForkchoice`/`handleNewPayload`);定义 `MultiLayerStorage.h:453/526/543` |
| `m_blockFactory->blockHeaderFactory()->createBlockHeader()` + `setNumber/setTimestamp` | `EngineServiceImpl.h` `buildPayload`(本文件既有) |
| `cryptoSuite()->hashImpl()->hash(bytes const&)` | `Hash.h:55` 虚重载;用法遍布仓内 |
| `TransactionReceipt::encode(bytes&)` | `TransactionReceipt.h:41`;用法 `LedgerMethods.h:110-111` |
| `EthBlockHeader{...21 字段...}.encode()/.hash()` | `EthBlockHeaderTest.cpp:117-141/227-238`(T3,33 条金值锚定) |
| `FixedBytes(byte const*, size_t)` | `FixedBytes.h:178`;用法 `OpSchedulerImpl.h` `toBcosH256` |
| `FixedBytes(std::string const&, FromHex)` | `FixedBytes.h:189` |
| `evmone::state::MPT` + `evmone::rlp::encode(uint64)` | `OpSchedulerImpl.h` executeOpBlock step 6(移出前的原实现) |
| `BOOST_THROW_EXCEPTION(X{} << errinfo_comment{...})` | `EngineServiceImpl.h` 既有 8 处 |
| `bcos::task::syncWait` / gtest fixture / MLS 测试夹具 | `EngineVersionGateTest.cpp`(T5a,同目录) |
| `OpSchedulerImpl` 构造(receiptFactory, chainId, OpForkTimestamps) | `EngineVersionGateTest.cpp:296-297`;`OpSchedulerImplTest.cpp` |

---

## 5. 关键路径静态走查

1. **模板分支不实例化**:`updateForkchoice`/`handleNewPayload`/`handleGetPayload` 的
   `if constexpr` 条件是当前实例化的成员 `c_opMode`,被丢弃分支不实例化
   ([stmt.if]/2)。因此 OP 模式下 `m_memPool.get().remove(view)` / `seal(...)` /
   `buildPayload` 全部不实例化——测试用的空 `StubMemPool` 才能编过;反向,通用组合根
   不实例化 `handleOpNewPayload`,`SchedulerType::BlockEnv` 等名不参与查找。
2. **协程规则**:`co_await` 只出现在 `try` 块内,两个 `catch` 体内只有 `co_return` /
   `BOOST_THROW_EXCEPTION`——`co_await` 不得出现在 handler 中([expr.await]/2),已规避。
3. **持锁跨 co_await**:执行段在 `std::unique_lock lock(x_state)` 内跨多个 `co_await`。
   这正是 §4.4 安全前提 2 的要求,且 §4.4 的失效判据显式覆盖该用法;通用路径
   `handleNewPayload` 早有同型先例(持锁 `co_await mergeBackStorage()`)。
4. **失败路径无泄漏**:步骤 4/5 的 INVALID 直接 `co_return`,`view`(含 mutable 层)
   随协程帧析构,未 `pushView`,MLS 层栈无残留。
5. **窄化**:四个 u256→u64 由 `validateOpNewPayloadRequest` 前置拒绝越界,
   `rebuildOpEthHeader` 与 `BlockEnv` 构造里的 `.value()` 因此为全函数;前置条件被破坏
   时抛 `std::bad_optional_access`(响,而非静默错哈希)。
6. **聚合初始化字段序**:`EthBlockHeader` 21 字段、`OpBlockEnv` 9 字段、
   `OpBlockCommitments` 8 字段的指定初始化器顺序已逐字段与声明序核对一致。
7. **ODR**:`toBcosH256` 从 `OpSchedulerImpl.h` 移到 `OpEngineSeam.h`(同一
   `bcos::evm::engine::detail`,单一定义),原 6 处调用点不变。
8. **链接边**:engine 新增 `#include <bcos-codec/rlp/EthBlockHeader.h>`;`engine` →
   `ledger` →(PUBLIC)`codec`(`bcos-ledger/CMakeLists.txt:27`),无需改 CMake,
   且不是 bcos-evm 依赖。
9. **行宽/格式**:全部改动 ≤100 列;新文件按 clang-format 归一;
   `EngineServiceImpl.h` **未**整文件 clang-format(HEAD 版本本身对 clang-format-17
   不干净,整跑会污染无关行),仅手工对齐新增行。
10. **通用路径零行为变化**:`git diff -w` 复核——除新增 include/异常/声明与
    `if constexpr` 包裹外,既有语句一字未改(仅缩进)。

---

## 6. 测试清单(`EngineOpBranch`,8 例)

| 用例 | 断言 |
|---|---|
| `TimestampVersionGateRejectsMismatch` (a) | Isthmus×V3 抛 `UnsupportedFork`;pre-Isthmus×V4 抛 `UnsupportedFork`(M4 方向);Isthmus×V4 过闸(落到 SYNCING) |
| `BlockHashMismatchIsInvalidWithNullLatestValidHash` (b) | Invalid、非 `InvalidBlockHash`、latestValidHash 无值、error 含 "blockHash";未注册 parent 仍非 SYNCING ⇒ 证明步骤 2 先于步骤 3 |
| `UnknownParentIsSyncingAndWritesNothing` (c) | Syncing + 无 latestValidHash/error;块与 parent 均未登记(不入库) |
| `ForkchoiceWithAttributesRefusedButHeadStillAdvances` (d) | 抛 `UnsupportedOpPayloadAttributes`;`getSafeBlockNumber()==1`(证明 head 已推进);随后无 attributes 的 FCU 返回 Valid |
| `GenericCompositionRootStillRejectsV4` (e) | 通用组合根 V4 仍 `UnsupportedEngineApiVersion`(T5a 回归) |
| `ConsensusErrorFromExecutionMapsToInvalid` (f) | 向量 = 单条 `0xff` 型字节(静态全过、blockHash 自洽),Invalid + latestValidHash==parentHash + error 含 "OP block execution rejected" + 未登记 ⇒ 判决来自执行层分类而非静态短路 |
| `NonZeroBlobGasUsedIsInvalidUnderIsthmus` (g) | Invalid + null + error 含 "blobGasUsed" |
| `GetPayloadIsRefusedInOpMode` (h) | 抛 `OpPayloadBuildingUnsupported` |

**未覆盖(有意)**:端到端 VALID 路径(六项全对 + 块登记)。手写字面量无法造出与真实
执行一致的 stateRoot/receiptsRoot——那是 T6 金向量 gate 的职责。故
`registerOpBlock` 本体在本任务只有静态走查 + 先例映射,无运行时覆盖,**列入 T6 必须
覆盖项**。

---

## 7. 偏离与欠账(交 T7 回填 spec §6.4 / 偏离台账)

1. **`executionRequests` 无载体**:`NewPayloadRequest`(Types.h)没有该成员,spec §6.1
   步骤 2 的"在场且空"在本层**恒真**。已在 `validateOpNewPayloadRequest` 注释写明:
   重组头把 `requestsHash` 钉在 OP 空请求常量上,一旦将来有载体,非空列表会以
   blockHash 失配呈现——正是 spec 指定的桶。
2. **-32602 不可判**:缺参/畸形属 RPC 解析层,本期 RPC 整体豁免;
   `NewPayloadRequest` 到手时"缺参"与"空数组"不可区分。故 `rawTransactions` 缺失等
   按 INVALID + 点名字段报,而非 -32602。
3. **JSON-RPC 错误码无映射实体**:4 个新异常类型只在注释里承载
   -38005/-38003/-32603;映射随 EngineEndpoint 注册(§6.4)交付。既有通用异常同样如此。
4. **`getPayload` OP 模式无条件拒绝**,而非仅 V4(理由见 §2 表);spec 字面是
   "getPayloadV4",此处更严,已注释。
5. **§5.1 两项额外比对**(Jovian `blobGasUsed`、`requestsHash`)不计入"六项比对面",
   避免与 §4.1 的计数口径冲突。
6. **`InvalidBlockHash` 枚举**仍留在 Types.h 供通用路径使用(§6.4 已有清理欠账)。
7. **父子号连续性未校验**(`blockNumber == parentNumber + 1`)——spec 六步未要求,
   本期不做,记一条。
8. **CMake 接入随 T6**:`EngineOpBranchTest.cpp` 与 `EngineVersionGateTest.cpp` 均需
   `${CMAKE_SOURCE_DIR}` include 路径 + 把 `EngineServiceImpl.cpp` 编入测试源
   ("编入与链 engine 库二选一"),放 `if(TARGET bcos-framework)` 门控块内。

---

## 8. Commit

`feat(engine): newPayload OP 分支(校验七步/执行/六项比对面分档/块登记,链头进度表与
RPC 端点本期整体豁免)`

---

# 附:T5b 审查修复轮(控制器裁定 I1+I2+M1+M2+M5+M8;I3/I4 记账)

**未编译验证**同前。

## F1 — I1:StubOpScheduler 零金值驱动三组盲区(`EngineOpBranchTest.cpp`)

审查者论断成立:接缝是**纯鸭子类型**(无继承、engine 侧零 bcos-evm 依赖),因此
stand-in 是接缝的一等公民。新增 `StubOpScheduler` 自备全部 9 个依赖名
(`BlockEnv`/`ExecuteResult`/`ConsensusError`/`StorageError`/`c_ethBlockHeaderTable`/
`commitmentsOf`/`computeTxRoot`/`isIsthmusActiveAt`/`isJovianActiveAt`/`executeOpBlock`),
并以 `static_assert(StubEngineService::c_opMode)` 钉住"stub 与 OpSchedulerImpl 过同一
探针"。

设计要点:
- `commitmentsOf` 被 engine **静态**调用,故不能读实例状态 → 把 `StubCommitments`
  内嵌进 `ExecuteResult`,由静态函数投影;
- `computeTxRoot` **转发真实** `computeOpTxRoot`——txRoot 参与 blockHash,伪造它等于
  两边一起伪造,失去意义;
- `prepareValidScenario()` 一处产出"parent 已登记 + blockHash 自洽 + commitments 与
  payload 逐字段相等"的基线,三组测试共用。

新增 4 个 TEST(总数 8 → 12):

| 用例 | 覆盖 |
|---|---|
| `ValidPayloadRegistersAllFourTables` (i) | VALID 端到端 + `latestValidHash==blockHash` + `executeOpBlockCalls==1`;**四表逐表断言**:①`s_hash_2_number` 经 `getBlockNumber(fromStorage)` 可查且值==blockNumber(顺带证明"登记块即合法 parent");②`s_number_2_hash` 键=十进制串、值==blockHash 原始字节;③`s_eth_block_header` 值==`ethHeader.encode()` 且 `header.hash()==blockHash`(登记与判决自洽);④`s_hash_2_receipt` 键==keccak(原始 envelope)、值==`receipt->encode()` |
| `EachComparisonSurfaceFieldMismatchIsNamed` (j) | 六项比对面**逐字段各一例**(表驱动 + `SCOPED_TRACE`,每例独立 fixture、只扰动一个字段):receiptsRoot / logsBloom / withdrawalsRoot / stateRoot / gasUsed / transactionsRoot;每例断言 INVALID + `lvh==parentHash` + validationError 点名该字段 + **未登记** |
| `StorageErrorFromExecutionIsInternalErrorNotInvalid` (k) | `OpStorageError` → 抛 `OpExecutionInternalError`(-32603),**绝不 INVALID**;且未登记。金向量 gate 结构上够不到此分支(一致向量不会触发存储故障) |
| `NonConsecutiveBlockNumberIsInvalid` (l) | 见 F2 |

VALID 基线里 `requestsHash` 置为 `header.requestsHash`(engaged),使 §5.1 的
requestsHash 比对在 VALID 路径上被真实走过;`blobGasUsed` 置 nullopt(pre-Jovian)。

## F2 — I2:父子块号连续性(`EngineServiceImpl.h` step 3 之后)

`payload.blockNumber != *parentBlockNumber + 1` → INVALID + `lvh=parent`。理由写进注释:
两个登记索引(`SYS_NUMBER_2_HASH`、`s_eth_block_header`)**按 number 作键**,缺此校验时
"parent 合法但 number 任意"的 payload 会静默覆写既有高度的索引——是链索引损坏,不只是
放行一个坏块。测试 (l) 断言 INVALID + lvh + 点名 "blockNumber" + `executeOpBlockCalls==0`
(证明校验先于执行)+ 未登记。

## F3 — M1:blockNumber 范围纪律(`EngineServiceImpl.cpp`)

`blockNumber` 是 `int64_t` 而非 u256,风险在**符号**不在宽度:负值会在头里回绕成巨大
uint64,并被 `lexical_cast` 成畸形登记键。`validateOpNewPayloadRequest` 加
`payload.blockNumber < 0` → INVALID,与 `narrowU256ToU64` 同一"先显式检查再窄化"纪律
(注释指向仓内静默截断事故记忆)。

## F4 — M2:回执/交易数不等即抛(`registerOpBlock`)

删 `std::min`。`rawTransactions.size() != receipts.size()` → 抛 `OpExecutionInternalError`。
理由:截断会**在仍判 VALID 的同时静默丢弃回执**;数目不等是执行层不变量被破坏(本节点
的 bug),不是对 payload 的判决,故走 -32603 而非 INVALID。随之移除不再需要的
`<algorithm>`。

## F5 — M5:锁注释冲突交叉引用(`EngineServiceImpl.h:302-304` 处)

既有"lock is released before any co_await"注释后补一段 scope note:该规则属
`updateForkchoice` 本身;OP `newPayload` 分支**确实**持锁跨 `co_await`,那是 §4.4
协程契约安全前提 2 的**要求**,§4.4 的失效判据同时覆盖该用法与 `handleNewPayload`
既有 TODO。"此注释陈述默认,§4.4 陈述经审计的例外",二者不矛盾。

## F6 — I3(记账不修):MLS 层无界增长

本分支永不 `mergeBackStorage()`,每个 VALID 块留一层不可变层 → 层数无界 + 读放大
O(接受块数)。已在 `pushView` 处注释点明:何时 merge(及与重组窗口的相互作用)属编排
接入职责,与 `SYS_CURRENT_STATE` 推进同族,**T7 回填 spec §6.4 欠账台账**。最小闭环块量
下是伸缩问题非正确性问题。

## F7 — I4(记账,转 T6 硬约束):头映射自算同源

`rebuiltHeader`/文件头注释均已标注:本文件所有 blockHash 由被测映射自身算出,故
**系统性错误的 payload→21 字段映射(如 prevRandao 与 parentBeaconBlockRoot 互换)在此
恒过**。唯一外部锚点是 **T6 gate 必须用 op-geth 金值 blockHash 喂 `newPayload` 并断言
VALID**——已写成 T6 的硬约束而非可选项。

## F8 — M8:删冗余 `EXPECT_NE`

测试 (b) 中 `EXPECT_NE(status, InvalidBlockHash)` 被上一行 `EXPECT_EQ(status, Invalid)`
蕴含,删除,语义保留在注释里。

## 未修(控制器裁定记账):M3/M4/M6/M7、I3 代码层、I4 代码层。

---

# 附:统一编译验证轮(**已编译验证**,首次真实构建)

`cmake --build build --target bcos-evm-opstack-tests -j8` → **通过并链接**;附带
`--target engine`(生产库)与 `--target test-bcos-engine`(engine 既有套件)亦通过。
**测试未运行**(按协调者要求,ctest 统一安排)。

三个编译错误、三个不同根因,均在 engine 三测试文件的构建中暴露:

## B1(**生产逻辑缺陷**,3 个 TU 同时报)— `registerOpBlock` 签名里的 OP 依赖名

```
EngineServiceImpl.h:861: error: no type named 'ExecuteResult' in
                         'bcos::scheduler_v1::SchedulerSerialImpl'
  note: in instantiation of template class EngineServiceImpl<..., SchedulerSerialImpl>
        requested here  ← static_assert(!GenericEngineService::c_opMode)
```

**根因不是 stub 缺依赖名**(协调者转述的假设),而是我的生产代码:成员函数的**声明**
随类模板一起实例化,只有**函数体**才惰性实例化。`registerOpBlock` 的形参写了
`const typename SchedulerType::ExecuteResult&`,于是**每一个**实例化(含通用组合根
`SchedulerSerialImpl`)都被要求提供该关联名 → 硬错误,`if constexpr` 救不了
(丢弃语句规则管的是**体**,不是**签名**)。三个测试文件全部因各自的
`static_assert(!GenericEngineService::c_opMode)` 触发通用根实例化而报同一处。

**修复(按规则 3,理由先行)**:`registerOpBlock` 改为成员函数模板,结果类型作推导形参
`template <class OpExecuteResult>`。理由:①这是**编译期必须**的修复,通用组合根否则无法
实例化——不是可选优化;②纯签名泛化,调用点 `registerOpBlock(view, payload, ethHeader,
*executeResult)` 靠推导,函数体一字未改,OP 语义零变化;③把"需要 OP 关联名"的位置从
类实例化点移到调用点,而调用点在 `if constexpr (c_opMode)` 内,只在 OP 模式实例化——
与本分支既有的分派纪律一致。`handleOpNewPayload` 无需改:它的 OP 依赖名(`BlockEnv`/
`ExecuteResult`/`ConsensusError`/`StorageError`)全在**函数体**内,本就正确。

> 该缺陷是"未编译验证"协议的直接代价:两轮人工审查(含我自己的静态走查)都没抓到
> "签名随类实例化"这条规则,只有编译器抓得到。记一条方法论账。

## B2(测试笔误)— `Keccak256::hash(std::string)` 名字遮蔽

```
EngineOpBranchTest.cpp:312: error: no viable conversion from 'const std::string'
                            to 'bytesConstRef'
```
`Keccak256` 只 override 了 `hash(bytesConstRef)`,该声明**遮蔽**基类
`crypto::Hash` 的其余 `hash` 重载(含 `std::string const&` 版),经派生类型调用时看不见。
修:`keccak.hash(bcos::bytesConstRef(seed))`(其 `std::string const&` 构造是 explicit,
不会隐式生效),注释写明遮蔽机理。
> 与 T3 在 codec 侧修的"名字遮蔽"是**同族但不同处**的问题,可作为该模式的第二个实例。

## B3(测试笔误,T6 文件)— 非依赖 `requires` 表达式不是 SFINAE

```
EngineNewPayloadGateTest.cpp:366: error: no member named 'executionRequests' in
                                  'bcos::engine::NewPayloadRequest'
```
`static_assert(!requires(NewPayloadRequest r) { r.executionRequests; })` 中,操作数类型是
**具体类型**,成员访问非依赖 → 直接硬错误,而非"约束不满足求值为 false"。修:引入
`template <class Request> concept HasExecutionRequestsCarrier = requires(Request r)
{ r.executionRequests; };` 使成员访问依赖化,再
`static_assert(!HasExecutionRequestsCarrier<NewPayloadRequest>, ...)`。断言语义与告警文本
完全保留(将来一旦加了载体,断言照样触发)。

## 未触发/无需改

`EngineVersionGateTest.cpp` **零改动**——它的报错纯属 B1 的下游表现。
未发现属于 EthBlockHeader/OpDepositEncode/golden 的错误,**无转派项**。
