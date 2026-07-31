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
终审批6 复审 **ACCEPT**(547b7308d):0 Critical / 0 Important / 7 Minor,批 6 收口。
  实现正确性独立核实: 五张表键/值/时机全对;第 4/5 表**共用同一 txHash 局部变量** → 同键是结构性保证而非测试巧合;失败语义为全有或全无(写入落在局部 fork,pushView 在 registerOpBlock 返回后)。
  两次注入实验(审查者自跑): ①注释掉新写入 → RawTransactionEnvelopesAreRegisteredUnderEthTxHash **唯一翻红**(238/239);②故意也往 SYS_HASH_2_TX 写 → **第 5 条反例断言精确翻红,三类型各一次,断言 1-4 保持绿**,全仓仅该用例红 → 证明这条反例断言是唯一护栏、非冗余。
  文档核实: q/r/s 与 §6.4 (f) 的**每一个引用行号逐一打开源码核对,全部属实**。并**加强条目 q**:rawTransactions 全仓**零生产赋值点**(不止"RPC 层未赋值"),该通路生产上完全断开——审查者背书置顶,且认为实际严重度高于文档描述。
  【方法论收获·比实施者自陈更进一步】M-3: 实施者说 standalone 腿"只是无回归检查",审查者查明**更弱**——本批改动的两个头文件**全部 includer 都在 if(TARGET bcos-framework) 门控内**,standalone 二进制三轮重建时间戳未变(依赖图无边),其 131/131 是**空真**,连"无回归检查"都算不上。
  → **§11 待修订(控制器采纳,与 5 视角结论一并落)**:除既有"每目录各做一次"外追加三句——(a) 报告必须点名**哪个目录是红绿见证**;(b) **依赖图为空的目录属空真,不得计入证据**;(c) **每批至少一个目录构成红绿见证**。不加 (c) 会留下真实失败模式:有人只在看不到该测试的目录跑注入、报"注入后依然全绿"而被误读为无回归——本批 standalone 腿正是这个形状,靠实施者主动披露才没成事故。
  【新发现待补记 §6.4】M-6: 两张 OP 专用表(s_eth_block_header / s_eth_hash_2_rawtx)**不在 Ledger.cpp:2001-2012 的创世表清单**,storageTool/archiveTool 对其不可见。既有缺口,批 6 扩大一格,台账未记。
  其余 Minor: M-1 命名债(审查者判实施者"不改名"的取舍**方向反了**——一次性重定向成本 < 持续误读成本,但有注释兜底故仅 Minor,下次触碰时改名+注记);M-2 EngineServiceImpl.h:1243 注释方位词写反;M-4 断言 3 对 0x02/0x04 用同一谓词未用尽判别力;M-5 OpSchedulerImpl.h:717 表名再发布无测试锚定(替身与断言同源,沿袭 c_ethBlockHeaderTable 既有形状);M-7 spec 与 README 日期口径不一。
  零漂移独立复核: git diff -w 42 增 10 删、非注释删除 0;OP 依赖名未进签名——**由 test-bcos-engine 用无该成员的 StubScheduler/BloomScheduler 实例化通过反证**(比读代码更强的证据)。
  ⚠️ spec §11 修订与 M-6 补记**故意延后**:5 个全分支复审视角正在读 spec 做一致性核对,此刻改它会让 P5 读到移动的靶子、行号失效。待 5 视角回收后合并处置。
全分支复审 P1(共识语义)回收:Critical 3 / Important 7 / Minor 5。报告 wb-review-p1-report.md。
  C1 baseFeePerGas **零校验**(EngineServiceImpl.cpp:334 与 .h:978 原样送进重组头与 OpBlockEnv;validateOpNewPayloadRequest .cpp:185-302 与六项比对面 .h:1044-1098 全文无该字段)——op-geth consensus/misc/eip1559/eip1559.go:53-56 强制 expectedBaseFee 相等。已记账条目 e,但**优先级被低估且零测试覆盖**(gate 测试 :534 直取向量 baseFee,变异矩阵无该项 → "33/33 全绿"对此零信息)。
  C2 -38005 闸放行 pre-Isthmus V3 → 随即必判 INVALID。C3 bad_alloc 等本地故障被 OpSchedulerImpl 的 catch(...) 改写成 ConsensusError → engine 判 INVALID(engine 两道 -32603 防线对该路径是**死代码**)。
  I1 FCU safe/finalized 零哈希答 SYNCING 致永久停滞 / I2 FCU head 跳跃>1 抛协议外异常 / I3 缺 gasUsed<=gasLimit 头校验 / I4 c_opMode 塌陷护栏**只挡 V4,V3 橡皮图章仍开**(台账 m 只覆盖 V4) / I5-I7 为记账项复核成立。
  **P1 主动删除两条既有欠账表述(有 op-geth 出处,控制器采纳)**: ①gasLimit 变化率对 OP 链不适用——op-geth eip1559.go:37-42 的 `if !config.IsOptimism()` 明确放行瞬时调整,本实现只查 ≤2^63-1 是**正确对齐而非缺口**;②excessBlobGas 恒钉 0 同样正确——CalcExcessBlobGas 对 OP 短路 return 0,Jovian 也不例外。
  【控制器独立核实 C2,结论比 P1 更强:这是 spec 自身两处规定打架】
    - spec:64 目标版本 = "newPayloadV4 + FCU V3(**Isthmus/Jovian**)" → pre-Isthmus **设计上不在范围内**;
    - spec:219 却把 -38005 闸写成双向 "Isthmus+ 禁 V3,**pre-Isthmus 禁 V4**" → 即闸**故意放行** pre-Isthmus V3;
    - 代码忠实实现了 :219(EngineServiceImpl.h:694 `isthmusActive != (version==4)`,pre-Isthmus+V3 → false!=false → 放行),随后撞上 .cpp:219-225 **无条件**要求 withdrawalsRoot → INVALID;
    - op-geth api_optimism.go 反向:pre-Isthmus **必须** withdrawalsRoot 为 nil(非 nil 才报错)。
    → 故本实现对一个 op-geth 会接受的合法 pre-Isthmus 块投 INVALID。**关键在于桶错了**:超范围应答"我处理不了"(-38005/-32603),而不是"这个块是坏的"——与 §4.3"存储故障绝不能报 INVALID"同一条纪律。最小修法约 3 行:闸改为对 pre-Isthmus 一律 -38005。
