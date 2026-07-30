# 全分支复审 · 视角 5 报告(测试有效性、文档一致性与可运维性)

分支 `feat-op-validator-loop` @ HEAD `2dfe9a13e`,BASE `42e62fcef`。
**本报告全程只读**:未修改任何源码/测试/金值,未执行 `cmake --build`,未运行任何测试目标。
所有需要构建才能坐实的论断一律标 `[需验证]` 并给出最小实验步骤,交协调者事后统一执行。

---

## 0. 一句话结论

**这 239 个绿色测试证明的东西比它们看上去少一层,但少的那一层不是随机的——它精确地落在
"整个语料里取值恒定的字段"和"金值 gate 结构上到不了的畸形输入"这两块上。**
gate 本身**不是**同义反复(我逐条核对过四条独立来源交叉断言,见 §1),前几批复审已经把
`number`/`timestamp`/`baseFeePerGas` 这一类问题识别并部分修好了;但**同一类问题还剩两个字段
(`feeRecipient`/`prevRandao`)没人管**,而 `validateOpNewPayloadRequest` 的 14 条拒绝里
**有 8 条从未被任何用例触碰**,其中 4 条一旦回归就是 UB 而不是错判。

未发现 Critical(无活体正确性缺陷)。

---

## 1. 先说 gate 到底证明了什么(必要的正面结论)

我逐条核对了 `EngineNewPayloadGateTest.cpp:861-971` 的五条断言的**来源独立性**,结论是
gate 确实不是自洽比对:

| 断言 | 位置 | 左侧 | 右侧 | 独立? |
|---|---|---|---|---|
| `computeTxRoot(raw) == golden.transactionsRoot` | `:878-879` | FB 实现 | op-geth `header.TxHash` | ✅ |
| `rebuildOpEthHeader().encode() == golden.encodedHeaderHex` | `:884-896` | FB 实现 | op-geth `rlp.EncodeToBytes(header)` | ✅ |
| `payload.blockHash == golden.blockHash` | `:901` | 输入即 golden | op-geth `block.Hash()` | ✅(反自算护栏) |
| 已登记 header RLP == `goldenEncoded` | `:928-930` | FB 写侧 | op-geth | ✅ |
| Σ(已存回执 gasUsed) == `payload.gasUsed` | `:969` | FB 执行+映射 | `_op_expected.header.gasUsed`(op-geth) | ✅ |

`makeGoldenRequest`(`:516-550`)确实只从 `vectors/<id>.json` 的 `env`/`_op_expected.header`
与 `golden/<id>.golden.json` 取值,**没有任何一处调用 `rebuildOpEthHeader` 再回填**。
`resealBlockHash`(`:564-567`)只在变异矩阵里用,33 条向量与链式对都不经过它。**这一条我确认属实。**

所以下面所有发现都不是"gate 是假的",而是"gate 的判别力在特定字段上恰好为零"。

---

## 2. 覆盖清点(清单法,不是印象)

### 2.1 `validateOpNewPayloadRequest`(`EngineServiceImpl.cpp:185-302`)—— 14 条拒绝

| # | 行 | 消息 | 覆盖用例 | 删掉这条生产代码后的后果 |
|---|---|---|---|---|
| 1 | `:200-206` | `executionPayload.rawTransactions is required on the OP path` | **无** | `EngineServiceImpl.h:771` `*payload.rawTransactions` → **空 optional 解引用 = UB** |
| 2a | `:207-210` | `withdrawals must be present and empty…`(**缺失**腿) | **无** | 走到 `payload.withdrawals` 不再被读,无直接后果,但静默放行畸形请求 |
| 2b | 同上(**非空**腿) | 同上 | `EngineNewPayloadMutation.NonEmptyWithdrawalsIsInvalid` | ✅ |
| 3 | `:211-214` | `expectedBlobVersionedHashes must be an empty array…` | `…NonEmptyExpectedBlobVersionedHashesIsInvalid` | ✅ |
| 4 | `:215-218` | `parentBeaconBlockRoot must be a 32-byte hash for newPayloadV4` | **无** | `EngineServiceImpl.h:773` / `:980` `*request.parentBeaconBlockRoot` → **UB** |
| 5 | `:219-225` | `withdrawalsRoot is required on the OP path (Isthmus+)` | **无** | `EngineServiceImpl.cpp:335` `.value()` → `bad_optional_access` → 被分类屏障接住 → **-32603 而非 INVALID**(判决类别改变) |
| 6a | `:226-229` | `excessBlobGas must be present and zero…`(**缺失**腿) | **无** | 同上类 |
| 6b | 同上(**非零**腿) | 同上 | `…NonZeroExcessBlobGasIsInvalid` | ✅ |
| 7 | `:230-233` | `blobGasUsed must be present on the OP path` | **无** | 同文件 `:234` 紧接着 `*payload.blobGasUsed` → **UB** |
| 8 | `:234-241` | `blobGasUsed must be zero before Jovian (OP Isthmus)` | gate #6 + `EngineOpBranch.NonZeroBlobGasUsedIsInvalidUnderIsthmus` | ✅✅ |
| 9 | `:252-255` | `blockNumber must not be negative` | **无** | 被 step 3 的父子号连续性遮蔽(负号只能配负父号),实际不可达,但零守护 |
| 10 | `:256-259` | `gasLimit exceeds the uint64 range…` | **无** | 同文件 `:269` `*narrowU256ToU64(...)` → **UB** |
| 11 | `:269-273` | `gasLimit exceeds the maximum block gas limit (2^63-1)` | `EngineOpBranch.GasLimitAboveInt64MaxIsInvalid` | ✅ |
| 12 | `:280-283` | `extraData exceeds the 32-byte maximum` | `EngineOpBranch.ExtraDataAboveThirtyTwoBytesIsInvalid`(含 32B 边界正例) | ✅ |
| 13 | `:284-287` | `gasUsed exceeds the uint64 range…` | **无** | `.cpp:329` `.value()` → `bad_optional_access` → -32603 而非 INVALID |
| 14 | `:288-291` | `blobGasUsed exceeds the uint64 range…` | **无** | `.cpp:336` `.value()` → 同上 |

