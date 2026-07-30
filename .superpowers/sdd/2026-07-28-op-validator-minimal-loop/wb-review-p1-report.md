# 视角 1 报告 · 正确性:Engine API 共识语义与状态机

**审查者**:视角 1(共识语义与状态机)
**范围**:`engine/bcos-engine/EngineServiceImpl.{h,cpp}`、`bcos-framework/bcos-framework/engine/Types.h`
**对照物**:op-geth `e8800cffe`(`eth/catalyst/api.go`、`eth/catalyst/api_optimism.go`、
`beacon/engine/types.go`、`consensus/beacon/consensus.go`、`consensus/misc/eip1559/*`、
`consensus/misc/eip4844/eip4844.go`)
**纪律**:全程只读。未编译、未跑测试、未修改任何文件。需要构建证实的条目标 `[需验证]` 并附最小验证步骤。

---

## 0. 一句话结论

**本实现会对 op-geth 会接受的块投反对票(C2、C3),也会对 op-geth 会拒绝的块投赞成票(C1)。**
六项比对面本身是干净的;漏洞全部落在**比对面之外的头字段父子一致性**、**fork×版本闸的
pre-Isthmus 分支**、以及**"本地故障 → ConsensusError → INVALID" 的跨层错分类**上。
另有一条**新发现的 FCU 规范不符**(safe/finalized 零哈希)会让闭环在符合 Engine API 规范的
CL 下永久停在 SYNCING。

---

## 1. 逐项回答视角问题

### Q1 状态桶完整性与 latestValidHash

| 桶 | OP 分支是否覆盖 | latestValidHash 取值 | 与 op-geth 一致? |
|---|---|---|---|
| VALID(已知块短路) | ✅ `EngineServiceImpl.h:915-920` | `payload.blockHash` | ✅ `api.go:872-876` |
| VALID(执行通过) | ✅ `:1118` | `payload.blockHash` | ✅ `api.go:934` |
| INVALID(step 2 静态/blockHash) | ✅ `:761-778` | `null` | ✅ `api.invalid(err, nil)` |
| INVALID(step 3 号连续性/3a 时间戳/step 4 ConsensusError/step 5 比对) | ✅ `:816-820, :886-890, :994-999, :1094-1098` | `parentHash` | ✅ `api.invalid(err, parent.Header())` |
| SYNCING(parent 未知) | ✅ `:799-802` | `null` | ✅ `delayPayloadImport` |
| **ACCEPTED** | ❌ 从不返回 | — | op-geth 在"父块头已知但状态不可用"时返回 ACCEPTED(`api.go:900-904`)。本实现的 `SYS_HASH_2_NUMBER` 只在 VALID 分支与状态同层写入,该状态在真账本上不可达 → **无害缺失**(Minor) |
| **INVALID(无效祖先)** | ❌ 从不返回 | — | **有害缺失**,见 I5(台账 k) |

`latestValidHash` 的取值逐桶与 op-geth 一致,**这一项没有发现问题**。step 2 恒 null / 过了
parentKnown 恒 parentHash 的二分法与 op-geth 的 `api.invalid(err, nil)` vs
`api.invalid(err, parent.Header())` 完全对齐。

### Q2 前置校验顺序对照

| # | 本实现 | op-geth 对应 | 差异 |
|---|---|---|---|
| 0 | 版本闸 `isVersionSupported` `:536` | `NewPayloadV3/V4` 的参数 switch | 等价 |
| 1 | -38005 timestamp×version `:692-702` | `checkFork(...)` `api.go:757/781` | **不等价 → C2** |
| 2 | 静态校验 + blockHash `:761-778` | `checkOptimismPayload` + `ExecutableDataToBlock` `api.go:812-816/829` | 比对面见 Q3 |
| 3 | parentKnown(storage)`:797-802` | `GetBlock(parentHash)` `api.go:886-890` | 等价 |
| 3′ | blockNumber == parent+1 `:816-820` | `consensus.go:268-270` | 等价 |
| 3a | timestamp > parent `:868-891` | `api.go:891-894` / `consensus.go:253-256` | 等价(父头缺失时跳过,见 M3) |
| 3b | 已知块 → VALID `:915-920` | `api.go:872-876` | **顺序差**:op-geth 把已知块短路放在 parentKnown **之前**。两者在本实现的不变式下可观测等价(已登记块的 parent 必然已登记),注释 `:851-859` 已论证。**接受** |
| — | (缺)invalid-ancestor 缓存 | `api.go:878-880` | **I5** |
| 3c | 非链尾 parent → -32603 `:955-964` | 无对应(op-geth 直接执行侧链) | **I6**(台账 l) |
| 4 | 执行 | `InsertBlockWithoutSetHead` | — |
| 5 | 六项比对 + 2 项 | `ValidateState`/`verifyHeader` | 见 Q3 |
| 6 | 登记 + pushView | 同上 | 原子性见 Q7 |

