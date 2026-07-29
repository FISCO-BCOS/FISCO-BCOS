# Task 6 报告：金向量 gate + 两块链式 + 变异矩阵(13 类 18 例)

**状态**:代码与测试写就并提交,**未编译验证**(用户指令:开发期跳过 FISCO 编译/ctest;
统一编译验证由控制器在本任务之后安排)。

**产物**
- 新增 `bcos-evm/test/opstack/EngineNewPayloadGateTest.cpp`(1310 行,23 个 TEST)
- 修改 `bcos-evm/test/CMakeLists.txt`(三个 engine 测试源 + `EngineServiceImpl.cpp` 编入 +
  include 路径 + `ledger` 链接;全部在 `if(TARGET bcos-framework)` 门控块内)
- 修改 `EngineOpBranchTest.cpp` / `EngineVersionGateTest.cpp` 文件头 CMake 注释(从"尚未接线"
  改为"已由 Task 6 接线",并把二选一护栏指向 CMake 中的护栏注释)
- `vectors/` 与 `golden/` **零触碰**:`git status --porcelain -- .../t8n/vectors .../t8n/golden`
  为空(提交前实测);`ports/` `transaction-scheduler/` `bcos-rpc/` 未触碰

---

## 1. T5b 硬约束的落地(本任务存在的理由)

`EngineOpBranchTest.cpp` 里每一个 blockHash 都由被测映射自身
(`detail::rebuildOpEthHeader`)算出,所以 payload→21 字段的**系统性**错映射
(如 prevRandao 与 parentBeaconBlockRoot 互换)两侧同步移动、恒过。本 gate 的三条硬规则:

| 规则 | 落地位置 |
|---|---|
| `payload.blockHash` 一律取 `golden.blockHash`(op-geth `block.Hash()`),**绝不自算** | `makeGoldenRequest()` 单一赋值点;`runGoldenVector()` 步骤 (3) 另有 `ASSERT_EQ(payload.blockHash, goldenBlockHash)` 反悔守卫——防止未来重构把它悄悄换成自算而让整个 gate 退化为同义反复 |
| `seal.txRoot` 断言取 `golden.transactionsRoot` | `runGoldenVector()` 步骤 (1):`EXPECT_EQ(computeTxRoot(rawTransactions), golden.transactionsRoot)`;VALID 本身又蕴含 `result.txRoot == transactionsRoot`(EngineServiceImpl.h:802-808 的第六项比对),两条合起来得 `result.txRoot == golden.transactionsRoot` |
| `encode() == golden.encodedHeaderHex` 逐字节断言**先于** hash 断言(裁定 C3) | `runGoldenVector()` 步骤 (2);失配时 `recordHeaderFields()` 把 21 个重建字段逐个 `RecordProperty` 出来(外加 actual/golden 两条完整 RLP hex),做字段级定位 |

显式划界的例外共**两处**(审查 M1 更正:初版报告说"唯一"不实),都在变异矩阵内,且都不可
避免——op-geth 对一个从未存在过的块没有金值:#8.1–#8.5 改头字段后经 `resealBlockHash()`
重算,#7 把 `requestsHash` 位移一位后重算 hash。真正要守的不变式不是"只有一处",而是:
**33 条向量与链式对不经过任何自算点**,它们的判定一律对着 op-geth 自己的 `block.Hash()`。
文件头与两处函数注释都按此措辞写明。

## 2. gate 结构

```
EngineNewPayloadGate          (5 例)
├── IsthmusSingleTransfer                       isthmus_transfer_basic
├── JovianMultiTransfer                         jovian_transfer_multi
├── IsthmusDepositOnly                          isthmus_deposit_only
├── AllThirtyThreeGoldenVectors                 manifest.txt 全量(断言 ids.size()==33)
└── ChainedPairParentKnownThroughBlockRegistration
EngineNewPayloadMutation      (18 例,见 §4)
```

### 每向量的执行链(`runGoldenVector`)

1. `loadVectorSample(id)`:`vectors/<id>.json`(env 8 字段 + `_op_expected.header` 7 字段)+
   `golden/engine/<id>.golden.json`(blockHash / transactionsRoot / extraData / excessBlobGas /
   rawTransactions / encodedHeaderHex);`_info.hardfork` 仅精确 isthmus|jovian(无默认档,
   同 `T8nReplayHarness.h:423-437` 纪律)。
