# DIVERGENCES.md — OP 块级差分 gate 台账（bcos-evm-ref M-B3+M6）

Plan: `docs/superpowers/plans/2026-07-11-bcos-evm-ref-op-differential-gate-mb3-m6.md`。
先例：`bcos-evm/test/opstack/t8n/vectors/DIVERGENCES.md`（M-T tx 级 gate 台账，格式沿用）。

**本文件是回放器（`OpT8nReplay.Vectors` / `test/opstack/OpT8nReplayTest.cpp`）唯一允许
视为非致命的豁免来源。** 回放器 C++ 里不写死任何期望/实测数值——它只解析本文件的
机器可读 `ALLOWLIST` 行（见下「机器格式」）来判定刚观测到的分歧是否已在此以豁免状态
立案。未立案的 `(vectorId, field)` 分歧、或以非豁免状态立案的分歧，照常翻红
（`DIVERGE <vector> <field> want=<> got=<>`）；已立案且豁免的分歧打印
`KNOWN-DIVERGE <vector> <entry> field=<field> want=<> got=<>`（stdout 可见 +
`RecordProperty` 计数，不是断言失败）。

## 三选一归因（plan 预注册，无第四选项）

- **(a) `bcos-evm-ref` 缺陷。** 生产代码真实错误。在此立案 `FINDING-N`
  （根因 file:line、机理、**定量对账**——每条 ALLOWLIST 行的 want−got 差值必须由机理
  公式复现、影响向量表），ALLOWLIST 行 `status=PENDING-FIX`。**gate 只报告不修**：
  修复走独立 plan/PR，落地时同 commit 翻状态（或向量转绿后删条目）。
- **(b) 生成器/语料/回放器接线缺陷。** 修那一侧；生成器或语料被改动 → **整批重生成**
  （生成器与向量同 commit 入库）→ 分歧应整体消失，不留存续 ALLOWLIST 条目——但纪律
  要求在下方「Fixed pre-commit」留纸面痕迹（机理 + 修改点 + 为什么属 harness 非语义）。
- **(c) 接受的差异。** harness 与金标准之间真实、永久的行为差异，需人工审阅裁定。
  以 `status=PENDING-USER-SIGNOFF` 立案并**停下上报**——用户明确签核后才改
  `SIGNED-OFF` 变为豁免。**gate 永不自授 (c)。**

归因必须携证据（op-geth 行号、计算出的差值），禁止「看起来像」。

## 机器格式（ALLOWLIST 行）

每个豁免 `(vectorId, field)` 一行 HTML 注释（渲染不可见，回放器
`DivergenceLedger::load` 以 regex 解析）：

```
ALLOWLIST vectorId=<id> field=<field> entry=<ENTRY-ID> attribution=<a|b|c> status=<STATUS> want=<w> got=<g>
```

（实际立案时整行包进 HTML 注释 `<!-- … -->`；上方模板故意**不带**注释包裹，
否则模板自身会被回放器的 regex 当成真条目解析——首轮运行已实证会咬。）

- `field` 必须与回放器比较器自身的字段串逐字一致（`gasUsed`、`receiptsRoot`、
  `receipts[N].gasUsed`、`postState.<address>.balance`、
  `postState.<address>.storage.<slot>`、`postState.<address>.uncovered` 等——
  见 `OpT8nReplayTest.cpp` 各 check 调用点）。
- 仅 `attribution=a status=PENDING-FIX` 与 `attribution=c status=SIGNED-OFF` 豁免；
  其余组合（含 `c` + `PENDING-USER-SIGNOFF`）照常翻红——(c) 在人签核前绝不许绿。
- `want`/`got` **必填且四元组全匹配**（与比较器打印的规范串逐字节一致：数值为
  `0x` 前缀小写最简 hex；哈希/地址为 `0x` 定长小写；缺席端为 `<absent>`）。
  豁免钉死在「这一对已知错值」上——值漂移（缺陷变形、向量重生成、同字段新 bug）
  即不再匹配，红回来，这正是设计意图。
- `entry=` 必须指向本文件一个真实存在的 `## <ENTRY-ID>` 标题——悬空引用 = 构建失败。
- 本轮运行从未命中的豁免条目 = 构建失败（过期豁免即红，防僵尸 ALLOWLIST）。

---

## FINDING-1 访问列表挂名未触地址被谎报为 deleted_accounts（诊断性差分缺陷）— ✅ FIXED（Task 2 commit `d93e1ce`，六出口消毒）

> **归因追认：用户已于 2026-07-12 追认 (a) 分类**（共识中性的写集卫生缺陷按生产真实缺陷论——
> 终审与 Task 2 审查独立背书的三理由：diff 是本仓生产接口（StateDiffWriteback 契约）、
> OpHost.cpp:158-160 自家先例、有明确修复方向与转绿判据）。修复须另立 plan，只报不修维持。