**顺序本身没有制造语义错位**(唯一的顺序差 3b 已被论证为可观测等价)。问题全部出在
**比对面缺项**与**闸的判定式**上,不在顺序上。

### Q3 比对面清点:payload 里有、比对面里没有的字段

逐字段核对(✅ = 与 op-geth 有等价约束;❌ = 缺口):

| 字段 | 静态检查 | 进重组头(⇒ 被 blockHash 钉住) | 语义校验 | op-geth 对应 |
|---|---|---|---|---|
| parentHash | — | ✅ | parentKnown + 链尾 | ✅ |
| feeRecipient / prevRandao | — | ✅ | 无(自由输入) | ✅ 同 |
| stateRoot / receiptsRoot / logsBloom / gasUsed | — | ✅ | ✅ 六项比对 | ✅ |
| withdrawalsRoot | 必需 | ✅ | ✅ 六项比对 | ✅ |
| transactionsRoot(派生) | — | ✅ | ✅ 六项比对 | ✅ |
| blockNumber | ≥0 | ✅ | ✅ parent+1 | ✅ |
| timestamp | — | ✅ | ✅ > parent | ✅ |
| gasLimit | ≤2^63-1 `cpp:269-273` | ✅ | 无变化率约束 | ✅ **OP 允许瞬时调整**(`eip1559.go:37-42` `if !config.IsOptimism()`),故**此处无缺口**——先前若干处把"gasLimit 变化率"列为欠账,对 OP 而言是**误列** |
| **gasUsed vs gasLimit** | ❌ | — | ❌ | ❌ **缺**:`consensus.go:266-268` |
| **baseFeePerGas** | ❌ 无任何检查 | ✅ | ❌ | ❌ **缺**:`eip1559.go:53-56` → **C1** |
| **extraData** | 仅 ≤32 字节 | ✅ 原样 | ❌ 无 OP 形状校验 | ❌ 缺:`ValidateOptimismExtraData`(台账已记) |
| excessBlobGas | 必须在场且 =0 | ✅ 钉 0 | — | ✅ **正确**:`eip4844.go` `CalcExcessBlobGas` 对 OP 短路恒返回 0 |
| blobGasUsed | 必需;pre-Jovian 必须 =0 | ✅ | Jovian 起由 seal 比对 | ✅(见 M4) |
| expectedBlobVersionedHashes | 必须为空 | — | — | ✅ 自洽:解码器只认 0x7E/0x02/0x04,blob tx(0x03)进不来 |
| requestsHash | — | ✅ 钉常量 | ✅ 与 seal 比对 | ✅ |
| `transactions`(通用载体) | ❌ 不检查 | — | — | M1 |

**结论:比对面上只有三个真缺口 —— `baseFeePerGas`(C1)、`gasUsed ≤ gasLimit`(I3)、
`extraData` OP 形状(已记账)。其余字段与 op-geth 有等价约束。**

### Q4 错误分类纪律

engine 层的分类链是**完整且正确**的:分类屏障(`:724-744`)+ 执行段三 catch(`:994-1042`)
+ 登记期不变式(`:1213-1227`)覆盖了每一条出口,`OpExecutionInternalError` 透传、其余
`catch(...)` 一律 -32603。**engine 层本身我没有找到分类错位。**

但分类在**跨层**处被破坏:`OpSchedulerImpl.h:839-873` 的 `catch(const std::exception&)` /
`catch(...)` 在 `bridge.poisoned()` 为假时**一律重抛 `OpConsensusError`**。engine 的
`catch (const typename SchedulerType::ConsensusError&)` 因此把**任何非桥来源的本地故障**
(`std::bad_alloc`、evmone 内部 `std::runtime_error`、TBB 异常……)判成 **INVALID +
latestValidHash=parent**。这正是设计 §4.3 明令禁止的方向 → **C3**。

### Q5 forkchoiceUpdated

- **safe/finalized 零哈希**:`:264-281` 三个哈希无条件查 `SYS_HASH_2_NUMBER`,任一
  `nullopt` 即 SYNCING。Engine API 规范把 `0x00…00` 定义为"尚无 safe/finalized"的**合法
  编码**,op-geth 显式跳过(`api.go:342`、`api.go:356`)→ **I1(新发现,不在台账)**。
- **head 零哈希**:本实现答 SYNCING,op-geth 答 INVALID(`api.go:245-248`)。同属 I1。
- **head 跳跃 > 1**:`:337-342` 抛 `InvalidForkchoiceState`(**异常**,不是 payloadStatus)。
  op-geth 对未知 head 答 SYNCING、对已知非规范 head 答 VALID/INVALID,**从不抛协议外错误**
  → I2。