全分支复审 P2/P3/P4 回收(P5 未回)。
【P2 解码与执行】Critical 3 / Important 3 / Minor 3,三条 Critical **均不在 §6.4 台账内**:
  C-1 **RLP 长形态长度前缀的前导零未被拒绝**。RLPDecode.h:92-113 只查 `payloadLength < 56`,不查长度字节自身前导零;op-geth rlp/decode.go:1113 `if buffer[start] == 0 → ErrCanonSize`。攻击面:把 `0x7e f9 01 04 …` 改写成 `0x7e fa 00 01 04 …`(payload 一字不动),本实现解出**完全相同**的 DepositTx;而 computeOpTxRoot(OpEngineSeam.h:172-185)对**原始线上字节**建根 → 攻击者只要让 payload.transactionsRoot 与非规范字节自洽,本实现判 VALID、op-geth 判 INVALID。**证伪 spec §6.4 (n) 与 OpEngineSeam.h 两处"已拒绝 Go 会拒的一切非规范编码"的断言。** 测试打不到的原因已定位:OpSchedulerImplTest.cpp:425-476 两个手写 writer 的注释明写"non-canonicality 只放 payload 不放 framing"。
  C-2 EIP-7702 授权项 yParity 解成 uint256(OpSchedulerImpl.h:477 decodeU256Scalar),op-geth tx_setcode.go 是 **uint8**;yParity 编成 `0x82 0x01 0x00` 时本实现在 OpTransition.cpp:67 `continue` 跳过该授权、块可 VALID,op-geth 整笔解不出判 INVALID。
  C-3 legacy / 0x01 被硬拒 → op-geth 正常处理的块本实现判 INVALID;spec:311 "唯二 op-geth 能处理而本实现硬拒"**不实**(实为三条,且第三条触发频率最高)。
  Imp-3 规范性严格层在 OpSchedulerImpl.h 与 EthBlockHeader.cpp **被复制成两份独立实现**,六条严格性用例只测了其中一份。
  Imp-2 mapOpReceipt 丢 cumulativeGasUsed/contractAddress/output,而这些回执**要落库**,不在 §2 非目标覆盖内。
  【控制器独立核实 C-1:确认】RLPDecode.h:92-113 逐行读过,确无长度前导零检查;op-geth readUint(decode.go:1098-1114)的 `buffer[start]==0` 分支已打开原文。C-1 成立,且是本轮唯一"可被主动构造、当前即可达"的共识分歧。
【P3 存储与生命周期】Critical 0 / Important 6 / Minor 4。E-b 世界内四条正确性性质**全部成立**(写回原子性:pushView 在全部成功之后;失败块零残留;桥一块一实例;缓存写穿一致)。
  I-1 **probeHasStorage 不判零值**(Storage2Ledger.h:682 判据不含 is_zero),而同文件 :486-489 自己写明"生产 HostContext::set 对零值照写不删,真实链账户表必然含零值槽行" → 生产账本上 has_storage 误真 → 消费链已跟到底(state.cpp:259 `.has_initial_storage` → host.cpp:88 `if (acc.has_initial_storage) return true` 的 is_create_collision)→ **同一个 CREATE2 在 op-geth 成功、在本桥 INVALID**。
  I-2 /apps/ 下任一非 20 字节 hex 表名(BFS 建的表)→ 每个 OP 块永久 -32603。 I-4(新)executeOpBlock 为取一个已知地址的槽做了全量 visitAccounts,白白物化约 26% 账户的完整槽表。
  I-5(新)SYS_NUMBER_2_BLOCK_HEADER / SYS_NUMBER_2_TXS **同样不写**,§6.4 (f) 只记了 SYS_HASH_2_TX → 通用"按块号取块"对 OP 块抛异常。
  **I-6 台账 (r)/(s) 被 P3 下修(与批 6 复审不矛盾:批 6 核的是行号与代码形态属实,P3 核的是可达性与后果)**:(r) LedgerMethods.h:235 确为未判 has_value 的解引用,但守门是 :217-218 的 SYS_HASH_2_TXS 行,而 registerOpBlock 连 SYS_NUMBER_2_TXS 都不写 → 对 OP 块**结构性不可达**,台账"会先由 OP 块暴露"的**因果是反的**;(s) 双回调形态确凿,但 double-resume 需异常从 handle.resume() 逃逸,而 bcos-task/Task.h:62-70 有 continuation 时已捕获、syncWait(Wait.h:56-91)根任务全包 try/catch → 后果是**错误分类而非 UB**。
