// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

// SmokeCompileTest — opstack-executor is a compiled library (OpEthBlockExecute.cpp and
// friends), but its template surface is header-only and compiles ONLY at instantiation. This
// TU includes every public header and EXPLICITLY INSTANTIATES that surface, so a base-API
// break fails this build immediately.

#include <opstack-executor/OpCommon.h>
#include <opstack-executor/OpEthBlockExecute.h>
#include <opstack-executor/OpEthBlockSteps.h>
#include <opstack-executor/OpEthDeposit.h>
#include <opstack-executor/OpEthReceipt.h>
#include <opstack-executor/OpExecutionPolicy.h>
#include <opstack-executor/OpForkSpec.h>
#include <opstack-executor/OpScheduler.h>
#include <opstack-executor/OpSchedulerPolicy.h>

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

// ---- the bcos-evm-free OP layer (OpPolicy on the ethereum-executor primitives) ----

template bcos::task::Task<bcos::protocol::TransactionReceipt::Ptr>
bcos::executor_v1::opstack::opRunDeposit<MutableStorage>(
    bcos::executor_v1::eth::EthereumState<MutableStorage>&,
    bcos::executor_v1::eth::EthBlockInfo const&, bcos::executor_v1::eth::BlockHashLookup,
    bcos::executor_v1::opstack::DepositTx const&, bcos::executor_v1::opstack::OpForkSpec const&,
    evmc::VM&, uint64_t, int64_t, bcos::protocol::TransactionReceiptFactory const&, int64_t);

template std::variant<bcos::executor_v1::eth::EthTxProperties, std::error_code>
bcos::executor_v1::eth::validateTransaction<MutableStorage, bcos::executor_v1::opstack::OpPolicy>(
    bcos::executor_v1::eth::EthereumState<MutableStorage>&,
    bcos::executor_v1::eth::EthBlockInfo const&, bcos::protocol::Transaction const&, evmc_revision,
    int64_t, int64_t, bcos::executor_v1::eth::EthCallParams const&,
    bcos::executor_v1::opstack::OpPolicy const&);

template bcos::task::Task<bcos::protocol::TransactionReceipt::Ptr>
bcos::executor_v1::eth::runTransaction<MutableStorage, bcos::executor_v1::opstack::OpPolicy>(
    bcos::executor_v1::eth::EthereumState<MutableStorage>&,
    bcos::executor_v1::eth::EthBlockInfo const&, bcos::executor_v1::eth::BlockHashLookup,
    bcos::protocol::Transaction const&, evmc_revision, evmc::VM&,
    bcos::executor_v1::eth::EthTxProperties const&, uint64_t,
    bcos::executor_v1::eth::EthCallParams const&, bcos::protocol::TransactionReceiptFactory const&,
    int64_t, bcos::executor_v1::opstack::OpPolicy const&);

template bcos::task::Task<bcos::executor_v1::opstack::OpFeeParams>
bcos::executor_v1::opstack::loadOpFeeParamsAsync<MutableStorage>(MutableStorage&);

template bcos::task::Task<std::map<evmc::bytes32, evmc::bytes32>>
bcos::executor_v1::opstack::opEthMessagePasserStorage<MutableStorage>(MutableStorage&);

// ---- the shared block stages (OpEthBlockSteps.h) ----

template bcos::task::Task<void> bcos::executor_v1::opstack::preBlockOpEthSteps<MutableStorage,
    std::vector<bcos::bytes>>(MutableStorage&, bcos::protocol::BlockHeader const&,
    bcos::executor_v1::opstack::OpForkSpec const&, std::vector<bcos::bytes> const&,
    std::vector<bcos::executor_v1::opstack::DepositTx> const&, evmc::VM&,
    std::shared_ptr<bcos::executor_v1::opstack::OpStorageErrorSlot> const&,
    std::optional<bcos::executor_v1::opstack::OpRecentBlockHashes<MutableStorage>>&,
    std::optional<std::string>&, std::optional<uint16_t>&, bool);

// finalizeOpEthBlockResult is NOT explicitly instantiated here: its state-root path calls
// ledger::mpt::computeMptStateRoot, which requires a MultiLayerStorage view
// (storage2::mutableStorage(flatView)) — a bare MemoryStorage cannot satisfy it. The production
// view-type instantiation is already compile-checked in this same binary by OpSchedulerTest and
// OpBlockVerifierTest, which call it with MultiLayerStorage<...>::ViewType.

BOOST_AUTO_TEST_SUITE(SmokeCompileTest)

BOOST_AUTO_TEST_CASE(HeadersInstantiate)
{
    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_SUITE_END()