语料补强（spec rev.3）`access_list` 案首轮回放即咬出——该案 tx1 的 EIP-2930
访问列表挂名一个从未被调用/写入/预存在的地址
`0xdddddddddddddddddddddddddddddddddddddddd`（listed-uncalled，空 storageKeys），
回放器写集覆盖断言（OpT8nReplayTest.cpp:679-684）报
`postState.0xdd….uncovered`。

- **现象**：`isthmus_access_list` / `jovian_access_list` 各 1 条
  `DIVERGE … postState.0xdddddddddddddddddddddddddddddddddddddddd.uncovered
  want=<covered> got=<uncovered>`。同案 tx2（同构调用、无访问列表，对消面收窄
  对照）与 `setcode_7702_skips`（访问列表挂名 delegateAddr——本就在候选集内）
  均无此分歧；两 fork 其余全部字段（含两笔 gasUsed 的 2930 冷暖差价）比对通过。
- **根因**（evmone 检出 @ 本仓 vendored 版本，生产内核；行号经 2026-07-12 审查勘正——
  下方三处取代先前误标的 `host.cpp:454`/`state.cpp:229`/`state.cpp:241-246`，
  `OpTransition.cpp:192` 核对无误、不动）：
  `test/state/state.cpp` `transition()` 预热访问列表时对每个挂名地址走
  `host.access_account(a)` → `host.cpp:446`
  `m_state.get_or_insert(addr, {.erase_if_empty = true})`——不存在的账户被
  无条件塞进 `m_modified`；`state.cpp:202` `build_diff()` 对
  `erase_if_empty && is_empty() && !just_created` 的账户走
  `deleted_accounts.emplace_back(addr)`（state.cpp:214-217）。
  `OpTransition.cpp:192` 把该 diff 原样作为 receipt.state_diff 交给消费方。
- **定量对账**：该案访问列表中「不在候选集且全程未被引用」的地址恰 1 个
  （0xdd…dd；另一条目 aclAddr 是 tx.to + pre 账户，本就 covered）⇒ 每 fork
  恰 1 条 uncovered 分歧，共 2 条——与观测逐条吻合。语义状态零影响：删除
  不存在账户是 no-op，postState 全部数值比对（balance/nonce/code/槽并集）
  两侧一致，块头六字段一致。
- **金标准侧**：op-geth 的 2930 预热只进 access-list journal，不 touch 状态
  （警惕面：EIP-161 touch 只由转值等触发）；未触及的不存在地址不进任何写集，
  postState（候选集发射）正确不含 0xdd…dd。
- **为什么属 (a)**：生产路径（OpTransition → applyStateDiff 消费方）对外
  声称「该 tx 删除了此账户」——真实 DB 适配层会据此下发多余删除。属生产侧
  diff 发射缺陷（vendored evmone build_diff 的过近似），非语料/回放器接线。
  **只报不修**：修复走独立 plan（方向：build_diff 不报告从未 dirty 的
  erase_if_empty 空账户，或 OpTransition 出口过滤 pre 中不存在且未被写的
  deleted_accounts 条目），落地重放转绿后删本条。
- **结案（Task 2 六出口消毒）**：修复采取「产地出口过滤」方向——全部六个 diff 生产
  出口（`OpTransition.cpp` receipt、`OpDepositTx.cpp` receipt、`OpBlockFinalize.cpp`
  finalize、`OpBlockExecute.cpp` 系统调用、`EthTransition.cpp` transition/finalize）
  经 `bcos::evmref::sanitizeStateDiff(view, …)` 剔除 view 中不存在账户的删除条目
  （adapter/StateDiffSanitize.h，视图时序规则：产地自身 view、applyDiff 之前）。
  两条 ALLOWLIST 豁免同 commit 删除，`isthmus_access_list`/`jovian_access_list`
  回放转绿。判别单测：`test/opstack/OpStateDiffSanitizeTest.cpp`（幽灵剔除红→绿、
  真 EIP-161 删除恒绿存活、EIP-6780 同 tx 生灭红→绿）。
- **失明换防留档（spec §5①）**：消毒使 deleted 侧的幽灵发射类缺陷对差分 gate
  **永久失明**——产地已剔除，回放器 `postState.*.uncovered` 写集覆盖断言不再能
  从 deleted_accounts 观测到此类过近似发射；modified 侧的 `.uncovered` 信号仍在
  （modified_accounts 原样透传，挂名进 modified 的过近似照常被咬）。deleted 侧
  失明由消费端 `applyStateDiffStrict` tripwire（adapter/StateDiffWriteback.h，
  四个 **opstack** 消费者测试文件全量换装；eth 侧测试消费者仍 raw、无 tripwire——
  出口消毒本身已含 EthTransition 两出口）承接：任何未来出口漏消毒/新幽灵源在写回型测试
  即刻抛错翻红，不依赖语料覆盖。