2. `seedPreState`:`pre` → `evmone::test::from_json<TestState>` → 生产桥
   `Storage2Ledger<ViewType>` + 生产播种 `seedFromTestState`(与 `EbT8nReplayTest.cpp` 的
   `Storage2Backend::fromPre` 同一对),`fork()→newMutable()→pushView` 落成一层不可变层,
   engine 之后 `fork()` 出来的 view 可见。
3. `registerVerifiedBlock(parentHash, 0)`:33 条孤立向量的 parent 由 fixture 预登记
   (spec §7.2),编码逐字节抄 `BaselineScheduler.h:207-220`。
4. 包装 payload(21 字段映射见 §3)→ `newPayload(request, 4)`。
5. 断言:`VALID` / `latestValidHash == golden.blockHash` / `validationError` 为空 /
   `s_hash_2_number` 已登记 / `s_eth_block_header[number]` 的值**逐字节等于**
   `golden.encodedHeaderHex`(登记表与判定自洽,且登记的就是 op-geth 的那份 RLP)。
6. `RecordProperty` 逐向量留痕,key 一律经 `recordKey(id, suffix)` 前缀 `v_<id>_`
   (审查 I3:gtest 按 key 覆盖不追加,33 全量跑在一个 TEST 内,固定 key 会只剩最后一条):
   `v_<id>_status` / `_block_hash` / `_tx_root` / `_tx_count`,失配时另加 21 个
   `v_<id>_hdr_NN_<field>` 与 `_hdr_encoded_{actual,golden}`。

### fork 阈值注入(必要的 fixture 配置,不是改金值)

语料里 **33 条向量的 `env.currentTimestamp` 全是 `0x3f2`**,Isthmus/Jovian 由 `_info.hardfork`
区分而非时钟;而 `OpSchedulerImpl` 按注入阈值从时间戳解析 fork(design §4.2 D2)。故
`forkTimestampsFor(jovian)` 逐向量给出让这一个时间戳落进该向量声明 fork 的阈值对:

- isthmus: `{isthmusTime=0, jovianTime=UINT64_MAX}` → `configAt` 落 Isthmus,
  `isJovianActiveAt` 假(§5.1 的 `blobGasUsed==0` 规则生效);
- jovian: `{isthmusTime=0, jovianTime=0}` → Jovian,`isJovianActiveAt` 真。

两种情况 `isIsthmusActiveAt` 均为真,满足 V4 版本闸(§6.1 step 1)。

### 两块链式(spec §7.2,裁定 A2)

消费 `golden/engine/chained/` 的专用向量对,顺序刻意做成"同一份 payload B 前后各投一次,
两次之间只发生了 A 被接受"这一件事:

| 步 | 动作 | 断言 |
|---|---|---|
| 0 | 断言 `chainB.env.parentHash == chainA.golden.blockHash` | 真实 `InsertChain` 血缘,不是手工拼接 |
| 1 | 播种 **chainA.pre**,只预登记 **A 的 parent**(B 的 parent 不预登记) | — |
| 2 | `newPayload(B)` | **SYNCING** + 未入库(spec §7.2"跳过 FCU 直接投未知 parent 的块") |
| 3 | `newPayload(A)` | VALID + `latestValidHash==A` + 已登记 |
| 4 | `updateForkchoice(head=safe=finalized=A, attrs=nullptr, 3)` | VALID + `latestValidHash==A` |
| 5 | `newPayload(B)`(同一 request 对象) | **VALID** ← parent-known 由块登记自然满足 |
| 6 | `s_eth_block_header` 的 "1" 与 "2" 两键都在 | 链索引真长了一块,B 没覆盖 A |

B **不重新播种**:B 的 pre 就是 A 的 post(README 已核对 `chainB.pre.json` 与
`chainA.post.json` 键集相等),所以第 5 步过 = A 的执行确实把 post 状态留在了 engine 读的
那份 storage 里——闭环的"存储半边",叠在"登记半边"之上。

## 3. 21 字段映射表(payload → 头,金值来源)

