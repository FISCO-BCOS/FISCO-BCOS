// MemoryLedgerT8nReplayTest.cpp — 真账本桥 Task 2：MemoryLedger 回放腿。
//
// 与 OpT8nReplayTest.cpp 的 TestState 腿共用 T8nReplayHarness.h 的
// replayAllVectors<Backend> 模板、同一份 test/opstack/t8n/vectors/*.json +
// DIVERGENCES.md，仅播种/写回/建根/postState 遍历的后端换成
// bcos::evm::ledger::MemoryLedger（MemoryLedgerBackend，见 T8nReplayHarness.h）。
// stateRoot 经真账本桥 Task 5 的泛型 stateRootOf<Ledger>（visitAccounts 驱动的自研遍历
// 建根，adapter/StateRootCompute.h）直接对 MemoryLedger 建根——原"导出为 TestState 再调
// 既有 stateRootOf"的过渡实现已随 Task 5 删除。
#include "T8nReplayHarness.h"

TEST(MemoryLedgerT8nReplay, Vectors)
{
    replayAllVectors<MemoryLedgerBackend>("memory-ledger");
}
