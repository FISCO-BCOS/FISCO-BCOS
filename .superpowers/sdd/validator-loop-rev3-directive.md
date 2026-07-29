# 验证者闭环 spec rev.3 / plan v2 修订指令书(控制器裁定,4 视角审查全数采纳)

对象:
- spec: docs/superpowers/specs/2026-07-28-op-validator-minimal-loop-design.md(rev.2 → rev.3)
- plan: docs/superpowers/plans/2026-07-28-op-validator-minimal-loop.md(→ v2)

## A. 结构性裁定(Critical)

A1【六项比对面】现有 `OpBlockSeal`(OpBlockSeal.h:24-31)只有 5 字段(receiptsRoot/logsBloom/withdrawalsRoot/requestsHash?/blobGasUsed?),无 stateRoot/gasUsed/txRoot。裁定:`OpExecuteBlockResult = { receipts; seal(现有结构原样不动); stateRoot; gasUsed; txRoot }` 三独立成员与 seal 并列;全篇"seal 六字段"改称"**六项比对面**(seal 的 receiptsRoot/logsBloom/withdrawalsRoot + result 的 stateRoot/gasUsed/txRoot)";§4.1/§6.1.4/§7.1/§7.3/§7.5 同步改写。txRoot 是净新增计算(对 rawTransactions 建 trie,复用 vendored MPT/rlp,金值判据 golden.transactionsRoot)。

A2【链式向量对】T6"A 的 blockHash 设为 B 的 parentHash"拼接法作废(三重破产:静态校验先挡路/金值失效/state 与 number 不符)。裁定:T2 金值仪式**离线生成专用链式向量对**——op-geth `GenerateChainWithGenesis` 块数 1→2(经 InsertChain 头校验),两块各发射完整 payload 字段 + golden,产物入 `golden/engine/chained/`(vectors/ 依旧零触碰);T6 两块链式用例消费该对(B 的 pre 即 A 的 post,不重播种)。spec §7.2 同步改写。

A3【extraData 改为原样发射】"选值 250/6"作废(与生成器既有惯例 denominator=50/elasticity=6 冲突,且 **Jovian extraData 是 17 字节含强制 minBaseFee**,generator README:42/99-100 在案)。裁定:golden 的 extraData = 生成块 `header.Extra` **原样发射**,不做人工选值;spec §5.1 修正:Isthmus 9B(0x00‖denom u32‖elast u32)/ **Jovian 17B(另含 minBaseFee)**,值以生成块为准。T2 仪式定性改为"**扩展 opt8n-ref 发射段**"(emit 阶段追加输出 `block.Hash()/header.TxHash/header.Extra/tx.MarshalBinary()/encodedHeaderHex`),非"从 env+txs 构头"。

A4【SYS_CURRENT_STATE 不写】rev.2 的"FCU head 推进时写 SYS_CURRENT_STATE"作废(全新只读路径引入落库,原子性/写放大/测试面全未定义,且本期无消费者)。裁定:**本期 FCU 保持现状只读+内存态**,SYS_CURRENT_STATE 推进列入 §6.4 欠账台账(编排接入时定)。相关联合测试要求随之消解。

A5【fork 阈值注入】`OpForkSchedule::configAt` 无地基(现仅具名工厂;SystemConfigs 无 op fork 键)。裁定:新增 `struct OpForkTimestamps { uint64 isthmusTime; uint64 jovianTime; }`,经 OpSchedulerImpl 构造参数注入(与 chainId 同通道,**不动 SystemConfigs 枚举**,守 framework 最小触碰);T4 产出 `OpForkSchedule::configAt(timestamp, OpForkTimestamps) → OpForkConfig`(自由函数或静态方法,落 bcos-evm/opstack);-38005 判定同源;gate fixture 按向量 timestamp 播种两阈值(Isthmus 向量 ∈ [isthmus, jovian)、Jovian ∈ [jovian, ∞))。

A6【RPC 端点整体豁免】T5 Step 2b 作废(newPayloadV4 RPC 注册越 spec §2 豁免边界;且任何 V4 RPC 注册都会改通用节点 RPC 面)。裁定:**本期不动 bcos-rpc**——V4 支持只在 EngineServiceImpl 层(gate 直调);`engine_newPayloadV4`/`engine_getPayloadV4` RPC 端点注册整体列入 §6.4 欠账;§6.3 的 getPayloadV4 行为改述为"EngineServiceImpl::getPayload(id, 4) 在 OP 模式返回明确错误(出块未 OP 化),单测直调断言"。Global Constraints 补回"RPC 解析层/端点注册本期豁免,gate 直调 EngineServiceImpl"。

A7【Holocene baseFee 一致性欠账】§6.4 追加条目:"Holocene EIP-1559 baseFee 父子一致性校验(用父块 extraData 参数重算子块 baseFee 并比对)未实现——真实 op-geth 会拒绝的 baseFee 错块本验证者放行",与 extraData 形状校验并列,不得被掩盖。

## B. Plan 任务结构调整

