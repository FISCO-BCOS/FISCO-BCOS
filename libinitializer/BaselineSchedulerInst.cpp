// Explicit instantiations of the production BaselineScheduler specializations — the single
// TU that pays for compiling the BaselineScheduler implementation (BaselineScheduler-tpp.h)
// for the node's real executor / scheduler types. Keep in sync with the extern template
// declarations in BaselineSchedulerInitializer.h: the three specializations below are the
// ones Initializer.cpp builds (parallel v1, serial v1, serial v2-Ethereum).
#include "BaselineSchedulerInitializer.h"
#include "bcos-transaction-scheduler/BaselineScheduler-tpp.h"

namespace bcos::scheduler_v1
{
template class BaselineScheduler<initializer::GlobalStateStorage,
    executor_v1::TransactionExecutorImpl,
    SchedulerParallelImpl<initializer::GlobalStateMutableStorage>, ledger::LedgerInterface>;
template class BaselineScheduler<initializer::GlobalStateStorage,
    executor_v1::TransactionExecutorImpl, SchedulerSerialImpl, ledger::LedgerInterface>;
template class BaselineScheduler<initializer::GlobalStateStorage,
    executor_v1::eth::EthereumExecutor, SchedulerSerialImpl, ledger::LedgerInterface>;
}  // namespace bcos::scheduler_v1