【P4 代码组织】Critical 0 / Important 5 / Minor 8。
  I-1 engine 对 SchedulerType 有 **11 条要求,只有 1 条被 c_opMode 编译期探测**(EngineServiceImpl.h:186 只探 executeOpBlock),OpEngineSeam.h:5-26 自称"公开接缝面"却只公开 3 条。给 OpBlockCommitments 加第 9 个成员 → EngineServiceImpl.h:1052-1093 的 else-if 链**一字不改继续编译通过,新字段永不比对 → 错判 VALID**;给 OpBlockEnv 加字段 → engine 静默传 0 → 好块判 INVALID。§6.4 (i)② 只覆盖了反方向。
  I-2 decodeEip1559Tx/decodeSetCodeTx 是 **40 行克隆、5 处校验各写两遍**——正是 a47b00e78 yParity 事故的同一结构;P4 预言"下一个必然只补一边的是 op-geth ErrTipAboveFeeCap"。
  I-5 CMake 注释与 §6.4 (i)④ 共同断言的"仓库根无无扩展名文件"**被证伪**(LICENSE 存在)——结论对、论据错,而论据错会让下一个在根加 `version`(真实 C++20 标准头名)的人误判。
  **【P4 证伪一条既有欠账,控制器复核确认删除】**"EthBlockHeader::decode 靠测试二进制传递链接、生产接入须处理"**不成立**:基线 42e62fcef 的 rlp/ 只有 3 个 .h 无 .cpp,故基线无需该行;本分支新增 bcos-codec/CMakeLists.txt:24 `aux_source_directory(bcos-codec/rlp SRC_LIST)` 已把 EthBlockHeader.cpp 编进 codec,bcos-ledger/CMakeLists.txt:27 链 codec 为 PUBLIC、engine/CMakeLists.txt:14 链 ledger 为 PUBLIC → engine 传递拿到定义与 include。残留仅"engine 未具名 codec"这条弱隐患(改 ledger PUBLIC→PRIVATE 会打断,一行可修)。依赖方向零违反:engine/ 内所有 "bcos-evm" 命中均为注释,无一条 #include。