## Fixed pre-commit（(b) 纸面痕迹——生成器/语料侧缺陷，修后整批重生成，无存续豁免）

首轮回放（2026-07-11，回放器 @ 本 commit，向量 @ 5a22595 原始批）25/25 向量在
pre 解析阶段即 FAILURE，未进比对；归因均为 (b)，修复生成器后整批重生成
（cases 25 + vectors 25 同 commit），重放 **0 分歧、2943 次比对全 pass**。

### PRE-COMMIT-1：pre 账户形状不是 EF state-test 形状（生成器发射缺陷）

- **现象**：全部 25 向量 `[json.exception.out_of_range.403] key 'code'/'nonce'
  not found`——回放器把 `pre` 原样喂 evmone 金 loader
  `from_json<TestState>`（statetest_loader.cpp:353-356，`j_acc.at("nonce"/
  "balance"/"code")` 三字段硬必填），而生成器用 `types.GenesisAlloc` 原生
  marshal 发射 `pre`，其 `types.Account` 对 Nonce/Code 是 `omitempty`
  （geth t8n alloc 约定，非 EF 形状）；原始批 90 个 pre 账户中 36 个缺 nonce、
  74 个缺 code。
- **为什么属 harness 非语义**：纯 JSON 表示层——缺席字段语义本就是 0/空码，
  补全后账户内容逐字节等价（见重生成 diff：仅增 `"nonce":"0x0"`/`"code":"0x"`
  类字段，`_op_expected` 与 postState 数值零变化）。
- **修改点**：`generator/main.go` 新增 `outputAccount`/`emitPre`——`pre` 按
  EF 形状 balance/nonce/code 恒发射（storage 仅非空时）；`postState` 保持
  GenesisAlloc 形状不动（其 `{"balance":"0x0"}` 零账户约定由回放器自己的
  比较器消费，不经 evmone loader）。

### PRE-COMMIT-2：attributes depositor 地址错值（语料常数缺陷）

- **现象**（PRE-COMMIT-1 修后暴露）：全部 25 向量
  `processOpBlock threw block-level error: first tx is not the L1 attributes
  deposit`——语料 `cases.go` 的 `attributesDepositor` 写成
  `0xdead…deaddead`（"dead"×10），注释自称 "Canonical" 但真正的 op-stack
  L1 info depositor 是 `0xdeaddeaddeaddeaddeaddeaddeaddeaddead0001`
  （op-geth `eth/downloader/receiptreference.go:28` `systemAddress`；
  op-node `rollup/derive` `L1InfoDepositerAddress`；本仓 `OpPredeploys.h`
  `OP_DEPOSITOR` 同值）。`processOpBlock` 首笔硬断言
  `to==OP_L1_BLOCK && from==OP_DEPOSITOR`（自加严）直接拒块。
- **为什么 op-geth 自检没咬**：op-geth 执行层从不校验 deposit 的 from 地址
  （InsertChain 只跑 Process+ValidateState），错误 depositor 照样出合法块——
  这恰是回放侧硬断言存在的意义。
- **为什么属 harness 非语义**：语料应当模拟真实 OP 链的 L1 attributes deposit，
  错地址是语料书写错误；换成规范地址后 op-geth 侧执行语义不变
  （见重生成 diff：仅 from 与 postState 中 depositor 账户键变化，
  gasUsed/receiptsRoot/各 fee 期望值零变化）。
- **修改点**：`generator/cases.go` `attributesDepositor` →
  `0xDeaDDEaDDeAdDeAdDEAdDEaddeAddEAdDEAd0001`（附来源注释）。

### PRE-COMMIT-3：本文件模板行被 regex 误读（台账文档缺陷，非生成器）

首版本文件「机器格式」节把 ALLOWLIST 模板整行包在 `<!-- … -->` 里，回放器
regex 将模板自身解析成真条目并因 `entry=<ENTRY-ID>` 悬空而 FAILURE。
模板改为不带注释包裹（见上），实际立案时才加。

### 首轮差分结论

重生成批（本 commit 入库版本）回放：**25/25 向量、2943 次比对、0 分歧、
0 KNOWN-DIVERGE**——无 (a) FINDING、无 (c) 候选。豁免机制以变异自测验证
（gasUsed/balance/storage/bloom/receipt status/`_op_l1_fee` 缺席/receipts
计数/hardfork/manifest 缺多文件共 11 项变异全部翻红；四元组豁免、值漂移拒绝、
c 未签核拒绝、悬空 entry、过期豁免共 6 项台账机制自测全部符合预期）。