B1 T5 拆分:**T5a** = 版本闸成员化(maxEngineVersion 构造参数)+ opMode 编译期判据(`if constexpr (requires { scheduler.executeOpBlock(...); })` 探测,禁运行时 bool——与既有全模板风格一致)+ 通用组合根 V4 零漂移测试;**T5b** = newPayload OP 分支本体(校验七步/执行/分档/块登记)+ 单测。任务总数 8(T1-T4, T5a, T5b, T6, T7)。

B2 fixture 闭包(T5b/T6 的 Files/Interfaces 必须列全):EngineServiceImpl 直构五参——MemPool 用本地 Stub(仿 EngineServiceTest.cpp:147-204 桩,须满足 remove(view)/seal(limit,view,out))、Executor 用 StubExecutor 本地复刻(EbT8nReplayTest.cpp:45-49 复刻惯例)、BlockFactory 用 `bcos::test::createBlockFactory(createNormalCryptoSuite())`(链 protocol-tars + testutils)、storage fixture 仿 EbT8nReplayTest::Impl;**警示:勿抄 EngineServiceTest.cpp:221-229 的悬垂局部桩工厂模式**,scheduler/executor 与 service 同生命周期。CMake:engine 头 include 路径(`${CMAKE_SOURCE_DIR}` 级)与 `EngineServiceImpl.cpp` 编入测试源均放 `if(TARGET bcos-framework)` 门控块内;链接可能需补 `ledger` 库;注释写明"编入与链 engine 库二选一"护栏。

B3 T4 static_assert 具名:`static_assert(scheduler_v1::TransactionScheduler<OpSchedulerImpl<MLS::ViewType>, MLS::ViewType, StubExecutor, std::vector<protocol::Transaction::Ptr>>);`(Storage 实参必须是 ViewType 而非 GlobalStateStorage)。

B4 T2 自检扩充:(a) `_op_expected.header` 全部 7 个共有字段(receiptsRoot/gasUsed/logsBloom/withdrawalsRoot/blobGasUsed/stateRoot/requestsHash)与 golden 逐字段对账;(b) typed tx 断言 `golden.rawTransactions[i] == 向量._op_raw[i]` 逐字节(防生成器漂移;deposit 走 OpDepositEncode 交叉);(c) golden 每条附 `encodedHeaderHex`(整头 RLP hex)。

B5 s_eth_block_header 表名常量放 `bcos-evm/bcos-evm/engine/` 头内(不动 LedgerTypeDef.h,守自家约束)。

## C. 测试与验收修订

C1 变异矩阵改"**13 类 18 例**":原 11 行 + blobGasUsed≠0(Isthmus)行 + "同 payload SYNCING→补 parent→重发→VALID"行;"六项比对面"行展开为 6 个独立用例(每例只改一字段,断言 validationError 点名);非 blockHash 桶的 INVALID 用例**同时断言 latestValidHash==parentHash**。

C2 "parent 已验证"操作性定义(§6.1.5):"⟺ parent hash 存在于 SYS_HASH_2_NUMBER(仅 VALID 分支写入,存在即已验证);fixture 预登记 = 对该不变式的显式豁免(等价可信创世前提),仅测试合法";补一句"块登记表不随 FCU head 回退回滚"。

C3 gate 翻红可定位性:blockHash 断言前先断言 `EthBlockHeader::encode() == golden.encodedHeaderHex` 逐字节(字段级定位),再断言 hash;失败时 RecordProperty dump 21 字段 hex。

C4 探针 5 fixture 注明:`SchedulerSerialImpl` + `MockExecutorSerial` 本地复刻(testSchedulerSerial.cpp:20-75 先例);V4 拒绝在版本闸完成,不触达 executor。

C5 N0 基线:时点 = **Task 6 结束、Task 7 开始前**;engine Boost 用 `test-bcos-engine --list_content 2>&1 | sort > n0-engine.txt`(**输出走 stderr,实测陷阱**);双路 gtest `--gtest_list_tests | sort`;三份存档 `.superpowers/sdd/n0-*.txt`。

C6 验收清单统一:filter 补 `OpDepositEncode.*`(Task 3 钉两个独立 GTest 套件名 EthBlockHeader/OpDepositEncode);`<基座>` 展开为 `$(git merge-base HEAD feat-evm-ledger-bridge)`;spec §8 与 plan 末清单逐行同构。

C7 T5b TDD 增两条:OpConsensusError→引擎层 INVALID 映射(非静态校验路径);blobGasUsed 校验行。

## D. 措辞与引用

D1 Global Constraints 的"§4.6(a)"改为"移植 spec(2026-07-24)§4.6(a)"。
D2 chainId:构造参数注入,配置键读取责任在组合根(engine 初始化处),gate fixture 直接传值——写明。
D3 §5.1 blobGasUsed 注保留并与 C1 新矩阵行呼应。
D4 风险表增:金值仪式含链式对生成;移除 FCU 写放大条目(A4 后不适用)。

## 执行要求

- spec 全文重写为 rev.3(头部注明 rev.3 与本指令书出处);plan 重写为 v2(8 任务)。
- 两文件提交:`git add -f docs/superpowers/... && rtk git commit`(info/exclude 在案)。
- 重写时保留 rev.2 中未被本指令触及的正确内容;禁止引入新决策(有疑问按本指令字面)。
