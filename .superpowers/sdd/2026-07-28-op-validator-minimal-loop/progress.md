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