| # | EthBlockHeader 字段 | payload 载体 | 值来源 |
|---|---|---|---|
| 1 | parentHash | `parentHash` | vector `env.parentHash` |
| 2 | ommersHash | —(协议常量) | `EngineServiceImpl.cpp` 的 `c_emptyOmmersHash` |
| 3 | feeRecipient | `feeRecipient` | `env.currentCoinbase` |
| 4 | stateRoot | `stateRoot` | `_op_expected.header.stateRoot` |
| 5 | transactionsRoot | —(由 rawTransactions 推导) | `computeOpTxRoot(golden.rawTransactions)`,与 `golden.transactionsRoot` 交叉断言 |
| 6 | receiptsRoot | `receiptsRoot` | `_op_expected.header.receiptsRoot` |
| 7 | logsBloom | `logsBloom` | `_op_expected.header.logsBloom`(512 hex) |
| 8 | difficulty | —(常量 0) | 协议常量 |
| 9 | number | `blockNumber` | `env.currentNumber` |
| 10 | gasLimit | `gasLimit` | `env.currentGasLimit` |
| 11 | gasUsed | `gasUsed` | `_op_expected.header.gasUsed` |
| 12 | timestamp | `timestamp` | `env.currentTimestamp` |
| 13 | extraData | `extraData` | **golden.extraData**(原样发射,9B/17B 两形) |
| 14 | prevRandao | `prevRandao` | `env.currentRandom` |
| 15 | nonce | —(常量 8 零字节) | 协议常量 |
| 16 | baseFeePerGas | `baseFeePerGas` | `env.currentBaseFee` |
| 17 | withdrawalsRoot | `withdrawalsRoot`(OP 扩展) | `_op_expected.header.withdrawalsRoot` |
| 18 | blobGasUsed | `blobGasUsed` | `_op_expected.header.blobGasUsed` |
| 19 | excessBlobGas | `excessBlobGas` | **golden.excessBlobGas**(恒 0x0) |
| 20 | parentBeaconBlockRoot | `request.parentBeaconBlockRoot` | `env.parentBeaconBlockRoot` |
| 21 | requestsHash | —(常量 sha256("")) | 协议常量,并由 seal 的 requestsHash 反查漂移 |

外加 `withdrawals = []`、`expectedBlobVersionedHashes = []`、
`rawTransactions = golden.rawTransactions`(含 0x7E deposit envelope——`vectors/` 里根本没有
这份字节)。

## 4. 变异矩阵 13 类 18 例 —— 逐行落位表

`latestValidHash` 纪律(**修订自审查 I1/I2**,措辞按控制器裁定重写):

- #2–#7 全部在 §6.1 step 2 内被拒,即在 parentKnown **之前**,实现对它们一律返回
  `latestValidHash = null`(`EngineServiceImpl.h:664-687`);#8.1–#8.6 在 parentKnown 之后被拒,
  故断言 `== parentHash`。每例二者必居其一,不留空。
