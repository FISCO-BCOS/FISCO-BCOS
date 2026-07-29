# 终审批 2 报告:共识安全(一行级 + 已知块短路)

基线 HEAD `a47b00e78`(含批 1)。**编译与测试均未豁免**。

| 项 | 状态 | 位置 |
|---|---|---|
| B2-1 `catch(...)` 兜底 | 已修 | `EngineServiceImpl.h` `handleOpNewPayload` step 4 |
| B2-2(a) 已知块 VALID 短路 | 已修 | 同上,新 step 3b |
| B2-2(b) 非链尾 parent 明确报错 | 已修(仅断言,不含完整能力) | 同上,新 step 3c |
| B2-3 gasLimit ≤ 2^63-1 | 已修 | `EngineServiceImpl.cpp` `validateOpNewPayloadRequest` |
| B2-4 extraData ≤ 32B | 已修 | 同上 |
| B2-5 异常消息函数名 | 已修 | `Storage2Ledger.h:469` |
| B2-6 CMake 护栏注释 | 已修(改为事实描述) | `bcos-evm/test/CMakeLists.txt:89-96` |

---

## B2-1 `catch(...)` 兜底

**根因**:`executeOpBlock` 只有 step 3+4 被自己的 try 包住;step 1 解码循环、step 5
`sealOpBlock`+`stateRootOf`、step 6 `computeOpTxRoot`+`mapOpReceipt` 全在 try 之外。任一处抛出
非 `OpConsensusError`/`OpStorageError` 的异常(`bad_alloc`、evmone 侧 `std::runtime_error`……)
→ 两条 typed catch 均不匹配 → 逃出 `handleOpNewPayload` → 在调用方 `co_await` 处重抛。
**既不是 INVALID 也不是 -32603**,§4.3 的"存储故障绝不报 INVALID"在这些路径上退化成"无分类"。
放大项:本仓已知 typed-catch RTTI 旁路(evmone `-fno-rtti`,`std::exception` typeinfo 非唯一),
`OpSchedulerImpl.h:678/684` 与 `Storage2Ledger.h` 四处早已成对配兜底,engine 边界是全链路
**唯一**缺兜底的一层,而它决定共识判决。

**改法**:在两条 typed catch 之后补 `catch (...)` → `OpExecutionInternalError`(-32603)。
选 -32603 而非 INVALID 是安全默认:未知本地故障不该让本节点对区块投反对票。
handler 内无 `co_await`([expr.await]/2),与既有 StorageError 分支同形。

**消息带判别标识**(批 1 经验 2):文案含 `unclassified exception`。理由已写进代码注释——
`catch(...)` 会把不同块级拒绝抹成同一异常类型,不给标识就是给未来的测试挖坑。同一原因,
本轮所有共用 `OpExecutionInternalError` 的测试都改用**消息子串断言**而非仅断言类型。

**op-geth 对照**:不适用(这是本实现独有的分层缺口,op-geth 无对应结构)。

---

## B2-2(a) 已知块 VALID 短路

**根因**:同一 payload 连投两次,第二次仍重新执行——parentHash 仍在 `SYS_HASH_2_NUMBER`、
`blockNumber == parent+1` 仍成立,于是在**已含本块状态**的 view 上重执行,必然失配并报 INVALID
(用户交易块 nonce 已推进 → `processOpBlock` 块级错误;纯 deposit 块 mint 二次入账 →
receiptsRoot 失配)。重复投递不是重组:CL 超时重发、op-node 重启后重放 unsafe 块都是常规路径。

**op-geth 对照**:`eth/catalyst/api.go:872-876` 对已知块**直接短路返回 VALID**。这是全部终审
发现中唯一一条"拒绝 op-geth 会接受的块"。

**改法**:step 3(parentKnown + 连续性)之后、执行之前,查 `payload.blockHash` 是否已在
`SYS_HASH_2_NUMBER`;在则直接 `Valid` + `latestValidHash = blockHash`,不执行、不重复登记。