- **head 回退**:`:319-327` 静默答 VALID 且不更新跟踪态。设计 §1 已如实描摹并明示沿用;
  但在 OP 验证者语境下这是"对一次真实重组回答 VALID 而实际什么都没做" → I2。
- **内存态 vs 持久态一致性**:OP 分支的 newPayload **完全不读** `m_forkchoiceState`
  (parentKnown 走 storage,`:797`),FCU 也**完全不写**任何登记表(裁定 A4)。两套状态因此
  **不共享判据、不会相互污染**——这一项是干净的。代价是 FCU 在 OP 模式下纯属装饰:它既不
  推进链头,也不影响任何 newPayload 判决。

### Q6 `c_opMode` 探针塌陷

B4-3 护栏(`:561-570`)确实堵住了 V4 橡皮图章。**但它是版本作用域的,而失效模式不是**:
`c_opMode` 塌陷后 **V3** 请求仍然落进通用分支 `:580-659`,而通用分支
(设计 §1 自陈:"对外部 payload 零执行、零语义验证、零落库——外部 payload 登记即 Valid")
会在 `validateExecutionPayload(payload, 3)` 通过、`parentKnown` 由内存 head 满足后,
**返回 VALID + payload 自报 blockHash,全程不执行区块**。OP 组合根传 `maxEngineVersion = 4`,
V3 天然在闸内。→ **I4(新;台账 m 只覆盖 V4)**。

### Q7 `registerOpBlock` 写入原子性

**这一项是干净的,无需修改。** 依据:`MultiLayerStorage::fork()`
(`bcos-framework/bcos-framework/storage2/MultiLayerStorage.h:526-540`)返回的是把不可变层
列表**拷贝**进去的局部 `View`;`view.newMutable()`(`EngineServiceImpl.h:967`)在该局部
View 上开可变层;五张表的写入全部落在这一层;只有最后一句
`m_globalStateStorage.get().pushView(std::move(view))`(`:1117`,`MultiLayerStorage.h:543-551`)
才把它挂到全局层栈上。因此:

- `registerOpBlock` 中途任一 `writeOne` 抛出 → 协程展开、局部 `view` 析构 → **全局存储零改动**,
  连执行产生的状态改动一起丢弃 → 不存在"块号已登记但头没写"的半登记态;
- 异常经分类屏障(`:733-744`)出成 -32603,不是 INVALID,符合 §4.3;
- `pushView` 是函数体最后一条语句,其后无可抛出的步骤。

**"下一个块 parentKnown 通过但读头失败"这一失效场景在当前实现下不可达。**

---

## 2. 发现清单

### Critical

---

#### C1 · `baseFeePerGas` 无任何父子一致性校验 —— 本节点放行 op-geth 会拒的块
**已记账为条目 (e) / 正文"Holocene EIP-1559 baseFee 父子一致性校验"(裁定 A7)。
独立判断:该裁定仍然成立,但优先级被严重低估,应与置顶的 (k)/(l) 并列。**

`file:line`
- `engine/bcos-engine/EngineServiceImpl.cpp:334` —— `.baseFeePerGas = payload.baseFeePerGas`
  (原样进重组头)
- `engine/bcos-engine/EngineServiceImpl.h:978` —— `.baseFeePerGas = payload.baseFeePerGas`
  (原样进 `OpBlockEnv`)
- `bcos-evm/bcos-evm/engine/OpSchedulerImpl.h:238` ——
  `blk.base_fee = narrowU256ToU64(env.baseFeePerGas, ...)`(原样进 `BlockInfo`)
- 缺口位置:`EngineServiceImpl.cpp:185-302` `validateOpNewPayloadRequest` 全函数无
  `baseFeePerGas` 字样;`EngineServiceImpl.h:1044-1098` 六项比对面无该字段。
- op-geth:`consensus/misc/eip1559/eip1559.go:53-56`
  `expectedBaseFee := CalcBaseFee(config, parent, header.Time)` + 不等即拒。

**失效场景(具体输入 → 本实现返回 → op-geth 返回 → 后果)**

1. 前置:块 N 已被本节点接受并登记(`s_eth_block_header[N]` 有头)。
2. 攻击者/故障 sequencer 构造块 N+1,一切字段照常,唯独把 `baseFeePerGas` 从
   `CalcBaseFee(parent)` 应有的值改成 **0**。
3. 用改后的 baseFee **本地重算** stateRoot / receiptsRoot / logsBloom / gasUsed / withdrawalsRoot,
   并按 21 字段重算 `blockHash` 填回 payload(payload 自洽)。
