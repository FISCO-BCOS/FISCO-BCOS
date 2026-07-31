# 对抗性复核 V1 报告 —— 表 1 的两条「日常流量即触发」

- 复核者:V1(可构建、独占 `build/`)
- 被复核对象:`.superpowers/sdd/2026-07-28-op-validator-minimal-loop/state-divergence-audit-report.md`
  的【分歧 1-1】与【分歧 6-2】(= 汇总表 1 的第 1、2 行)
- 我方 HEAD:`7b7e0afb3`,分支 `feat-op-validator-loop`
- op-geth 基准:`e8800cffe`(`git log -1` 实测确认:`e8800cffe feat(sdm): core/types: add
  post-exec tx encoding support (#789)`)
- 基线:`build/bcos-evm/test/bcos-evm-opstack-tests` **250/250 PASSED**(复核前实测,
  二进制时间戳 Jul 31 10:56,`--gtest_list_tests` 计数 250)
- **两条都做了真实实验**(临时用例 6 条,验证后已还原;完整源码见本文附录,可原样重放)

---

## 复核 1-1:legacy / 0x01 一律拒块

**审计原结论**:`OpSchedulerImpl.h:660-676` 的 `decodeOneRawTx` 只认 `0x7E/0x02/0x04`,
任何含 legacy 或 access-list 交易的块直接 INVALID。

### A. 我方代码:引用还对吗?读对了吗?

**对,且读对了。**

`bcos-evm/bcos-evm/engine/OpSchedulerImpl.h:660` `decodeOneRawTx`,:674 是唯一的兜底 throw:

```
674:    throw OpConsensusError("OpSchedulerImpl: raw tx decode: unsupported tx type byte 0x" +
675:                           std::to_string(static_cast<unsigned>(typeByte)));
```

调用点 `OpSchedulerImpl.h:812-815`(`executeOpBlock` step 1)对每条 raw tx 无条件调用,
在 `try` 块之外 —— 抛出后由 `engine/bcos-engine/EngineServiceImpl.h:995-999`
`catch (const typename SchedulerType::ConsensusError&)` 接住,**返回 INVALID**。
实测确认了这条完整链路(见 D)。

**审计漏看的一点(对定性有影响)**::648-659 的注释自称