**位置的三条约束**(已写进注释):在 step 2 **之后**(畸形 payload 仍按自身格式被拒,不因哈希
命中被放行);在 parentKnown/连续性**之后**(只对"坐在它声称位置上的块"给这个答案);在
B2-2(b) 非链尾检查**之前**(重投块的子高度必然被它自己占着,顺序反了会被 (b) 误伤)。

依据不变式:`SYS_HASH_2_NUMBER` 只由 VALID 分支写入,故"在表中"⟺"本节点执行并接受过它",
与 step 5 判定 parent 的口径同一条。

---

## B2-2(b) 非链尾 parent(控制器裁定:只做断言)

**根因**:`fork()` 取层栈顶 = **当前链尾**状态,而非 payload 声明的 parent 的状态。二者不同时,
执行跑在错误基态上,产出必然失配,却以 INVALID 报出——把"本节点算不了"伪装成"这个块是坏的"。

**改法(本轮)**:parent 是链尾 ⟺ 子高度尚未被登记。用现有表即可判定:查
`SYS_NUMBER_2_HASH[payload.blockNumber]`,存在则抛 `OpExecutionInternalError`,消息点明
`non-tip parent not supported`。**不引入任何新账本**。(能走到这里且高度被占,必是真正的
兄弟块/侧链投递——重投块已在 (a) 短路。)

**未做的部分**:任意-parent 基态需要 `blockHash → MLS 层` 映射,当前不存在,属架构工作,
写入 spec §6.4 欠账(本轮交付的是**明确拒绝**,不是能力)。

---

## B2-3 gasLimit ≤ 2^63-1

**根因**:`gasLimit` 只校验了 uint64 范围;执行侧 `toBlockInfo`(`OpSchedulerImpl.h:183`)再用
裸 `static_cast<int64_t>` 填 `BlockInfo::gas_limit` → 超过 2^63-1 时块 gas 池为**负**。与批 1 的
C4(deposit `gas_limit` 无检查窄化)同属"无检查的有符号窄化"类。

**op-geth 对照**:`consensus/beacon/consensus.go:262-264`(`params.MaxGasLimit`)。

**接受面**:不变。此类块今天也被拒(首笔 deposit 无法从负 gas 池支付),改动只是让它按写明的
理由被拒,而不是碰巧被拒。

---

## B2-4 extraData ≤ 32 字节

**根因**:长度完全未校验,原样进头 RLP。

**与 §6.4 park 项的边界**(已写进注释):§6.4 park 的是 **OP 形状**校验(Isthmus 9B /
Jovian 17B / version 字节 / 可解码)——那是"合规尺寸字段的内容"问题;**32 字节上限**是
op-geth `beacon/engine/types.go:294-296` 里独立的、链无关的长度界,不在 park 范围。

测试同时断言 **32 字节整仍被接受**(界为闭区间,与 op-geth 一致)。

---

## B2-5 / B2-6

- `Storage2Ledger.h:469` 异常消息 `Storage2Ledger::visitAccounts:` → `::fetchAllStorage:`
  (该 throw 实际位于 `fetchAllStorage` 体内)。一行,纯消息,无行为改变。
- `bcos-evm/test/CMakeLists.txt` 护栏注释改为事实描述:`engine` 是默认 STATIC +
  `UNITY_BUILD ON`(整库一个成员),链接器只在有**未解析符号**时才抽取归档成员;测试已把
  `EngineServiceImpl.cpp` 直接编入、符号已定义 → 成员**不会被抽取**,同时链 `engine`
  会**静默通过**,不会 duplicate symbol。注释现在明说"护栏是约定,不是构建强制;真正的失败
  形态是没人发现的陈旧/重复定义",并保留"两者只留其一"的操作指令。

---

## 自验:注释掉修复 → 重建 → 必须翻红 → 还原 → 复绿

每项均实际执行(禁用 → `cmake --build build --target bcos-evm-opstack-tests` → 跑该用例 →
还原)。**5/5 全部如期翻红**:

| 禁用的修复 | 翻红用例 |
|---|---|
| B2-1 `catch(...)`(改成 `catch(int)`) | `EngineOpBranch.UnclassifiedExecutionEscapeIsInternalErrorNotEscaping` FAILED |
| B2-2(a) 已知块短路(整段删除) | `EngineOpBranch.AlreadyKnownBlockShortCircuitsToValidWithoutReExecuting` FAILED |
| B2-2(b) 非链尾检查(整段删除) | `EngineOpBranch.NonTipParentIsRefusedExplicitly` FAILED |
| B2-3 gasLimit 上界(整段删除) | `EngineOpBranch.GasLimitAboveInt64MaxIsInvalid` FAILED |
| B2-4 extraData 上界(整段删除) | `EngineOpBranch.ExtraDataAboveThirtyTwoBytesIsInvalid` FAILED |

还原后全量复绿(见下)。B2-5/B2-6 为消息/注释修正,无行为面,不适用翻红自验。

**判别力说明**(批 1 经验 2 的直接应用):B2-1、B2-2(b) 与既有的
`receipt count`/`null receipt` 不变式**共用** `OpExecutionInternalError`。因此这两例除断言
异常类型外,都用 `boost::diagnostic_information` 取全文并断言消息标识
(`unclassified exception` / `non-tip parent`);新加的测试辅助 `thrownDiagnostic` 就是为此。
B2-2(a) 的判别力锚点是 `executeOpBlockCalls` 计数(仍为 1),而非仅看 VALID——没有短路时该调用
照样发生,计数是唯一能证明"真的短路了"的断言。

---

## 回归

| 目标 | 结果 |
|---|---|
| `bcos-evm-opstack-tests` 编译 | 通过 |
| `--gtest_filter='Engine*:OpSchedulerImpl*:EthBlockHeader*:OpDepositEncode*'` | **61/61 PASSED**(基线闭环 56 + 本批新增 5) |
| 无 filter 全量 | **217/217 PASSED**(基线 212 + 5) |
| `engine`(生产库) | 编译通过 |
| `test-bcos-engine`(既有 11 例) | 编译通过,`*** No errors detected` |

新增 5 例:`UnclassifiedExecutionEscapeIsInternalErrorNotEscaping` /
`AlreadyKnownBlockShortCircuitsToValidWithoutReExecuting` / `NonTipParentIsRefusedExplicitly` /
`GasLimitAboveInt64MaxIsInvalid` / `ExtraDataAboveThirtyTwoBytesIsInvalid`。

## 通用组合根零漂移

`git diff -w --stat` 对两个 engine 生产文件:**91 + 24 行,全部是插入,0 删除**——即既有语句
(含通用 `else` 分支)**逐字未变**,连空白都没动。所有新增逻辑位于
`handleOpNewPayload`(OP-only 函数,只在 `if constexpr (c_opMode)` 内实例化)与
`validateOpNewPayloadRequest`(OP-only 非模板函数)。
**未新增任何成员函数**,因此不存在"OP 依赖名进入签名"的风险(T5b 踩过的坑:签名随类模板
实例化、体才惰性实例化)。

`ports/`、`vectors/`、`golden/`、`transaction-scheduler/`、`bcos-rpc/` 零触碰。

---

# 附:批 2 审查修复轮(I-1 + I-2 + Minor + 记账)

回归 **220/220**(上轮 217 + 本轮 3),Engine 闭环 **64/64**(61 + 3);`engine` 生产库编过,
`test-bcos-engine` `*** No errors detected`。

## I-1 `catch(...)` 覆盖面未闭合 —— 已修(**真缺口,审查者判断正确**)

**我上一轮的错误**:第一轮的 `catch(...)` 只包住 `executeOpBlock` 一次调用,却在报告与 commit
message 里写成"engine 边界是全链路最后一层缺兜底处"、§4.3 纪律"已闭合"——**overclaim,现已在
代码注释、本报告中一并更正**。仍在任何 handler 之外的:step 2 的
`computeTxRoot`/`rebuildOpEthHeader`/`ethHeader.hash()`、step 5 的 `commitmentsOf` 与比对、
**step 6 整个 `registerOpBlock`**(其 `lexical_cast`/`Entry::set`/`ethHeader.encode()`/
`receipt->encode()`/`hashImpl.hash()`/四处 `storage2::writeOne` 抛出的 `bad_alloc`、tars 编码
异常等——审查者澄清得对:那两处 `BOOST_THROW_EXCEPTION(OpExecutionInternalError)` 自带分类,
**不是**问题)。