4. 本实现:step 2 通过(baseFee 无检查,blockHash 自洽);step 4 用 `base_fee = 0` 执行;
   step 5 六项**全部命中**(执行侧用的就是同一个 0) → **VALID + latestValidHash = blockHash**,
   并**写库**(五张表 + pushView)。
5. op-geth:`VerifyEIP1559Header` → `invalid baseFee: have 0, want …` → **INVALID +
   latestValidHash = parent**。
6. 后果:本节点接受了一条**永久性状态分叉**——所有 EIP-1559 交易的 basefee 销毁额、
   `L2 fee vault` 余额、发送者余额全部与规范链不同,且此后每个后继块都以这条错误状态为父。
   这是"验证者"这个角色**存在的理由**所对应的攻击面:sequencer 少收/多收 basefee 正是
   验证者应当抓住的第一类作弊。

**为什么说优先级被低估**:台账把它与"extraData 形状校验"并列成一条普通欠账。但二者不是
同一量级——extraData 形状错只是格式不合规,baseFee 错**直接改变每一笔交易的余额结算**。
且当前**没有任何测试覆盖它**:`EngineNewPayloadGateTest.cpp:534` 的 baseFee 直接取自向量
`env.currentBaseFee`,§7.3 的 13 类 18 例变异矩阵**没有 baseFee 变异项**(已 grep 确认)。
"33/33 全绿"在这一项上是完全无信息的。

**最小修法(供协调者判断,本次不实施)**:step 3a 已经把父头解出来了
(`EngineServiceImpl.h:875-885` 的 `parentHeader`),`CalcBaseFee` 需要的
`parent.gasUsed / gasLimit / baseFeePerGas / extraData / timestamp` **全部就位**。
这条欠账的"缺前置条件"理由自 B4-1 打通读路之后已经不成立了。

---

#### C2 · -38005 闸放行 pre-Isthmus payload,随后必然判 INVALID —— 对 op-geth 会接受的块投反对票
**新发现,不在 §6.4 台账内。**

`file:line`
- `engine/bcos-engine/EngineServiceImpl.h:692-702` ——
  `if (isthmusActive != (version == 4)) throw UnsupportedFork;`
- `engine/bcos-engine/EngineServiceImpl.cpp:219-225` ——
  `if (!payload.withdrawalsRoot.has_value()) return "withdrawalsRoot is required on the OP path (Isthmus+)";`
- `bcos-evm/bcos-evm/opstack/OpForkSchedule.h:51-60` —— 本闭环 **Isthmus+ only**,
  `configAt` 把 sub-`isthmusTime` 时间戳也解析成 Isthmus 配置。

**失效场景**

前置:OP 组合根注入 `OpForkTimestamps{ isthmusTime = T, ... }`,`T > 0`(任何真实的
Isthmus 升级链都是这样——Isthmus 有一个具体的激活时间戳)。

1. CL 投递一个 **Holocene 块**(`timestamp < T`),按规范用 `engine_newPayloadV3`,
   `withdrawalsRoot = nil`(op-geth `api_optimism.go:31-33` 明确要求 pre-Isthmus
   **必须**为 nil)。
2. 本实现:`isthmusActive = false`,`version == 4` 为 false,二者相等 → **闸放行**。
3. 进入 `runOpNewPayloadSteps` → `validateOpNewPayloadRequest` → `withdrawalsRoot` 缺失
   → **`INVALID + latestValidHash = null` + validationError "withdrawalsRoot is required"**。
4. op-geth:`checkOptimismPayload` 通过(pre-Isthmus 且 `WithdrawalsRoot == nil`)→
   正常执行 → **VALID**。
5. 后果:本节点对一条**完全合法的 Holocene 块**投 INVALID。op-node 收到 INVALID 会**立刻
   放弃该分支**并把它标记为坏块——这是本清单里唯一一条"让 CL 主动丢弃合法链"的路径,
   比 SYNCING 类停滞更难恢复。

同一逻辑也命中 V1/V2:`isthmusActive = false` 且 `version != 4` → 闸放行 → 后续必 INVALID,
而 op-geth 答的是 `unsupportedForkErr`(方法误用,不是块的判决)。

**根因**:闸写成了"fork 与版本必须**一致**",而闭环的真实能力是"**只支持 Isthmus+ 且只支持
V4**"。正确的闸应是 `if (!isthmusActive || version != 4) throw UnsupportedFork;`——
把 pre-Isthmus 归入"本实现不支持这个 fork"(-38005 的原义),而不是放进来再判成坏块。
设计 §6.1 step 1 的原文("Isthmus+ 禁 V3,pre-Isthmus 禁 V4")本身就写漏了这一半,
实现忠实地实现了这个漏。