**8 条零覆盖 / 2 条半覆盖 / 14 条。**(判定方式:对每条消息全文在 `bcos-evm/test/` 下 `grep -rl`;
8 条返回空。)

### 2.2 OP 分支主流程(`EngineServiceImpl.h:663-1253`)

| 检查 | 行 | 覆盖 |
|---|---|---|
| step 1 -38005 双向 | `:692-702` | gate #1(两向)+ `EngineOpBranch.TimestampVersionGateRejectsMismatch` ✅ |
| step 2 blockHash 失配 | `:774-778` | gate #2/#7 ✅ |
| step 3 parentKnown → SYNCING | `:799-802` | gate #9/#10 + 链式 ✅ |
| step 3 父子号连续性 | `:816-820` | `EngineOpBranch.NonConsecutiveBlockNumberIsInvalid` ✅ |
| step 3a 父头**解码失败** → -32603 | `:876-885` | **无**(需要往 `s_eth_block_header` 塞坏字节) |
| step 3a timestamp 单调 | `:886-890` | 3 桩例 + 链式(唯一金值锚)✅ |
| step 3a 父头缺失 → SKIP | `:869-871` | `EngineOpBranch.MissingParentHeaderSkipsTimestampCheck` ✅ |
| step 3b 已知块短路 | `:915-920` | 桩(计次)+ gate(真执行)✅ |
| step 3c 非链尾 → -32603 | `:956-964` | `EngineOpBranch.NonTipParentIsRefusedExplicitly` ✅ |
| step 4 ConsensusError → INVALID | `:994-999` | 2 例(解码腿 + `catch(...)` 重分类腿)✅ |
| step 4 StorageError → -32603 | `:1000-1007` | 桩 + gate 存储布局注入 ✅ |
| step 4 `catch(...)` → -32603 | `:1008-1042` | `UnclassifiedExecutionEscapeIsInternalErrorNotEscaping` ✅ |
| step 5 八条比对 | `:1052-1093` | `EachComparisonSurfaceFieldMismatchIsNamed` 8 行 + gate #8.1-8.6 ✅✅ |
| step 6 收据数不等 / 空收据 | `:1213-1227` | 2 例(含反例标识)✅ |
| 分类屏障 `catch(...)` | `:733-744` | 3 例(三个窗口)✅ 但见 I-5 |

**唯一无覆盖项:step 3a 的"存储里的父头解码失败 → -32603"(`:876-885`)。** 这是 B4-1 打通
`s_eth_block_header` 读路时唯一没配用例的分支。构造成本很低(fixture 直接往
`s_eth_block_header["0"]` 写 `"garbage"`,再投一个 parent 已登记的块),失效场景明确:
若该分支被误写成 `co_return makeStatus(Invalid, ...)`,本节点会因**自己存储损坏**而对一个好块投
INVALID——正是 §4.3 明令禁止的那类错误。

### 2.3 `OpSchedulerImpl.h` 解码器拒绝分支(26 处 `throw OpConsensusError`)

已覆盖(11):`gas limit exceeds int64_t range`(deposit/eip1559/setcode 三 call site)、
`chain id mismatch`(×2)、`invalid y parity`(×2,含 2 与 256)、`EIP-2 malleable signature`、
`non-canonical leading zero`、`too wide for uint64`、`wrong length for address`、
`wrong length for hash`、`invalid boolean value`、`unsupported tx type byte`(仅类型断言)。

**未覆盖(以下每条都能用一条 `DepositFieldEncodings` 变体在 ~5 行内构造)**:
- `:306-307` `expected scalar (…), got list`
- `:309-310` `scalar too wide for uint256`(u64 腿已覆盖,u256 腿没有)
- `:327-328` `expected string (…), got list`
- `:428` `expected list`
- `:436-438` `unexpected trailing bytes in …`(**8 个 call site 全部零覆盖**)
- `:412` `input too short for 'to'`
- `:509` `sender ecrecover failed`
- `:663` `empty envelope`
- `:158-159` `field exceeds uint64_t range: OpBlockEnv::baseFeePerGas` ← **见 I-8,这条不只是没测**

`expectExhausted` 的零覆盖值得单独点名:它是**唯一**阻止"envelope 尾部追加垃圾字节"的防线,
而尾部垃圾会改变 `computeOpTxRoot` 的叶子字节(它哈希原始线上字节)却不改变解析结果——即
§6.4 (n) 记的那条"`computeOpTxRoot` ↔ `DeriveSha` 等价性依赖解码器严格性"隐式契约。
契约的守护者本身零守护。

---

## 3. 金值语料的结构性盲区(自己扫的取值集合)

扫描方式:对 `t8n/vectors/*.json`(33)与 `golden/engine/chained/chain{A,B}.golden.json`(2)
逐文件 `jq` 取值后 `sort -u`。**下表是实测,不是转述。**

### 3.1 `env` 八字段(33 条孤立向量)

| 字段 | 不同取值数 | 取值集合 |
|---|---|---|
| `currentNumber` | **1** | `0x1` |
| `currentTimestamp` | **1** | `0x3f2` |
| `currentBaseFee` | **1** | `0x3a699d00` |
| `currentRandom` | **1** | `0x0` |
| `currentCoinbase` | **1** | `0x4200000000000000000000000000000000000011` |
| `currentGasLimit` | **2** | `0x989680`(×32)、`0x1c9c380`(×1,仅 `isthmus_big_block_130tx`) |
| `parentBeaconBlockRoot` | **1** | `0x0b0b…0b` |
| `parentHash` | 33 | 逐条不同 |

