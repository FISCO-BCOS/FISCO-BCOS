// OpT8nReplayTest.cpp — M-B3+M6 Task 3：OP 块级差分回放 gate。
//
// 消费 test/opstack/t8n/vectors/*.json（schema v3-block，op-geth
// GenerateChain+InsertChain 金标准，生成器见 t8n/generator/），整块回放
// processOpBlock → sealOpBlock，与 _op_expected（header 六字段 + 逐 receipt）
// 和 postState（决策记录 8：双向 + applyDiff 写集覆盖）比对。
//
// 真账本桥 Task 2：向量循环体（manifest/DIVERGENCES 装载、逐向量 JSON 解析、
// processOpBlock/sealOpBlock 调用、header/receipt/postState 比对、
// RecordProperty 汇总）已整体迁入 T8nReplayHarness.h 的 replayAllVectors<Backend>
// 模板；本文件仅保留 TestState 腿的一行调用（TestStateBackend 语义原样，行为
// 零变化——重构前后 RecordProperty 逐值相同，见 task-2-report.md）与不依赖
// 向量回放的 structurallyUnrecoverable 谓词边界单测。MemoryLedger 腿见同目录
// MemoryLedgerT8nReplayTest.cpp。
#include "T8nReplayHarness.h"

// ── structurallyUnrecoverable 谓词边界单测（防退化恒真/恒假后标记逃逸复活）──
TEST(OpT8nReplay, StructurallyUnrecoverablePredicateBoundaries)
{
    auto mk = [](uint64_t v, const intx::uint256& r, const intx::uint256& s) {
        evmone::state::Authorization a{};
        a.v = v;
        a.r = r;
        a.s = s;
        return a;
    };
    const intx::uint256 one{1};
    EXPECT_FALSE(structurallyUnrecoverable(mk(0, one, one)));
    EXPECT_FALSE(structurallyUnrecoverable(mk(1, one, kSecpHalfN)));       // s == N/2 合法
    EXPECT_TRUE(structurallyUnrecoverable(mk(2, one, one)));               // v > 1
    EXPECT_TRUE(structurallyUnrecoverable(mk(0, one, kSecpHalfN + 1)));    // s > N/2
    EXPECT_TRUE(structurallyUnrecoverable(mk(0, intx::uint256{0}, one)));  // r == 0
    EXPECT_FALSE(structurallyUnrecoverable(mk(0, kSecpN - 1, one)));
    EXPECT_TRUE(structurallyUnrecoverable(mk(0, kSecpN, one)));            // r >= N
    EXPECT_TRUE(structurallyUnrecoverable(mk(0, one, intx::uint256{0})));  // s == 0
}

TEST(OpT8nReplay, Vectors)
{
    replayAllVectors<TestStateBackend>("test-state");
}