**改法(采用"整体纳入同一 try"的等价形式)**:把 §6.1 步骤 2–6 抽成
`runOpNewPayloadSteps(request)`,`handleOpNewPayload` 收缩为「版本闸 + **分类屏障**」:

```
step 1 版本闸(在屏障之外——UnsupportedFork/-38005 本身是已分类结果,不得被改标)
try { co_return co_await runOpNewPayloadSteps(request); }
catch (const OpExecutionInternalError&) { throw; }   // 已分类,原样透传(保留更具体的消息)
catch (...) { throw OpExecutionInternalError{"...outside block execution..."}; }
```

选"包装函数"而非"在原体外再套一层 try",有三个理由:①版本闸自然留在屏障外;②原 200 行函数体
**零位移零重排**(本轮 `git diff -w` 对该文件仅 6 行删除,全为注释行);③新函数签名
`Task<PayloadStatus>(const NewPayloadRequest&)` **不含任何 OP 依赖名**,声明随类模板实例化无害,
体仍惰性实例化(T5b 踩过的坑不重犯)。

`executeOpBlock` 那条 `catch(...)` 保留:它给执行期逃逸更具体的消息,且被屏障的透传分支原样放行。

### 判别力(批 1 经验 2 的第二次应用,**本轮实测踩中一次假绿**)

屏障消息与执行期兜底消息都含 "unclassified exception"。自验时发现:**只禁用执行期兜底,
测试 (m) 仍然通过**——因为逃逸被屏障接住,而 (m) 当时只断言了那个共有子串。这正是协调者警告的
假绿。已把 (m) 收紧为:必须含 `"OP block execution threw an unclassified exception"`
**且不得含** `"outside block execution"`。收紧后重做自验,如期翻红。

### 自验(注释掉 → 重建 → 翻红 → 还原 → 复绿)

| 禁用项 | 翻红用例 |
|---|---|
| 屏障 `catch(...)`(改 `catch(int)`) | `StaticValidationPhaseEscapeIsInternalError`、`ComparisonPhaseEscapeIsInternalError`、`RegistrationPhaseEscapeIsInternalError` **3/3 FAILED** |
| 屏障 `catch(const OpExecutionInternalError&){throw;}`(删除) | `NonTipParentIsRefusedExplicitly` FAILED(其 `non-tip parent` 标识被屏障通用消息覆盖)**+ `UnclassifiedExecutionEscapeIsInternalErrorNotEscaping` FAILED**(见下方勘误)⇒ 透传分支是承重的 |

> **勘误(2026-07-29,终审批 3 / B3-13b 补记)**:上表第二行原先只列了
> `NonTipParentIsRefusedExplicitly` 一条,**低估了透传分支的翻红范围**。删除
> `catch (const OpExecutionInternalError&) { throw; }` 后,
> `UnclassifiedExecutionEscapeIsInternalErrorNotEscaping` **同样翻红**——根因与前者相同,
> 也正是本报告下文那条经验的第 3 条:屏障不是把内层异常放过去,而是用**自己的消息重新
> 构造**一个新异常,把内层的 `OP block execution threw an unclassified exception` 标识
> 覆盖掉,于是该用例收紧后的正/反例双断言(必须含执行期标识、不得含屏障标识)两头都不满足。
> 复审者已复现两次确认非 flaky。**修复本身正确,只是本表的证据不完整**;该经验已成文进
> spec §11 审查检查单第 3 条。
| 执行期 `catch(...)`(改 `catch(int)`,收紧断言后) | `UnclassifiedExecutionEscapeIsInternalErrorNotEscaping` FAILED |

### 新增 3 例(逐窗口注入,各覆盖一个此前无 handler 的区段)