### 3.2 加上链式对后(全 35 份 golden 文档)

| 字段 | 全 35 份的取值数 | 唯一支点 |
|---|---|---|
| `number` | 2(`1`/`2`) | 仅 `chainB` |
| `timestamp` | 2(`0x3f2`/`0x3fc`) | 仅 `chainB` |
| `baseFeePerGas` | 2(`0x3a699d00`/`0x39467ac7`) | 仅 `chainB` |
| `gasLimit` | 2(`0x989680`/`0x1c9c380`) | **仅 `isthmus_big_block_130tx`** |
| `parentBeaconBlockRoot` | 2(`0x0b…`/`0x0c…`) | 链式对 |
| **`prevRandao`(`currentRandom`)** | **1(全零)** | **无** |
| **`feeRecipient`(`currentCoinbase`)** | **1** | **无** |

### 3.3 `_op_expected.header` 七字段(33 条)

| 字段 | 不同取值数 | 唯一支点 |
|---|---|---|
| `stateRoot` | 19 | — |
| `receiptsRoot` | 28 | — |
| `gasUsed` | 24 | — |
| `logsBloom` | **2** | 非零者仅 `isthmus_contract_logs` / `jovian_contract_logs` |
| `withdrawalsRoot` | **2** | 非空根者仅 `isthmus_message_passer_write` / `jovian_message_passer_write` |
| `blobGasUsed` | **2** | 非零者仅 `jovian_da_mix`(`0x90ec0`) |
| `requestsHash` | 1 | 协议常量,设计如此 |

`golden.extraData`:2 种(Isthmus 9B `0x000000003200000006` ×16 / Jovian 17B
`0x0100000032000000060000000000000000` ×17)。

### 3.4 结论:21 个头字段的判别力分档

- **协议常量,零判别力属正常**(4):`ommersHash`、`difficulty`、`nonce`、`requestsHash`、
  `excessBlobGas`(生产端硬编码 0,静态校验已钉死);
- **判别力充分**(5):`parentHash`、`stateRoot`、`transactionsRoot`、`receiptsRoot`、`gasUsed`;
- **单支点(掉一条向量即归零)**(6):`number`/`timestamp`/`baseFeePerGas`(→ 链式对)、
  `gasLimit`(→ `isthmus_big_block_130tx`)、`blobGasUsed`(→ `jovian_da_mix`)、
  `logsBloom`/`withdrawalsRoot`(→ 各 2 条);
- **判别力恒为零**(2):**`feeRecipient`、`prevRandao`**。

---

## 4. 发现

### Important

---

#### I-1 `feeRecipient` / `prevRandao` 的 payload→header 映射判别力恒为零(**新发现,同类问题批 3 只修了 3 个字段就停了**)

**证据**:§3.2 的实测取值集合——`currentCoinbase` 与 `currentRandom` 在**全部 35 份**金值文档里
各只有一个取值。

**为什么现有三道防线都拦不住**:
1. **gate**(`EngineNewPayloadGateTest.cpp:884-896`)只断言 `encode() == golden.encodedHeaderHex`。
   若 `EngineServiceImpl.cpp:321` 的 `.feeRecipient = payload.feeRecipient` 改成硬编码
   `0x42…0011`、`:333` 的 `.prevRandao = payload.prevRandao` 改成 `bcos::h256{}`,
   35 份文档的输入值恰好就是那两个常量 → **encode() 逐字节不变 → 33/33 + 链式全绿**。
2. **`EngineOpBranchTest.cpp`** 自己在 `:428-433` 明文声明它拦不住任何映射错误
   (`sealWithBlockHash` 与 `rebuiltHeader` 两侧都调同一个 `rebuildOpEthHeader`,一起移动)。
3. **`EthBlockHeaderTest.cpp`** 的 `buildHeader` 直接从 `env` 组装 `EthBlockHeader`,
   **根本不经过 `rebuildOpEthHeader`** → 与该映射无关。

**`prevRandao` 更严重,因为执行腿也零覆盖**:`EngineServiceImpl.h:977` 把
`payload.prevRandao` 填进 `BlockEnv`,`OpSchedulerImpl.h:240` 转成
`BlockInfo::prev_randao`。我扫过 `t8n/generator/cases.go` 的 33 个 case,
**没有任何一个合约读 PREVRANDAO(0x44)**(`fee_env_observer` 观测的是
GASPRICE/BASEFEE/SELFBALANCE,见 `cases.go:541-548` 与 case 的 `_info.description`)。
所以把 `.prevRandao = payload.prevRandao` 换成 `.prevRandao = bcos::h256{}`
在**头腿和执行腿同时**不会翻红。

**生产后果**:真实 OP 块的 `prevRandao` 是 L1 的 `mixDigest`,几乎必然非零。头腿回归 →
本节点对**每一个**块答 `blockHash does not match the reconstructed block header`(响亮但全崩);
执行腿回归 → 任何调用 PREVRANDAO 的合约(随机数是常见用法)算出不同结果 → stateRoot 失配 →
**对 op-geth 接受的块投 INVALID**,即共识分歧。`feeRecipient` 的头腿回归在 OP 主网上恰好**不会**
暴露(常量正是生产值 SequencerFeeVault),要到别的链才炸。

**已记账?** 否。§7.5 rev.3.2 与 gate 测试 `:1137-1150` 的 B3-4 段落只点了
`number`/`timestamp`/`baseFeePerGas` 三条,并且给了 DO-NOT-DELETE 横幅;
`feeRecipient`/`prevRandao` 处在完全相同的位置却无人提及。