`[需验证]` 实验统一执行完成(wb-verify-report.md,4 次增量提交):**20 个实验,19 证实 / 1 证伪**。全部注入已还原,build 重配重建,in-tree 239/239 + test-bcos-engine 绿,standalone 131/131,git status 干净。
  【硬证据升级】P2 C-1 从"读代码得出"升级为**实测**:`0xf9 0x00 LL` 被接受,且 txRoot **实算出两个不同值**(0xe7fe64f3… vs 0xd1c4b2f3…)→ 共识分歧不再是推理。同时证伪 OpEngineSeam.h:156-163 的"解码器已拒绝 Go 会拒的一切非规范编码"。
  P2 C-2 实测(auth.v=256 被接受)/ P3 I-1 实测(零值槽上 has_storage=1)/ P3 I-2 实测(/apps/HelloWorld → poisoned=1)/ P1 C2 实测(pre-Isthmus+V3 → Invalid,validationError 逐字命中预测)/ P1 C3 实测(bad_alloc → INVALID,消息 "…rejected the payload: std::bad_alloc")/ P1 I1 实测(零 safe/finalized → Syncing,tracked head 未推进)。
  P5 E3 **8/8**:每一条零覆盖静态校验删掉后 239/239 全绿。E5(expectExhausted 的 throw 可删,8 个调用点全无守护)、E6(gasLimit 只靠 isthmus_big_block_130tx 单条向量)同样证实。
  **【批 8 解锁】P4 §3.3 实测:嵌套 `requires std::derived_from<typename S::ConsensusError, std::exception>` **软失败**,两次独立证明(最小 TU + 真实 OpSchedulerImpl vs SchedulerSerialImpl,四条 static_assert 全过)→ concept 方案**无需退化**,c0288b8b0 那类硬错不会重演。**
  P4 §3.1 三腿全证实:给结构体加成员**静默编译通过且全绿**(= 新字段永不比对的后果链闭合);结构化绑定 tripwire 硬报错;tripwire 在 8 成员下无害(含通用组合根)。§7.1 三腿证实 CMake 护栏可真正强制。
  **【E4 证伪 —— 且证伪的是测试自己的注释】** 删掉已知块短路后测试确实翻红,但**不是文档所述的机制**:step 3c 抛 OpExecutionInternalError("non-tip parent not supported",即生产 -32603),在第一个子用例就中止整个 TEST。单跑 isthmus_deposit_only 得到同一异常 → `EngineNewPayloadGateTest.cpp:1216-1234` 声称"两个子用例都以 INVALID 返回、经由两种不同机制"**两处都假**,且与其上方 10 行的放置理由自相矛盾。
  【实验者自己抓到的操作事故·比结论更值钱】它险些记下错误的见证声明:`Storage2LedgerTest.cpp` 与 `OpSchedulerImplTest.cpp` **同样在 if(TARGET bcos-framework) 门控内**,故 **standalone 对本轮每一个实验都是空真**,不止 engine 那几个。发现途径:standalone 重建报 "Built target" 但**二进制日期是 7-29、源码是 7-30**。→ 印证批 6 复审提的 §11 三句修订(点名见证目录 / 空真不计入证据 / 每批至少一个真见证)是必需的,已在报告 §0 更正。
  【两条对既有结论的精化】①E1c/E2b(实验者自加的反向对照)**收窄** P5 I-1:这两个字段**并非无人读**——任何偏离语料常量的取值会让 11 个测试翻红;真正的盲区精确地是"硬编码成语料常量"这一种,而这恰是 prevRandao 最现实的回归(语料 0x0,生产是 L1 mixDigest)。②P3 I-2 **比原报更糟**:`firstError()` 是字面量 `"std::exception"`(boost 的 non_hex_input 从不覆写 what()),运维在既有链上会看到每个块都 -32603 且**理由为空内容**。
  未做项:P5 建议的 4 条 UB 检查的 ASan 变体——语料的 optional 恒有值,普通构建下解引用根本不发生,ASan 无信号;要观察它需要 I-2 建议的**新测试**,不属本验证轮。
终审批9 已派(账本桥零值槽,P3 I-1 已实测确认存在)。构建目录移交批 9;状态分歧审计(只读)并行中。
【控制器亲自完成批 9 第一步调研(三次 529 过载后接手,纯读任务)——结论与假设方向相反,批次划分需改】
  **Q3「零值槽会不会进 state trie」答案:不会。** `fetchAllStorage`(Storage2Ledger.h:485-494)对零值槽**直接 throw**,而非收进 map:
    `if (evmc::is_zero(slotValue)) throw std::runtime_error("...zero-valued storage slot ... indicates a write-back leak")`
  该 throw 被 `visitAccounts`(:250-265)的 `catch(const std::exception&)` 接住 → `poison()` → 遍历中止、产物全部作废。而 `stateRootOf` 正是走 visitAccounts → visitAccountsImpl(:536)→ fetchAllStorage。
  **所以没有静默的 stateRoot 分歧(好消息,比担心的情况好),代价是响亮的全面失败:生产账本上每个块都 -32603。**
  **而这一点桥自己在 :486-489 的注释里逐字预言过**:"此规则仅在桥自写的 E-b 世界成立……**不得被继承到编排接入层**(生产 HostContext::set 对零值照写不删,真实链账户表**必然**含零值槽行)"。**但全仓不存在任何 E-b/生产模式开关**(grep ProductionMode/strictInvariant/mode 皆空),规则是硬编码的——"不得被继承"没有任何机制保证。
  **两处判据互相矛盾且方向相反**:fetchAllStorage:485 **查**零值 → throw(节点瘫);probeHasStorage:682 **不查**零值 → 返回 true(EIP-7610 误判)。以太坊正解是"零值槽 = 槽不存在",两处都不对。
  → **批 9 范围重定义**:不是"三行加个 is_zero",而是把零值语义在三处统一到"零值 = 不存在":①fetchAllStorage 从 throw 改 skip;②probeHasStorage 加 is_zero;③读单槽路径与前两者一致。**同时必须保住 E-b 世界的写回泄漏检查**(那条 poison 有真实价值,直接删会丢一条不变量守护)——需要一个"不得被继承"的落地机制,而不是把检查一删了之。
  → **优先级上调**:与条目 q 同级,列入 op-node 实连前置清单(它和 P3 I-2 的 /apps/ 非 hex 表名是同一个 poison 机制的两个生产触发点,后者已被上一轮实测确认 poisoned=1)。
  【复现的失败模式】这条与终审视角 4 的总评同型:**"发现了但没传播到位"**——桥的注释精确预言了生产后果,却始终停在注释,没进 §6.4 台账、没进接入前置清单,于是五轮审查里没人把它当接入阻塞项看。
  【运维可见性叠加】P3 I-2 已实测 `firstError()` 是字面量 "std::exception"(boost non_hex_input 不覆写 what()),故上述 -32603 抵达运维时**理由为空内容**。
