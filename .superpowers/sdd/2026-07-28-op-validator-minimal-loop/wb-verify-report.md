# 全分支复审 · `[需验证]` 实验统一执行报告

分支 `feat-op-validator-loop`,worktree `/Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/ledger-bridge`。
执行者:实验执行者(唯一持有构建目录)。所有实验均按各复审报告写明的最小步骤执行,**未自行重新设计**。

## 0. 构建环境与基线(红绿见证的口径)

| 构建目录 | 配置 | 承载的目标 | 基线 |
|---|---|---|---|
| `build/`(**in-tree**) | `Release`,`FULLNODE=ON TESTS=ON`,vcpkg toolchain,**本次执行前已 reconfigure** | `bcos-evm-opstack-tests`(**含 3 个 engine 测试源**)、`test-bcos-engine` | **239/239 + engine 全绿** |
| `bcos-evm/build/`(standalone) | `Debug` | `bcos-evm-opstack-tests`(**engine 测试被 `if(TARGET bcos-framework)` 门控排除**) | 131 例 |

**红绿见证归属声明**:凡涉及 `engine/bcos-engine/EngineServiceImpl.{h,cpp}` 与
`EngineOpBranchTest / EngineNewPayloadGateTest / EngineVersionGateTest` 的实验,
**只有 in-tree `build/` 构成见证**;standalone 对它们是"依赖图无边"的空真,本报告一律不采信。
凡只涉及 `OpSchedulerImpl.h` / `Storage2Ledger.h` / `RLPDecode.h` 的实验,两个目录都可作证,
本报告以 in-tree 为准并在需要时点名。

**假绿防护**:每次重建后核对二进制 `mtime` 是否刷新(脚本 `rt.sh` 每次打印
`BUILD-OK <mtime>`),新增用例一律另用 `--gtest_list_tests` 或 `--gtest_filter` 命中数确认。

---

## 1. 实验结果总表

(见下方逐条)

---

## 2. P5 · E1 / E2 —— 该报告的两条核心论断

### E1 `prevRandao` 硬编码为零(头腿)

- **来源**:`wb-review-p5-report.md` I-1,实验 (a)
- **改了什么**:`engine/bcos-engine/EngineServiceImpl.cpp:333`
  `.prevRandao = payload.prevRandao,` → `.prevRandao = bcos::h256{},`
- **实测现象**:in-tree `bcos-evm-opstack-tests` **239/239 全绿**,`test-bcos-engine` **全绿**。
- **结论**:**证实**。

### E1b `prevRandao` 硬编码为零(执行腿,追加实验)

- **改了什么**:在 E1 基础上再改 `EngineServiceImpl.h:977`
  `.prevRandao = payload.prevRandao,` → `.prevRandao = bcos::h256{},`(BlockEnv → `BlockInfo::prev_randao`)
- **实测现象**:**239/239 全绿**,engine 全绿。
- **结论**:**证实** —— I-1 关于"头腿和执行腿**同时**零覆盖"的论断成立。

### E1c 反向对照(本次追加,原报告未要求)

- **改了什么**:`.prevRandao = bcos::h256("0x1111…11")`(**非**语料常量)
- **实测现象**:**11 例翻红**(`EngineNewPayloadGate.*` 6 例 + `EngineNewPayloadMutation.*` 5 例)。
- **意义**:见 §2.5 的**重要限定**。

### E2 `feeRecipient` 硬编码为 `0x42…0011`

- **来源**:`wb-review-p5-report.md` I-1,实验 (b)
- **改了什么**:`EngineServiceImpl.cpp:321`
  `.feeRecipient = payload.feeRecipient,` → `.feeRecipient = bcos::Address("0x4200000000000000000000000000000000000011"),`
- **实测现象**:**239/239 全绿**,engine 全绿。
- **结论**:**证实**。

### E2b 反向对照(本次追加)

- **改了什么**:同上,但常量换成 `0xdeadbeef…deadbeef`
- **实测现象**:**11 例翻红**(与 E1c 完全同一组用例)。

### 2.5 与原报告预期不符 / 需要限定的地方(**重点**)