---

## FINDING-D1 slot/calldata DA scalar 失配（不立案：单测覆盖 + generator 结构性不可生成）

> **只做文档备注，不登记 ALLOWLIST。** 回放器对「本轮从未命中的豁免条目」判 FAILURE
> （T8nReplayHarness.h:199-200，防僵尸 ALLOWLIST）；失配向量不存在于语料
> （manifest 33 条无此项），登记即翻红。

**现象（生产分叉，不在语料内）**：Jovian 块的 DA footprint gas scalar 权威来源是首笔
L1 attributes deposit 的 calldata[176:178]（op-geth `ExtractDAFootprintGasScalar`,
rollup_cost.go:555）。若 attributes deposit 在 EVM 中失败（REVERT/OOG），L1Block
`setL1BlockValuesJovian` 的 sstore 被回滚 → slot8[18:20) 保持上一块旧值，而 calldata
始终携带本块正确值。FISCO 执行器此前读 slot8，会产出与 op-geth（读 calldata）不同的
DA footprint → `blobGasUsed` 失配 → 块判 INVALID。

**修复与覆盖**：`OpBlockExecute.cpp` feeLoaded 分支（D-1）从首笔 attributes calldata
覆盖 slot 读出值（激活块 176B 强制 0，正常块 ≥178B 取固定偏移 [176:178]）。场景由
`OpBlockExecute.D1CalldataScalarOverridesSlot`（slot=9 vs calldata=400）与
`OpBlockExecute.D1ActivationBlockSlotStaleValueDoesNotLeak` 单测覆盖。

**为什么不入语料**：t8n generator 的 `assertL1BlockConsistency`（generator/main.go:1045-1047）
用同一 `fp.daScalar` 写 slot8 与 calldata[176:178]，结构性无法生成失配向量。若未来
generator 支持 pre-slot 注入，再补向量并移除本条备注。

---

## FINDING-D1-blockhashes t8n harness 桩与 engine 的 BLOCKHASH 差异（不立案：当前语料不覆盖）

> **只做文档备注，不登记 ALLOWLIST。** 回放器对「本轮从未命中的豁免条目」判 FAILURE
> （T8nReplayHarness.h:199-200）；本备注无对应向量，登记即翻红。

**现象**：t8n 回放 harness 的 `ParentOnlyBlockHashes` 桩（T8nReplayHarness.h:246-255）对非
parent 高度返回零；engine 执行器自 D1 起用 `RecentBlockHashes`（懒加载 SYS_NUMBER_2_HASH）返回
真实祖先 hash。**当前 33 向量无任何窗口内祖先查询**（BLOCKHASH 只查 N-1 或不出现在合约里），
故两者在现语料上一致，回放 gate 不受影响。

**未来耦合**：若新增含 `blockhash(N-2)`（N-2 ≥ 0）的向量，harness 桩会返回零而 engine 返回
真实 hash → golden 与 engine 分歧。届时须同步更新 harness 桩（或给桩注入 RecentBlockHashes 的
TestState 变体），否则回放 gate 翻红。本备注即该耦合的预警。

---

## FINDING-A5 解码严格性契约（不立案：单元测试直接覆盖，gate 不可观测）

> **只做文档备注，不登记 ALLOWLIST。** 回放器对「本轮从未命中的豁免条目」判 FAILURE
> （T8nReplayHarness.h:199-200，防僵尸 ALLOWLIST）；本备注无对应向量，登记即翻红。

**契约**（`OpSchedulerImpl.h` "canonical-encoding strictness" 注释块）：三层解码严格性
（B4-2 per-field + C1 length-prefix + whole-envelope `assertCanonicalRoundTrip`）保证非
canonical RLP 不 survive 解码，从而 raw-bytes `computeOpTxRoot` == op-geth 式重编码
`DeriveSha` 的 txRoot。

**与本 gate 的关系**：本 gate 的 33-vector 语料（真实 op-geth `tx.MarshalBinary()` 字节）正是
"round-trip 恒不误杀 canonical 输入" 的 corpus-scale 证明 —— 全部向量经 `decodeOneRawTx` +
`assertCanonicalRoundTrip` 回放通过。但 gate 本身**观测不到**该契约（语料无任何非 canonical
字节，回放只能证明 "canonical 全过"，不能证明 "非 canonical 全拒"）。故契约由
`OpSchedulerImplTest.cpp` 的 B4-2/C1/RoundTrip/txRoot 等价单测直接钉住；**改解码器者须先跑
`OpSchedulerImpl.*` + `OpT8nReplay.*`**（任一红 = 严格性被放宽，或误杀了 canonical 字节）。