基础设施状况:批 9 连续三次因服务端 529 Overloaded 在第一步终止(工作区每次均干净、零损失);状态分歧审计同因 529 终止,报告未创建。派发暂停,待服务端恢复。
终审批9 完成(3d96d0665 三问初稿 + 900ee0e22 修复+6例 + 6f4359ac4 墓碑实测 + f735b07a9 文档,**239→246**),复审已派。
  裁定落地:读路零值槽改为**跳过**(以太坊语义,geth trie 删零值槽);「applyDiff 从不留下零值槽行」这条不变量的守护**移到写路径**,形态为「removeOne 后回读该键,行仍存活则 throw」(后置**结果**校验,非意图断言)。只动 Storage2Ledger.h + Storage2LedgerTest.cpp 两个代码文件。
  **【本批最有价值的发现·实施者答第二问答出来的】** fetchAllStorage:485 那条 throw **零测试覆盖**(删掉无一用例翻红);而契约②有一条**间接**覆盖 —— Storage2LedgerTest.cpp:274 的 `EXPECT_FALSE(has_storage)`,**其有力恰恰因为 probeHasStorage 不判零值**。故按派单把读路改对之后,applyDiff 即使漏写一行零值槽那条断言照样通过 → **"只改读路造成守护净减少"是确定会发生的,不是风险**。若不在写路补一条真会响的检查,本批是净负。**这正是派单强制先答第二问的收益。**
  **【实施者拒绝形式主义断言,与 §11 通则独立同构】** 它指出 `assert(!is_zero(value))` 写在 setStorage 前是**同义反复**——该分支结构上不可能带零值走到那里,断言永不触发、永远无法被测试,原话"**测不出来的守护等于没有**"。此认识与批 4 固化进 §11 的通则(风险点必须当场配会翻红的断言)独立同构,由实施者自行推到。
  **【控制器一条技术警告被实测否决】** 我警告 removeOne 写的是 DELETED_TYPE 墓碑、故 existsOne 可能对刚删的键返回 true 使后置校验"永远在响",并建议改用 readOne + liveContent。实施者**实测**:existsOne 把 DELETED_TYPE **判为不存在**(而 range() 仍能扫到该行),判据保留 existsOne;且 "readOne + liveContent" **在类型上不可组合**(liveContent 判的是 range() 的原始变体,existsOne 已在层间解析里做完同一件事)。**控制器建议的技术判断有误;"要求实测而非推理"这一条是对的,实测把它了结。**
  三问结论:①applyDiff 是桥世界**唯一**写槽者(:314/319/370/374),绕过它的写入源(transaction-executor/HostContext.h:288、bcos-ledger/Ledger.cpp:1844/1873、旧执行器 EVMHostInterface.cpp:81)**全在桥的世界之外且不经桥写回** → (A) 成立;③第三处 fetchStorage/get_storage **本就一致**,第 4-9 处(建根侧 is_zero→continue 等)亦一致,**没有第五处**。
  **【实施者从正面找到控制器反推的结论的出处】** #6 `adapter/StateRootCompute.cpp:25-26` 的 `is_zero → continue` 才是"零值槽不进 trie"的**实际实现依据**;控制器原是从 fetchAllStorage 抛异常反推。两条独立路径同结论 → 该结论可当定论。
  新记 §6.4 条目 t/u;桥 design §6 撤销 rev.2 限定。新发现 #8 `MemoryLedger.cpp:19` + `accounts()` 可变暴露面为 #2 同类缺口,触发面限于测试代码,按裁定不在本批修(正确修法=收窄 accounts() 暴露面,已记 u①)。
  测试:in-tree **246/246**;standalone 131/131 但**二进制时间戳停在 7-29 未变,对本批是空真、不作红绿证据**(实施者主动声明,唯一见证是 in-tree);test-bcos-engine 干净;E-b 三腿 33×3。三探针各自翻红且**红集互不相同、各自非空**(原写"互不重叠"不准确,已于复审 + fix 轮勘误:红集 {z1,z4,z5} / {z2,z5} / {z6},z5 同时被前两个覆盖——它是墓碑层+零值层的联合锚,两边都响是对的):读路 throw 复原→3 红;probeHasStorage 去零值层→2 红;删写路后置回读→1 红。
  实施者自陈四条诚实边界(待复审判是否低估):①"每块 -32603" 是读码+同路径实证的**合成推理**,未在真实节点端到端复现;②isZeroSlotValue 对"32 字节键 + 非 32 字节值"有意判"有内容",与 fetchAllStorage 同情形 throw 形成**有意不对称且无专门用例**;③后置回读多一次层内查表,未做性能测量;④**接入阻塞项仍在**:条目 t 第一个生产毒旗触发点(非 20 字节 hex 的 /apps/ 表名)本批未修,它牵涉"OP 状态根如何覆盖非 20 字节地址的 FISCO 原生账户"这一**尚无裁定的语义问题**。