E1/E2 的结论**如原报告所述成立**,但 E1c/E2b 揭示了原报告措辞里被省掉的一层,
若不写清会让后来者高估缺口:

**语料常量已实测确认**:
- `t8n/vectors/*.json` 的 `env.currentRandom` **全语料唯一取值 `"0x0"`**;
- `env.currentCoinbase` **全语料唯一取值 `"0x4200000000000000000000000000000000000011"`**。
  (`grep -oh … | sort -u` 各只回一行,与 I-1 的推断一致。)

因此这两个字段的判别力**不是"恒为零"**,而是**"恰好只在'把字段替换成语料常量'这一个方向上为零"**:
- 任何**其它**误映射(读错字段、写错常量、字段错位)都会被 gate 的
  `rebuildOpEthHeader().encode() == golden.encodedHeaderHex` 立刻抓到(11 例红);
- **唯独**"用语料常量替换 payload 来源"这一类回归静默通过。

对 `prevRandao` 这条**实质影响不变**(生产里 L1 mixDigest 几乎必然非零,而语料常量是零,
所以"硬编码为零"正是最可能的实现走样形态,且它零守护);
对 `feeRecipient` 影响同样不变(语料常量恰是 OP 主网生产值)。
但报告 §2 表述"该映射零覆盖"应精确化为"**对语料常量方向的回归零覆盖**"。
I-1 建议的字段敏感性用例(逐字段扰动 payload 断言 `encode()` 必变)恰好**也堵不住**这一类
(扰动的是 payload,而硬编码后 payload 不再被读 → `encode()` 不变 → 断言反而会红)——
换句话说 **I-1 提议的修法确实能堵住这条**,这一点本实验间接证实了:E1c/E2b 表明只要
payload 值离开语料常量,`encode()` 就会变,因此该敏感性用例在硬编码实现下必然翻红。
**建议采纳 I-1 的修法。**

---

## 3. P2 · C-1 / C-2 —— 两条 Critical(只加测试,不改生产代码)

两条都通过**直接调用** `bcos::evm::engine::detail::decodeOneRawTx(rawEntry, chainId)`
(它是 `detail` 里的自由 `inline` 函数,测试 TU 可直接触及)来观测,
避免走 `executeOpBlock` 时被 `catch(...)` 重分类或被不相关的 first-tx 闸误伤。
探针是**报告式**的(打印是否抛 + 抛了什么),不是断言式的 —— 用 `EXPECT_TRUE(threw)`
把"没抛"表现为红,红即缺口存在。见证目录:**in-tree `build/`**
(该文件亦在 standalone 编译,但本次以 in-tree 为准)。

### C-1 长形态 RLP 长度前缀的前导零未被拒绝

- **来源**:`wb-review-p2-report.md` C-1
- **改了什么**:`OpSchedulerImplTest.cpp` 末尾临时加 `VerifyProbe.C1NonCanonicalListLengthPrefix`。
  取 `DepositFieldEncodings::envelope()` 的规范字节(86 B,外层 `0x7e f8 0x53 …`),
  把外层列表头改写成 `0xf9 0x00 0x53`(lenOfLen=2,长度字节带前导零),payload 一字不动 → 87 B。
- **实测现象**:

  ```
  PROBE[C1] canonical size=86 mutated size=87
  PROBE[C1] decoded OK; gas=1000000 same-as-canonical=1
  PROBE[C1] DID NOT THROW
  PROBE[C1] txRoot(canonical)=0xe7fe64f3cf4769c3d01d423035da8c7d8eef30db62c5be56c6bc379d6b6d625e
  PROBE[C1] txRoot(mutated) =0xd1c4b2f390677cc8de81b4f863f5992123c45a4890d402117618a8dd222a0e0a  differ=1
  ```

  即:**解码成功,解出与规范形态完全相同的 `DepositTx`**(`gas` 一致),
  而 `computeOpTxRoot` 对同一笔语义交易给出**不同的 txRoot**。