**[需验证]** —— 现有测试全部用 `kIsthmusTimestamp`(≥ isthmusTime),该分支零覆盖。
最小验证步骤:在 `bcos-evm/test/opstack/EngineOpBranchTest.cpp` 的 `OpFixture` 里把
`OpForkTimestamps::isthmusTime` 设为一个大于向量时间戳的值,用 `version = 3` 投一个
`withdrawalsRoot = nullopt` 的 payload,跑 target `test-bcos-evm`;
**期待现象**:返回 `PayloadValidationStatus::Invalid` +
`validationError == "withdrawalsRoot is required on the OP path (Isthmus+)"`,
而非抛 `UnsupportedFork`。

---

#### C3 · 本地故障(`bad_alloc` 等)被 `OpSchedulerImpl` 重分类为 `ConsensusError` → engine 判 INVALID
**新发现。台账 (j) 只记了"`catch(...)` 丢消息、四类块级拒绝共用一条 validationError",
**没有**记"`catch(...)` 同时把非块级故障吸进了共识判决"。这是两个不同的问题,后者严重得多。**

`file:line`
- `bcos-evm/bcos-evm/engine/OpSchedulerImpl.h:839-844` ——
  `catch (const std::exception& e) { if (bridge.poisoned()) throw OpStorageError(...); throw OpConsensusError(e.what()); }`
- `bcos-evm/bcos-evm/engine/OpSchedulerImpl.h:845-873` —— `catch (...)` 同样的二分。
- `engine/bcos-engine/EngineServiceImpl.h:994-999` ——
  `catch (const typename SchedulerType::ConsensusError& e) { co_return makeStatus(Invalid, latestValidHash, ...); }`
- 设计自陈的不变式:`EngineServiceImpl.h:1000-1007`
  "A storage fault is NOT a verdict on the block: -32603, never INVALID."

**失效场景**

1. 节点在执行块 N 时**内存耗尽**(或 evmone 内部抛出任何非桥来源的 `std::runtime_error`,
   或 TBB 抛出并发异常)。
2. `processOpBlock` 内部抛 `std::bad_alloc`;`Storage2Ledger` 的毒旗**未置位**
   (故障不来自存储读写)。
3. `OpSchedulerImpl` 的 `catch` → `bridge.poisoned()` 为假 → 抛 **`OpConsensusError`**。
4. engine 的 typed handler 命中 → **`INVALID + latestValidHash = parentHash` +
   validationError "OP block execution rejected the payload: std::bad_alloc"**。
5. op-geth 同样场景:`InsertBlockWithoutSetHead` 返回错误,`api.invalid(err, parent)` —— 
   **op-geth 在这一点上其实也把插入失败判成 INVALID**,所以严格说这不是与 op-geth 的
   分歧;**但它是与本设计 §4.3 自陈不变式的分歧**,而那条不变式正是本分支花了三轮复审
   (B2-1、I-1、批 6)去建立的。engine 层为此加了两道 `catch(...)` → -32603 的防线,
   而这两道防线**对这条路径全是死代码**——`OpSchedulerImpl` 在更内层先把它改写成了
   `ConsensusError`。
6. 后果:一个纯本地的资源故障让本节点**对一条可能完全合法的块投反对票**,并把这个判决
   发给 CL。在多验证者部署下,这等于用一台机器的 OOM 去污染整条链的 attestation。

**为什么 engine 层单独修不了**:engine 只能看到 `ConsensusError` 这一个类型,信息在
`OpSchedulerImpl` 那一层已经丢了。修法必须在 `OpSchedulerImpl`:典型做法是把
`std::bad_alloc` / `std::bad_array_new_length` 等已知的资源类异常单独 rethrow 为
`OpStorageError`(或新增第三类 `OpInternalError`),而不是全部塌进 `OpConsensusError`。
**该修法在视角 2/3 的文件里,请协调者交叉分派。**

**[需验证]** 最小验证步骤:在 `bcos-evm/test/opstack/EngineOpBranchTest.cpp` 里用一个会在
`processOpBlock` 期间抛 `std::bad_alloc` 的注入点(或临时在
`OpSchedulerImpl::executeOpBlock` step 3 前插一行 `throw std::bad_alloc{};`),
跑 target `test-bcos-evm`;**期待现象**:请求返回
`PayloadValidationStatus::Invalid`(而非抛 `OpExecutionInternalError`),
即证实本地故障被判成了共识否决。

---

### Important

#### I1 · FCU 对 safe/finalized 零哈希答 SYNCING,闭环在符合规范的 CL 下永久停滞
**新发现,不在台账。**(注:这段代码是 BASE 既有的,本分支未改——已用
`git diff 42e62fcef..HEAD -- engine/bcos-engine/EngineServiceImpl.h` 核实;但 OP 模式
**原样复用**了它,因此它现在是 OP 闭环的承重件。)

`file:line`:`engine/bcos-engine/EngineServiceImpl.h:264-281`