| 用例 | 注入点 | 断言 |
|---|---|---|
| `StaticValidationPhaseEscapeIsInternalError` (r) | stub 静态 `computeTxRoot` 抛(RAII guard 复位) | -32603 + 标识 `outside block execution` + `executeOpBlockCalls==0` + 未登记 |
| `ComparisonPhaseEscapeIsInternalError` (s) | stub 静态 `commitmentsOf` 抛(开关放在 `ExecuteResult` 里,因为它是静态函数读不到实例状态) | 同上,且 `executeOpBlockCalls>0`(证明窗口在执行**之后**) |
| `RegistrationPhaseEscapeIsInternalError` (t) | `ThrowingEncodeReceipt`(派生自 tars 实现,只覆写 `encode()` 抛出)——`registerOpBlock` 里**唯一由测试掌握实现**的调用,替真实危险(`writeOne`/`Entry::set` 的 `bad_alloc`、tars 编码异常)站位 | 同上;并断言登记失败的块**不可见为已登记**(未 `pushView`,已落的写随 view 丢弃) |

## I-2 链尾判据的假等价 —— 已改注释(**审查者判断正确,反例成立**)

原注释写 "the parent is the tip **iff** nothing is registered at the child's height" 是无条件假等价。
审查者的反例(`HASH_2_NUMBER[Q]=5` 而 `NUMBER_2_HASH[5]` 未被 Q 占据 → 以 Q 为 parent 的 6 号块
绕过 3c 拿到 VALID)成立;5b 的父子号连续性补不上(6==5+1 照过)。

**已按裁定只改表述,不改行为**:注释现分方向陈述——

- **(⟸) 高度被占 ⇒ parent 不是链尾**:无条件成立,**这正是本检查用来拒绝的方向**;
- **(⟹) 高度空 ⇒ parent 是链尾**:依赖一条此前未成文的不变式——
  **`SYS_HASH_2_NUMBER` 中每个哈希都在 `SYS_NUMBER_2_HASH` 中占据它自己的高度(两表一致)**。
  `registerOpBlock` 与生产先例 `BaselineScheduler.h:207-220` 都**成对写**,故真实账本上判据精确;
  只写一张表的存储(典型即测试夹具 `registerVerifiedBlock`,自称 test-only 豁免)可以造出
  "高度空但 parent 非链尾",此检查会放行。

不变式已在注释中点名,并请求同步写入 **spec §6.4**(与任意-parent 基态欠账并列)。

## Minor

零漂移数字更正:上一轮实测为 **90+24**(非 91+24),结论不变(`^-` 行数 0、无新成员函数)。
本轮该文件 `git diff -w --numstat` = **84 增 / 6 删**,6 行删除**全部是注释行**(step 3c 的假
等价表述、执行期兜底的 overclaim 表述),逐行核对无一触及通用 `else` 路径;新增的
`runOpNewPayloadSteps` 是本轮唯一新成员函数,**签名无 OP 依赖名**。

## 记账(本轮不改代码,请同步 spec)

1. **⚠️ `-32603` 目前是名义值**:全仓没有任何代码把 `OpExecutionInternalError` 映射到 JSON-RPC
   -32603——RPC 端点注册整体在 §6.4 欠账里。报告与 5 处代码注释都写"返回 -32603",读者极易
   误以为已接线。建议 spec 用"**语义位号,尚未接线**"的措辞统一表述,并在 §6.4 单列一条
   "异常类型 → JSON-RPC code 映射"。
2. **⚠️ 合法重组被 3c 挡下(建议置顶 op-node 实连前置清单)**:首块被判坏后 CL 投递同高度竞争块,
   现在得 -32603,而 op-geth 能正常处理。这是"只做断言不做能力"裁定的内在后果,但它把
   op-geth 可处理的路径变成硬拒绝——比"任意-parent 基态"更早被真实网络触碰。
3. **⚠️ 相邻路径答案落差**:B2-2(a) 对**深层旧块**同样短路(与 op-geth 一致)。配合 3c,
   "重投旧块 → VALID" 而 "投旧块的兄弟 → -32603",两条相邻路径答案差别很大,应在 spec 写明,
   免得被读成不一致的 bug。