- **结论**:**证实**。C-1 描述的两条链路(解码器接受 + txRoot 依赖线上字节)**都**实测坐实。
- **一处需要更正原报告的细节(非实质)**:探针顺带打印 `signedEnvelope-bytes-differ=0`。
  这不是"解码器做了规范化",而是 **deposit 的 `signedEnvelope` 按设计恒为 `{}`**
  (`OpSchedulerImpl.h:543`,deposit 无签名)。txRoot 的分歧来自 `computeOpTxRoot`
  直接哈希 `rawTxBytes`(`OpEngineSeam.h:174-185`),与 `signedEnvelope` 无关 ——
  C-1 原文的机制描述正确,此处只是提醒后来者别用 `signedEnvelope` 去复现。
- **顺带证实**:`OpEngineSeam.h:156-163` 那句
  「decoders reject every non-canonical encoding Go's `rlp` rejects」**实测为假**。

### C-2 EIP-7702 授权项 `yParity` 宽度未与 op-geth 的 `uint8` 对齐

- **来源**:`wb-review-p2-report.md` C-2
- **改了什么**:`OpSchedulerImplTest.cpp` 末尾临时加 `VerifyProbe.C2AuthorizationYParityTooWideForUint8`。
  用 `evmone::rlp::encode_tuple` 造一笔 0x04 setcode,`authorizationList` 含一项
  `Authorization{.chain_id = kChainId, .v = 256, .r = 1, .s = 1}` ——
  `rlp_encode(const Authorization&)`(`eth/utils/rlp_encode.cpp:96-100`)把 `v` 当 `uint256` 编,
  256 正好编成 **`0x82 0x01 0x00`**,即报告要求的那个字节序列(无前导零,规范编码)。
- **实测现象**:

  ```
  PROBE[C2] decoded OK; authList size=1 auth.v=256
  PROBE[C2] DID NOT THROW
  ```

- **结论**:**证实**。`decodeAuthorizationList`(`OpSchedulerImpl.h:477`)的
  `decodeU256Scalar` 接受了一个 op-geth 会以 `rlp: input string too long for uint8` 整笔拒绝的值,
  整笔交易正常解出。

---

## 4. P5 · E3 - E6

### E3 `validateOpNewPayloadRequest` 的 8 条零覆盖拒绝

- **来源**:`wb-review-p5-report.md` I-2 / §2.1 表
- **改了什么**:**逐条**(8 次独立的 改→重建→跑→还原)把对应的
  `return std::string("…");` 替换成 `/* E3-REMOVED */;`(保留 `if`,只去掉拒绝),
  每次只动一条。脚本见 scratchpad `e3.py`,每轮都独立重建了两个二进制。
- **实测现象**(全部 in-tree):

  | 移除的检查 | `bcos-evm-opstack-tests` | `test-bcos-engine` |
  |---|---|---|
  | #1 `rawTransactions is required…` | 239/239 | 绿 |
  | #4 `parentBeaconBlockRoot must be a 32-byte hash…` | 239/239 | 绿 |
  | #5 `withdrawalsRoot is required…` | 239/239 | 绿 |
  | #7 `blobGasUsed must be present…` | 239/239 | 绿 |
  | #9 `blockNumber must not be negative` | 239/239 | 绿 |
  | #10 `gasLimit exceeds the uint64 range…` | 239/239 | 绿 |
  | #13 `gasUsed exceeds the uint64 range…` | 239/239 | 绿 |
  | #14 `blobGasUsed exceeds the uint64 range…` | 239/239 | 绿 |

- **结论**:**证实**,8/8 与原报告预期完全一致。
- **未做的部分(如实说明)**:原报告建议对其中 4 条 UB 类另用 **ASan 构建**跑。
  **本次未做**——现有测试语料的 optional 全部 engaged,常规构建里这 4 条根本不会解引用空
  optional,ASan 也不会有额外信号;真要观察 UB 需要另造一个"缺字段"的请求,
  那已经是原报告建议的**补测**而不是本次的验证实验。作为无法执行/无增量记录在此。

### E4 已知块短路(`EngineServiceImpl.h:915-920`)—— **与原报告预期不符,M-5 需修正**

- **来源**:`wb-review-p5-report.md` M-5
- **改了什么**:删掉 step 3b 的
  `if (auto knownBlockNumber = co_await getBlockNumber(view, payload.blockHash, fromStorage); …) co_return makeStatus(Valid, …)` 整块。
