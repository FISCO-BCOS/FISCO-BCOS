// Explicit instantiations of the test-suite BaselineScheduler specializations — the
// single TU in this binary that pays for instantiating the scheduler implementation:
//   1. the shared-mock specialization (SharedBaselineSchedulerMock.h), used by
//      testBaselineScheduler / FIB101 / TestExecuteStateRootRegression /
//      TestEthCallHistory / TestMPTSchedulerWiring;
//   2. the FullChainFixture specialization (extern template declared in
//      FullChainFixture.h), used by the MPT genesis / L1-upgrade / smoke tests.
// Keep in sync with the extern template declarations in those two headers.
// test-bcos-rpc has its own FullChainFixture instantiation TU
// (bcos-rpc/test/unittests/rpc/FullChainFixtureInst.cpp).
//
// SKIP_UNITY_BUILD_INCLUSION: FullChainFixture.h pulls RocksDB / testutils headers
// whose names collide with the using-directives other unity-merged TUs carry — the
// same reason the FullChainFixture consumers are compiled standalone.
#include "FullChainFixture.h"
#include "SharedBaselineSchedulerMock.h"
#include "bcos-transaction-scheduler/BaselineScheduler-tpp.h"

namespace bcos::scheduler_v1
{
template class BaselineScheduler<test::sharedmock::SharedMultiLayerStorage,
    test::sharedmock::SharedMockExecutor, test::sharedmock::SharedMockScheduler,
    ledger::LedgerInterface>;
template class BaselineScheduler<test::fullchain::FCMultiLayerStorage, test::fullchain::FCExecutor,
    test::fullchain::FCWritingScheduler, ledger::Ledger>;
}  // namespace bcos::scheduler_v1
