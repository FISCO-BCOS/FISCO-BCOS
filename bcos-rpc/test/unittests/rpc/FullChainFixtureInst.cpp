// Explicit instantiation of the FullChainFixture BaselineScheduler specialization for
// test-bcos-rpc — the counterpart of transaction-scheduler/tests/SharedBaselineSchedulerInst.cpp.
// Required because FullChainFixture.h declares the specialization extern template:
// EthGetProofIntegrationTest.cpp (this binary's only FullChainFixture consumer) then
// suppresses its own implicit instantiation and links against this one.
//
// SKIP_UNITY_BUILD_INCLUSION like the FullChainFixture consumer itself: the fixture
// header pulls RocksDB / testutils headers whose names collide with the
// using-directives other unity-merged TUs carry.
#include "transaction-scheduler/tests/FullChainFixture.h"
#include "bcos-transaction-scheduler/BaselineScheduler-tpp.h"

namespace bcos::scheduler_v1
{
template class BaselineScheduler<test::fullchain::FCMultiLayerStorage,
    test::fullchain::FCExecutor, test::fullchain::FCWritingScheduler, ledger::Ledger>;
}  // namespace bcos::scheduler_v1