- **原报告 / 代码注释的预期**:
  `EngineNewPayloadGateTest.cpp:1216-1234` 明写
  「with the short-circuit removed, **both sub-cases come back INVALID**」,
  且给了两种**不同机制**(transfer 的 sender nonce 已推进 → `processOpBlock` 拒 → INVALID;
  deposit-only 的 mint 二次入账 → 收据/状态比对失配 → INVALID)。
- **实测现象**:

  ```
  [ RUN      ] EngineNewPayloadGate.GoldenVectorRedeliveryIsValidWithoutReExecution
  unknown file: Failure
  C++ exception with description "non-tip parent not supported: a different block is already
  registered at this height, so the forked view's base state is not the payload's parent"
  thrown in the test body.
  ```

  用例确实**翻红**,但:
  1. **不是 INVALID**,而是 **step 3c 的非链尾检查抛出 `OpExecutionInternalError`**
     (生产上映射为 **-32603**,不是判决);
  2. 异常**逃出 `newPayload` 并中止整个 TEST**,所以第二个子用例
     (`isthmus_deposit_only`)**根本没运行**;
  3. 我另做了一次实验把循环临时改成只跑 `isthmus_deposit_only`,得到的是
     **完全相同的那条 non-tip 异常** —— 两个子用例走的是**同一条**路径,
     而不是注释所说的"两种不同机制"。