```
auto safeBlockNumber      = co_await getBlockNumber(view, forkchoiceState.safeBlockHash, fromStorage);
auto finalizedBlockNumber = co_await getBlockNumber(view, forkchoiceState.finalizedBlockHash, fromStorage);
if (!headBlockNumber || !safeBlockNumber || !finalizedBlockNumber) → SYNCING
```

op-geth:`eth/catalyst/api.go:342`(`if update.FinalizedBlockHash != (common.Hash{})`)、
`api.go:356`(safe 同)——**零哈希 = "尚未 finalize/尚无 safe",直接跳过检查**。
`api.go:245-248`:零 **head** 哈希才是 INVALID。

**失效场景**:链启动后 op-node 完成第一个 payload 的 newPayload,随即发
`FCU{head = A, safe = 0x00…00, finalized = 0x00…00}`(L1 尚未 finalize 任何 L2 块时的
规范编码)。本实现:`getBlockNumber(view, h256{})` 查 `SYS_HASH_2_NUMBER[32 个零字节]`
→ `nullopt` → **SYNCING,且 `m_trackedHeadBlock` 不更新**。op-geth:**VALID +
latestValidHash = A**。op-node 收到 SYNCING 会认为 EL 仍在同步、不推进 safe head,
下一轮仍然发同样的零哈希 → **永久停滞**。

覆盖情况:OP 分支的三个 FCU 用例
(`EngineOpBranchTest.cpp:806-810` 等)全部令 `head == safe == finalized ==` 同一个**已登记**
哈希,零哈希分支零覆盖。

**[需验证]** 最小验证步骤:在 `EngineOpBranchTest.cpp` 的
`ForkchoiceWithAttributesRefusedButHeadStillAdvances` 旁加一例,令
`safeBlockHash = finalizedBlockHash = bcos::h256{}`、`headBlockHash = headHash`(已登记),
`payloadAttributes = nullptr`,跑 target `test-bcos-evm`;
**期待现象**:`result.payloadStatus.status == Syncing`(而非 `Valid`),证实停滞。

---

#### I2 · FCU head 跳跃 > 1 抛协议外异常;head 回退静默 VALID
`file:line`:`engine/bcos-engine/EngineServiceImpl.h:319-342`

- `*headBlockNumber < tracked` → 静默 `Valid`,**不更新跟踪态**(设计 §1 已如实描摹并声明
  "沿用此语义并明示",故只作复核:在 OP 验证者语境下,这是对一次真实重组回答 VALID 而
  实际什么都没做——CL 会以为 EL 已经跟上);
- `*headBlockNumber != tracked + 1` → 抛 `InvalidForkchoiceState`。**这是异常,不是
  payloadStatus**。op-geth 对任何 head 都只返回状态,从不用协议外错误表达"跳得太远"
  (`api.go:270-305` 对未知 head 走 SYNCING;已知 head 无论跳多远都走 `SetCanonical`)。
  op-node 在同步追赶、或在一次 FCU 里同时推进多个已 newPayload 过的块时会命中这条。
  **未记账。**

#### I3 · 缺 `gasUsed <= gasLimit` 头校验
`file:line`:`engine/bcos-engine/EngineServiceImpl.cpp:256-291`(gasLimit 上界、gasUsed
宽度都查了,唯独没有二者的关系);op-geth:`consensus/beacon/consensus.go:266-268`。

今天大概率由执行侧 gas pool 兜住(`OpSchedulerImpl.h:237` 把 gasLimit 灌进
`BlockInfo::gas_limit`),因此我**不主张它当前可利用**;但批 1 的"负 gas pool"事件
(`OpSchedulerImpl.h:163-190` 的 `narrowGasLimit` 注释)恰恰是"执行侧 gas 会计被绕过"的
实例。op-geth 把这条放在**头校验**而不是执行里,正是因为它是**独立于执行的**冗余闸。
一行静态检查,**未记账**。

#### I4 · `c_opMode` 塌陷护栏只挡 V4,V3 上的橡皮图章仍然开着
`file:line`:`engine/bcos-engine/EngineServiceImpl.h:561-570`(护栏)、`:580-659`(通用分支)。
**台账 (m) 只覆盖 V4。**

`c_opMode` 一旦静默塌成 false(签名漂移,注释 `:540-560` 自陈无诊断),**V3** 请求走通用
分支:`validateExecutionPayload(payload, 3)` 对 OP payload 全部通过(withdrawals 在场且空、
blobGasUsed/excessBlobGas 在场)、`request.parentBeaconBlockRoot` 在场、
`expectedBlobVersionedHashes.empty() && transactions.empty()`(OP 用 `rawTransactions`,
通用 `transactions` 为空)→ 不触发 Accepted 短路 → `parentKnown` 由 op-node 刚设的**内存
head** 满足 → **`Valid` + payload 自报 blockHash,零执行**(`:658-659`)。
护栏的注释自己说它堵住了"橡皮图章后果链",但它是版本作用域的,而失效模式不是。
最小修法与台账 (m) 相同:生产组合根旁 `static_assert(c_opMode)`;或把护栏改成
"非 OP 构建拒绝任何带 `rawTransactions` 的 payload"(版本无关)。

