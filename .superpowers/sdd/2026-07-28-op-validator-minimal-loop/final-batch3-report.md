# 终审批 3 报告:补测盲区 + 文档传播

基线 `45b2e8603`(批 1 + 批 2 已过审)。**编译与测试不豁免**,全部实跑。

**硬约束遵守情况**:生产代码**零语义改动**。唯一触及生产文件的改动是
`EngineServiceImpl.h` 中 `registerOpBlock` 的**一段纯注释**(B3-8 明文要求"`registerOpBlock`
处加一行注释点明"),`git diff -w` 全部落在注释行,编译产物行为不变。除此之外只动测试文件、
一个新的金值校验清单文件、和文档。

## 0. 结果

| 项 | 基线(45b2e8603) | 本批后 |
|---|---|---|
| in-tree `bcos-evm-opstack-tests` | 220/220 | **225/225**(主轮 224 + fix 轮 I-1 补测 1) |
| 本闭环新增测试(相对 merge-base 156) | 53 | **58** |
| `EngineOpBranch` | 20 | **22** |
| `EngineNewPayloadGate` | 5 | **8** |
| `EngineNewPayloadMutation` | 18 | 18(断言收紧,数量不变) |
| `test-bcos-engine`(Boost) | No errors | **No errors** |

新增 5 例(主轮 4 + fix 轮 1)+ 4 处既有用例的断言加固(不新增计数但新增判别力)。零退化。

standalone 未重新测量:本批改动全部落在 `bcos-evm/test/CMakeLists.txt` 的
`if(TARGET bcos-framework)` 守卫内,standalone 源列表逐字未动。

---

# 第一部分:补测 7 条

每条都做了"注释掉/篡改对应生产逻辑 → 重建 → 必须翻红 → 还原 → 复绿"自验。**9 次注入,
9 次如期翻红,0 次漏网**。翻红用例名逐条列在下面(自动采集自 gtest 输出,非手抄)。

## B3-1 `blobGasUsed` / `requestsHash` 两条比对零覆盖 ✅

**改法**:`EngineOpBranchTest.cpp` 的 `EachComparisonSurfaceFieldMismatchIsNamed` mismatch 表
从 6 行扩到 **8 行**:
- `blobGasUsed`:`StubCommitments::blobGasUsed` 从 `nullopt` 改为 `optional{1}`。比对的门是
  **optional 是否 engaged**(不是 fork),所以 engage 它并给一个与 payload 槽位(Isthmus 下被
  静态校验钉死为 0)不同的值,正是"seal 报了一个头没声明的 DA footprint"这一条件;
- `requestsHash`:把 seal 侧那份常量翻一 bit——正是该比对存在的理由(本库的空 requests 常量
  与 OP 侧 `OP_EMPTY_REQUESTS_HASH` 漂移)。

**自验**:

| 注入 | 翻红 |
|---|---|
| `commitments.blobGasUsed` 比对短路为恒假 | `EngineOpBranch.EachComparisonSurfaceFieldMismatchIsNamed` |
| `commitments.requestsHash` 比对短路为恒假 | `EngineOpBranch.EachComparisonSurfaceFieldMismatchIsNamed` |

对照:注入前这两次短路是 **50/50 全绿**(终审视角 3 实证),现在各自翻红。

## B3-2 块登记写侧只有桩在守 ✅

**改法**:`runGoldenVector`(33 条向量每条都跑)新增第 (6) 段,两条断言:
1. **完整性**:逐笔断言 `SYS_HASH_2_RECEIPT` 里存在键 = `keccak(raw EIP-2718 envelope)`
   的行(并断言同一块内 envelope 互异,否则求和会静默丢项);
2. **内容**:把每笔存储的回执**解码回来**(`receiptFactory->createReceipt(bytesConstRef)`),
   断言 **gasUsed 之和 == 该块 golden 头的 `gasUsed`**。`mapOpReceipt` 存的是每笔自身的
   `gas_used`(不是累计值,`OpReceiptMap.h:75-80`),所以这个和**恰好**是 op-geth 金值里的
   头字段。这一步把"有字节在那儿"变成"这些字节解码出的正是 op-geth 记的 gas"。

**自验**:

| 注入 | 翻红 |
|---|---|
| 删除 `registerOpBlock` 的全部 `SYS_HASH_2_RECEIPT` 写入 | `EngineOpBranch.ValidPayloadRegistersAllFourTables`、`EngineNewPayloadGate.{IsthmusSingleTransfer,JovianMultiTransfer,IsthmusDepositOnly,AllThirtyThreeGoldenVectors}` |
| **只写第一笔回执**(`if (index > 0) continue;`) | `EngineNewPayloadGate.{IsthmusSingleTransfer,JovianMultiTransfer,AllThirtyThreeGoldenVectors}` |

对照:注入前第一条只红 1 例(桩场景)。第二条注入是**新增的判别力**——它专门验证"完整性"
那一半:`IsthmusDepositOnly` 在此注入下**不红**,因为它只有一笔交易,写第一笔就是写全部。
这是正确行为,记在此以免被读成漏网。

## B3-3 收据数护栏无测试固定 ✅

**改法**:新增两条用例,分别钉住 task-5b 审查 M2 否决的两个设计要素:
- `EngineOpBranch.ReceiptCountMismatchIsRefusedNotSilentlyTruncated`:1 笔交易 / 0 条回执 →
  必须抛 `OpExecutionInternalError`,而不是 `std::min` 截断后照样判 VALID;
- `EngineOpBranch.NullReceiptIsRefusedNotSkipped`:数量相等但含空回执 → 必须抛,而不是
  `continue` 跳过。

两条都按 **B3-13a 的正/反例纪律**写:正例标识 = 该护栏自己的消息
(`receipt count differing from the transaction count` / `returned a null receipt`),
反例标识 = 分类屏障的 `unclassified exception` **必须不出现**。缺反例那一半的话,删掉护栏后
只要损坏恰好触发屏障,测试照样绿——批 1 和批 2 各踩过一次的那个坑。

**自验**:

| 注入 | 翻红 |
|---|---|
| 把 M2 否决的设计改回去(`std::min` 截断 + `continue`-on-null) | `EngineOpBranch.ReceiptCountMismatchIsRefusedNotSilentlyTruncated`、`EngineOpBranch.NullReceiptIsRefusedNotSkipped` |

对照:注入前该改动是 **206/206 全绿**。

## B3-4 `number`/`timestamp`/`baseFeePerGas` 唯一支点未显式化 ✅

**改法**:在链式用例里,`requestB` 构造完成后**立刻**(位于该用例全部 fatal `ASSERT_*`
之前)对 `chainB` 重建头的这三个字段做 `EXPECT_EQ`,值取自 `chainB.env`(金值来源,不是
硬编码);并另加三条 `EXPECT_NE`,断言它们**确实与 chainA 不同**——否则"第二个取值"这个前提
本身就不成立。段首挂了 `DO NOT DELETE` 标记与理由(33 条向量这三字段全常量,这是全套测试里
唯一的第二数据点)。

位置是刻意的:此前这三个字段的守护隐含在第 5 步的端到端 VALID 判决里,而那之前有若干
`ASSERT_*`,任何更早的致命失败(或将来为"稳定性"跳过/拆分该用例)都会把三个字段的全部覆盖
一起带走。现在它们在任何东西能中止测试之前就已求值,且自己报自己的名。

**自验**:

| 注入 | 翻红 |
|---|---|
| `rebuildOpEthHeader` 的 `.number` 硬编码为 `1` | `EngineNewPayloadGate.ChainedPairParentKnownThroughBlockRegistration` |

（与终审视角 3 的 M9 结果一致——仍是且只是这一条红。差别在于:现在红的是一条**自我说明**的
断言,失败信息直接写着"sole non-constant `number` assertion in the suite",而不是一个语焉不详的
端到端 VALID 失败。）

## B3-5 金值 provenance 无机内钉死 ✅

**选型说明(brief 二选一)**:brief 给的选项 (a) 是"读 manifest 头部注释的 commit 串"。
**实测该串不存在**——`golden/engine/manifest.txt` 的头部注释里没有任何 commit;pin 实际存在于
`vectors/*.json` 的 `_op_test_vectors.generator_commit` 字段里(每条一份,机器可读)。
因此采用选项 **(b) SHA256 清单**,并**额外**把已经存在的那份机器可读 pin 也变成断言。两半互补:

| 半 | 挡什么 | 挡不住什么 |
|---|---|---|
| (a) `golden/engine/SHA256SUMS`(39 个文件:33 golden + chained 6) | 金值字节的**任何**变化,无论谁改的、用什么改的 | 一个同时刷新 SHA256SUMS 的人 |
| (b) 33 条 `vectors/*.json` 的 `generator_commit == e8800cff…` + `generator == "opt8n-ref"` | 金值所扩展的语料被换成非 pinned op-geth 产物 | 同上 |

**诚实边界**(已逐字写进 `SHA256SUMS` 文件头和测试注释):没有任何本地机制能证明"重新生成者
真的用了 op-geth"。这两半能保证的是:**重新生成不再是无声的**——它变成一次必须显式修改
名为 `SHA256SUMS` 的文件的、可审查的动作,而不是一次"金值看着旧了,刷一下"的静默退化。
`SHA256SUMS` 用标准 shasum 格式,可以脱离本仓验证:
`grep -v '^#' SHA256SUMS | shasum -a 256 -c`。

新增用例 `EngineNewPayloadGate.GoldenCorpusProvenanceIsPinned`,除上述两半外还断言**清单完整**
(磁盘上的 golden 文件集合 == 清单覆盖的集合),否则新加一个未列入清单的金值文件就无人校验。

**自验**(两次注入,分别打两半):

| 注入 | 翻红 |
|---|---|
| 篡改 `isthmus_transfer_basic.golden.json` 的 `blockHash` 一个字符(模拟"重新生成") | **全库 11 例**(见下) |
| 篡改 `vectors/jovian_da_mix.json` 的 `generator_commit` | `GoldenCorpusProvenanceIsPinned`(单独红,证明 (b) 半独立生效) |

> **披露更正(批 3 复审 M-4)**:本表第一行初稿只列了 4 例——那是 `EngineNewPayloadGate.*`
> **过滤器下**的全部失败,采集确实是自动的,但**报告没有披露加了过滤器**,读者会以为那是全库
> 结果。复审者实测全库 11 例。这与 B3-13b 纠正批 2 的是同一类遗漏(证据表不完整),
> 我在同一份报告里又犯了一次,记此为戒。**全库实测(clean binary,重新测量)11 例**:
> `EthBlockHeader.{IsthmusSingleTxTransferBasic,AllThirtyThreeGoldenVectors}`、
> `EngineNewPayloadGate.{GoldenCorpusProvenanceIsPinned,IsthmusSingleTransfer,AllThirtyThreeGoldenVectors,GoldenVectorRedeliveryIsValidWithoutReExecution}`、
> `EngineNewPayloadMutation.{ComparisonSurfaceTransactionsRoot,UnknownParentIsSyncing,SamePayloadResubmittedAfterParentBecomesKnown,ForkchoiceWithAttributesRefusedButHeadStillAdvances,StorageLayoutFaultIsInternalErrorNotInvalid}`。
> 全库口径更有信息量:它显示金值一旦变动,**不止 provenance 用例**会红——以
> `isthmus_transfer_basic` 为底料的整条测试链都会红,provenance 用例的价值在于它是唯一
> **指名道姓说出原因**的那一条。

两次注入后均以原字节回写,`git status --porcelain -- .../t8n/` 为空(实测)。

## B3-6 `validationError` 断言过宽 ✅

**改法**:两个测试文件里**全部 15 处** `find(...) != npos` 改掉:
- 引入 `expectValidationError(status, <完整消息>)` 与
  `expectComparisonMismatch(status, <字段名>)`(后者拼 `"execution result does not match
  payload field: " + field` 后**精确**比较);
- 唯一保留非精确匹配的是 `ConsensusErrorFromExecutionMapsToInvalid`——它的消息尾部是解码器
  自己的 `what()`,测试并不拥有那段文本。那一条改成**前缀锚定 + 反例标识**:必须以
  `"OP block execution rejected the payload: "` 开头,且**不得**含比对桶的前缀。

**为什么这不是洁癖**:`find("blobGasUsed")` 同时命中两条语义相反的消息——静态校验桶
(`latestValidHash = null`)和比对桶(`latestValidHash = parent`)。桶的区分此前**没有被消息
同一性钉住**。

**自验**:

| 注入 | 翻红 |
|---|---|
| 把静态 Isthmus 消息改写成比对桶形状的 `"execution result does not match payload field: blobGasUsed"` | `EngineOpBranch.NonZeroBlobGasUsedIsInvalidUnderIsthmus`、`EngineNewPayloadMutation.NonZeroBlobGasUsedIsInvalidUnderIsthmus` |

这次注入在**旧的子串断言下会全绿**(消息仍含 `blobGasUsed`),是这条改动判别力的直接证据。

## B3-7 VALID 后重复投递用例 ✅(批 2 已落地短路,可做)

**发现**:批 2 已经带来了 `EngineOpBranch.AlreadyKnownBlockShortCircuitsToValidWithoutReExecuting`,
它已经断言了 brief 要求的三点(VALID / `latestValidHash == blockHash` / `executeOpBlockCalls`
不增)。**brief 这一条在桩层面已被满足**。

因此本批补的是它够不着的另一半:`EngineNewPayloadGate.GoldenVectorRedeliveryIsValidWithoutReExecution`
——**真调度器 + 真播种状态**下重投同一份金值 payload。这才是短路的真实理由所在:没有短路时
第二次投递不是"多做一遍功",而是**在第一次已提交的状态之上重放**,必然不一致,于是节点对一个
自己刚接受的块回答 INVALID。两条子用例覆盖两种发生方式(桩都做不到,因为桩的"执行"没有状态可重放):

- `isthmus_transfer_basic`:用户交易的 sender nonce 已推进 → `processOpBlock` 拒绝 → INVALID;
- `isthmus_deposit_only`:只有 L1 attributes deposit,mint 会被**二次入账** → 比对失配 → INVALID。

另断言重投后 `s_eth_block_header` 里该高度的字节**未变**(第二次投递没有再写一遍)。

**自验**:

| 注入 | 翻红 |
|---|---|
| 删除 step 3b 的"已知块 → VALID"短路 | `EngineOpBranch.AlreadyKnownBlockShortCircuitsToValidWithoutReExecuting`、`EngineNewPayloadGate.GoldenVectorRedeliveryIsValidWithoutReExecution` |

---

# 第二部分:文档传播 8 条

## B3-8 `SYS_HASH_2_TX` —— 只补披露,不补实现 ✅

三处同时落:

1. **spec §6.4 新增条目 (f)**:事实(`prewriteBlockToBuffer` 同一函数下一段就写这张表)+
   后果(**OP 接受的块,回执可按 tx hash 查到,原始交易本体无法取回**)+ 裁定(本轮只披露);
2. **`bcos-evm/README.md` 的"明确不构成的宣称"列表**新增一条,措辞同上;
3. **`registerOpBlock` 处的注释**(唯一的生产文件改动,纯注释):点明这张表没写、后果是什么、
   为什么不是"顺手补上"的事(OP 路径持有的是 raw envelope 而非 `bcos::protocol::Transaction`,
   存什么、按谁的编码存是真决策),并指向 §6.4 (f)。**在此之前它是一处无声跳过。**

## B3-9 spec 悬空引用 ✅

按 **B3-12 选项 (a)** 一并解决:`task-6-report.md` 已 `git add -f` 入库,spec §7.3 对它的
`§4/§8b` 引用不再悬空。另在 `task-6-report.md` 的 §8b 开头补了一段**交叉引用勘误**,记下
"spec 曾指向本文件 §5 是错的,§5 是无关的偏离台账"这件事,让记录自洽。

## B3-10 `-32602` 分类偏离未披露 ✅

spec §6.4 新增条目 (g):缺失的 `rawTransactions` 判 INVALID 而非 -32602,理由是
`NewPayloadRequest` 对象层无法区分"缺参"与"空数组"(那是 RPC 解析层的区分,而 RPC 端点本期
整体豁免,条目 d)。此前只在**未入库的** `task-5b-report.md` §7 交代过。README 同步加一行。

## B3-11 `ConsensusError → INVALID` 覆盖不对称未披露 ✅

spec §6.4 新增条目 (h) + README 一条。措辞按事实分层,不含糊:`OpConsensusError → INVALID`
**有**一条真调度器用例(坏类型字节 → 解码期抛),但 `OpSchedulerImpl` 的 `catch(...)`
**重分类**路径抵达 engine 的链路(即 T4 修复真正针对的场景)engine 侧无用例;而
`OpStorageError → -32603` 有真调度器 + 真桥的端到端覆盖。**两条腿不等价可信**——这句话此前
从 spec/README 完全看不出来。

## B3-12 归档标准成文 ✅

采用**选项 (a) 全归档**,写进 **spec 新增 §8.1**:

- `task-N-report.md` / `final-batch-N-report.md` / `n0-*.txt` / 探针报告 **一律入库**,
  且必须 `git add -f`(该目录被 `.git/info/exclude` 覆盖,不加 `-f` 会被**静默**跳过——
  这正是此前漏归档的直接机制,写进标准里免得再犯);
- `*-brief.md` 与 `review-*.diff` 不入库;
- **spec / README 只允许引用已入库的文件**。

本批实际入库 8 份:`task-{1,2,3,4,5b,6}-report.md` + `final-batch{1,2}-report.md`
(`task-5a` / `task-7` / `n0-*` / 探针报告此前已在库),外加本报告。

## B3-13a 把"兜底断言"经验写进 review checklist ✅

写进 **spec 新增 §11「审查检查单:兜底断言与假绿」**(位置选 spec 而非
`docs/superpowers/`:这条规则是被本 spec 的三轮修复实证出来的,和 §6.4 欠账、§8 验收清单在
同一份文档里才会被同一批读者读到)。内容含三条实证的完整机制描述,以及规则本身:

> 凡新增 `catch(...)` 兜底或任何会重写异常消息的屏障,其测试必须同时给**正例标识**
> (必须含本层消息)与**反例标识**(不得含相邻层消息);且每条防线要有独立的翻红实验,
> 确认删掉防线 X 时红的**恰好**是为 X 写的用例。

外加一条自然推论:同样的道理适用于所有 `validationError` 断言 → 精确匹配/前缀锚定
(即 B3-6,已落地)。

## B3-13b 补记批 2 报告的证据遗漏 ✅

`final-batch2-report.md` 的第二轮自验表第二行原先只列 `NonTipParentIsRefusedExplicitly`。
已补记:删除透传分支后 **`UnclassifiedExecutionEscapeIsInternalErrorNotEscaping` 同样翻红**,
根因就是本报告 §B3-13a 的第 3 条(屏障用自己的消息**重新构造**异常、覆盖内层标识,于是该
用例收紧后的正/反例双断言两头都不满足)。措辞明确写了"**修复本身正确,只是本表的证据不完整**"。

## B3-13 零散记账 ✅

spec §6.4 新增条目 (i),四小项:`computeOpTxRoot` 双算 + 形参应为 `forward_range`;
`c_opMode` 只探测一个成员名(签名漂移会静默退化,护栏只有三个 `static_assert`);
三个 engine 测试 TU 匿名命名空间同名类型在 `UNITY_BUILD` 下是硬冲突;
`${CMAKE_SOURCE_DIR}` 实际作用于整个测试 target 的 20+ 个源文件(实测无遮蔽,但既有注释里
"暴露面有界"的表述弱化了范围)。

**另**:`task-6-report.md` 的交叉引用勘误见 B3-9。

## 顺带:文档数字刷新(视角 4 的口径要求)

spec §8 与 README 里 T7 快照的实测数字(206/206、新增 50 例)在批 1/2/3 之后已过时。两处都
加了回填段:**in-tree 224/224、本闭环新增 57 例**(T7 时 50,批 1 +11、批 2 +2、批 3 +4),
并注明 standalone 未重新测量及其理由。README 的 gate 描述里补上了金值 provenance 校验一项。

---

# 变更清单

| 文件 | 性质 |
|---|---|
| `bcos-evm/test/opstack/EngineOpBranchTest.cpp` | mismatch 表 6→8 行(B3-1);新增 2 例收据护栏(B3-3);全部断言精确化(B3-6) |
| `bcos-evm/test/opstack/EngineNewPayloadGateTest.cpp` | 逐笔回执 + gasUsed 求和(B3-2);链式三字段支点(B3-4);provenance 用例(B3-5);断言精确化(B3-6);真执行重投用例(B3-7) |
| `bcos-evm/test/opstack/t8n/golden/engine/SHA256SUMS` | **新增**(B3-5),标准 shasum 格式 + 自解释文件头 |
| `engine/bcos-engine/EngineServiceImpl.h` | **纯注释**(B3-8),零语义 |
| `docs/superpowers/specs/…-op-validator-minimal-loop-design.md` | §6.4 增 (f)(g)(h)(i);§7.5 增批 3 单测层增量;§8 数字回填 + 新增 §8.1 归档标准;新增 §11 审查检查单 |
| `bcos-evm/README.md` | 三条新披露 + 数字刷新 |
| `.superpowers/sdd/…/final-batch2-report.md` | B3-13b 勘误 |
| `.superpowers/sdd/…/task-6-report.md` | 交叉引用勘误 |
| `.superpowers/sdd/…/task-{1,2,3,4,5b,6}-report.md`、`final-batch{1,2}-report.md` | **归档入库**(B3-9/B3-12) |

`vectors/` 逐字节未动(两次注入实验后均已回写并实测 `git status` 为空);
`ports/` `transaction-scheduler/` `bcos-rpc/` 零触碰。

---

# Fix 轮(批 3 复审:Approved,0 Critical;必修 I-1 / M-2 / M-3 / M-4)

回归:**in-tree 225/225**(fix 轮 +1 例)、`test-bcos-engine` "No errors detected"、
`vectors/` 与 `golden/` 逐字节未变(两次注入均已回写并实测)。

## I-1【撤回错误归因 + 补测】——已闭合

**先撤回**:初稿 CONCERNS #2 的两句话**都不成立**,我在此撤回。我称"没有不改生产代码就能触发
的入口"并请求裁定加测试注入点——**错**。复审者实测首次即构造成功,而我根本没有尝试构造:我从
"既有的坏类型字节走的是解码期直接 `throw`"这一条正确观察,跳到了"因此该腿不可达"这个不成立的
结论,中间缺的正是 `OpSchedulerImpl.h:777-779` 那条注释——**它自己就点名**了
`processOpBlock` 的 "first tx is not the L1 attributes deposit" 会逃逸 typed catch。证据就在我
引用过的那段代码里,我没读到底。这是归因失败,不是构造困难。

**补测**:`EngineNewPayloadGate.ConsensusErrorViaCatchAllReclassificationIsInvalid`
(真调度器 + 真桥,12 行,无新 fixture),按复审给的构造:

```cpp
auto scenario = prepareScenario("isthmus_contract_logs");   // 2 txs,[0] 是 L1 attributes deposit
auto& raws = *scenario.request.executionPayload.rawTransactions;
raws.erase(raws.begin());            // 首笔不再是 attributes deposit -> OpBlockExecute.cpp:40
resealBlockHash(scenario.request);   // 保持 payload 自洽,否则静态 blockHash 检查先挡下
```

断言:INVALID + `latestValidHash == parentHash` + 未登记,并按 spec §11 给**双标识**——
正例含 `"typed catch bypassed"`(**只由** `OpSchedulerImpl.h:769/788-791` 产生;typed handler
产生的是 `e.what()`),反例**不得含**比对桶前缀 `"execution result does not match payload field: "`。

**翻红自验(两次注入,均全库无过滤)**:

| 注入 | 翻红 |
|---|---|
| `catch (...)` → `catch (int)`(该腿整体失效) | `OpSchedulerImpl.FirstTxNotAttributesDepositIsConsensusError`、`OpSchedulerImpl.ThrowingStorageIsStorageError`、**`EngineNewPayloadGate.ConsensusErrorViaCatchAllReclassificationIsInvalid`** |
| `catch(...)` 内改抛 `OpStorageError` 而非 `OpConsensusError`(腿在但分类错) | `OpSchedulerImpl.FirstTxNotAttributesDepositIsConsensusError`、**`EngineNewPayloadGate.ConsensusErrorViaCatchAllReclassificationIsInvalid`** |

第二次注入是刻意加的:它证明新用例钉的是 **INVALID 这一侧的分类**,而不只是"有异常发生"。
两次注入前该用例都是**新增的唯一 engine 侧证据**——注入前的三条红里,另外两条都是调度器层的。

**spec/README 同步**:§6.4 条目 (h) 改写为"缺口已闭合 + 构造方式 + 两次翻红自验",并注明
**保留该条目的理由是断言精度受条目 (j) 限制**;README 的对应条从"覆盖不对称"改为
"两条腿已对称覆盖,但断不到具体是哪一处 throw"。

**⚠️ 一个必须记下的操作教训**:做完最后一次注入实验后,我的驱动脚本只还原了源码、**没有重建**,
于是我拿**带变异的二进制**去跑了一次全量,看到 2 红,并一度把它误判成"新用例导致 pre-existing
用例翻红的顺序依赖"。重建后 **225/225 全绿**,无任何顺序依赖。教训:**变异实验的最后一步必须是
"还原 + 重建 + 复绿",少一步都会污染下一次测量**——差一点就据此写出一份错误的 CONCERNS。

## M-2【文档事实错误】——已修

spec `:403` 漏否定词:"即它们此前被任何用例触碰过" → "即它们此前**从未**被任何用例触碰过"。
原文断言了与所记录事实相反的意思。

## M-3【悬空引用未做全量扫描】——已修,并把"扫描"写进标准

`git add -f` 了 `.superpowers/sdd/probe-ledger-bridge-report.md`(README:123 引用)。
**并按要求做了全量扫描**——脚本抽出 spec / `bcos-evm/README.md` / 金值目录 README 里所有
`.superpowers/…`、`docs/…` 形状的引用,逐个比对 `git ls-files`。扫描**又发现第三处**:
spec:11 引用的 `.superpowers/sdd/validator-loop-rev3-directive.md`(rev.3 的裁定书)同样未入库,
一并 `git add -f`。**复扫结果:悬空引用 0 处**。

§8.1 归档标准同步加严两点:(1) 归档类目明确加入**探针报告 `probe-*.md`** 与
**控制器裁定书/决策记录 `*-directive.md`**(即 spec 会援引为权威出处的文件);
(2) 明文规定判定方式是**全量扫描而非逐条点名**,并把"批 3 首轮点名式修补 → 复审发现第二处 →
扫描又发现第三处"写进去作为该规定的实证理由。

## M-4【证据披露不完整】——已修

见上文 B3-5 自验表下的「披露更正」段:初稿的 4 例是 `EngineNewPayloadGate.*` **过滤器下**的
全部失败,报告未披露过滤器;全库实测 **11 例**,已逐条列出。与 B3-13b 纠正批 2 的是同一类
遗漏,已在报告里明写"我在同一份报告里又犯了一次"。

## ⚠️ 台账补记(不改代码)

spec §6.4 新增条目 **(j)**:`catch(...)` 重分类**丢弃 `e.what()`**,导致 `OpBlockExecute.cpp`
的四处块级 throw(空块 :37 / 首笔非 attributes :40 / deposit 乱序 :55 / 非 deposit 校验失败)
抵达 engine 后**共用同一条泛化 `validationError`**,运维无法区分是哪一类拒绝。这是 RTTI 变通的
**既有**后果(非本批引入),此前不在台账上。条目里同时写明:它**限定了 I-1 补测的断言精度**
——只能断到"走了 `catch(...)` 这条腿",断不到"是四处中的哪一处";真正的修法是消除 RTTI 变通
本身,不是在这一层拼消息。README 同步一条。

## 澄清接收

- **C2(`yParity > 1`)已在批 1 完全关闭**(主提交 `YParityEquals2/256IsConsensusError` +
  fix 轮 setcode 侧双子用例 + 翻红自验)。初稿 CONCERNS #5 的"建议下一批直接做"**作废**,不要重做;
- **C1(语料 `currentRandom`/`currentCoinbase` 恒定)属批 5**;其中"重跑 opt8n-ref 必须同步刷新
  本批新加的 `SHA256SUMS`"已由控制器记入台账。

# CONCERNS(交控制器裁定)

1. **`SYS_HASH_2_TX` 只披露未实现**——按裁定执行,但重申后果:任何按交易哈希取回交易的读路径
   对 OP 块无数据可返。补实现需要先定"存什么编码"(OP 路径手上只有 raw envelope),是设计决策
   不是补丁,建议在 op-node 实连前置清单里给它一个明确位置而不是留在台账尾部。
2. ~~**B3-11 的覆盖不对称本轮只补了披露,没补测试**……需裁定加测试注入点。~~
   **【已撤回,见 Fix 轮 I-1】** 该归因不成立:不需要任何生产代码改动,12 行测试即可构造,
   复审者首次即构造成功。缺口已闭合(`ConsensusErrorViaCatchAllReclassificationIsInvalid`,
   两次翻红自验)。**残留的真实限制**改记为 spec §6.4 条目 (j):`catch(...)` 丢弃 `e.what()`,
   四处块级 throw 共用一条 `validationError`,故断言精度到"哪条腿"为止。
3. **B3-5 的边界**:SHA256 清单挡不住"同时刷新清单"的重新生成。若要更硬,唯一实质选项是把
   pin 与金值哈希放到**本仓之外**(CI secret / 上游制品仓库)。当前方案是"让重新生成变成可审查的
   显式动作",不是密码学保证——这一点已写进文件头和测试注释,不希望它被读成后者。
4. **终审视角 3 的 C1(语料 `currentRandom` 恒 0、`currentCoinbase` 恒 `0x42..0011`,21 字段里
   2 个是空的)本批未处理**——brief 未列入 B3。它需要**重跑 opt8n-ref 生成新向量**,属于金值仪式
   而非补测,且会改动 `golden/`(并因此需要同步刷新本批刚加的 `SHA256SUMS`)。建议单列一批,
   与 B3-5 的清单刷新一起做。
5. ~~**终审视角 3 的 C2(`yParity > 1` 零守护)本批未处理,建议下一批做掉。**~~
   **【已作废】** 控制器澄清:C2 **已在批 1 完全关闭**(主提交 `YParityEquals2/256IsConsensusError`,
   fix 轮补 setcode 侧双子用例并做了翻红自验)。我在初稿里根据终审视角 3 的原始清单提出建议时
   没有核对批 1 的产物——**不要重做**。
6. **`progress.md` 本轮未提交**:工作区里它带有控制器写入的终审台账改动,不属于本批产物,
   我未 `git add` 也未改动它,留给控制器自行处置。