终审批9 复审 **通过**(3e6be40db/395c465c4/65da95d87/e09b9bcd9),0 Critical / 2 Important / 1 Minor,fix 轮已派原实施者。6 次注入(INJ-A/B/C/D/E/E′)全部还原+重建+复绿。
  三探针判别力**逐例(不只逐数)**核实一致:INJ-A 读路 throw 复原 → 3 红 {z1,z4,z5};INJ-B probeHasStorage 去零值层 → 2 红 {z2,z5};INJ-C 删写路后置回读 → 1 红 {z6}(独立复现前一任 INJ-4)。**修正实施者一处表述**:并非"互不重叠",z5 同时被 A/B 覆盖——是好事,z5 是墓碑层+零值层的联合锚。
  **【F-1 Important·本批自己引入的分类错误】** 新守护抛 std::runtime_error,而 applyDiff **从不置毒旗**(Storage2Ledger.h:222-229)→ 落到 OpSchedulerImpl.h:834-844 时 poisoned()==false → 分类成 OpConsensusError → 按 :72-79 契约映射 **INVALID,永远不会是 -32603**。但它守护的是"桥自己写回有 bug"即**节点本地故障**,按 §4.3 必须 -32603。用 INVALID 回答合法 payload = 本节点拒绝规范链并永久分叉,**正是既有用例 StorageLayoutFaultIsInternalErrorNotInvalid 专门防的那类错误**,而新守护绕过了它。既有两处 applyDiff tripwire **同病**,本批扩大了一个触发点。
  **【F-2 Important·把控制器的警告推进了一层】** 实施者实测否决墓碑警告的结论成立,但复审发现**只钉了一半**:INJ-D 改 MemoryStorage::existsOne 语义 → z7 三条断言全响(MemoryStorage 侧钉得牢);**但生产 Storage2Ledger<MultiLayerStorage::ViewType> 走完全不同的一段代码** —— View 无 existsOne 成员 → storage2::existsOne 回退 View::readOne→getValue。INJ-E 给 View 加一个"省掉 Value 构造"的 existsOne 成员 → **246/246 全绿零红**;INJ-E′ 同一成员改无条件 true → 12 红(证明实验非空)。**→ 生产路径上这条判据是靠今天的实现巧合成立的**,将来谁给 View 加 existsOne 就静默失效且无测试会响。修法=给 z7 补 MLS View 之上的孪生用例(EbT8nReplayTest 已有 MLS fixture 先例)。
  **【F-3 Minor·白捡】** 非 32 字节槽**值**这条路在 test/opstack **零测试**——不只实施者自陈的"不对称无用例",连**终审 M-1 自己修的那个 throw 也从无红绿见证**(唯一沾边的 StorageLayoutFault* 注入的是 29 字节**键**,走另一条 throw)。另:t① 异常类型写错,/apps/HelloWorld 实际先在 boost::algorithm::unhex 抛 non_hex_input,非 std::length_error(结论不变)。
  复审对四条诚实边界的判定:①"每块 -32603" 若有偏差是**偏保守**——visitAccounts 是全量遍历,全状态里只要有一行零值槽就毒掉**每个**块,不需要"这块碰到它",故"每块"是下界而非夸张;②**低估**(即 F-1);③可接受(成本上界=每块 SSTORE-to-zero 条数,与既有写入同阶);④判断正确。**无一条掩饰。**
  复审附带发现:因建根侧本就 is_zero→continue,z1 的"根逐字节相等"断言是**恒真的那一半**,判别力实际来自 poisoned() 与 size()==1 —— 冗余而非缺陷。文档 t/u 行号全部核对属实无灌水。见证归属诚实,并追加核实"空真不掩盖编译风险":Storage2Ledger.h **无任何生产 .cpp 包含**,standalone 根本不编译它。
  【控制器操作错误】我**先派了复审 agent、才写派单文件**,导致它启动时 final-batch9-review-brief.md 不存在(全仓 find 无命中),只能按 dispatch 里的转述执行。它如实记了这条缺口并建议"后续派单一律先 git add -f 再派"。**采纳为硬规程**:派单落盘并提交 → 再 dispatch,顺序不可颠倒。
  【断流事故处置记录】前一任复审者在 INJ-4 完成、INJ-5 刚起时断连,**留下未还原注入**(守护被删+泄漏被注入)。控制器 git checkout 还原 → 重建(时间戳 19:35→19:37,确认非假绿)→ 246/246 复绿。其断流前最后一句输出恰含最有价值结论(INJ-4:真实泄漏+无守护时 239 个既有测试**全部通过**、唯一红是新增 z6 → 从反方向证明缺口过去真实存在),纯属运气——**"每完成一个注入就提交"已从建议改为硬要求**。
