// MemoryLedgerT8nReplayTest.cpp — 真账本桥 Task 2：MemoryLedger 回放腿。
//
// 与 OpT8nReplayTest.cpp 的 TestState 腿共用 T8nReplayHarness.h 的
// replayAllVectors<Backend> 模板、同一份 test/opstack/t8n/vectors/*.json +
// DIVERGENCES.md，仅播种/写回/建根/postState 遍历的后端换成
// bcos::evm::ledger::MemoryLedger（MemoryLedgerBackend，见 T8nReplayHarness.h）。
// stateRoot 经"导出为 TestState 再调既有 stateRootOf"的过渡实现——Task 5 交付
// MemoryLedger 自研遍历建根后删除该过渡步骤。
#include "T8nReplayHarness.h"

TEST(MemoryLedgerT8nReplay, Vectors)
{
    replayAllVectors<MemoryLedgerBackend>("memory-ledger");
}