**`[需验证]` 最小实验(两次,各一行改动)**:
- (a) `engine/bcos-engine/EngineServiceImpl.cpp:333` → `.prevRandao = bcos::h256{},`
- (b) `engine/bcos-engine/EngineServiceImpl.cpp:321` →
  `.feeRecipient = bcos::Address{std::string{"0x4200000000000000000000000000000000000011"}},`
- 各自 `cmake --build` 后跑 `bcos-evm-opstack-tests` 全量 + `test-bcos-engine`。
- **预期(我的论断)**:两次都 **239/239 全绿**。若任一翻红,本条降级。

**建议修法(比再加金值便宜且覆盖面更大)**:加一条**不需要任何金值**的字段敏感性用例——
对 `rebuildOpEthHeader` 的 17 个 payload 来源字段逐个扰动,断言
`encode()` **必然改变**:

```
TEST(EngineOpBranch, RebuildOpEthHeaderIsSensitiveToEveryPayloadSourcedField)
// baseline = rebuildOpEthHeader(p, txRoot, pbbr).encode();
// for each of the 17 fields: perturb p (or txRoot/pbbr), EXPECT_NE(encode(), baseline)
```

它一次性关掉整类问题(含 `number`/`timestamp`/`gasLimit` 的单支点脆弱性),
且**不替代**金值 gate——它证明"字段被读了",gate 证明"读到了正确的位置"。两者互补。

---

#### I-2 `validateOpNewPayloadRequest` 14 条拒绝里 8 条零覆盖,其中 4 条回归后是 UB 而非错判

**证据**:§2.1 全表。判定方式是对每条消息全文在 `bcos-evm/test/` 下 `grep -rl`,8 条返回空。

**为什么这块最不该空着**:共享上下文自己写明"33 条金值向量**结构上绕过**解码器严格性、
签名校验、状态机三类失败模式"。静态校验层是**第四类**同样被绕过的东西——一条自洽的金值
payload 永远不会缺 `withdrawalsRoot`、不会 `gasUsed > 2^64`。这 8 条恰恰是真实 op-node
(或攻击者)最先碰到的面。

**升级为"UB 类"的 4 条**(注意区别:不是"判错",是"未定义行为"):
- `:200-206` 删 → `EngineServiceImpl.h:771` `*payload.rawTransactions`;
- `:215-218` 删 → `EngineServiceImpl.h:773` / `:980` `*request.parentBeaconBlockRoot`;
- `:230-233` 删 → `EngineServiceImpl.cpp:234` `*payload.blobGasUsed`;
- `:256-259` 删 → `EngineServiceImpl.cpp:269` `*narrowU256ToU64(payload.gasLimit)`。

四处都是**空 optional 的 `operator*`**,不是 `.value()`——不抛异常,分类屏障接不住,
在 release 构建里是静默读越界。这些前置条件今天只由**注释**维系
(`EngineServiceImpl.cpp:313-317` 的 "Precondition:" 段落),按 §11 rev.3.3 的通则
"写下契约就必须给出会翻红的断言",这四条契约当前处于零守护状态。

**`[需验证]`**:逐条注释掉上述 8 条 `return std::string(...)`,重建后跑全量。
**预期:8 次全部 239/239 全绿**(UB 的 4 条可能表现为随机通过/崩溃,视分配器而定;
建议这 4 条用 ASan 构建跑)。

**建议**:8 条各补一例即可(每条 6-8 行,复用 `EngineOpBranchTest` 的
`prepareValidScenario` + `sealWithBlockHash`),按本文件既有纪律用 `expectValidationError` 精确匹配。

---

#### I-3 链式对——全套最独特的覆盖——却是 provenance 最弱的产物