- **结论**:**证伪 / 需降级**。
  - 用例作为"短路存在性"的**红证人有效**(移除即红),这一点不受影响;
  - 但注释 `:1216-1234` 与 M-5 里"两条子用例均因 INVALID 翻红""两种不同机制"的**论述为假**,
    实际先命中的是 `:956-964` 的非链尾闸。这也反过来**印证**了 `:906-910` 那段
    placement 注释自己写的理由("BEFORE the non-tip check below, because a re-delivered block
    always has a child height occupied (by itself) and would otherwise trip that check")——
    注释的 placement 论证是对的,而它下方那段"删掉会怎样"的论证是错的,两段互相矛盾。
  - **建议**:把 `EngineNewPayloadGateTest.cpp:1222-1234` 那段改写为实测事实
    (「移除短路后,重投块先撞上 step 3c 非链尾闸并抛 -32603;两个子用例机制相同」),
    并把 M-5 的"名字强于断言"结论保留但换掉理由。

### E5 `expectExhausted` 的 throw(`OpSchedulerImpl.h:436-438`)

- **来源**:`wb-review-p5-report.md` §2.3 / E5
- **改了什么**:把 `throw OpConsensusError("… unexpected trailing bytes in " + what)`
  换成 `(void)body; (void)what;`(8 个 call site 一起失效)。
- **实测现象**:in-tree **239/239 全绿**,`test-bcos-engine` 全绿。
- **结论**:**证实**。这条"唯一阻止信封尾部追加垃圾字节"的防线在全套测试里**零守护**。
  与 C-1 合看尤其值得注意:两者是同一条隐式契约
  (`computeOpTxRoot` ↔ `DeriveSha` 等价性依赖解码器严格性)的两个破口,**都无守护**。

### E6 `gasLimit` 的单支点

- **来源**:`wb-review-p5-report.md` I-4 / E6
- **改了什么**:`EngineServiceImpl.cpp` 的
  `.gasLimit = narrowU256ToU64(payload.gasLimit).value(),` → `.gasLimit = 0x989680,`
- **语料实测**:`vectors/*.json` 的 `env.currentGasLimit` 只有两个取值 ——
  **32 份 `0x989680` + 1 份 `0x1c9c380`(`isthmus_big_block_130tx`)**。
- **实测现象(第一半)**:**恰好 1 例翻红** ——
  `EngineNewPayloadGate.AllThirtyThreeGoldenVectors`,失败 SCOPED_TRACE 唯一指向
  `isthmus_big_block_130tx`,两条断言:
  `rebuildOpEthHeader().encode() != golden.encodedHeaderHex`(`:893`)与
  `expected VALID, validationError=blockHash does not match the reconstructed block header`(`:915`)。
  其余 238 例全绿。
- **实测现象(第二半)**:原报告说"删除 `isthmus_big_block_130tx` 后再跑"。
  **删除向量文件会连带打红语料清单用例**(`SHA256SUMS` 39/39 + 文件集合双向断言),
  所以我改用**等价且不触碰语料**的做法:在 `AllThirtyThreeGoldenVectors` 的循环里
  临时 `continue` 掉该 id(`ASSERT_EQ(ids.size(), 33U)` 仍然成立)。
  结果:**239/239 全绿**。
- **结论**:**证实**。`gasLimit` 字段的全部判别力**集中在一份向量上**,
  该向量一旦被删/改,整个 `gasLimit` 映射即刻零守护。

---

## 5. P3 · 存储与生命周期

两条都是**只加探针,不改生产代码**(`Storage2LedgerTest.cpp` 末尾临时追加),
探针以"缺陷不存在"为断言,**红 = 缺陷存在**。

### P3 I-1 零值槽被算作"有存储"

- **来源**:`wb-review-p3-report.md` I-1
- **改了什么**:`Account(storage, 0x01_address, false).create()` 后,用
  `storage2::writeOne(storage, StateKey{tableNameOf(addr), <32 字节键>}, Entry{32 个 0 字节})`
  **绕过 `applyDiff`** 直写一行零值槽(模拟通用 FISCO 执行器 `HostContext::set` 的写法),
  再读 `bridge.get_account(addr)->has_storage`。
- **实测现象**:`PROBE[P3-I1] has_storage=1 poisoned=0`
- **结论**:**证实**。`probeHasStorage` 对零值槽**静默答 true 且不毒旗**,
  与同文件 `fetchAllStorage` 对零值槽**抛异常**的判据完全相反 ——
  报告描述的不对称属实。E-b 世界今日不可达,编排接入后即 CREATE2 碰撞判定分歧。

### P3 I-2 `/apps/` 下非 20 字节 hex 表名 ⇒ 毒旗

- **来源**:`wb-review-p3-report.md` I-2
- **改了什么**:往 `SYS_TABLES` 插一行键 `/apps/HelloWorld`(值为任意标记串),
  再调 `bcos::evm::stateRootOf(bridge)`。
- **实测现象**:

  ```
  PROBE[P3-I2] threw=0 root=a6b5d50f…39b71 poisoned=1 firstError=std::exception
  ```

- **结论**:**证实**。`stateRootOf` **不抛**、返回一个(错误的)根,但 `poisoned()==true` ——
  正是 `OpSchedulerImpl.h:888/893` 会转成 `OpStorageError` → engine **-32603** 的形态,
  且**每块必走**。
- **一条原报告没提到、但更糟的实测细节**:`firstError()` 的内容是字面的 **`"std::exception"`**,
  不是 `boost::algorithm::non_hex_input` 的任何有用信息
  (boost 的该异常类型没有覆写 `what()`,落到 `std::exception::what()` 的默认串)。
  也就是说,一条既有链上的运维人员看到的是"每个块 -32603,原因:`std::exception`" ——
  **零定位能力**。这与 P5 的 I-5(信息湮灭点)是同一类问题,建议合并处置。

### P3 I-3(性能:`visitAccountsImpl` 无谓物化)

- **无法执行 / 不适用**。该 `[需验证]` 项写的是"**改完之后**跑 standalone 131 例即可"——
  它是对一个**尚未落地的修法**的验收步骤,而不是一个可以验证既有论断的实验。
  本次不实现修法,故不执行;记录在案。

---

## 6. P1 · 共识语义

三条,前两条只加探针,C3 需要一行生产注入。

### P1 C2 pre-Isthmus 块被判 INVALID 而非 -38005

- **来源**:`wb-review-p1-report.md` C2(第 230 行 `[需验证]`)
- **改了什么**:`EngineOpBranchTest.cpp` 末尾临时加探针 ——
  `makeOpRequest({}, kPreIsthmusTimestamp, …)`(时间戳 999 < `kIsthmusTime`=1000),
  把 `withdrawalsRoot` 置 `std::nullopt`(Holocene 块的合法形态),投 `newPayload(request, 3)`。
  (与原报告"把 `isthmusTime` 改大"等价,且不用动 fixture。)
- **实测现象**:

  ```
  PROBE[P1-C2] status=1 (Invalid=1) validationError=withdrawalsRoot is required on the OP path (Isthmus+)
                latestValidHash=<none>
  ```

- **结论**:**证实**,连 `validationError` 文本都与原报告预测**逐字一致**。
  step 1 的闸("fork 与版本必须一致")放行了 pre-Isthmus + V3,随后 Isthmus-only 的
  `withdrawalsRoot` 静态校验把一条**完全合法的 Holocene 块**判成 INVALID。

### P1 C3 本地 `bad_alloc` 被重分类成共识否决

- **来源**:`wb-review-p1-report.md` C3(第 277 行 `[需验证]`)
- **改了什么**:`OpSchedulerImpl.h` 的 `executeOpBlock` Step 3+4 的 `try` 内**第一行**插
  `throw std::bad_alloc{};`(即报告写的"step 3 前插一行")。
- **实测现象**(`EngineNewPayloadGate.IsthmusSingleTransfer`):

  ```
  isthmus_transfer_basic: expected VALID,
    validationError=OP block execution rejected the payload: std::bad_alloc
  ```

  状态码实测为 `1` = `Invalid`(gtest 打印 `Which is: 1-byte object <01>` vs 期望 `<00>`=Valid)。
- **结论**:**证实**。一次纯本地的内存耗尽产出的是
  **INVALID + latestValidHash = parentHash**,而不是 -32603 ——
  engine 层的两道 `catch(...)` → -32603 防线对这条路径确实是死代码
  (`OpSchedulerImpl` 在更内层已把它改写成 `OpConsensusError`)。

### P1 I1 FCU 零 safe/finalized 哈希 ⇒ SYNCING

- **来源**:`wb-review-p1-report.md` I1(第 316 行 `[需验证]`)
- **改了什么**:`EngineOpBranchTest.cpp` 末尾临时加探针 ——
  `headBlockHash` = 已登记块,`safeBlockHash = finalizedBlockHash = bcos::h256{}`,
  `payloadAttributes = nullptr`,调 `updateForkchoice(..., 3)`。
- **实测现象**:

  ```
  PROBE[P1-I1] status=2 (Valid=0, Syncing=2) latestValidHash=<none> trackedSafe=<none>
  ```

- **结论**:**证实**,与原报告预测一致。而且**跟踪态也没推进**
  (`getSafeBlockNumber()` 仍是 `<none>`)—— 这正是"下一轮 CL 还发同样的零哈希 → 永久停滞"
  的机制,原报告推断的这一半也一并坐实。

---

## 7. P4 · 代码组织

### P4 §3.1 `OpBlockCommitments` 加成员 ⇒ engine 比对链静默漏比(含绊线验证)

- **来源**:`wb-review-p4-report.md` §3.1
- **实验 A(证明漏洞存在)**:在 `OpEngineSeam.h:121` 后加 `bcos::h256 probe;` 成员,
  **不加**任何绊线 → **编译通过,239/239 全绿**。
  ⇒ **证实**:比对链对聚合元数变化零感知。
- **实验 B(证明修法可行)**:保留 `probe` 成员,在 `EngineServiceImpl.h:1050` 之后加
  报告提议的结构化绑定绊线(8 个名字)→ **编译失败**:

  ```
  engine/bcos-engine/EngineServiceImpl.h:1052:21: error:
    type 'const OpBlockCommitments' decomposes into 9 elements, but only 8 names were provided
  ```

  (报告预测的文本是 `cannot decompose ... into 8 names`,clang 21 的实际措辞是
  `decomposes into 9 elements, but only 8 names were provided` —— 语义完全一致,仅措辞差异。)
- **实验 C(证明修法无副作用)**:去掉 `probe` 成员、**保留**绊线 →
  **239/239 全绿 + `test-bcos-engine` 全绿**。特别地,`EngineOpBranchTest.cpp:375` 的
  `static_assert(!GenericEngineService::c_opMode)` 与通用组合根的实例化**都没有受影响**,
  印证了"依赖类型上的结构化绑定在模板里合法,实例化期才解析,不破坏库纯净"这条论证。
- **结论**:**三条全部证实**。这是本次验证里**最便宜且已实测可落地**的修法(1 行 + 8 个名字)。

### P4 §3.3 I-1 concept 的嵌套 `requires` 在通用组合根上是软失败还是硬错

这是派单单独点名的一条,做了两层验证。

- **第一层:最小 TU(语言层面)**。scratchpad `p4_concept.cpp`,
  用 `OpLike`(有全部成员)/ `GenericLike`(一个都没有)两个 stand-in,
  concept 同时含
  (i) 形参 `typename S::BlockEnv const& env` / `typename S::ExecuteResult const& r`,
  (ii) 嵌套 `requires std::derived_from<typename S::ConsensusError, std::exception>;` ×2。
  `c++ -std=c++20 -fsyntax-only` → **exit 0**,即
  `static_assert(OpSchedulerSeam<OpLike>)` 与 `static_assert(!OpSchedulerSeam<GenericLike>)`
  **同时成立** ⇒ **软失败**。
- **第二层:真类型(工程层面,更有说服力)**。在 `EngineOpBranchTest.cpp` 里临时落地
  P4 提议的 `OpSchedulerSeam`(**逐字包含**那两条嵌套 `requires`,并把
  `BlockEnv` 的 9 个字段全部展开),对**真实的**
  `OpScheduler`(= `OpSchedulerImpl<ViewType>`)与
  `bcos::scheduler_v1::SchedulerSerialImpl` 分别求值:

  ```cpp
  static_assert(OpSchedulerSeam<OpScheduler>);
  static_assert(!OpSchedulerSeam<bcos::scheduler_v1::SchedulerSerialImpl>);
  static_assert(EngineLike<OpScheduler>::c_opMode);
  static_assert(!EngineLike<bcos::scheduler_v1::SchedulerSerialImpl>::c_opMode);
  ```

  **四条 `static_assert` 全部通过,编译零错误,探针用例绿。**
- **结论**:**证实软失败**。P4 §3.3 方案**不需要退化**为
  「探针保持现状 + `if constexpr` 内首行 `static_assert`」——完整版可以直接驱动探针本身。
  (退化版也在最小 TU 里一并试编过,同样编译通过,可作为备选。)
  这条与 `c0288b8b0` 踩过的坑**不是同一类**:那次的失败是 OP 依赖名进了**成员函数签名**
  (签名在类模板被命名时就急切实例化,`if constexpr` 救不了);
  而 concept 的 `requires` 表达式内部是替换失败友好的,两者的求值时机不同。

### P4 §7.1 CMake 护栏(「编入源码 vs 链 engine 库二选一」)

- **来源**:`wb-review-p4-report.md` §7.1
- **实验 A(护栏无假阳性)**:只加报告提议的 4 行 `get_target_property` + `IN_LIST` + `FATAL_ERROR`,
  **不加**违规 link → 在**全新目录** `scratchpad/cfgA` 上 `cmake -B … -S .` → **exit 0,配置成功**。
- **实验 B(护栏真会响)**:再加一行
  `target_link_libraries(bcos-evm-opstack-tests PRIVATE engine)` → 全新目录 `cfgB` →
  **exit 1**:

  ```
  CMake Error at bcos-evm/test/CMakeLists.txt:156 (message):
    bcos-evm-opstack-tests linked `engine` AND compiles in EngineServiceImpl.cpp
  ```

- **实验 C(证明"今天确实静默"这个前提)**:去掉护栏、**保留**违规 link,
  在 in-tree `build/` 上 reconfigure + 构建 → **链接成功、239/239 全绿**,
  链接器只给了一条与本议题无关的
  `ld: warning: ignoring duplicate libraries: …` 提示,**没有任何 duplicate symbol 错误**。
  ⇒ 注释里"会静默链过、失败形态是无人察觉的陈旧定义"这条论断**实测属实**,
  §7.1 提议的 4 行护栏是可落地的可执行断言。
- **结论**:**三条全部证实**。