#### I5 · INVALID 判决不留痕 → INVALID↔SYNCING 倒挂
**已记账为条目 (k)(置顶)。复核:裁定仍然成立,优先级恰当,无需上调。**
`file:line`:`EngineServiceImpl.h:797-802`(SYNCING 出口)、op-geth `api.go:878-880`
(`checkInvalidAncestor`)。补一条台账没写的细节:本实现连**内存态**的 invalid 缓存都没有,
所以哪怕不落库,一个进程内的有界 LRU 也能把这条闭掉——修复成本远低于 (l)。

#### I6 · 非链尾 parent 硬拒挡下合法重组
**已记账为条目 (l)(置顶)。复核:仍然成立。**补一条独立判断:台账把它描述为"首块被判坏后
CL 投同高度竞争块"。实际触发面更宽——**任何 unsafe 链重组**(sequencer 正常重组、L1
derivation 与 unsafe 链 consolidation 失败)都会投递同高度的不同块,而这在 OP 上是**常规
事件而非异常路径**。因此 (l) 不只是"边界情形",它意味着本闭环只对**严格线性、永不重组**的
投递序列可用。

#### I7 · extraData OP 形状未校验
**已记账(正文 + 裁定 A7 并列)。复核:成立。**补一条耦合关系:extraData 承载的正是
Holocene/Jovian 的 EIP-1559 参数(`eip1559_optimism.go:147/195`,9/17 字节 + 版本字节),
而 C1 的 `CalcBaseFee` **需要读父块的 extraData** 才能算出期望 baseFee。**两条欠账必须
一起修**:只修 extraData 形状不修 baseFee,等于校验了参数的格式却不校验参数被怎么用。

---

### Minor

- **M1** `engine/bcos-engine/EngineServiceImpl.cpp:185-302`:OP 路径不校验通用载体
  `payload.transactions` 为空。调用方在 OP 路径塞进任意 `bcos::protocol::Transaction`
  会被静默忽略。今天无 RPC 入口(台账 q),无消费者。
- **M2** `engine/bcos-engine/EngineServiceImpl.h:970`
  `fiscoHeader->setTimestamp(static_cast<int64_t>(payload.timestamp))`:`timestamp ≥ 2^63`
  会变成负数并经 `OpSchedulerImpl.h:236` `blk.timestamp` 让 EVM `TIMESTAMP` 返回负值,
  而重组头 RLP 里存的是原 uint64。无上界校验。荒谬输入,且 step 3a 单调性会在第二个块拦住,
  但第一个块没有下界。
- **M3** `EngineServiceImpl.h:868-891`:父头缺失即跳过时间戳检查(bootstrap 契约,注释
  `:861-867` 已说明);同函数按**块号**读父头而 op-geth 按**哈希**读 —— **已记账为条目 (p)**,
  复核成立,且与 I6/(l) 是同一根解除时必须同时改的两处。
- **M4** `EngineServiceImpl.h:1080-1086`:`blobGasUsed` 比对写成
  `commitments.blobGasUsed.has_value() && ...`。若 seal 在 Jovian 下**未 engage**该字段
  (`OpBlockSeal.cpp:74-81` 的 `cfg.has_da_footprint` 为假),比对**静默跳过**,DA 足迹
  语义不再被校验(重组头仍会把 payload 自报值钉进 blockHash,所以只是自洽而非正确)。
  今天两侧都读同一个 `jovianTime`,一致;但这是"两个独立 fork 判定必须永远同步"的隐式契约,
  没有断言看着它。
- **M5** `EngineServiceImpl.h:494-506`:OP 模式 `getPayload` 无条件抛
  `OpPayloadBuildingUnsupported`,包括 V1/V2/V3。行为正确(OP 模式从不建块),仅记为偏离。

---

## 3. 我核对过但**没有**发现问题的项(避免后续重复劳动)

1. **`latestValidHash` 逐桶取值**:与 op-geth `api.invalid(err, nil)` /
   `api.invalid(err, parent.Header())` 完全一致(Q1 表)。
2. **`registerOpBlock` 原子性**:不存在半登记态,论证见 Q7(含 `MultiLayerStorage.h:526-551`
   的 fork/pushView 语义核实)。
