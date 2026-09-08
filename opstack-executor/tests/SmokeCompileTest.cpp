// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

// SmokeCompileTest — opstack-executor is a compiled library (OpBlockExecute.cpp and friends),
// but its template surface is header-only and compiles ONLY at instantiation. This TU includes
// every public header and EXPLICITLY INSTANTIATES that surface, so a base-API break fails this
// build immediately.

#include <opstack-executor/OpBlockExecute.h>
#include <opstack-executor/OpCommitments.h>
#include <opstack-executor/OpCommon.h>
#include <opstack-executor/OpScheduler.h>
#include <opstack-executor/OpSchedulerPolicy.h>
#include <opstack-executor/OpstackExecutor.h>
#include <opstack-executor/RecentBlockHashes.h>
#include <opstack-executor/Storage2State.h>
#include <opstack-executor/Storage2StateHelpers.h>

#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <boost/test/unit_test.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace
{
using MutableStorage = bcos::storage2::memory_storage::MemoryStorage<bcos::executor_v1::StateKey,
    bcos::executor_v1::StateValue,
    bcos::storage2::memory_storage::Attribute(bcos::storage2::memory_storage::ORDERED |
                                              bcos::storage2::memory_storage::LOGICAL_DELETION)>;
// OpScheduler<MLS> is instantiated in opstack-executor-scheduler-tests. MLS backend
// must satisfy CheckpointStorage (open()), so MemoryStorage is not a valid backend —
// do not add MultiLayerStorage<MutableStorage, void, MutableStorage> here.
}  // namespace

// ---- explicit instantiations ----

template struct bcos::executor_v1::opstack::OpstackExecutor::ExecuteContext<MutableStorage>;

template bcos::task::Task<bcos::protocol::TransactionReceipt::Ptr>
bcos::executor_v1::opstack::OpstackExecutor::executeTransaction<MutableStorage>(MutableStorage&,
    bcos::protocol::BlockHeader const&, bcos::protocol::Transaction const&, int,
    bcos::ledger::LedgerConfig const&, bool, bcos::evm::opstack::OpFeeParams const&, int64_t,
    uint64_t, evmone::state::BlockHashes const*);
template bcos::task::Task<bcos::protocol::TransactionReceipt::Ptr>
bcos::executor_v1::opstack::OpstackExecutor::executeDeposit<MutableStorage>(MutableStorage&,
    bcos::protocol::BlockHeader const&, bcos::evm::opstack::DepositTx const&, uint64_t, int64_t,
    bcos::ledger::LedgerConfig const&, evmone::state::BlockHashes const*, bool);

template void bcos::evm::engine::preBlockOpSteps<MutableStorage, std::vector<bcos::bytes>>(
    MutableStorage&, bcos::protocol::BlockHeader const&, bcos::evm::opstack::OpForkConfig const&,
    std::vector<bcos::bytes> const&, std::vector<bcos::evm::opstack::DepositTx> const&,
    bcos::executor_v1::opstack::OpstackExecutor&,
    std::optional<bcos::evm::engine::detail::RecentBlockHashes<MutableStorage>>&,
    std::optional<std::string>&, std::optional<uint16_t>&);

BOOST_AUTO_TEST_SUITE(SmokeCompileTest)

BOOST_AUTO_TEST_CASE(HeadersInstantiate)
{
    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_SUITE_END()