- **来源诚实交代**:spec §7.3 的注按字面读("非 blockHash 桶的 INVALID 用例同时断言
  `latestValidHash==parentHash`")会要求 #3–#6 断言 parentHash。本文件与初版报告里
  "桶由在哪一步被拒定义"这条论证,来自 **T5b 实现自身的注释**,**不是 spec 原文**——初版报告
  §4 把它当作"符合 spec"引用,构成循环论证(审查 I1),此处更正。
- **裁定(I2,控制器)**:采纳**实现口径**,**修 spec 而非改实现**。理由:静态校验阶段根本
  还没查过 parent,声称"最近有效祖先 = parent"是未经验证的断言;Engine API 允许 null;
  工程上更诚实。本轮**不改实现、不改断言**,只把测试文件与报告的措辞从"符合 spec"改成
  "实现口径 + spec 字面未达 + 已裁定"。spec §7.3 注与 §8 第三条由 **T7** 同步修订为
  "非 blockHash 桶**且已过 parentKnown**的 INVALID 才断言 parentHash"。
  在该修订落地前,这几例对实现正确、对 spec 现行字面**已知偏离**——记录在案,不掩盖。

| # | spec §7.3 行 | TEST 名(`EngineNewPayloadMutation.*`) | 断言 |
|---|---|---|---|
| 1 | 时间戳×版本闸 | `TimestampVersionGateRejectsMismatch` | Isthmus payload 走 V3 → `UnsupportedFork`(-38005);反向(节点 isthmusTime 高于该 payload 时间戳 → V4)同例内做控制断言 |
| 2 | blockHash 重组 | `TamperedBlockHashIsInvalidWithNullLatestValidHash` | 金值 blockHash 翻一个 bit → INVALID + **null** + error 含 "blockHash" + 未入库(且是 `Invalid` 而非废弃的 `InvalidBlockHash`) |
| 3 | withdrawals 非空 | `NonEmptyWithdrawalsIsInvalid` | INVALID + null + 点名 "withdrawals" |
| 4 | expectedBlobVersionedHashes 非空 | `NonEmptyExpectedBlobVersionedHashesIsInvalid` | INVALID + null + 点名 |
| 5 | excessBlobGas ≠0 | `NonZeroExcessBlobGasIsInvalid` | INVALID + null + 点名 |
| 6 | blobGasUsed ≠0(Isthmus) | `NonZeroBlobGasUsedIsInvalidUnderIsthmus` | INVALID + null + 点名;Jovian 半边由 gate 里 17 条 jovian 向量(携非零 blobGasUsed 而 VALID)正面覆盖 |
| 7 | executionRequests 非空 | `NonEmptyExecutionRequestsLandsInBlockHashBucket` | 见 §5 偏离 ②:静态断言无载体 + requestsHash 位移代理 → INVALID + null + 点名 "blockHash"(spec 指定的桶) |
| 8.1 | 六项比对面 receiptsRoot | `ComparisonSurfaceReceiptsRoot` | INVALID + **parentHash** + 点名 + 未入库 |
| 8.2 | logsBloom | `ComparisonSurfaceLogsBloom` | 同上 |
| 8.3 | withdrawalsRoot | `ComparisonSurfaceWithdrawalsRoot` | 同上 |
| 8.4 | stateRoot | `ComparisonSurfaceStateRoot` | 同上 |
| 8.5 | gasUsed | `ComparisonSurfaceGasUsed` | 同上 |
| 8.6 | transactionsRoot | `ComparisonSurfaceTransactionsRoot` | 见 §5 偏离 ①:`TxRootDriftScheduler` 装饰器(真执行,只位移返回的 txRoot)→ INVALID + parentHash + 点名 |
| 9 | parent 未知 | `UnknownParentIsSyncing` | SYNCING + latestValidHash 空 + validationError 空 + 块与 parent 均未入库 |
| 10 | 同 payload 重发(裁定 C1) | `SamePayloadResubmittedAfterParentBecomesKnown` | SYNCING → 补登记 parent → **同一 request 对象**重发 → VALID + 已入库 |
| 11 | attributes 拒绝 | `ForkchoiceWithAttributesRefusedButHeadStillAdvances` | 先用金向量走 VALID 入库,再 FCU 带 attrs → `UnsupportedOpPayloadAttributes`(-38003);**两点断言**:`getSafeBlockNumber()` 已置为该块号(head 照常推进)+ 随后无 attrs 的 FCU 仍返回 Valid(未回滚) |
| 12 | 通用版本闸零漂移 | `GenericCompositionRootStillRejectsV4` | 通用组合根(`SchedulerSerialImpl` + `MockExecutorSerial`)收 V4 金向量 → `UnsupportedEngineApiVersion`;`getPayload(V4)` 同样;编译期半边是文件顶部的 `static_assert(!GenericEngineService::c_opMode)` |
| 13 | 存储故障 | `StorageLayoutFaultIsInternalErrorNotInvalid` | 见 §5 偏离 ③:向 storage2 注入一条真实的布局违规行 → `OpExecutionInternalError`(-32603),**绝不 INVALID** + 未入库 |

计数:13 类 = #1–#7(7)+ #8 合记 1 类 + #9–#13(5);18 例 = 12 类各 1 例 + #8 展开 6 例。
文件内 `EngineNewPayloadMutation` 恰好 18 个 TEST。

## 5. 偏离台账(三条,均在源码注释中同样写明)

**① #8.6 transactionsRoot 用装饰器而非纯变异。**
engine 的第六项比对是 `commitments.txRoot != transactionsRoot`,两侧都由同一个
`computeOpTxRoot` 对同一份 raw bytes 求得(EngineServiceImpl.h:802-808 自己就写了"结构上恒等,
保留它是为了将来执行侧改从**解析结果**推导 txRoot 那天"),因此真调度器下**不可能**用改
payload 的方式触达。`TxRootDriftScheduler` 是对真 `OpSchedulerImpl` 的薄装饰:真播种、真执行、
真出块,只把返回的 `txRoot` 位移——即精确模拟那一天,不多不少。seam 是纯 duck typing
(`OpSchedulerImpl.h:539-559`),转发已发布的名字即可,`static_assert(DriftEngineService::c_opMode)`
钉住探针可解析。

**② #7 executionRequests 无载体,用 requestsHash 代理。**
`NewPayloadRequest`(bcos-framework/engine/Types.h)没有 `executionRequests` 成员(T5b 已记同一条
偏离),因此"非空"无法直接构造。本例做两件事:(a) 文件顶部
`static_assert(!requires(NewPayloadRequest r){ r.executionRequests; })`——**载体一旦被加进来,
这条断言立刻翻红**,强制把真检查与真用例补上,而不是让约束继续静默地"真空成立";
(b) 用金向量头把 `requestsHash` 位移一 bit 后重算 hash 当作 payload.blockHash 投进去,复现
"一个真的携带非空 requests 的块"在线上的样子——engine 用空 requests 常量重建 → blockHash 失配
→ 正是 spec 指派的桶(INVALID + null)。

**③ #13 用"storage2 布局违规行"而非 ThrowingStorage。**
spec §7.3 写的是 ThrowingStorage。实际注入点选在更下层也更真实的地方:往已播种账户表里写一行
键既不是 `ACCOUNT_TABLE_FIELDS` 已知短字段名、也不是 32 字节槽键的行——
`Storage2Ledger::fetchAllStorage` 对这种布局不变式违规直接 throw(Storage2Ledger.h:467-471),
`visitAccounts` 捕获并置毒旗,`executeOpBlock` 第 5 步的毒旗检查把它翻译成 `OpStorageError`
→ engine 的 `catch (StorageError)` → -32603。
选 **L2ToL1MessagePasser 自己的表**(0x42..16)是刻意的:`executeOpBlock` 的遍历一找到该账户就
`return false` 停止,所以排序更靠后的表里的坏行永远走不到;而放在它**自己**表里,
`fetchAllStorage` 在 visitor 被调用之前就会抛,与迭代顺序无关。
选择理由:engine 的 `GlobalStateStorageType` 是固定的模板参数,要做 ThrowingStorage 得另造一个
满足 MultiLayerStorage 全部后端契约的存储类型——收益相同(被测的仍是
`catch (StorageError) → OpExecutionInternalError` 这一条分类路径),风险与代码量都更大。
本例中块是**先完整执行完**才失败的,这正是"存储故障不是对区块的判决"的语义。
(纯桩版的同一分支另有 `EngineOpBranchTest.cpp` 的 (k) 覆盖。)

## 6. CMake 接线说明

全部改动落在 `bcos-evm/test/CMakeLists.txt` 既有的 `if(TARGET bcos-framework)` 门控块内
(standalone 构建 `bcos-evm/build` 不含 bcos-framework 传递依赖,行为不变)。

1. **三个测试源**:`EngineVersionGateTest.cpp`(T5a)、`EngineOpBranchTest.cpp`(T5b)、
   `EngineNewPayloadGateTest.cpp`(本任务)。前两个是前序任务写就但推给本任务接线的。
2. **`${CMAKE_SOURCE_DIR}/engine/bcos-engine/EngineServiceImpl.cpp` 编入测试源**,附护栏注释:
   > 编入与链 `engine` 库**二选一**(裁定 B2)。`engine` 目标就是由这个 .cpp 构建的
   > (engine/CMakeLists.txt GLOB `bcos-engine/*.cpp`),两者并存 = 重复符号。选"编入"是为了
   > 不把 `engine` 的 PUBLIC 依赖闭包拖进 bcos-evm 测试二进制。
   > **若将来把 `engine` 加进 target_link_libraries,必须在同一次编辑里删掉这条源条目。**
3. **include 路径**(三条,均为原始源码路径而非目标 usage requirement):
   - `${CMAKE_SOURCE_DIR}` — 解析 `#include "engine/bcos-engine/EngineServiceImpl.h"`
     (沿用 `engine/test/CMakeLists.txt:12` 的仓库根上 include 路径惯例);
   - `${CMAKE_SOURCE_DIR}/bcos-ledger` — `EngineServiceImpl.h` include 了
     `<bcos-ledger/LedgerMethods.h>`;
   - `${CMAKE_SOURCE_DIR}/transaction-scheduler` — 通用组合根用例实例化
     `<bcos-transaction-scheduler/SchedulerSerialImpl.h>`(纯头,不需要连边)。

   **为什么写死路径**:根 `CMakeLists.txt` 把 `bcos-ledger`(71 行)与 `transaction-scheduler`
   (末尾)排在 `bcos-evm`(65 行)**之后**,所以处理到 bcos-evm/test 时 `if(TARGET ledger)`
   为假——哪怕是完整 in-tree 构建。这一点在注释里写明了。
4. **`target_link_libraries(... PRIVATE ledger)`**:供 `LedgerMethods.cpp` 的非模板符号
   (OP 分支用的 `getBlockNumber(storage, hash, fromStorage)` 那个重载是头内模板,但
   LedgerMethods.h 的非模板声明不是),与 `engine` 目标自己 PUBLIC 链的是同一个库。
   目标名的解析发生在 generate 阶段,故"目标晚于本调用创建"不影响链接——注释里也写了。

## 7. 未编译验证的替代依据

- **API 先例对照**:fixture / 存储桩 / `registerVerifiedBlock` / `readEntry` / 组合根成员序
  逐行取自 `EngineOpBranchTest.cpp`(T5b 已过同一轮静态走查);金值装载(`loadManifestIds`/
  `asH256`/`asBytes`/`vectors` 顶层解包)逐行取自 `EthBlockHeaderTest.cpp`(T3);播种取自
  `EbT8nReplayTest.cpp` 的 `Storage2Backend::fromPre`;通用组合根 fixture 取自
  `EngineOpBranchTest.cpp` 的 (e) 与 `testSchedulerSerial.cpp:20-46`。
- **语义走查**:所有断言的期望值都对着 `EngineServiceImpl.h::handleOpNewPayload` 的分支顺序
  (step 1 → 2 → 3 → 号连续性 → 4 → 5 六项 → 6 登记)与
  `EngineServiceImpl.cpp::validateOpNewPayloadRequest` 的逐条返回串核过一遍;
  `validationError` 的点名子串取自这两处的字面量。
- **`blk.extra_data` / `blk.blob_gas_used` 惰性核实**:两者在 `bcos-evm/bcos-evm/opstack/`
  下**无任何消费点**(grep 为空),所以 gate 把它们按 payload 真值填入既不改变执行/seal 结果
  (与 `T8nReplayHarness.h` 不填的既有腿一致),也不会让 Jovian DA footprint 比对变成同义反复。
- **金值可达性**:33 条向量的执行结果已由既有三腿回放(`OpT8nReplayTest` / `MemoryLedger` 腿 /
  `EbT8nReplayTest`)对着同一份 `_op_expected` 校验过,本 gate 复用的是同一条执行路径
  (`processOpBlock` + `Storage2Ledger`),新增的只是 engine 侧包装与头/哈希断言。
- **DIVERGENCES 纪律**:本任务不跑测试,故"翻红三选一"归属统一验证阶段;真跑之后若有向量
  翻红,按 `DIVERGENCES.md` 的 ALLOWLIST 四元组归因,**禁止改向量或金值**。

## 8. 提交

```
5dba8f393  test(bcos-evm): 验证者金向量 gate 33/33 + 两块链式闭环(专用向量对)+ 13 类 18 例变异矩阵
           4 files changed, 1374 insertions(+), 18 deletions(-)
```

提交前 pre-commit 钩子跑了 `clang-format -style=file:.clang-format`,首次提交因格式被拒,
按钩子改写后的结果重新暂存提交(仅换行/续行,无语义改动);`engine/bcos-engine/*` 被钩子
一并检查但**无改动**(本任务未触碰 engine 源码)。

改动文件:
- `bcos-evm/test/opstack/EngineNewPayloadGateTest.cpp`(新增)
- `bcos-evm/test/CMakeLists.txt`
- `bcos-evm/test/opstack/EngineOpBranchTest.cpp`(仅文件头注释)
- `bcos-evm/test/opstack/EngineVersionGateTest.cpp`(仅文件头注释)

## 8b. 审查整改(T6 review,Approved 有条件)

> **交叉引用勘误(2026-07-29,T7 记、终审批 3 归档时补记)**:spec §7.3 rev.3.1 曾把 I1/I2 的
> 来龙去脉指向"`task-6-report.md` §5"——**错**。本文件的 §5 是与之无关的三条结构性偏离台账;
> I1/I2 的记录在 **§4 前言**与**本节(§8b)的 I1/I2 两行**。spec 侧指针已在 T7 改正,
> `EngineNewPayloadGateTest.cpp` 的对应注释同步改正。本文件自 2026-07-29 起入库
> (`git add -f`,见 spec §8.1 归档标准),此前 spec 对它的引用是悬空的。

审查结论:Spec ✅(有条件)、Approved;金值判别力经审查者独立实测加强确认(33 向量内 8 个
h256 头字段两两互异 → 错映射必被抓);三条结构性偏离(§5 ①②③)全数裁定为合理替代;
链式因果与 CMake 假设均获实证。整改如下:

| 项 | 裁定 | 本轮动作 |
|---|---|---|
| **I2** latestValidHash 口径 | spec owner 裁定:**采纳实现口径,修 spec 而非改实现** | 不动实现与断言;测试文件 §"latestValidHash discipline" 注释块与本报告 §4 前言重写为"实现选择 + spec 字面未达 + 已裁定 + T7 修订 spec" |
| **I1** 循环论证 | 修 | 同上——把"桶由在哪一步被拒定义"明确标注为 **T5b 实现注释**的论证而非 spec 原文,并写明初版报告的引用错误 |
| **I3** RecordProperty 覆盖 | 修 | 新增 `recordKey(id, suffix)`,所有 key 前缀 `v_<id>_`;`runGoldenVector` 的 5 条留痕与 `recordHeaderFields` 的 21 条 + 2 条 RLP 全部改用它。原因写进注释:gtest `RecordProperty` **按 key 覆盖不追加**,而 33 全量跑在**一个** TEST 内,固定 key 只会留下最后一条、静默丢弃另外 32 条 |
| **M1** "唯一自算点"不实 | 修 | 文件头改为"两处自算点(#8.1–#8.5 的 `resealBlockHash`、#7 的 requestsHash 位移),但**33 例与链式对不经过任何自算点**"——后者才是真正要守的不变式 |
| **M2** jovian blobGasUsed 计数不实 | 修 | 实测:17 条 jovian 向量里 `_op_expected.header.blobGasUsed` 非零的**只有 1 条**(`jovian_da_mix` = `0x90ec0`)。#6 注释改为准确表述,并说明其余 16 条两侧都不覆盖 |
| **#8.6 漂移值论证** | 建议采纳 | `TxRootDriftScheduler` 注释补充:txRoot 是 keccak256 输出(空列表也得 `0x56e81f…b421` 空根常量,不是零),故 `h256{}` 可证与真值不等,变异不会静默退化成 no-op |
| **M3/M4/M5** | 记账不修 | FCU 腿归属、"错误码"实为异常类型而非线上 JSON-RPC 码、`${CMAKE_SOURCE_DIR}` 头文件遮蔽风险——留给 T7 的打钩措辞与统一编译验证阶段 |

整改提交独立于主提交(见 §8),仅注释与 RecordProperty key,**无断言语义改动**;仍未编译验证。

## 9. 遗留 / 交给 Task 7

- 统一编译验证(`cmake` + `ctest -R 'BcosEvmOpstackTests'`,以及
  `--gtest_filter='EngineNewPayloadGate.*:EngineNewPayloadMutation.*'`)。
- spec §7.4 五探针留痕(其中探针 ④"块登记接线探针"的宿主用例即本文件的
  `ChainedPairParentKnownThroughBlockRegistration`——注入跳过块登记写入后,第二块必转 SYNCING;
  探针 ⑤ 的宿主即 `GenericCompositionRootStillRejectsV4`)。
- §8 验收清单逐条打钩与 N0 相对基线对比。
