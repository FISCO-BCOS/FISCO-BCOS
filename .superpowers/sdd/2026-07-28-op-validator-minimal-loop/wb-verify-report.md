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
