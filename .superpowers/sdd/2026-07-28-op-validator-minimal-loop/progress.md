# SDD ledger — plan: docs/superpowers/plans/2026-07-28-op-validator-minimal-loop.md
!! 用户指令(2026-07-28): 开发期跳过编译/运行, 测试照写照提交, T1-T6 后统一编译验证再进 T7; 各任务报告须标注'未编译验证'
Task 1: minor (deferred): 拓扑起点简化(未提 gate syncWait 驱动分支,承自 brief 措辞)
Task 1: complete (commits 42e62fc..959ebbf, review clean 静态)
Task 2: minor (deferred): chained 对未内嵌 generator_commit(README 有);opt8n-verify 用后即删,35/35 自检取信报告(抽 2 条独立复算过)
Task 2: complete (commits 959ebbf..12ed60b, review clean; Jovian extraData 版本字节实测 0x01 已如实记载)
Task 3: minor (deferred): 语料盲区 currentRandom 恒 0(prevRandao 右对齐路径未探测);⚠️编译期核验项 4 条留统一验证阶段
Task 3: complete (commits 12ed60b..270b0f2, review clean 静态; 审查者独立第三方 RLP 复算 33/33+39/39; 实施者抓到 deposit to=null 真 bug)
Task 4: fix round 1/5 (3 addressed, 0 open — C1 enterList 三路/C2 rlp_encode include/I1 yParity 校验; commits f067d23..5c69034)
Task 4: minor (deferred): M1 非规范整数/M2 setcode to nil/M3 十进制冒充hex/M5 mint 0-vs-nil(T6 变异矩阵钉 M1/M2);M4 configAt 不读 isthmusTime 交 T5b 知悉;yParity>1 分支无向量覆盖(编译期分支)
Task 4: complete (commits 270b0f2..5c69034, review clean 静态; 审查+复审均独立 python RLP 复算,setcode 字节级已补)
Task 5a: complete (commit a6513b5, review pending; c_opMode 用 AnyArg sink 探测规避 engine 库反向依赖 bcos-evm——无直接仓内先例,已在报告标注重点复核项;EngineVersionGateTest.cpp 未接入 CMake,随 Task 6 一并落,同 T5b 护栏)
Task 5a: fix round 1/5 (2 addressed, 0 open — I1 断言收窄+T5b 路标/I2 AnyArg→成员模板取址; commits a6513b5..56c1999)
Task 5a: complete (commits 5c69034..56c1999, review clean 静态; EngineVersionGateTest 未接 CMake 随 T6)
Task 5b: fix round 1/5 (6 addressed: I1 stub 端到端+六项 mismatch+-32603 / I2 块号连续性 / M1 M2 M5 M8; commits 672e102..ecd0cc5)
Task 5b: minor (deferred): I3 永不 mergeBackStorage(层无界+读放大,T7 回填 §6.4);I4 payload→header 映射自算同源(T6 硬约束:必须用 op-geth 金值 blockHash 喂 newPayload);M3 computeOpTxRoot 双算+input_range 应改 forward_range;M4 不写 SYS_HASH_2_TX;M6 c_opMode 只探 executeOpBlock;M7 UNITY_BUILD 撞名隐患;I2 校验须 T7 回填 spec §6.1 步骤3
Task 5b: complete (commits 56c1999..ecd0cc5, review clean 静态)
Task 6: fix round 1/5 (5 addressed: I1 口径诚实化/I3 RecordProperty 逐向量 key/M1/M2/#8.6 论证; commits 5dba8f3..5cbebb3)
Task 6: minor (deferred): 报告 §5 交叉引用应为 §4/§8b;M3 FCU 腿归属/M4 错误码=异常类型非线上码/M5 CMAKE_SOURCE_DIR 遮蔽风险 → T7
Task 6: complete (commits ecd0cc5..5cbebb3, review clean 静态; 审查实测 33 向量 8 个 h256 头字段两两互异→错映射必被抓)
统一编译验证阶段: 3 轮修复 — T3 名字遮蔽+undefined-inline(8b7bbb7);T5b registerOpBlock 签名依赖名致通用根无法实例化+2 笔误(c0288b8);T4 typed-catch RTTI 旁路补 catch(...) 兜底(fe2a40c,Important 实现缺陷)
首次 ctest: 50/50(Engine*/OpSchedulerImpl*/EthBlockHeader*/OpDepositEncode*) + 全量 opstack 206/206 零回归
遗留(T7 承接): RTTI 旁路是二进制级现象,需扫 T5b/T6 新增的 catch(const std::exception&) 块是否也需 catch(...) 兜底
Task 7: complete — 五探针留痕(probe-op-validator-gate-report.md,判别力 3/7/2/8/3 例,注入零入库)+ N0 三份基线(206/131/11,探针后 diff IDENTICAL)+ spec §8 验收 11 项全过 + 文档回填(spec rev.3.1:§7.3 口径/§6.1 step3b/§6.4 追加 5 条/§8 打钩;README engine 闭环状态节)
Task 7: 遗留承接已处理 — T6 §5 交叉引用勘误(应为 §4/§8b,已改测试注释);M3 FCU 腿归属裁定不改(记账);M4 升格 spec §6.4(d)+README;M5 CMAKE_SOURCE_DIR 遮蔽风险评估入 CMake 注释(仅 concepts/ 同名,目录项被跳过,无害)
Task 7: RTTI 旁路复扫结论 — 新增代码无需再补 catch(...);engine 两处 typed catch 是精确类型匹配(裸 throw,无 wrapexcept)且探针③实证绑定;OpSchedulerImpl 已由 fe2a40c 兜底
Task 7: minor (deferred): Storage2Ledger.h:469 异常消息误写函数名(visitAccounts→fetchAllStorage);真 OpSchedulerImpl 抛 OpConsensusError→engine INVALID 缺端到端用例;既有 engine Boost 套件对版本上界漂移无判别力
Task 7: complete (commit 382bd00, 五探针判别力 3/7/2/8/3 翻红、N0 三份基线 diff IDENTICAL、验收 11 项全过、in-tree 206/206 standalone 131/131 engine-boost 11/11)
整分支终审: 5 视角并行派出(共识正确性/C++集成生命周期/测试判别力猎杀/文档诚实性/边界合流风险),范围 42e62fce..382bd005 15 commits 72 files +8769-213
终审视角5(边界/合流) 完成: 零触碰 5 项全空、通用路径结构性零漂移(if constexpr 包裹 else 原样)、engine 库纯净、standalone 门控正确、提交卫生干净。
  C1(Critical) 合流不可 merge/rebase: 与 feat-evm-opstack-port 真实公共祖先是 8c8dbd7(非 42e62fce),两线在同一祖先后各自重写同一层——主线 bcos-evm/bcos-evm/engine/ 目录不存在、opstack 30→14 文件(OpBlockExecute/OpBlockSeal/OpDepositTx/OpExecCommon/OpValidate 全消失或折叠进 OpTransition.h/OpReceipt.h)、eth/utils C 路线仅剩 stdx/。本分支 OpSchedulerImpl→OpEngineSeam→OpBlockSeal/mpt/rlp 依赖链在主线逐符号不存在。
  C2(利好) 顶层 engine/ 模块主线零触碰(blob 同 merge-base),EngineServiceImpl 改动可干净移植,但 SchedulerType 依赖名需先重建。
  推荐路径: 第一批直接移植(bcos-codec RLP 两组新文件/Types.h 加性字段/EngineServiceImpl 骨架);第二批需重新对接主线新形状(OpScheduler 等价物);顺序=先重建执行桥接层再套 engine 分支,最后带 33 金值+链式+变异矩阵作回归基线;动手前先确认主线是否仍在活跃重构。
终审视角4(文档诚实性) 完成: 无夸大声称,§10 边界一致遵守,报告主动自我纠错(撤回自己不实表述/主动交代探针落点偏差/主动交代既有套件零判别力);静态复核测试数字 33/23/12/2/8/5=50、156+50=206 全部属实。失败模式=传播不完整(T5b 审查已发现但停在中间产物,未进 §6.4/README):
  Critical1: SYS_HASH_2_TX 从不写入(registerOpBlock 自引的 prewriteBlockToBuffer 先例写了),OP 接受块的原始交易无法按 hash 取回——未在 §6.4/README 披露(M4 记账未升格)
  Imp2: spec §7.3 line350 引用 task-6-report.md 但该文件未入库=悬空引用
  Imp3: rawTransactions 缺失判 INVALID 而非 -32602(缺参与空数组不可分),仅在未入库报告里交代
  Imp4: 真调度器 OpConsensusError→engine INVALID 仅桩覆盖(StorageError 有真调度器探针,不对称),未披露
  Imp5: 8 份 task 报告只归档 3 份,无成文标准
  Minor6: M3/M6/M7 仅存于 progress.md 一行
终审视角2(C++集成/生命周期) 完成:
  C1(Critical) handleOpNewPayload 两条 typed catch 无 catch(...) 兜底——executeOpBlock 只有 step3+4 被 try 包住,step1 解码/step5 sealOpBlock+stateRootOf/step6 computeTxRoot+mapOpReceipt 全在 try 外;非 OpConsensusError/OpStorageError 异常逃逸后既非 INVALID 也非 -32603,§4.3"存储故障绝不报 INVALID"退化为"什么都不是"。registerOpBlock 的两处 BOOST_THROW 在 catch 之后同样不经分类。建议补 catch(...) → OpExecutionInternalError(安全默认:未知故障不投反对票)。
  I1(Important) 存储增长量化: 真正的墙是 stateRootOf 每块重建全量 trie,经 visitAccountsImpl 对每账户两次 range 扫描,每次 range 为每一层建迭代器 → 2·K·(N+1)。K=100 时 N≈1e5(约2天)跟不上 2s 出块;K=1e4 时 N≈1e3(约35分钟)即跟不上。内存约 2.5GB/天。台账"scaling 非 correctness"低估 2-3 个数量级,应改为带 N 硬上界并升为接入阻塞前置。附带:visitAccountsImpl 绕过 m_accountCache + probeHasStorage 结果在 stateRoot 路径不用 = 2× 无谓开销。
  I2 §4.4 前提1 已被同文件通用路径证伪: mergeBackStorage 在 withCacheStorage 下走 tbb::parallel_invoke 真跨线程(测试用 CachedStorage=void 测不出,生产带 cache)。修 I1 的方向恰是让 OP 路径开始调 merge → 加 merge 那天持锁跨 co_await 许可同时失效。三条欠账实为一件事,应合并为"接入前置"。
  I3 c_opMode 是 SFINAE: executeOpBlock 签名漂移(改具体类型/加第二个 auto/加重载)→ 静默 false → 走通用分支 → OP payload 携空 withdrawals 通过校验 → FCU 后 head==parent 使 parentKnown 成立 → 返回 Valid + 自报 blockHash 且全程不执行区块(橡皮图章)。唯一护栏是三个测试里的 static_assert。建议列为生产接入必备检查项 + 通用分支对 version==4 直接拒绝。
  I4 x_state 独占期=整块执行时间(含秒级 stateRoot),阻塞 getSafeBlockNumber/getPayload/exchangeCapabilities;updateForkchoice 三次存储查询在锁外(当前只读良性 TOCTOU,SYS_CURRENT_STATE 落地后成真竞态)。
  M1 CMAKE_SOURCE_DIR 实测无遮蔽(仓库根无无扩展名文件)但作用于全 target 20+ 测试源,注释"有界"表述弱化范围。M2 "编入vs链库二选一"护栏机制描述错误(静态库无未解析符号则不抽取成员,静默通过,非 duplicate symbol)。M3 三 TU 匿名命名空间同名类型当前无 ODR 问题但 unity build 下是硬冲突(engine target 已 UNITY_BUILD ON)。M4 fixture 析构序全部正确;潜伏陷阱:OpBlockEnv::fiscoHeader 是引用成员且 executeOpBlock 是接引用的协程,任何"先拿 Task 稍后 await"的改法立即悬垂。M5 x_state 不可重入,补 BLOCKHASH 256 祖先窗口时若回调 engine 即自死锁。
  已核实无问题(免重复排查): 依赖名残留无第二处;异常逃出协程不 terminate(锁与 view 先于 handler 展开析构);co_return 在 catch handler 内合法;嵌套 syncWait 无重入(栈局部,内层同步完成即跳过 wait);applyDiff 不穿越 evmone noexcept 帧;ConsensusError→INVALID 与 StorageError→-32603 两条主路径均有真调度器/真桥实证覆盖(非仅桩)。
  未覆盖: OpSchedulerImpl.h:703 catch(...) 重抛的 OpConsensusError 抵达 engine 的链路(即 T4 修复真正针对的场景)engine 侧无用例。
终审并发事故: 视角1/视角3 在注入未还原时 API 断连,已核实二者断连前均自行还原(rg MUTATION|PROBE 零命中、git status 仅台账),已恢复继续。教训:变异实验类审查须与只读审查错开或独占工作区。
终审视角3(测试判别力猎杀) 完成: 12 次变异注入→编译→跑测→还原,6 红 6 绿。翻红落点精准(M1 十条/M4 八条/P1/P5 各三条与探针报告逐字吻合/M8/M9/M10 各一条且正是应红那条)=测试非摆设、探针数字经得起复现。扣分全在"绿灯来源":6 次全绿中 5 次是语料退化或无人守,其中 3 次恰落在文件头/探针报告宣称已闭环处。评级 B+。
  C1(Critical) gate 立身之本有 2/21 字段是空的: 语料 currentRandom 34 份全 0x0、currentCoinbase 全 0x4200..0011 → M2(prevRandao→h256{})与 M3(feeRecipient 硬编码)各 50/50 全绿。M1 之所以红只因 parentBeaconBlockRoot 恰好非零。诚实说法是"21 字段里 19 个被钉死"。生产后果:首个真实 OP 块(prevRandao 恒非零)会全量判 INVALID(fail-closed 非静默分叉),但这正是 gate 声称要拦的错误。修法最便宜:重跑 opt8n-ref 补 1 条 currentRandom!=0 且 currentCoinbase 非 0x42..11 的向量。【呼应 T3 审查已记 minor"语料盲区 currentRandom 恒 0",当时 defer,现被实证击中】
  C2(Critical) yParity>1 拒绝在 206 例零守护: M6 删两处 throw → 206/206 全绿。这是 T4 审查 I1 落地的共识级修复(op-geth 在 sender recovery 前即拒),注释自陈不加会被 static_cast<uint8_t> 静默截断,而本仓有 costofprecompiled 静默截断前科。修法:OpSchedulerImplTest 加 yParity=2/256 两例(构造信封即可,不需金值)。
  I1 探针②取景遮住唯一无人守的两条: M5(blobGasUsed/requestsHash 两条 §5.1 比对短路)50/50 全绿——step5 的 8 条比对有 2 条从未被触碰,而探针"只吞六项保留两条"的设计让读者读不出这点。jovian_da_mix 只证明"值相等放行"不证明"值不等拦截"。修法:StubOpScheduler mismatch 表加第 7/8 行。
  I2 块登记写侧只有桩守: M8 删 SYS_HASH_2_RECEIPT 全部写入 → 仅红 1 条(桩场景手搓 receipt);33 条真实块的真实 receipt 编码从未与任何东西比对。M7 把 review M2 否决的"静默截断+跳过空收据"改回去 → 206/206 全绿,那场设计辩论结论无测试固定。
  I3 stub VALID 端到端是自算同源(matchingCommitments 从 payload 逐字段抄),真实价值只有块登记接线;桩完全不看 BlockEnv → 9 个 env 字段桩层零判别力。
  I4 number/timestamp/baseFeePerGas 唯一支点是链式对一条用例(33 条这三字段全常量),M9 硬编码 number=1 仅红 ChainedPair 一条,且该用例第4步是 fatal ASSERT_TRUE,一旦被跳过/拆分三字段同时失守。
  m1 金值无机内钉死(golden json 无 generator_commit/校验和,pin 只在 README 散文)→ 若有人用本实现重生成金值,gate 静默退化为同义反复且全绿。m2 validationError 均子串匹配(find("blockHash") 也被 parentBlockHash 满足)。m3 33 条对 header 映射语境边际信息约等于 4 条(不应当作 33 份独立证据)。m4 VALID 后重复投递无用例。m5 记功:EngineOpBranchTest:360-365 主动声明本文件抓不到映射错误并点名 T6 承接;TxRootDriftScheduler 覆盖结构不可达的第六面——不应被后续重构简化掉。
  零测试守护清单: OpSchedulerImpl.h:441/483(yParity)、EngineServiceImpl.h:810-823(两条额外比对)、EngineServiceImpl.h:912-926(收据数护栏)。
终审视角1(共识正确性,基准 op-geth e8800cffe) 完成: 头哈希层判定等价(21 字段与 ExecutableDataToBlockNoHash 逐字段对齐,含 excessBlobGas OP 短路/requestsHash=sha256("")/常量项,无缺陷);校验层与状态机层未达同进同退。
  C1(Critical,已实测复现) 执行基态取当前链尾 view 而非 payload 声明的 parent 状态 + 缺"已知块直接 VALID"短路 → 重复投递已接受的块被判 INVALID(op-geth api.go:872-876 短路返回 VALID)。实测两条路径均复现:用户交易块因 nonce 已推进块级报错、纯 deposit 块因 mint 二次入账 receiptsRoot 失配。CL 超时重发/op-node 重启重放 unsafe 块是常规路径,非重组,不在 park 范围。唯一一条"拒绝 op-geth 会接受的块"的发现。
  C2(Critical) 外层交易 chainId 从不校验(仅 EIP-7702 授权元组校验 chainId)→ 跨链重放交易被接受;op-geth transaction_signing.go:284-285 ErrInvalidChainId 冒泡为整块无效。
  C3(Critical) 未强制 EIP-2 低 s 上界(recoverTxSender 直调 ecrecover 预编译语义,按定义允许高 s)→ 签名延展交易被接受,(r,n-s,1-yParity) 构造的块本实现 VALID、op-geth 整块拒(crypto.go:244-248)。
  C4(Critical) deposit gas_limit 无符号→有符号裸转换(OpSchedulerImpl.h:408 无范围检查,与同文件 narrowU256ToU64 纪律矛盾)→ 规范 8 字节 0xFFFF...FF 使 receipt.gas_used 为负 → blockGasLeft 被抬高约 2^63、cumulative 减小 → 同块后续交易可越真实 gasLimit 执行,gasUsed 变 2^64-|c| 且攻击者填同值即过六项比对 → 块 VALID。本分支引入(新解码器第一次让既有负值处理接受不可信线上字节)。
  I1(Important,不在台账) 无 timestamp>parent.timestamp 单调校验;放大效应:timestamp 是本闭环 fork 选择器(configAt),块 N+1 可用早于 N 的时间戳把活跃 fork 回退(Jovian→Isthmus)改变 DA 记账与 baseFee 语义。根因与 M5(Holocene baseFee)共用:s_eth_block_header 只写不读(全仓零处读取)。
  I2 OP 模式 FCU 强制 head 恰好 +1(op-geth 无此约束)且 OP newPayload 从不更新 m_trackedHeadBlock → 成批投递 payload 后再发 FCU(op-node 派生流水线常规形态)抛 InvalidForkchoiceState。
  I3 解码器接受非规范 RLP(前导零/u64 9 字节静默留低 8 字节/定长字段短负载右对齐补零、超长截断从不报错/单字节 0x00 当 false),op-geth 每一类硬拒;额外分量:computeOpTxRoot 取原始线上字节而 op-geth DeriveSha 是从已解析结构体再规范编码 → 非规范编码下两者对同一组 payload 字节算出不同 blockHash(OpEngineSeam.h:124-133 注释"DeriveSha convention"严格意义不成立)。deposit envelope 无签名自校验,from/to/sourceHash 长度不符被静默补零/截断成另一个地址后照常执行。
  I4 无"已知无效祖先"记忆 → 坏块后代返 SYNCING 而非 INVALID(op-geth invalidBlocksHits/checkInvalidAncestor),op-node 会持续同步一条永不成立的链。§6.4 park 的是 SYNCING 完整语义,判决倒挂本身未记账。
  M1 extraData 32 字节上限未校验(形状已 park,上限是独立一条)。M2 gasLimit>2^63-1 未校验(verdict 仍 INVALID 无接受面扩大,但同属无检查有符号窄化)。M3 无 ACCEPTED 桶(已 park,天然退化)。M4 存储故障→-32603 vs op-geth INVALID:本实现更安全,有意偏离应入台账。M5 Holocene baseFee 已 park,与 I1 同根因应同批解决。M6 executionRequests 真空成立已 park 且有 static_assert 钩子。M8 txRoot 一项当前恒真式(注释自陈)。
  总评根因: 静态校验层系统性偏松,方向全部是"接受 op-geth 会拒的块",根因不是漏某条检查而是从未读回 parent 头(s_eth_block_header 只写不读)→ op-geth verifyHeader 中所有以 parent 为参照的条款(timestamp/baseFee/gasLimit 变化率/gasUsed 上界)整体缺席。
  【关键方法论证据】金向量语料全部由可信 generator 产出规范字节、且按块号顺序单次投递,恰好绕开本次全部 Critical 的触发面 → 33/33 全绿 + 50/50 闭环不能覆盖 C1-C4/I1/I2/I4。
  修复优先级: C1(短路+基态断言) → C4(一行 narrow 检查) → C2/C3(解码器两条显式校验) → I1(打通 s_eth_block_header 读路,顺带铺 M5) → I2/I3/I4。
终审修复排期(用户"直接推进"授权,串行不并发):
  批1 解码器加固(已派,实施者 aff5d65) — 共识 C2 chainId/C3 低s/C4 deposit gas 窄化 + yParity 补测;要求每项"注释掉→翻红→恢复"自验
  批2 共识安全(brief 已就绪 final-batch2-brief.md) — B2-1 catch(...) 兜底【最紧急】/B2-2 已知块短路+非链尾 parent 断言/B2-3 gasLimit 上界/B2-4 extraData 32B/B2-5 消息修正/B2-6 CMake 注释
  批3 补测+文档(brief 已就绪 final-batch3-brief.md,硬约束不改生产代码) — 7 条补测(blobGasUsed/requestsHash 零覆盖、块登记真链路、收据数护栏、三字段唯一支点、金值 provenance、断言收紧、VALID 重投)+ 6 条文档传播(SYS_HASH_2_TX 仅披露不补实现<控制器裁定>、悬空引用、-32602 偏离、ConsensusError 覆盖不对称、归档标准、零散记账)
  批4 机制级(brief 已就绪 final-batch4-brief.md) — B4-1 timestamp 单调+打通 s_eth_block_header 读路【最高价值,为 Holocene baseFee 铺地基】/B4-2 解码器严格性+txRoot 同源澄清/B4-3 c_opMode 静默失效护栏/B4-4 visitAccountsImpl 2× 开销(桥 E-b 三腿零回归是硬线)/B4-5 可选 无效祖先记忆
  批5 语料重生成(待 op-geth 生成环境) — 补 currentRandom!=0 且 currentCoinbase!=0x42..11 的向量 + gate 逐字段扰动元断言
  不可直接修(需架构/外部输入): 重复投递完整版(blockHash→MLS 层映射不存在)、存储层 merge 时机(与 §4.4 协程契约耦合,加 merge 即失效持锁许可)、x_state 独占期、合流策略(待确认主线是否活跃重构)、SYS_HASH_2_TX 是否补写实现
终审批1 完成(commit 0811e497,206→212 例): C4 deposit+typed gas 窄化/C2 chainId 传参链/C3 低s+r,s范围/yParity 6 例补测。实施者自验抓到自己两个测试构造 bug(单交易块被"首笔必须 deposit"闸遮蔽、合约创建需 53000 intrinsic 而测试固定 21000),修正前均产生假"仍通过"。
批1 审查: 共识合规 ✅ Approved。逐条等价核实——C3 群阶取 Fr::ORDER(非 FpSpec)正确、s==n/2 放行两端一致、下界 r/s>=1 已加、homestead 无条件生效;C2 类型均 uint64 无截断面、deposit 正确排除(op-geth signing:270-277 同序)、加固了内层授权比对基准(此前 tx.chain_id 全由攻击者控制);C4 残余区间 (blockGasLimit, INT64_MAX] 经 OpDepositTx.cpp:95-96 显式抛出无缺口。不误拒:实测语料 206 笔真实签名交易(163 eip1559/39 deposit/4 setcode)逐笔过四道新闸。传参链只加在 detail:: 自由函数,executeOpBlock 签名逐字未动,c_opMode 探针未受影响。
  I-1(fix中) 实施者"C4-typed/C2-setcode 无法独立证明"被实测推翻:遮蔽只在异常类型层不在消息层(修复在位"raw tx decode: ..." vs 移除后"processOpBlock threw a block-level error"),改消息子串断言即 100% 判别。审查者另否掉换构造绕法(非空 authorization list 仍被 EIP-7702 每条 25000 intrinsic 挡住)。
  I-2(fix中)【核心缺口】decodeSetCodeTx 的 yParity 检查仍零守护::502 与 :548 是复制的独立语句非共享函数,两个新测试只喂 eip1559;删 :548 后 212/212 依旧全绿。"受命关闭的缺陷类只关了一半"。
  M-1 报告 test-bcos-engine 证据张冠李戴(engine/test 对 OpSchedulerImpl 零引用,touch 不触发重编译);真证据是 EngineNewPayloadGateTest.cpp:352/354 同 TU 双实例化。M-2 narrowGasLimit 缺 fieldName(支撑 I-1 消息精度)。
  ⚠️ 全部负向用例依赖未记录巧合: r=1 恰是曲线合法 x 坐标(1³+7=8,p≡7 mod 8 使 2 为二次剩余)故 ecrecover 成功,负向交易才能走到被测检查;换成非合法 x 值则先抛 "sender ecrecover failed",用例以错误理由静默转绿。已要求 builder 注释点明。
  ⚠️ catch(...) 兜底抹掉块级错误消息(fe2a40c29)是"类型层不可判别"的根因,任何需区分两种块级拒绝的未来测试都会撞同一堵墙——关联批2 B2-1。
终审批1 fix轮 完成(commit a47b00e7,5 项全 ADDRESSED): I-1 消息断言破类型层遮蔽(新增 expectOpConsensusErrorWithMessage)/I-2 setcode yParity 双子用例(实施者自验中独立重新发现该子用例同样需消息断言,同类遮蔽)/M-2 narrowGasLimit 加 fieldName 三调用点同步/M-1 报告证据改指 EngineNewPayloadGateTest.cpp:351-354 同 TU 双实例化/builder 注释记载 r=1 曲线合法 x 坐标约束。复审 3 组翻红实验全部成功(注释 setcode chainId / typed narrowGasLimit / :548 yParity 各一次,均翻红后还原复绿),212/212 零退化,无新破坏。
  【控制器操作失误教训】我在实施者返回通知前就派了复审,两者并发触碰同一文件,实施者最终校验时发现复审者的实验残留(双方均正确还原,无损害)。后续批次:实施者通知到达后再派复审,不并发。
终审批2 完成(commit 5ba78dbe,212→217 例): B2-1 catch(...) 兜底/B2-2(a) 已知块 VALID 短路/(b) 非链尾 parent 明确拒/B2-3 gasLimit 上界/B2-4 extraData 32B 闭区间/B2-5 消息修正/B2-6 CMake 注释。5/5 自验翻红。
批2 审查: 共识合规 ✅ Approved。零漂移经独立复核(diff -w --numstat = 24+90,^- 行数 0,无新成员函数);短路三元组与 op-geth api.go:871-876 逐字段一致且早于 newMutable 连 MLS 层都不新建;gasLimit 界 == params MaxGasLimit 0x7fffffffffffffff、extraData 闭区间 == MaximumExtraDataSize 32;语料 extraData 直方图 {9B:16, 17B:17} 全 ≤32 且 vectors 根本不含该字段→B2-4 对语料结构上不可达;五条共用 OpExecutionInternalError 的消息标识互不包含(unclassified exception/non-tip parent/storage failure/receipt count/null receipt)。自验抽验 3 次证实——同删 3b+3c 后 status==Valid/lvh/validationError 三条全过、仅 executeOpBlockCalls 实得 2,证明"VALID 本身判别力为零、计数锚点必要"。回归亲验 217/217 + 61/61 + test-bcos-engine 无错误。
  I-1(fix中)【真缺口】catch(...) 覆盖面未闭合: 只包住 executeOpBlock 一次调用;step2 computeTxRoot/rebuildOpEthHeader/hash()、step5 commitmentsOf、**step6 整个 registerOpBlock** 仍在 handler 外。其中两处 BOOST_THROW(OpExecutionInternalError) 自带分类不是问题,问题是同函数内 lexical_cast/Entry::set/encode()/hashImpl.hash()/四处 storage2::writeOne 抛出的非 OpExecutionInternalError 异常(bad_alloc/tars 编码异常)→ 逃逸后既非 INVALID 也非 -32603,B2-1 要消灭的形态换段代码重现。报告"engine 边界最后一层缺兜底已闭合"属 overclaim。
  I-2(fix中) 链尾判据假等价: :767-770 注释写 "parent is the tip iff nothing registered at child's height" 无条件成立不实。方向(i)成立;(ii)依赖未成文不变式"SYS_HASH_2_NUMBER 每个哈希的高度都在 SYS_NUMBER_2_HASH 被自己占据"(两表一致)。审查者实测反例:seed 两表不一致(P=0,Q=5)→#1 parent=P number=1 VALID(链尾=块1)→#2 parent=Q number=6 通过 3b/连续性(6==5+1)/NUMBER_2_HASH[6] 空 → 放行,实测 status=VALID executeCalls=2,parent Q 非链尾却用链尾基态。批5b 连续性补不上该洞。生产账本(两表成对写)判据正确,故只改注释+写入 spec §6.4 不变式。
  ⚠️(记账) -32603 是名义值——全仓无代码把 OpExecutionInternalError 映射到 JSON-RPC -32603,而报告与 5 处注释都写"返回 -32603";合法重组被 3c 挡下(首块判坏后 CL 投同高度竞争块得 -32603,op-geth 能正常处理)应在 op-node 实连前置清单置顶;B2-2(a) 对深层旧块同样短路,配合 3c 形成"重投旧块→VALID,投旧块兄弟→-32603"的相邻路径落差,须写进 spec。
  ⚠️ M-1 短路位置比 op-geth 晚(op-geth 在 parent 查找之前):blockHash∈HASH_2_NUMBER 但 parentHash∉ 时本实现答 SYNCING、op-geth 答 VALID;因两表成对写该状态不可达,仅记偏离。B2-6 CMake 注释改事实描述后护栏仍无构建强制。
终审批2 fix轮 完成(commit 45b2e860,217→220 例)+ 复审通过: I-1 分类屏障闭合——step2-6 全体移入新成员函数 runOpNewPayloadSteps,屏障 try/catch(OpExecutionInternalError){throw;}/catch(...)→-32603 三窗口全闭合,已分类异常经透传分支原样放行;I-2 链尾判据改方向性表述(⟸无条件/⟹依赖两表一致不变式,点名 registerVerifiedBlock 是显式豁免者)。新增 3 例逐窗口注入(Static/Comparison/Registration PhaseEscape)。
  复审三组翻红实验独立复现:①禁屏障 catch(...)→3 例全红 ②删透传分支→NonTipParent + UnclassifiedExecutionEscape **2 例**(报告只列 1 例,遗漏——根因:屏障用自己消息重新构造异常覆盖内层原始消息;已复现两次非 flaky)③禁执行期 catch(...)→1 例。三集合互不相同=防线未互相顶替。
  签名核查(T5b 坑):runOpNewPayloadSteps(const NewPayloadRequest&) → Task<PayloadStatus>,均为 framework 裸 struct 非 SchedulerType:: 关联名;EngineOpBranchTest.cpp:341 的 static_assert(!GenericEngineService::c_opMode) 显式用非 OP 调度器实例化整个类模板,编译通过即佐证。零漂移 84 增/6 删,6 行全注释(I-2 旧表述 4 行 + I-1 overclaim 2 行)。回归亲验 220/220 + test-bcos-engine 无错误。
  【连续三轮的假绿模式,已固化进批3 brief B3-13a】①批1:catch(...) 把不同拒绝抹成同一类型→只断言类型的用例在修复删除后仍通过 ②批2 自验:屏障与执行期兜底消息共有 unclassified exception 子串→只禁一层仍被另一层接住 ③批2 复审:删透传分支后屏障用自己消息重新构造异常覆盖内层。规则=凡新增兜底/重写消息的屏障,测试必须同时给正例标识(必须含本层)与反例标识(不得含相邻层),且每条防线独立翻红实验。
终审批3 完成(commit f31c537d,220→224 例)+ 审查 Approved(0 Critical/1 Imp/3 Minor): 补测 7 条 + 文档传播 10 条,生产代码零语义改动(唯一触及 EngineServiceImpl.h +14/-1 全注释行,B3-8 授权)。9 次变异注入 9 次翻红,审查抽验 3+1 次全部属实(恰好 1/2/2 红 + provenance 红)。
  【关键对照实验】同一处生产缺陷(B3-6 静态 Isthmus 消息改写成比对桶形状):旧子串断言下 224/224 全绿,新精确断言下 2 红 → 断言精确化被正面实证为判别力净增加,非洁癖。反向脆弱性判定"有界且响亮"(10 字面量最高重复 3 次、比对面走参数化只硬编码前缀、EXPECT_EQ 打印 expected/actual)。
  B3-5 三条自陈核实为真: manifest 里确无 commit 串(grep=0),pin 真在 vectors/*.json 的 _op_test_vectors.generator_commit(33/33 携带 e8800cffe...);SHA256SUMS 脱离本仓 shasum -c 独立验证 39/39 全 OK(=33 golden + chained 6);诚实边界"挡不住同时刷新清单的重新生成"表述准确,三处一致未包装成密码学保证。
  【控制器裁定被实测证实】B3-11 闭合入口存在,实施者 CONCERNS #2 两句均不成立: OpSchedulerImpl.h:760 try 内调 processOpBlock,:769 catch(...) 在 :787-791 重分类为 OpConsensusError;该 catch 注释 :777-779 自己点名 OpBlockExecute.cpp:40 的 throw 会逃逸 typed catch;审查者探针实测(首次即通过)返回 Invalid + lvh==parentHash,validationError 逐字含 "typed catch bypassed by a known RTTI issue..."(该消息只由 :769/:788-791 产生)。最省构造 ~12 行:prepareScenario("isthmus_contract_logs") → raws.erase(begin()) → resealBlockHash。已派 fix 补测 + 撤回错误归因。**教训:实施者的"做不到"结论必须验证,否则会诱导批准不必要的生产代码改动。**
  fix 轮(进行中): I-1 补测 B3-11 + 撤回 CONCERNS#2 归因;M-2 spec:403 漏否定词(断言了与事实相反的意思);M-3 README:123 引用未 git-tracked 的 probe-ledger-bridge-report.md——违反本批自己新写的 §8.1,需 add -f + 全量引用扫描;M-4 B3-5 自验表列 4 例实为全库 11 例红(加了 gtest filter 未披露,与 B3-13b 纠正批 2 的同类遗漏讽刺地重合)。
  ⚠️ 补记 §6.4: catch(...) 丢弃 e.what() → OpBlockExecute.cpp 4 处 throw(空块/首笔非 attributes/deposit 乱序/非 deposit 验证失败)抵达 engine 后共用同一条泛化 validationError,运维无法区分;且限制 I-1 补测的断言精度(只能断到"走了 catch(...) 这条腿")。
  ⚠️ 澄清: 视角3 的 C2(yParity>1 零守护)**已在批1 完全关闭**(主提交 YParityEquals2/256 + fix 轮 setcode 双子用例 + 翻红自验),勿重做;C1(语料 currentRandom/currentCoinbase 恒定)属批5,且重跑 opt8n-ref 必须**同步刷新 SHA256SUMS**。
终审批3 fix1(ca18c4fb2)+ fix2(7421a988f) 完成,批3 收口(三提交 f31c537d→ca18c4fb→7421a988,224→225 例):
  fix1: I-1 补测 ConsensusErrorViaCatchAllReclassificationIsInvalid(真调度器+真桥,12 行构造首次即通过)+ 撤回"入口不可达"归因;M-2 spec:403 否定词;M-3 悬空引用 add -f;M-4 证据披露改全库口径;§6.4 (j) 补记 catch(...) 丢弃 e.what()。复审独立复现第二种翻红(改抛 OpStorageError → 223/2,与实施者逐字一致),证明该用例钉的是 INVALID 分类而非"有异常";3 个 gtest_shuffle 种子均 225/225,澄清"顺序依赖"误判系重建缺失。
  fix2: 复审发现 **fix1 自己引入第四处悬空引用**(spec §6.4 (j) 引用 docs/audits/2026-07-12-typed-catch-rtti-investigation.md,该文件仅存在于另一分支 feat-evm-mb1-block-execution d0937e8a1 且路径带 bcos-evm-ref/ 前缀),使"复扫 0 处"失实。裁定不迁移文档、改引用。实施者全仓扫描又发现 3 处代码注释同源悬空(OpSchedulerImpl.h 1 + T8nReplayHarness.h 2),共修 4 处。控制器自验确认本闭环 spec/README 的 4 条 .superpowers 引用**全部 TRACKED**。
  【控制器操作教训】自验脚本两次输出误导:①rg 无匹配时打印帮助文本被 while 当文件名 ②RTK 压缩输出合并了重复行前缀,险些据此报告"4 处全悬空"的错误结论。**关键判据必须每行自带明确标签(TRACKED/DANGLING),不可依赖压缩输出。**
  裁定(记账不修): A 面剩余 5 处跨 epic 悬空引用属其他 epic——opstack 移植 spec 引用未入库的 task-{6,7}-report.md、eth-utils-removal-c-route-todo.md 引用 task-7-brief.md(与 §8.1"brief 不入库"直接冲突,应改引用而非归档)、t8n/vectors/{DIVERGENCES.md,manifest.txt} 引用未入库的 docs/superpowers/plans/。合流时统一处置。B 面 6 处已归档历史报告的同源引用**保留不改**(采纳实施者判断:追改会掩盖"这条引用一直是悬空的"这个事实本身)。
  后续项(已列入报告,本轮不实现): CI 引用完整性检查——抽 spec/README/**代码注释**里 .superpowers/、docs/ 且以 .md/.txt/.json 结尾的路径比对 git ls-files;代码注释必须纳入(本轮 4 处里 3 处在代码注释,只扫文档会全漏);例外机制=指向仓外/他分支的路径必须带仓或分支前缀,或写成省略号形式表明不可解析(本轮 4 处修正可作检查器正样本)。
用户裁定: SYS_HASH_2_TX **需要补写实现**(不止披露)→ 新增批 6,排在批 4 之后(brief final-batch6-brief.md 已就绪)。
  控制器预调研查明两条硬约束: ①通用路径 LedgerMethods.h:123-148 写入的是 bcos protocol::Transaction 的 encode(tars),**不是**以太坊 raw envelope,且有 storeToBackend 去重语义;②该表有真实读取侧消费者(Ledger.cpp 多处 asyncGetRows/批量按哈希取交易会解码回 bcos Transaction)。**故直接写 envelope 原字节不可接受——通用消费者会拿到解不出或解错的对象,静默错误结果比明确缺失更危险。**
  要求实施者先给方案再动手,候选:(A) 映射为 bcos Transaction 编码后写入(需确认 deposit sourceHash/mint/isSystemTx 与 7702 authorizationList 是否有损)/(B) 写 raw envelope 到 OP 专用表 s_eth_hash_2_rawtx(表名按 B5 放 bcos-evm/engine/,不动 LedgerTypeDef.h)/(C) 两者都写。控制器倾向:映射有损则选 B(保真优先不污染通用契约),无损则 A 最省,C 仅当"通用查询确需 + 映射有损"同时成立。
终审批4 完成(c39449a01 + a0fab9cd3,225→237)+ 审查 Approved + fix 轮(f38e941d7,→238/238):
  四项:B4-1 timestamp 单调(op-geth 等号也拒)+ 打通 s_eth_block_header 读路(完整 21 字段 decode,33 条金值逐字段 decode+re-encode 双向锚定,规避"读反+写反"假绿)/B4-2 解码器严格性(逐类对上 op-geth rlp ErrCanonInt/errUintOverflow/ErrCanonSize/Bool 只认 0-1)/B4-3 c_opMode 护栏/B4-4 visitAccountsImpl 优化(实测热路 7→2 即 3.5×,高于 brief 估的 2×)。
  审查亲自复现三条关键实测:去护栏后 V4 请求 "Actual: it throws nothing"(**橡皮图章后果链从推演变为实测**——落通用分支后 parentKnown 由内存 forkchoice head 满足 → makeStatus(Valid, payload.blockHash),全程不执行区块);B4-4 回退后三条计数断言全红而 EbT8nReplay 仍绿;B4-1 边界改 < 后 EqualParentTimestampIsInvalid 翻红。
  【审查者反证实验的价值】I-1: 实施者把"未命中不得入缓存"写成"唯一正确性风险点"却只落注释;审查者故意 emplace 回缓存 → **237/237 全部照绿** = 零守护空档。后果链真实(has_storage 默认 false → state.cpp:259 has_initial_storage → EIP-161/7610 空账户清除与 CREATE 碰撞判定)。fix 后 VisitAccountsMissDoesNotPoisonAccountCache 反证翻红。
  **实施者自我总结(建议固化进 §11)**:"凡是在报告里被我称为'风险点'的东西,都必须当场配一条会翻红的断言,否则那句话就是自我安慰。"
  I-2: 实施者以"spec 由协调者统一维护"为由把记账留报告,被 git log 证伪(批3 实施者自己改过三次 spec,§6.4 rev.3.2 的 f-j 即那时落的)。已落 rev.3.3 表 k-p 六条,k/l 置顶 op-node 实连前置清单。
  **【控制器裁定·边界问题】实施者可直接改 spec/README,这是本工作流惯例**——记账项由发现方当场落地,不得以"协调者统一维护"为由留在报告里。报告是过程记录,spec §6.4 才是读者会查的欠账台账。
  【构建纪律升级】上轮 standalone 构建目录陈旧,实施者"复绿"只在 in-tree 做过(审查者首次构建 standalone 时发现目标文件被重编才暴露)。**自验三步"还原+重建+复绿"在多构建目录仓库须对每个目录各做一次**——已要求写进通用自验条款。
  遗留: timestamp 单调的金值锚**只有链式对 1 次采样**(33 条孤立向量按契约跳过 parent 头检查,全二进制仅 4 次命中),已加 DO-NOT-DELETE 注释,根治靠批5 语料扩充(多块链);M-4 bool 0x01 分支零正例覆盖(isSystemTx 在 Isthmus+ 恒 false,语料造不出);⚠️ engine target 未链 codec,EthBlockHeader::decode 靠测试二进制传递链接,生产组合根接入时须处理。
终审批4 fix轮 复审 PASS(f38e941d7,238/238): 六项全 ADDRESSED,独立复现——I-1 反证实验精确 237/238 仅新用例翻红且失败消息逐字命中("visitAccounts 的 miss 结果污染了 m_accountCache");I-2 的 k-p 六条逐条核对无灌水(要求 (a) 本身点名两个欠账项 → k,l 两行,故 2+1+1+1+1=6);standalone 131/131 亲验(上轮正是此处陈旧出错);E-b 三腿 33×3 全绿。
  复审附注: standalone 套件经 if(TARGET bcos-framework) 门控**结构性排除** Storage2LedgerTest/EngineNewPayloadGateTest 等,I-1 守护测试在 standalone 不运行——属设计如此,报告未误称,记录备查。
  "置顶"的实现方式: k/l 是 rev.3.3 新增子表的前两行而非物理移到 a-j 之上,符合该文档"逐版追加不重排"的既有约定,正文已显式标注 k/l 为全清单最高优先级。
批4 收口。修复阶段测试增量总计 206 → 238(+32),每条增量均经"注释掉修复→必须翻红→还原→重建→复绿"自验。
终审批6 完成(f898e0a6c + 7b7e0afb3,238→239),复审已派(未回):
  **设计先行是本批最大价值**。用户裁定"需要补写实现"后,brief 要求先给方案再动手。调研结果推翻了"方案 A 只是代价高"的预设——(A) 写通用 SYS_HASH_2_TX **会造出假交易**:takeToTarsTransaction() 只覆盖类型 0/1/2/3(0x04/0x7E 在 Web3Transaction.cpp:408-413 硬拒);tars IDL 无 sourceHash/mint/authorizationList 承载位;Transaction::verify 无条件 ecrecover 并对 Web3 类型 forceSender → 未签名 deposit 得到**伪造 sender**;且 tars 反序列化任意字节**通常不抛**(字段全 optional + Tars.h:328-356 空 catch),工厂在 checkHash=false 下重算自洽哈希,返回**非空、看起来合法的假交易**,hash() != key 而无人校验 → 流入 eth_getTransactionByHash **与 txpool requestMissedTxs(共识/提案校验)**。
  裁定 **(B)**:raw envelope 写 OP 专用表 s_eth_hash_2_rawtx(表名常量按 B5 放 bcos-evm/engine/,未动 LedgerTypeDef.h),通用表**刻意不写**并在 spec/README 写明这是 OP 专用检索面。拒绝 (A) 与 (C)。
  新用例 RawTransactionEnvelopesAreRegisteredUnderEthTxHash 覆盖三类型各五条断言,含**反例断言 SYS_HASH_2_TX 必须仍为空**。registerOpBlock 现写五张表。
  §6.4 新增 q/r/s: q = bcos-rpc/EngineEndpoint.cpp:164 把以太坊 RLP 信封喂 tars 反序列化器且 rawTransactions 在任何生产路径从未赋值 → **op-node 实连第一道墙,已置顶于 k/l 之上**;r = LedgerMethods.h:233-235 未 has_value() 即解引用;s = RocksDBStorage.cpp:228-233 成功回调在 try 内 → 双回调 → 同一协程 handle resume() 两次(UB)。r/s 均 pre-existing,记账不修。
  实施者自陈三条待复审核实: ①**standalone 腿对本条不是红绿见证**——engine 测试仅编入 in-tree(if(TARGET bcos-framework) 门控),禁用写入时 standalone 照样 131/131,它只是无回归检查;建议把"各做一次自验"与"都构成红绿见证"这层区分补进 §11(控制器倾向采纳)。②命名债 ValidPayloadRegistersAllFourTables 现写五张表,**故意不改名**保批 3 报告的按名可追溯性。③零漂移:EngineServiceImpl.h 42 增/10 删,10 行删除全是注释行,-w 过滤后非注释删除为 0。
  【控制器操作纪律】本批与批 4 各有一次 watchdog 停滞,两次都停在"改完代码正要动文档"的节点(未提交 ~818 行 / ~180 行)。**已改为:代码一到绿就先提交,再动文档。**