> Deliberately narrow: only the three shapes processOpBlock's OpBlockTx variant understands
> (matching the t8n corpus's own three `_op_type` values ... — no legacy/access_list/blob).

括号内的**理由不成立**。`OpBlockTx::tx` 是
`std::variant<DepositTx, evmone::state::Transaction>`(`bcos-evm/bcos-evm/opstack/OpBlockExecute.h:15-19`),
而 `evmone::state::Transaction::Type`(`bcos-evm/bcos-evm/eth/state/transaction.hpp:41-61`)
本来就含 `legacy = 0` 与 `access_list = 1`;我方自己的
`validate_transaction`(`bcos-evm/bcos-evm/eth/state/state.cpp:470-475`)对这两型有**完整的
分支**(`access_list` 判 `rev < EVMC_BERLIN`,`legacy` 直接放行)。**执行层支持 5 型,只有
解码器裁到 3 型**。所以注释里"变体只懂三种"是错的,真实理由只有后半句"匹配 t8n 语料的三个
`_op_type`"。

### B. op-geth:引用还对吗?在执行路径上吗?

**对,逐行实测。** 在 `e8800cffe` 上:

- `core/types/transaction.go:191` `UnmarshalBinary`,:192-201 是 `b[0] > 0x7f` → legacy 分支;
- `core/types/transaction.go:212` `decodeTyped`,:218 `AccessListTxType`、:220 `DynamicFeeTxType`、
  :222 `BlobTxType`、:224 `SetCodeTxType`、:226 `PostExecTxType`、:228 `DepositTxType`。

在执行路径上:`beacon/engine/types.go` 的 `DecodeTransactions` 对 payload 逐条调
`UnmarshalBinary`,这正是 newPayload 的入口。我用同一份 Go 代码路径实跑(见 D)。

### C. 可达性:有没有一条真实调用路径?被别处堵住了吗?

**没有任何上游堵截。** 我把 engine 侧 OP 分支从 step 2 到 step 4 逐段读过:
`EngineServiceImpl.h` 里 `payload.rawTransactions` 只出现在四处(:771 `computeTxRoot`、
:990 `executeOpBlock`、:1206-1247 `registerOpBlock` 落表),**没有任何一处按交易类型过滤或
预校验**。step 2 的 payload 校验、step 3 的 parentKnown/连续性/链尾检查全部与交易类型无关。
`computeTxRoot`(:771,在 `executeOpBlock` 之前)对**原始字节**建 trie,不解码,所以也不会
提前拒。

### D. 触发构造:成本属实吗?写得出具体输入吗?

**属实,且比审计说的还直白 —— 我用 op-geth 真签的字节做了双向实验。**

**op-geth 侧**(`go test`,op-geth 工作树内临时包,已删):

```
=== RUN   TestRealSignedLegacyRoundTrip
    real signed legacy raw = 0xf86180018252089400000000000000000000000000000000000000de018082422d
                             a006f7ae89e2d9aead6cf3cd4c5bee214e7daf8e12f1e90a9c64254a211102486a
                             a04d55dda60976bdc2b5a6c5117e277729b78fcc77e26dcf09392bc7bed63e6101
    decoded OK: type=0 from=0x71562b71999873DB5b286dF957af199Ec94617F7
    beacon DecodeTransactions accepted 1 tx(s), type=0
--- PASS
=== RUN   TestDecodeV1Vectors
    e1808082520894...dead808082422d0101                 -> OK type=0 to=0x…dEaD gas=21000
    01e3822105808082520894...dead8080c0800101           -> OK type=1 to=0x…dEaD gas=21000
--- PASS
```

即:**一条真实签名、sender 可恢复的 legacy 交易,走 op-geth newPayload 的解码路径完全通过。**

**我方侧**(临时 gtest `V1Refute`,走真 `OpSchedulerImpl::executeOpBlock`,恶意交易置于
index 1、index 0 是合法的 L1-attributes deposit —— 沿用本文件既有的"避免被首笔闸误判"惯例):

```
[V1] legacy first byte = 0xe1, len=34,
     hex=e1808082520894000000000000000000000000000000000000dead808082422d0101
[V1] legacy -> OpConsensusError(INVALID): OpSchedulerImpl: raw tx decode: unsupported tx type byte 0x225
[V1] accesslist hex=01e3822105808082520894000000000000000000000000000000000000dead8080c0800101
[V1] 0x01 -> OpConsensusError(INVALID): OpSchedulerImpl: raw tx decode: unsupported tx type byte 0x1
[V1] real legacy -> OpConsensusError(INVALID): OpSchedulerImpl: raw tx decode: unsupported tx type byte 0x248
```

**同一串字节,op-geth 接受并恢复出 sender,我方在 step 1 直接判 INVALID。** 方向与审计一致
(我方拒绝 op-geth 接受的),触发成本为零。

### 派单必答:刻意裁剪还是遗漏?

**结论:是「只写在代码注释里的、理由不成立的范围裁剪」——既不是干净的"已声明偏离",也不是
纯粹的遗漏。三条证据:**

1. **spec 无明文裁定。** 通读 `docs/superpowers/specs/2026-07-28-op-validator-minimal-loop-design.md`
   (58KB)与 `2026-07-24-opstack-block-execution-port-design.md`:检索 `legacy` / `0x01` /
   `access_list` / `unsupported tx type` / `交易类型` / `裁剪` 全部**零命中**;§6.4 台账
   (a–s 共 19 条)里也没有对应条目。spec §11 的两条通则同样未覆盖。
2. **`git log -S "Deliberately narrow"` 只有一条**:`f067d23fe`(OpSchedulerImpl 双签名调度
   组件首次落地)。即"裁剪"与"组件首次实现"同批,没有独立的裁定 commit,也没有评审记录。
3. **注释给出的理由被源码证伪**(见 A):`OpBlockTx` 的变体、`validate_transaction`、
   L1 fee 的 `signedEnvelope` 通道全部**已经支持** legacy / access_list;真实约束只是
   "t8n 语料里只有 deposit/eip1559/setcode 三种 `_op_type`"(实测 `grep '"_op_type"'`
   语料目录:恰好这三个值)。**测试语料的覆盖面被当成了实现的能力边界。**

**因此定性**:与审计一致 —— 它是缺陷,不是已声明偏离。但比审计更准确的措辞是:
**"裁剪是刻意的,声明是错的,且从未上升到 spec 层"**。修复面比审计想的小得多:
解码器补两个分支(legacy 无类型字节、需按 EIP-155 处理 `v`;0x01 按 access_list 布局)即可,
下游一行不用动。

### E. 裁决

**CONFIRMED**(已尝试证伪并失败:找过上游过滤、找过 spec 裁定、找过下游不支持的理由,
三条都不成立;并用 op-geth 真签字节双向实测)。

**唯一的下修**:审计把它列为"★最高危 / 触发成本零"是对的,但审计说这是"遗漏",
应改为"**代码注释级的自声明裁剪 + 该声明的理由不成立 + 未进 spec 台账**"。
批次划分不变(仍是第 1 位),但修复条目要多一条:**改注释,不要只改代码**。

**审计里两个 `[需验证]` 我顺手结掉了一半**:0x03(blob)我方同样在 step 1 就拒
(未 dispatch),和 legacy/0x01 同机理;0x7D(PostExecTxType)在本 pin 上确实是新类型
(`core/types/transaction.go:226`),但它是否受分叉门控我仍未查清,**保留 `[需验证]`**。

---

## 复核 1-2:`c_systemTxsAddress` → 毒旗 → -32603

**审计原结论**:向 `0x…1000` 等 8 个地址转 1 wei 就让节点在该块上永久卡死(-32603)。

### A. 我方代码:引用还对吗?读对了吗?

**全部对,逐行核过。**

- `bcos-evm/bcos-evm/ledger/Storage2Ledger.h:740-754` `accountTableName`:命中
  `bcos::precompiled::c_systemTxsAddress` → `return false`(:749-750)。
- 读路四个方法各自 poison:`get_account` :125-131、`get_account_code` :175-181、
  `get_storage` :217-223。(`visitAccounts` 不走 `accountTableName`,它按 `/apps/` 前缀扫,
  所以只有**三个**读方法有这条守卫 —— 审计写"读路四个方法"是**小误**,第四个方法
  `visitAccounts` 没有这条判据。不影响结论。)
- 写路:`applyModifiedEntry` :409-413 `throw std::runtime_error`、
  `applyDeletedEntry` :496-500 同形。
- 毒旗 → `OpSchedulerImpl.h:874-875`(以及 :836-841 / :866-868 / :874 四个检查点)→
  `OpStorageError` → `EngineServiceImpl.h:1000-1006` → `OpExecutionInternalError` → **-32603**
  (:92-95 的注释明写 "JSON-RPC -32603")。

### B. op-geth:这 8 个地址在以太坊/OP 语义下是什么?

**是完全普通的账户地址。** 两条独立证据:

1. **不是 EVM 预编译。** `bcos-evm/bcos-evm/eth/state/precompiles.cpp:790-814`:`traits` 表
   最大项是 `{0x0100, EVMC_OSAKA, p256verify_…}`,`LOOKUP_TABLE_SIZE = max_idx + 1 = 0x101`;
   `is_precompile` 先做 `addr >= evmc::address{LOOKUP_TABLE_SIZE}` 早退(:836-837)。
   `0x…1000` > `0x0101` ⇒ **必然 false**。审计标的 `[需验证]`「evmone 预编译表不含 0x1000」
   **现已结掉:确认不含**。
2. **不是 OP 预部署,也无任何协议角色。** 在 op-geth `e8800cffe` 全仓 `grep` 这两个 40 位
   十六进制串(`…1000`、`…10003`),只命中若干无关的 ABI/receipt 测试十六进制大串,
   **无一处是地址常量**。OP 预部署全部在 `0x4200…` / `0xc0D3C0d3…` / `0xDeaDDEaD…` 段。

### 派单必答:具体是哪 8 个?

`bcos-framework/bcos-framework/executor/PrecompiledTypeDef.h:143-149` 的
`c_systemTxsAddress` 字面上有 **11 项**,其中 **3 项是表名而非地址**
(`SYS_CONFIG_NAME = "/sys/status"`、`CONSENSUS_TABLE_NAME = "/sys/consensus"`、
`ACCOUNT_MANAGER_NAME = "/sys/account_manager"`)—— 它们永远匹配不上 40 字符小写 hex,
对 `accountTableName` 是惰性项。**真正的 8 个 20 字节地址**(值取自同文件 :84-120):

| # | 常量 | 地址 |
|---|---|---|
| 1 | `SYS_CONFIG_ADDRESS` | `0x0000000000000000000000000000000000001000` |
| 2 | `CONSENSUS_ADDRESS` | `0x0000000000000000000000000000000000001003` |
| 3 | `AUTH_MANAGER_ADDRESS` | `0x0000000000000000000000000000000000001005` |
| 4 | `WORKING_SEALER_MGR_ADDRESS` | `0x000000000000000000000000000000000000100b` |
| 5 | `SHARDING_PRECOMPILED_ADDRESS` | `0x0000000000000000000000000000000000001010` |
| 6 | `AUTH_COMMITTEE_ADDRESS` | `0x0000000000000000000000000000000000010001` |
| 7 | `ACCOUNT_MGR_ADDRESS` | `0x0000000000000000000000000000000000010003` |
| 8 | `ACCOUNT_ADDRESS` | `0x0000000000000000000000000000000000010004` |

审计列的 8 个与此**完全一致**(它只是没提那 3 项惰性表名)。

**它们在 OP 主网/测试网上是否已被占用?** 离线无法查链上余额,不编造。**能确定的是**:
它们都不是 OP 预部署、不是 EVM 预编译、无任何协议角色,因而是**任何人都可以直接转账的普通
地址**,且落在低位"靓号"段 —— 这一段在公链上被 dust/spam 打中是常态。不需要"已被占用"
这个前提,分歧就成立:**只要有一笔交易触及它,节点就卡住。**

### C+D. 触发路径:是"转账"还是"被读到"?—— 实测,结论比审计更坏

派单问的是"一笔转到该地址的普通交易,真的会让桥走到毒旗吗",以及"这条路径是读路还是写路"。
**实测答案:是读路;而且根本不需要转账。**

实验一(审计原场景:普通 0x02 交易向 `0x…1000` 转 1 wei):

```
[V1] recovered sender = 1a4e62aaf3a0c2cbe903ef601b4d8e8a3f8cf3fb
[V1] seed poisoned=0
[V1] sysaddr -> OpStorageError(-32603): Storage2Ledger::get_account: address routes to /sys/
     (c_systemTxsAddress member); bridge refuses to guess the routing, see design §4.4
```

实验二(对照组,同一笔交易、收款人换成普通 `0x…9000`):

```
[V1] control: NO THROW, gasUsed=42000 receipts=2
```

对照组证明整条路径本来是通的,实验一的失败**只由收款人地址决定**。

实验三(**审计没想到的、更便宜的触发**):一笔 `value = 0`、收款人是**普通地址**的 0x02 交易,
仅仅在 **access list 里列了 `0x…1000`**:

```
[V1] accesslist-mention -> OpStorageError(-32603): Storage2Ledger::get_account: address routes
     to /sys/ (c_systemTxsAddress member); bridge refuses to guess the routing, see design §4.4
```

**三条实测共同给出的结论**:

- `firstError()` 来自 **`get_account`(读路,:125-131)**,不是 `applyModifiedEntry`(写路,
  :409-413)。写路的 throw 确实也会发生,但它在时间上更晚,而 `poison()` 只记**第一条**,
  所以 -32603 的诊断串永远是读路那句。审计把机理写成
  "`get_or_insert` → `build_diff` → `applyModifiedEntry` 抛错 → 毒旗" —— **顺序反了**:
  毒旗在 `Host::call` 第一次读该账户时就已置位,`applyModifiedEntry` 只是第二次撞墙。
  批 9 之后 `applyDiff` 的四级 catch 阶梯把写路也统一 poison(:315-337),两条路殊途同归,
  **裁决(-32603)不受影响**,但**归因要改**。
- 触发条件是 **"该地址被 EVM 触及"**,不是"被写入状态"。因此触发面比审计说的宽得多:
  `BALANCE` / `EXTCODESIZE` / `EXTCODEHASH` / `CALL`(哪怕 value=0)/ `STATICCALL` /
  **access list 里出现一次** —— 任何一种都够。一笔 `value=0`、只把该地址塞进 access list
  的交易,gas 成本约 21000 + 2400,**比审计说的"转 1 wei"更便宜、更不显眼**。
- 而且它**先于任何 gas/nonce 语义**:实验三里那笔交易在 op-geth 上是完全成功的普通交易。

### E. 裁决

**CONFIRMED,且需上修严重度。**

- 后果与审计一致:既不是 VALID 也不是 INVALID,而是 -32603;op-node 会反复重投该块,
  节点在这条链上永久卡死(活性故障)。
- 触发成本比审计所说**更低**:不需要转账,不需要 value,一次只读触及即可。
- 归因需订正:**读路先置毒旗**,写路的 `applyModifiedEntry` throw 是第二顺位。

---

## 裁决表

| 原编号 | 原结论 | 裁决 | 依据 |
|---|---|---|---|
| 表1 #1(分歧 1-1) | 只认 0x7E/0x02/0x04;legacy 与 0x01 一律拒块 | **CONFIRMED**(定性订正) | `OpSchedulerImpl.h:660/674` + `EngineServiceImpl.h:995-999` 实测 INVALID;op-geth `e8800cffe` `transaction.go:191/212` 对**同一串真签字节**解码成功并恢复出 sender。订正:注释 :648-659 自称的"变体只懂三种"被 `OpBlockExecute.h:15-19` + `transaction.hpp:41-61` + `state.cpp:470-475` 证伪 —— 执行层支持 5 型,只有解码器裁到 3 型;spec 与 §6.4 台账均无明文裁定 |
| 表1 #2(分歧 6-2) | `c_systemTxsAddress` → 毒旗 → -32603 | **CONFIRMED**(严重度上修,归因订正) | 三条实测:1 wei 转账 → -32603;对照组普通地址 → 正常出块(gasUsed=42000);**value=0、仅 access list 提及该地址 → 同样 -32603**。`firstError()` 来自读路 `Storage2Ledger.h:125-131`,不是写路 :409-413,审计的因果顺序反了。8 个地址已逐个列出;`is_precompile` 上限 `0x0101`(`precompiles.cpp:790-814/836-837`)⇒ 审计的 `[需验证]`「evmone 预编译表不含 0x1000」结案:确认不含 |

**没有一条被 REFUTED。** 我按判据 A–D 逐项找过反例(上游过滤、spec 裁定、下游不支持、
路径不可达、方向搞反),五条反驳路线全部失败。

---

## 「审计漏掉的」

1. **`decodeOneRawTx` 的错误消息把十进制当十六进制打印。**
   `OpSchedulerImpl.h:674-675`:`"...unsupported tx type byte 0x" + std::to_string(static_cast<unsigned>(typeByte))`。
   `std::to_string` 出**十进制**,却带了 `0x` 前缀。实测三个样本全错:
   `0xe1 → "0x225"`、`0xf8 → "0x248"`、`0x01 → "0x1"`(仅这个碰巧对)。
   这条消息会原样进 op-node 的 `validationError` 字段 —— 运维看到 "0x225" 会去找一个
   不存在的类型字节。**修 1-1 时顺手修掉。**

2. **c_systemTxsAddress 的触发面是"读"而不是"写"** —— 见复核 1-2 的 C+D。这直接影响修复
   方案的形状:只在写路加白名单/路由是**不够**的,读路的 `get_account`/`get_account_code`/
   `get_storage` 三处才是先触发的那一环。

3. **`c_systemTxsAddress` 里有 3 项是表名不是地址**(`"/sys/status"`、`"/sys/consensus"`、
   `"/sys/account_manager"`)。它们对 `accountTableName` 是死项(40 字符 hex 永不等于
   `/sys/...`),但它们**混在同一个"地址集合"里**,任何按 `.size()` 计数或按索引遍历该集合
   的新代码都会读到 11 而不是 8。是个埋着的坑,不是今天的 bug。

4. **`visitAccounts` 没有 `accountTableName` 守卫**(审计写"读路四个方法"实为三个)。
   它按 `/apps/` 前缀扫表,所以系统地址天然不在遍历范围内 —— 今天无害。但这意味着:
   **如果将来放宽读路守卫(让系统地址落到 `/apps/<addr>`),`visitAccounts` 会自动把它收进
   stateRoot,而三个读方法此时已不再报警** —— 修复 1-2 时必须把这两半一起考虑,否则会把一个
   响亮的 -32603 换成一个安静的 stateRoot 分歧(和 §6.4 条目 f 里"可发现的崩溃 vs 不可发现的
   错答案"是同一类权衡)。

5. **修复 1-1 的隐藏约束(spec §6.4 条目 n 已记账,但没人把它和 1-1 连起来)**:
   `computeOpTxRoot` 对**原始线上字节**建 trie,与 op-geth `DeriveSha` 的等价性**完全依赖
   解码器拒绝一切非规范编码**。给解码器补 legacy 分支时,legacy 的 EIP-155 / pre-155 双形态、
   `v` 的规范性、以及 `chainId` 推导都必须同样严格,否则会**无声**破坏 txRoot 一致性
   (不会有测试翻红)。这是 1-1 修复里最容易踩的雷,审计的"最小验证步骤"没提。

---

## 收尾状态

- 临时用例(gtest suite `V1Refute`,6 条)与 op-geth 侧临时 Go 包 `tmp_v1_refute` **均已删除**
  (`git checkout -- bcos-evm/test/opstack/OpSchedulerImplTest.cpp` / `rm -rf tmp_v1_refute`;
  op-geth 工作树 `git status` 亦确认 clean,tag `v1.101702.2`)。
- 还原后**重新构建**(二进制时间戳刷新到 `Jul 31 14:41:33`,非陈旧假绿),
  `--gtest_list_tests` 计数 **250**,`bcos-evm-opstack-tests` 全跑
  **`[==========] 250 tests from 33 test suites ran. [  PASSED  ] 250 tests.`**
- `git status` 干净(仅本报告为新增文件)。

### 附录:临时实验用例(可原样重放)

追加在 `bcos-evm/test/opstack/OpSchedulerImplTest.cpp` 末尾,复用该文件既有的
`leadingL1AttributesDeposit()` / `minimalEnv()` / `StorageFixture` / `seedFromTestState`
基础设施;`buildEip1559TransferTo` 是把既有 `buildEip1559RawTx` 的 `to`/`value` 参数化。
签名用 `r = 1, s = 1`(该文件已有注释论证 `r = 1` 是合法 secp256k1 x 坐标,ecrecover 必成功),
恢复出的 sender 在测试内先 `seedFromTestState` 播种余额 —— 因为本复核要证的是**收款人路由**,
不是签名有效性。

op-geth 侧脚本:`TestRealSignedLegacyRoundTrip` 用
`crypto.HexToECDSA` + `types.SignTx(LatestSignerForChainID(8453))` 造一条真签 legacy,
`MarshalBinary` → `UnmarshalBinary` → `types.Sender` 全通过,再走
`beacon/engine` 的 `DecodeTransactions` 同形逻辑,同样通过。产出的字节直接喂给
我方 `V1Refute.RealOpGethSignedLegacyBytes`。