终审批9 fix 轮完成(接续者,前任停滞半成品验收通过后直接落地;ecbfb8f/add588a/6bf3478/ba34b0a/a695f19/7a5aa73,**246→249**):
  F-1 闭合:applyDiff 写回失败置毒旗后重抛 → 分类归位 -32603;分类层新用例 (d2) 在 executeOpBlock 上钉住"写路失败 ≠ INVALID"。
  F-2 闭合:(z8) 把"墓碑 ≡ 不存在"钉到生产实际走的 MLS View 半边。**INJ-E 重放是本轮金标准见证:复审当时 0 红 → 现在 1 红且只有 (z8),四条断言全响** —— 用复审者自己设计的实验直接闭合其发现。
  F-3 闭合:(z9) 非 32 字节槽值用例(INJ-H:还原 M-1 两条 throw → 1 红且只有 z9,**终审 M-1 的 throw 首次获得红绿见证**);spec t① 异常类型勘误;"互不重叠"表述勘误。
  **【RTTI 新数据·全仓适用】** INJ-G(删 runtime_error 级 + 各级打前缀)证实前任"实测阶梯"注释:firstError() 全部 [VIA-ELLIPSIS]、零条 [VIA-EXCEPTION] → **`catch (const std::exception&)` 在本构建的 applyDiff 路径上形同虚设**(异常经 syncWait/协程 rethrow 后 std::exception 的非唯一 typeinfo 不匹配,具体派生类 typeinfo 仍唯一可匹配)。与 visitAccounts:256 的同 TU 直抛可匹配**不矛盾**(两处各有实测见证);但这意味着**凡异常穿过协程/exception_ptr 边界,typed catch 只能按具体派生类写**——应记入 §11/RTTI 排查报告的补遗。
  三条诚实边界:①(d2) 实测发现 LeakyDeleteStorage 在 OpSchedulerImpl 层**不可达**(该块执行 removeOne 零次调用),改用新增 WriteFailingStorage 触发同段 catch——钉的是分类逻辑本身,(z6)/ghost//sys/ 三条 tripwire 在分类层仍是"共用同段 catch"的推论;②(z8) 只覆盖单可变层,跨层墓碑(上层墓碑遮下层实值)的 getValue 分支未覆盖;③INJ-E 重放时**一次误改主仓 MultiLayerStorage.h**(worktree 纪律违规),已 cp 还原并确认主仓 git status 不出现——记录在案。
终审批9 fix 轮 re-review **通过**(d930285a6/9924306b8),三处最长推理链独立核完;1 Important 遗留 + 3 Minor,派 fix2。
  R-1(d2 替身):推论成立且**比实施者自陈更强**——是结构性保证非经验推论:applyModifiedEntry/applyDeletedEntry 为 private、全仓唯二调用点都在 applyDiff 的 try 内,public 面唯一写入口就是 applyDiff,无可绕路径;分类层 OpSchedulerImpl 三处全为 poisoned()-first。LedgerSeed.h:47 是第二调用点但全为测试调用,不构成缺口。
  **【R-2·实施者的调和被证伪,控制器上一条台账记录同错,在此更正】** "直抛 vs 穿协程"的对照**从一开始就不存在**——visitAccounts:327 也是 syncWait(协程),与 applyDiff 穿同一种边界。INJ-R2 实测(三处 catch 全剥回两级、同一 TU 同一二进制、四探针):runtime_error 在 applyDiff **和** visitAccounts 上都逃逸到 catch(...);length_error 在 get_storage **和** visitAccounts 上都正常命中 catch(const std::exception&)。**判别式是异常类型族,与协程边界无关**:runtime_error 子树 typed catch 不生效 / logic_error 子树正常生效 / 修法=显式 catch(const std::runtime_error&) 或 catch(...) 兜底 / **不得**表述为"std::exception 级形同虚设"(对 logic_error 族它是唯一正确的那级)。机制归因**未定**:实施者"非唯一 typeinfo 是 std::exception 的"与实测矛盾(若成立 length_error 也该逃逸),降格为"以实测为准"。→ **撤销上一条台账"凡穿协程边界 typed catch 只能按派生类写"的表述,以本条为准,§11 补遗按本条落。**
  **【R-2 副产品·Important·fix2】** fix 轮只给 applyDiff 加了四级阶梯,get_account/get_account_code/get_storage/visitAccounts 仍两级 → 按判别式,**读路每个 runtime_error 今天就在丢消息**(已实测一例:fetchAllStorage 的 "unknown key in account table" → firstError() 退化 "unknown exception"),而读路是毒旗主要来源;写路有 (d2) 消息保真断言,读路没有。
  R-3(z8 边界):跨层墓碑遮蔽在 removeOne 后置校验场景**结构性不可达**(墓碑必落顶层可变层 / readOne 命中非 NOT_EXISTS 即返回不读下层 / 相邻两条 co_await 无插层机会)——"未覆盖"应改述"结构性不可达故不补",否则后人会补一条无判别力的用例。
  Minor:spec t① 里"non_hex_input 不派生自 runtime/logic_error 所以 catch(...) 非冗余"理由推不出结论(它虚继承 std::exception 可被匹配),真实依据是 R-2 的 RTTI 实测;Storage2Ledger.h 注释两处措辞过强。
  例行全核准:边界零触碰(fix 轮**没有**顺手改框架而是补用例钉行为,方向正确);零漂移;249 逐名对上;spec 勘误到 boost 源码逐字相符。