**事实**(全部实测):
- `GoldenCorpusProvenanceIsPinned` 的 (b) 半(`EngineNewPayloadGateTest.cpp:1041-1052`)
  只遍历 `loadManifestIds()` 的 **33** 个 id,读 `vectors/<id>.json._op_test_vectors.generator_commit`。
  **链式对不在 `manifest.txt` 的 id 列表里**(`manifest.txt:42-49` 明写"NOT part of the 33-set…
  not listed by basename here"),因此从不被这半检查触及。
- `jq 'has("_op_test_vectors")' chained/chain{A,B}.golden.json` → **`false` / `false`**。
- 生成器本身放弃了这个信息:`t8n/generator/main.go:900`
  `_ = opGethCommit // recorded in golden/engine/README.md's generation record, not per-file`。

**后果**:链式对的唯一机内保护是 `SHA256SUMS`(我实跑 `shasum -a 256 -c SHA256SUMS`,
39/39 OK)。而 `SHA256SUMS` 是**事后冻结**——`git log` 显示 goldens 在 `12ed60bf3` 落地、
`SHA256SUMS` 在 `f31c537da` 才加,是对**已入库字节**取的和。它能证明"从那一刻起没被改过",
证明不了"来自 op-geth"。33 条还有第二根独立绳子(`vectors/` 是零触碰目录且逐条带
`generator_commit`);链式对**一根绳子都没有第二根**。

**而链式对承载的东西是全套里最不可替代的**(两处代码注释自己说的):
- `EngineNewPayloadGateTest.cpp:1137-1150`:`number`/`timestamp`/`baseFeePerGas` 的**唯一**支点;
- 同文件 `:1107-1116`:timestamp 单调校验的**唯一** op-geth 金值锚
  ("A counter instrumented over the whole binary shows the check reaching a real comparison
  exactly 4 times… and **this test, which is the only place it runs against op-geth-generated
  timestamps**")。

**即最独特的覆盖,挂在最弱的钉子上。**

**建议**(改动很小):`chainedBlockOutput`(`main.go:883-897`)加一个
`OpTestVectors *provenance \`json:"_op_test_vectors"\`` 字段,`runChainPair` 停止丢弃
`opGethCommit`;重新生成链式对(**必须在 pinned op-geth 下**,README 仪式原样)、刷新
`SHA256SUMS`;`GoldenCorpusProvenanceIsPinned` 的 (b) 半把 `chained/chainA`、`chained/chainB`
并入循环。

---

#### I-4 `gasLimit` 与 `number`/`timestamp`/`baseFeePerGas` 处境完全相同,却没拿到 DO-NOT-DELETE 横幅

批 3 给链式用例加了显式的 "**DO NOT DELETE OR MERGE AWAY**" 横幅
(`EngineNewPayloadGateTest.cpp:1107` / `:1137`),因为那三个字段的唯一支点在那里。

`gasLimit` 的唯一支点是 **`isthmus_big_block_130tx`**(§3.2 实测:33 条里只有它是
`0x1c9c380`,其余 32 条 + chainA + chainB 全是 `0x989680`)。而它是整个语料里**最大的文件
(32 KB,130 笔交易)**——也就是"套件跑太慢,先砍这条"时的第一顺位候选。砍掉它,
`gasLimit` 的 payload→header 映射判别力**立刻归零**,而**没有任何注释会提醒动手的人**。

同理未标注的还有 `blobGasUsed`(唯一支点 `jovian_da_mix`,这条在 gate 变异 #6 的注释里
`:1436-1441` **有**说明,算已覆盖)、`logsBloom`(唯一非零支点 `*_contract_logs` 两条)、
`withdrawalsRoot`(唯一非空根支点 `*_message_passer_write` 两条)——后两组无任何标注。

**建议**:在 `manifest.txt` 里给这四组向量各加一行注释说明它是哪个字段的唯一支点;
或采纳 I-1 的字段敏感性用例,一次性把"唯一支点"这个脆弱结构整体拆掉。

---

#### I-5 分类屏障是 §6.4 (j) 那类"信息湮灭"的**第二处**,尚未记账;且它自证了这一点

§6.4 (j) 记的是 `OpSchedulerImpl.h:869-872` 的 `catch(...)` 丢弃 `e.what()`,导致
`OpBlockExecute.cpp` 的四处 throw(`:37`/`:40`/`:55`/`:81`——我逐行核对过,行号属实)
共用一条消息。

**同样的机制在 engine 层还有两处,台账上没有**:

1. **分类屏障**,`EngineServiceImpl.h:733-744`:唯一的非透传 handler 是裸 `catch (...)`,
   抛出固定文本 `"OP newPayload threw an unclassified exception outside block execution
   (validation, comparison or registration phase)"`。**消息自己列了三个阶段,却不说是哪个。**
2. **执行期兜底**,`EngineServiceImpl.h:1038-1041`:同样是裸 `catch (...)`,
   前面两个 typed handler 只覆盖 `ConsensusError`/`StorageError`,**没有 `catch (const
   std::exception&)` 这一档**。

**测试自己把湮灭演示出来了**:`StaticValidationPhaseEscapeIsInternalError`(`:1265`)、
`ComparisonPhaseEscapeIsInternalError`(`:1283`)、`RegistrationPhaseEscapeIsInternalError`
(`:1303`)——三个结构完全不同的故障(`computeTxRoot` 抛 / `commitmentsOf` 抛 /
`receipt->encode()` 抛),三条断言**字面相同**:
`EXPECT_NE(diagnostic.find("outside block execution"), npos)`。运维在生产上看到这条消息时,
拿到的信息量与这三个测试拿到的一样多:零。

**关键区别于 (j)**:RTTI 旁路只影响**跨 evmone 库边界**抛出的异常。屏障要接的东西——
`boost::bad_lexical_cast`(`lexical_cast` 在 `:1144`)、tars 编码错误、
`std::bad_optional_access`(I-2 的前置条件被违反时)、`std::bad_alloc`、storage2 的
`bcos::Error`——**全都在同一个二进制里抛,`catch (const std::exception&)` 能正常绑定**。
也就是说:**这里丢消息不是 RTTI 的锅,是少写了一个 handler。**

**修法(两行,零风险)**:在两处 `catch (...)` 之前各插一档

```cpp
catch (const std::exception& e)
{
    BOOST_THROW_EXCEPTION(OpExecutionInternalError{} << bcos::errinfo_comment{
        std::string("OP newPayload threw outside block execution "
                    "(validation, comparison or registration phase): ") + e.what()});
}
```

`catch (...)` 原样保留兜住 RTTI 旁路的那部分,**标识串不变**,现有三条测试的正例断言
`find("outside block execution")` 继续通过——即**改动不破坏任何既有断言**。
按 §11 的正/反例纪律,补测时三条各加一条"消息含自己那一层特有子串"的正例即可把三者钉开。

---

#### I-6 `decodeOneRawTx` 的不支持类型消息把十进制当十六进制打

**`OpSchedulerImpl.h:674-675`**:

```cpp
throw OpConsensusError("OpSchedulerImpl: raw tx decode: unsupported tx type byte 0x" +
                       std::to_string(static_cast<unsigned>(typeByte)));
```

`std::to_string(unsigned)` 产出**十进制**。具体失效场景:

| 线上收到的类型字节 | 运维看到的消息 | 真相 |
|---|---|---|
| `0x01`(EIP-2930 access-list tx) | `type byte 0x1` | 巧合正确 |
| `0x03`(EIP-4844 blob tx) | `type byte 0x3` | 巧合正确 |
| `0x7f` | `type byte 0x127` | **错**,而 `0x127` 不是合法类型字节(>0x7f) |
| `0xff` | `type byte 0x255` | **错** |

运维拿 `0x255` 去 EIP-2718 / op-geth 里查会一无所获(类型字节按 EIP-2718 上界为 `0x7f`),
从而误判为"数据损坏"而非"收到了本实现不支持的合法交易类型"。

**为什么测试看不见**:唯一命中这条的用例
`EngineOpBranch.ConsensusErrorFromExecutionMapsToInvalid`(`:864-890`,类型字节 `0xff`)
只断言前缀 `"OP block execution rejected the payload: "`,尾部是解码器自己的 `what()`,
测试明文声明"this test does not own"(`:880-882`)。

**修法一行**:改用 `bcos::toHex`,或 `std::format("{:#04x}", typeByte)`。
顺带把该用例的断言收紧到含 `type byte 0xff`。

---

#### I-7 `baseFeePerGas ≥ 2^64` → 本实现 INVALID,op-geth 正常接受(§6.4 "唯二"之说被证伪)

**链路**:`validateOpNewPayloadRequest` 对 `baseFeePerGas` **没有任何上界检查**(§2.1 全表可见);
`rebuildOpEthHeader`(`.cpp:334`)按 u256 原样写进头,RLP 可编码,blockHash 校验通过;
`BlockEnv.baseFeePerGas`(`.h:978`)也是 u256;直到
`OpSchedulerImpl.h:238` `blk.base_fee = narrowU256ToU64(env.baseFeePerGas, "OpBlockEnv::baseFeePerGas")`
抛 `OpConsensusError`(注意:这行在 `:820`,**在 `try` 之外**,因此以 `OpConsensusError` 原样传出)
→ engine `catch (const ConsensusError&)`(`.h:994`)→ **INVALID**。

**op-geth 侧**(pinned `e8800cffe`,我实读):`beacon/engine/types.go:301-302`
只拒绝 `Sign() == -1 || BitLen() > 256`;`header.BaseFee` 是 `*big.Int`,
`vm.BlockContext.BaseFee` 也是 `*big.Int`。**`[2^64, 2^256)` 区间 op-geth 全部正常执行。**

**所以这是"op-geth 能正常处理、本实现硬拒"的第三条路径**,而 §6.4 rev.3.3 的表头明文写着
条目 (k)/(l) 是"本清单里**唯二**'op-geth 能正常处理、本实现硬拒或误答'的路径"。该论断不成立。

**严重性坦白**:真实链上 baseFee 不可能到 2^64 wei(约 1.8×10¹⁰ ETH/gas),
所以这是**理论分歧,不是实战风险**。我报它有两个理由,都不是"因为它会炸":
(a) §6.4 的"唯二"是一条会被后续读者当作完整性保证的措辞,它现在是错的;
(b) 它顺带说明"共识相关的数值上界检查"这一类还没有被系统清点过——
`narrowU256ToU64` 在执行侧的每一个 call site 都值得按同样口径过一遍。

**建议**:要么在 §6.4 把"唯二"改成"目前已识别的",并把本条作为 (t) 记入;
要么在 `validateOpNewPayloadRequest` 里补一条显式 `baseFeePerGas` 上界并给出 INVALID
理由(与 gasLimit 的处理方式对称),同时在注释里点明这是**故意窄于 op-geth**的偏离。

---

#### I-8 文档与代码不一致(两处,均在 spec 的权威段落)

**(a) §8 验收清单的数字已过期 14 例。**
spec `:480-484`("终审后回填")写 in-tree `bcos-evm-opstack-tests` **225/225**、
本闭环新增 **58** 例。实际 HEAD:`bcos-evm/test/opstack/*.cpp` 的 `^TEST` 计数为 **239**,
且 `.superpowers/sdd/.../final-batch6-report.md:235`、`final-batch6-review-report.md:266`
都明确记 **239/239**、`final-batch4-report.md:6` 记 237/237。
即批 4/6 的 +14 例从未回填到 §8。**§8 是"验收清单",是外部读者唯一会查的口径**;
它现在把交付规模低报了 14 例(相对 merge-base 的新增例数低报 25:实际 83,文档 58)。
§8.1 刚刚为报告归档立了诚实性标准,§8 自身的数字却是陈旧的。

**(b) §7.5 rev.3.2 的"子串匹配已全部移除"是假的。**
spec `:441`:「**`validationError` 断言一律精确匹配或前缀锚定**(§11 检查单末条),
子串匹配已全部移除。」
实测 `grep -n "validationError->find"`,engine 两个测试文件里**仍有两处非标识串的子串断言**:
- `EngineOpBranchTest.cpp:1411` `EXPECT_NE(status.validationError->find("timestamp"), npos)`
- `EngineOpBranchTest.cpp:1436` 同上

(`:886`、`GateTest:1318`/`:1321` 是 §11 明文认可的正/反例**标识串**,不算违规。)

**加重情节**:这两处是**批 4(B4-1)加进来的**,即写在 §11 那条规则**之后**——
规则刚立就被违反,而 spec 里那句"已全部移除"仍在原地。今天 `"timestamp"` 恰好只匹配
`"timestamp must be strictly greater than the parent's"` 一条,所以**不是活体假绿**;
但 §11 存在的全部理由就是"今天不歧义"不可持续。

**建议**:两处改 `expectValidationError(status, "timestamp must be strictly greater than the
parent's")`;§8 的数字回填到 239/239 / +83;§7.5 那句话改成"截至批 3 已全部移除;
批 4 新增两处待收敛"或直接随修改一并变真。

---

### Minor

- **M-1 七处 `file:line` 交叉引用已失效**(批 4/6 往 `EngineServiceImpl.h` 中段插了约 300 行):
  `EngineNewPayloadGateTest.cpp:587`、`EngineOpBranchTest.cpp:239` 的
  `EngineServiceImpl.h:993-995`(实际 `:1096-1097`);
  `GateTest:767`、`:876` 的 `:802-808`(实际 `:1072-1079`);
  `GateTest:1334` 的 `:664-687`(实际 `:755-778`);
  `GateTest:773` 的 `OpSchedulerImpl.h:539-559`(**实际落在 `decodeEip1559Tx` 里,完全无关**);
  `GateTest:1281` 的 `OpSchedulerImpl.h:769/787-791`(实际 `catch(...)` 在 `:845-873`)。
  §8.1 提议的 CI 检查只覆盖**路径**悬空,行号漂移不在其内。
  建议:CI 检查扩一条"`<本仓文件>:<行号>` 引用必须能在目标行附近 ±5 行找到引用文本的关键词",
  或干脆改为引用**符号名**而非行号。
  (对照:我抽查的其余行号引用**属实**——`OpBlockExecute.cpp:37/40/55`、
  `Storage2Ledger.h:467-471`、`LedgerMethods.h:233-235`、`EngineServiceImpl.cpp:200-206`、
  op-geth `consensus/beacon/consensus.go:262-264`、`beacon/engine/types.go:294-296`、
  `:301-302` 全部核对通过。)

- **M-2 §6.4 (c) 引用的 `static_assert` 形式与代码不符**:台账写
  `static_assert(!requires(NewPayloadRequest r){ r.executionRequests; })`,
  而 `EngineNewPayloadGateTest.cpp:362-381` 明文记录**这个朴素写法编译不过**
  (非依赖类型上的成员访问不是替换失败),实际用的是模板化 concept
  `HasExecutionRequestsCarrier<Request>`。台账照抄了一个不可编译的形式,
  后来者按它去别处复用会踩同一个坑。

- **M-3 生成器无任何 Go 测试,且最强的独立自检不可复现**:
  `t8n/generator/` 下无 `*_test.go`(全树 `find -name '*_test.go'` 为空);
  README `:143-155` 记的自检 (a)(35/35,用 `types.Header` 从 vector 自身字段重建后
  RLP+keccak,**不读 `encodedHeaderHex`**)靠的是 `cmd/opt8n-verify`,
  而 README `:174-179` 说该工具"discarded after use, never committed"。
  **缓解(必须同时说明,否则本条被夸大)**:C++ gate 每次运行都在做等价的字段级交叉——
  `productionHeaderOf(request).encode() == golden.encodedHeaderHex`,其中 request 的字段
  来自 `env` + `_op_expected.header`,右侧来自 op-geth。所以自检 (a) 的判别力实际上
  **已经被搬进了常跑的测试里**,丢失的只是"用 op-geth 自己的 `types.Header` 复核"这一层。
  真正的残留风险在 `buildGoldenRecord`(`main.go:694-719`)这段新增发射代码上:
  33 条向量的重生成 parity(`diff -q` 33/33 identical)只比对了**vector 输出**,
  golden 输出是新路径,**没有任何对照物**;唯一的机内断言是 `:696-698` 的
  `ExcessBlobGas == 0`。

- **M-4 `EngineOpBranch.ValidPayloadRegistersAllFourTables` 名字已过期(现为 5 张表)**——
  测试自己在 `:935-940` 解释了为何刻意不改名(批 3 报告按名引用它作为红证人),
  第 5 张表由 `RawTransactionEnvelopesAreRegisteredUnderEthTxHash`(`:1610`)覆盖。
  记录在案,不建议改动。

- **M-5 `EngineNewPayloadGate.GoldenVectorRedeliveryIsValidWithoutReExecution` 的名字强于它的断言**:
  该用例用真调度器,没有调用计次,只断言"第二次投递仍 VALID + 登记未变"。
  注释(`:1216-1234`)论证删掉短路后两条子用例都会变 INVALID,论证可信但**未被实验证实**。
  `[需验证]`:注释掉 `EngineServiceImpl.h:915-920` 的短路,重建后跑
  `--gtest_filter='EngineNewPayloadGate.GoldenVectorRedelivery*'`,
  预期两条子用例(`isthmus_transfer_basic` / `isthmus_deposit_only`)均因 INVALID 翻红。
  若只红一条,注释里"两种不同机制"的论述需要修正。

---

## 5. 可运维性:一个 INVALID 到底能定位到什么

站在运维视角把所有可能出现在 `validationError` / 异常 `errinfo_comment` 里的文本分档:

| 档 | 消息 | 定位能力 |
|---|---|---|
| **点名字段,可直接行动** | 14 条静态校验消息 + `execution result does not match payload field: <name>`(8 个字段)+ `blockNumber must be exactly one greater…` + `timestamp must be strictly greater…` + `blockHash does not match the reconstructed block header` | ✅ 好 |
| **点名阶段,带原因** | `OP block execution rejected the payload: <解码器 what()>` | ✅ 解码腿好(见 I-6 的十六进制瑕疵) |
| **只点名"一条腿"** | `…typed catch bypassed…`(§6.4 (j)) | ⚠️ 四处 throw 不可区分 |
| **只点名"三个阶段之一"** | `…unclassified exception outside block execution (validation, comparison or registration phase)` | ❌ **零定位能力**(I-5) |
| **只点名"执行期"** | `OP block execution threw an unclassified exception (typed classification bypassed)` | ❌ 同上 |
| **能力限制而非判决** | `non-tip parent not supported…` / `stored parent block header is undecodable: <err>` | ✅ 好(后者带原因) |

信息湮灭点总计 **3 处**(台账记了 1 处):
`OpSchedulerImpl.h:869-872`(已记为 (j))、`EngineServiceImpl.h:740-743`(**未记**)、
`EngineServiceImpl.h:1038-1041`(**未记**)。后两处按 I-5 各加一个
`catch (const std::exception& e)` 即可挽回绝大多数消息,因为它们要接的异常基本不跨
evmone 的 `-fno-rtti` 边界。

另有一处**次级**湮灭,属可接受设计但值得知道:`Storage2Ledger` 的毒旗只记 `firstError()`
(`Storage2Ledger.h:550-557`,"只记第一条,后续错误不覆盖"),所以一次执行里多重存储故障
只报第一条。头注已明说,不算缺陷。

---

## 6. 我核对过、结论是"没问题"的东西(避免下一位复审重复劳动)

- gate 的四条独立来源交叉断言逐条属实,`makeGoldenRequest` 无自算回填(§1);
- `shasum -a 256 -c SHA256SUMS` **39/39 OK**;`SHA256SUMS` 的文件集合与磁盘一致
  (测试 `:1019-1038` 双向断言),`golden/*.golden.json` 自 `12ed60bf3` 后再未被 git 修改;
- step 5 的**八条**比对全部有独立用例(`EachComparisonSurfaceFieldMismatchIsNamed` 8 行 +
  gate 变异 6 例),包括 §5.1 的 `blobGasUsed` / `requestsHash` 两条;
- Jovian 下 `blobGasUsed` 不存在"两边都不查"的窗口:`OpBlockSeal.cpp:73-81` 在
  `cfg.has_da_footprint` 时**必然**engage `seal.blobGasUsed`,与 Isthmus 的静态钉零互补;
- `mapOpReceipt` 存的确实是每笔自身 `gas_used`(`OpReceiptMap.h:76`),
  gate 的"回执 gasUsed 求和 == 头 gasUsed"断言成立;
- `EthBlockHeaderTest` 的 decode 反向断言是**逐字段**而非仅 re-encode 相等
  (`:248-269`),不存在"读反+写反"的对称假绿;
- `ASSERT_*` 遮蔽:`runGoldenVector` 是 void helper,`AllThirtyThreeGoldenVectors` 的
  33 次调用互不阻断;链式用例的三条 B3-4 断言已被显式前置到所有 fatal 之前(`:1151-1167`);
  **未发现被 fatal 挡住的死断言**;
- 用例间共享状态:每个用例各自 `StubFixture`/`GateFixture`/`prepareScenario` 新建存储,
  **未发现执行顺序依赖**(批 3 报告也用 3 个 shuffle 种子验过);
- §6.4 (f)/(g)/(j)/(r) 的事实与行号我逐条回源核对,**全部属实**——
  `registerOpBlock` 确实写 5 张表且刻意不写 `SYS_HASH_2_TX`(`.h:1239-1251` + `:1173-1200`),
  `LedgerMethods.h:234` 确实未判 `has_value()` 即 `txEntry->get()`,
  `OpBlockExecute.cpp` 确实是四处块级 throw。

---

## 7. 建议的处置优先级

| 序 | 条目 | 成本 | 理由 |
|---|---|---|---|
| 1 | I-5(两个 `catch (const std::exception&)`) | ~10 行 | 纯收益,不破坏任何既有断言,直接改善生产可定位性 |
| 2 | I-1 的字段敏感性用例 | ~40 行,无需金值 | 一次性关掉整类"取值恒定 ⇒ 零守护",顺带覆盖 I-4 |
| 3 | I-2 的 8 条补测 | ~60 行 | 其中 4 条守的是 UB 前置条件 |
| 4 | I-8 两处文档更正 | 分钟级 | §8 是外部读者的验收口径 |
| 5 | I-6 十六进制修正 | 1 行 | |
| 6 | I-3 链式对 provenance | 生成器 +1 字段 + 一次重生成仪式 | 需要 pinned op-geth 环境,排在能就地做的后面 |
| 7 | I-7 §6.4 "唯二"措辞 / baseFee 上界 | 分钟级 / ~5 行 | 理论分歧,但措辞错误应改 |
| 8 | M-1 行号引用 | 批量 | 建议顺手把引用改为符号名 |

---

## 8. `[需验证]` 实验清单(汇总,交协调者统一执行)

| # | 改动 | 目标 | 我的预期 |
|---|---|---|---|
| E1 | `EngineServiceImpl.cpp:333` → `.prevRandao = bcos::h256{}` | `bcos-evm-opstack-tests` 全量 | **239/239 全绿** |
| E2 | `EngineServiceImpl.cpp:321` → `.feeRecipient` 硬编码 `0x42…0011` | 同上 | **239/239 全绿** |
| E3 | 逐条注释掉 §2.1 表中 8 条无覆盖的 `return std::string(...)` | 同上(8 次) | **8 次全绿**;其中 4 条 UB 类建议用 ASan 构建 |
| E4 | 注释掉 `EngineServiceImpl.h:915-920` 已知块短路 | `--gtest_filter='EngineNewPayloadGate.GoldenVectorRedelivery*'` | **两条子用例均翻红**(若只红一条,M-5 的注释需修正) |
| E5 | 注释掉 `OpSchedulerImpl.h:436-438` `expectExhausted` 的 throw | 全量 | **全绿**(该防线零守护) |
| E6 | 删除 `isthmus_big_block_130tx` 后再跑 E-gasLimit:把 `.gasLimit = narrowU256ToU64(payload.gasLimit).value()` 换成常量 `0x989680` | 全量 | 未删时**翻红 1 例**;删后**全绿**(证明 I-4 的单支点) |

E1/E2 是本报告最核心的两条论断,建议优先执行。