3. **`excessBlobGas` 恒钉 0**:与 op-geth 一致——`CalcExcessBlobGas`
   (`consensus/misc/eip4844/eip4844.go`)对 `config.IsOptimism()` 短路 `return 0`,
   Jovian 也不例外。**先前若有人担心 Jovian 下 excessBlobGas 应为非零,可以排除。**
4. **gasLimit 变化率**:OP **不**约束(`eip1559.go:37-42` 的
   `if !config.IsOptimism()`),本实现只查 `≤ 2^63-1` 是**正确的对齐**,不是缺口。
   任何把"gasLimit 变化率"列为欠账的表述都应删除。
5. **`expectedBlobVersionedHashes` 必须为空**:与解码器只认 0x7E/0x02/0x04
   (`OpSchedulerImpl.h:660-676`)自洽,blob tx 进不来,不存在 op-geth 的
   "invalid number of versionedHashes" 对应缺口。
6. **step 3b 放在 parentKnown 之后**(与 op-geth 相反):在本实现的不变式下可观测等价,
   注释 `:851-859` 的论证经核对成立。
7. **engine 层错误分类链**:分类屏障 + 执行段三 catch + 登记期不变式覆盖了每一条出口,
   engine 层内部无分类错位。破口在跨层(C3)。

---

## 4. 交付格式四行

```
STATUS: 复审完成(只读,未构建未跑测试)。OP 分支的状态桶/latestValidHash/登记原子性是干净的;共识分歧集中在 baseFee 无父子校验、-38005 闸的 pre-Isthmus 分支、以及跨层错分类三处。
FINDINGS: Critical 3 —— C1 baseFeePerGas 零校验(放行 op-geth 会拒的块,已记账 e 但优先级被低估且零测试覆盖)/ C2 -38005 闸放行 pre-Isthmus 后必判 INVALID(新,对 op-geth 会接受的块投反对票)/ C3 bad_alloc 等本地故障被 OpSchedulerImpl catch(...) 改写成 ConsensusError → engine 判 INVALID(新,engine 两道 -32603 防线对该路径是死代码)。Important 7 —— I1 FCU safe/finalized 零哈希答 SYNCING 致永久停滞(新,不在台账)/ I2 FCU head 跳跃>1 抛协议外异常(新)/ I3 缺 gasUsed<=gasLimit 头校验(新)/ I4 c_opMode 塌陷护栏只挡 V4、V3 橡皮图章仍开(新,台账 m 只覆盖 V4)/ I5 INVALID 不留痕(记账 k,复核成立)/ I6 非链尾硬拒(记账 l,触发面比台账描述更宽)/ I7 extraData 形状(记账,须与 C1 同时修)。Minor 5 —— 通用 transactions 载体未校验 / timestamp 无上界 / 父头按号读(记账 p)/ blobGasUsed 比对可静默跳过 / getPayload 无条件拒。
EVIDENCE: (1) EngineServiceImpl.cpp:334 与 EngineServiceImpl.h:978 把 payload.baseFeePerGas 原样送进重组头与 OpBlockEnv,而 validateOpNewPayloadRequest(EngineServiceImpl.cpp:185-302)与六项比对面(EngineServiceImpl.h:1044-1098)全文无该字段——对照 op-geth consensus/misc/eip1559/eip1559.go:53-56 的 expectedBaseFee 强制相等,且 EngineNewPayloadGateTest.cpp:534 的 baseFee 直取向量、变异矩阵无该项,故 33/33 全绿对此零信息。(2) EngineServiceImpl.h:692-702 的闸判定式 `isthmusActive != (version==4)` 对 timestamp<isthmusTime 的 V3 请求求值为 false→放行,而 EngineServiceImpl.cpp:219-225 随即因 withdrawalsRoot 缺失返回 INVALID——op-geth api_optimism.go:31-33 对 pre-Isthmus 恰恰要求 withdrawalsRoot 为 nil 并返回 VALID。
CONCERNS: (a) C3 的修复点在 OpSchedulerImpl.h:839-873(视角 2/3 的文件),engine 层单独修不了——请交叉分派;(b) C1 的"缺前置条件"理由自批 4 B4-1 打通父头读路后已不成立(EngineServiceImpl.h:875-885 的 parentHeader 已把 CalcBaseFee 所需五个字段全部备齐),是否本轮就补,需协调者裁定;(c) I1/I2 所在的 updateForkchoice 三哈希段是 BASE 既有代码(git diff 已核实本分支未改),但 OP 模式原样复用使其成为闭环承重件——修它算"OP 闭环范围内"还是"基座欠账",需裁定;(d) I4 的最优修法(生产组合根旁 static_assert)与台账 m 是同一件事,但 m 被登记为"接入生产组合根时",而 V3 缺口意味着塌陷后果在 V4 被堵住后仍然存在,是否上调优先级需裁定。
```