终审批9 fix2 完成(d0b8186/69614a5/2f40616,**249→250**),控制器抽验措辞三处全部照 re-review 原文落地("形同虚设"已删/机制未定标注 :304/结构性不可达改述 :1110),**批 9 全链条收口**(实施→复审→fix→re-review→fix2,246→250,净增 11 例)。
  F2-1:读路四方法补四级阶梯 + (z10) 读路消息保真断言(INJ-I 删 visitAccounts 的 runtime_error 级 → 1 红且只有 z10)。F2-2:三处措辞照 re-review 原文。
  fix2 诚实边界三条(记账不阻塞):①删级自验只做了 visitAccounts 一处,其余三个读方法是"机械复制正确"的判断而非实测;②logic_error 族读路消息保真无专门断言((z9) 在两级阶梯下本来就绿,不是四级的见证);③RTTI 机制仍未定位(只归纳到"判别式=类型族"层,查明需看 __cxa_throw 拿到的 typeinfo 指针与各 .a 弱符号解析),注释已标注"机制未定",禁止在此上做推断式改动。
opstack↔op-geth 九层状态分歧审计完成(b0ac359b8→181a50e56,8 次分层提交,只读)。**新发现 14 条分歧 + 5 条文档失实;批 9 零值槽修复复核通过。**
  【四次断流后靠分层提交存活】本任务前三次派发全部零产出(529×1 + ECONNRESET×2 起步即断);改为"每层写完立即提交"后第 4 次断流仅损失当前层,resume 从第 2 层续,最终完成。**分层提交是抗断流的唯一有效手段,已验证。**
  **当前可构造(新)**:①legacy/0x01 被 decodeOneRawTx 一律拒块(OpSchedulerImpl.h:660-676 vs op-geth transaction.go:191-234)——**日常流量即触发**,与五视角 C6 独立同结论,故 C6 定性从"可能是范围"上调为**必修**;②**命中 c_systemTxsAddress 的 8 个普通以太坊地址(0x…1000 等)→ 桥毒旗 → -32603 永久卡死,一笔 1 wei 转账即可触发**(新,危害仅次于①,且是"外部可主动触发的拒绝服务");③第 4 层三条 op-geth EL 不做的更严准入;④**extraData 的 OP 形状零校验**,而它是下一块 baseFee 的参数源 → **与 C4 复合后 baseFee 完全失守**;⑤Jovian daFootprint>gasLimit 上限缺失;⑥Jovian attributes 长度/选择器不校验;⑦Jovian 激活块"不得含用户交易"缺失;⑧首块 timestamp 豁免 + configAt 把 pre-Isthmus 解析成 Isthmus。
  **生产接入才可达(新)**:①**最重——stateRootOf<Ledger> 收录每一个存活账户,无 EIP-161 空账户过滤**(StateRootCompute.h:82-93 的 visitAccounts 无过滤;op-geth 靠 Finalise(deleteEmptyObjects) 在入 trie 前删空账户,已核 statedb.go:821/864)→ 通用执行器写的账本必然含以太坊语义下的空账户 ⇒ **接生产账本当天每块 stateRoot 都对不上**。与"非 20 字节 /apps/ 表名"同类:FISCO 账本形状不满足以太坊状态树假设;②Jovian DA scalar 我方读 L1Block 槽 8[18:20]、op-geth 读 attributes calldata[176:178](恒等性 [需验证]);③execute_system_call 绕过 Host::call,4788/2935 REVERT 不回滚(op-geth 有 snapshot),唯一失败处理是 **Release 下被编译掉的 assert**;④MessagePasser 缺席时我方给空 MPT 根、op-geth 给全零哈希;⑤父头按高度读(与条目 p 同源);⑥**Karst 别名在本 pin 上证实无害**(op-geth 的 IsKarst 目前无任何执行侧调用方)——一条欠账被证伪可降级。
  **【最危险的文档失实】** OpSchedulerImpl.h:275-296 断言 "non-canonical input never survives decoding",并以此作为 **txRoot 建在原始线上字节而非重编码**的**唯一正当性论据**——而 C1 已实测出 txRoot 分歧,该断言直接被证伪。**不修的话下一个读者会认为这个设计已被证明安全。** 与 §6.4(n)、OpEngineSeam.h:156-163 同源,批 7 必须一并改。
  **【补上批 9 方案 A 的关键佐证】** EVMAccount::setStorage **恒写 32 字节**,故生产账本的零值槽行必然满足 isZeroSlotValue,读路不会每块毒旗——这是方案 A 成立的支点,此前未记账,应补进 §6.4 条目 u。
  审计自陈:33 条金值向量**结构上碰不到表 2 的任何一条**,"全绿"对生产就绪度无证据力。[需验证] 集中三处:Jovian L1Block 槽布局 / FlzCompressLen 逐字节差分 / evmone MPT 与 geth StackTrie 极端形状等价性。
