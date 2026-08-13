// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

// OpEngineBranchSmokeTest — compile-and-run verification that the ported engine OP branch
// (`EngineServiceImpl`'s `handleOpNewPayload` / `runOpNewPayloadSteps` / `registerOpBlock`, only
// instantiated when `c_opMode` is true) type-checks against `OpSchedulerImpl` on this branch.
// The full branch-by-branch suite lives on the source branch (EngineOpBranchTest.cpp); this file
// pins the one thing the two normal build targets cannot: an instantiation with an OP-mode
// scheduler, which forces the entire `if constexpr (c_opMode)` body to compile. The -38005
// pre-Isthmus gate is the runtime assertion (the version gate sits before the classification
// barrier, so it throws `UnsupportedFork`), but the compile-time instantiation of the body is
// the point.
#include "engine/bcos-engine/EngineServiceImpl.h"

#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/storage2/MultiLayerStorage.h>
#include <bcos-framework/testutils/faker/FakeBlock.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-framework/transaction-executor/TransactionExecutor.h>
#include <bcos-framework/transaction-scheduler/TransactionScheduler.h>
#include <bcos-task/Wait.h>
#include <opstack-executor/OpSchedulerImpl.h>
#include <boost/test/unit_test.hpp>
#include <stdexcept>

using bcos::executor_v1::StateKey;
using bcos::executor_v1::StateValue;
namespace memory_storage = bcos::storage2::memory_storage;

namespace
{
// Per-file local fixture copy (same convention as the source branch's test files).
template <class Key, class Value, bcos::storage2::ReadWriteStorage<Key, Value> Storage>
struct TrivialCheckpointStorage
{
    using CheckpointName = bcos::h256;

    Storage& m_storage;
    explicit TrivialCheckpointStorage(Storage& storage) noexcept : m_storage(storage) {}
    Storage& open() & { return m_storage; }
    [[noreturn]] Storage& open(CheckpointName const& /*unused*/) & { std::abort(); }
    void createCheckpoint(Storage& /*unused*/, CheckpointName const& /*unused*/) {}
    void deleteCheckpoint(CheckpointName const& /*unused*/) {}
    [[nodiscard]] std::optional<CheckpointName> latestCheckpointName() const
    {
        return std::nullopt;
    }
    [[nodiscard]] std::optional<CheckpointName> oldestCheckpointName() const
    {
        return std::nullopt;
    }
};

using MutableStorage = memory_storage::MemoryStorage<StateKey, StateValue,
    memory_storage::Attribute(memory_storage::ORDERED | memory_storage::LOGICAL_DELETION)>;
using BackendMemStorage = memory_storage::MemoryStorage<StateKey, StateValue,
    memory_storage::Attribute(memory_storage::ORDERED | memory_storage::CONCURRENT),
    std::hash<StateKey>>;
using CheckpointBackend = TrivialCheckpointStorage<StateKey, StateValue, BackendMemStorage>;
using MLS = bcos::storage2::MultiLayerStorage<MutableStorage, void, CheckpointBackend>;
using ViewType = typename MLS::ViewType;

struct StubMemPool
{
};

struct StubExecutor
{
    template <class Storage>
    struct ExecuteContext
    {
        bcos::task::Task<void> prepare() { co_return; }
        bcos::task::Task<void> execute() { co_return; }
        bcos::task::Task<bcos::protocol::TransactionReceipt::Ptr> finish() { co_return nullptr; }
    };

    template <class Storage>
    bcos::task::Task<bcos::protocol::TransactionReceipt::Ptr> executeTransaction(Storage&,
        const bcos::protocol::BlockHeader&, const bcos::protocol::Transaction&, int,
        const bcos::ledger::LedgerConfig&, bool)
    {
        co_return nullptr;
    }

    template <class Storage>
    bcos::task::Task<ExecuteContext<Storage>> createExecuteContext(Storage&,
        const bcos::protocol::BlockHeader&, const bcos::protocol::Transaction&, int,
        const bcos::ledger::LedgerConfig&, bool)
    {
        co_return ExecuteContext<Storage>{};
    }
};

using OpEngine = bcos::engine::EngineServiceImpl<StubMemPool, MLS, StubExecutor,
    bcos::evm::engine::OpSchedulerImpl<ViewType>>;
}  // namespace

BOOST_AUTO_TEST_SUITE(OpEngineBranchSmokeSuite)

BOOST_AUTO_TEST_CASE(OpModeInstantiatesAndGatesV4)
{
    // The compiler instantiating this composition is the test: `c_opMode` becomes true, which
    // forces `handleOpNewPayload` (and through it `runOpNewPayloadSteps` / `registerOpBlock`) to
    // be instantiated. A type conflict anywhere in the OP branch body fails the build here.
    BackendMemStorage backendStorage;
    CheckpointBackend checkpointBackend(backendStorage);
    MLS storage(checkpointBackend);

    constexpr uint64_t kIsthmusTime = 1000;
    bcos::evm::engine::OpSchedulerImpl<ViewType> scheduler(bcos::evm::opstack::OpForkTimestamps{
        .isthmusTime = kIsthmusTime, .jovianTime = kIsthmusTime + 1});
    StubMemPool memPool;
    StubExecutor executor;
    static auto blockFactory =
        bcos::test::createBlockFactory(bcos::test::createNormalCryptoSuite());

    // The OP composition root relaxes the version-gate upper bound to V4 (spec §6.3, ruling B1);
    // the generic root's V3 default would refuse version 4 before the OP branch's -38005 gate.
    OpEngine engine(memPool, storage, executor, scheduler, blockFactory,
        /*ledger=*/nullptr, bcos::engine::c_defaultBlockTxCountLimit,
        static_cast<std::uint32_t>(bcos::engine::ApiVersion::V4), /*delegate=*/nullptr);

    // A V4 newPayload with a pre-Isthmus timestamp hits the -38005 version gate (which lives
    // outside the classification barrier), proving the OP branch is wired end-to-end.
    bcos::engine::NewPayloadRequest request;
    request.executionPayload.timestamp = kIsthmusTime - 1;  // pre-Isthmus
    request.executionPayload.blockNumber = 1;
    request.executionPayload.rawTransactions = std::vector<bcos::bytes>{};
    request.executionPayload.withdrawals = std::vector<bcos::engine::WithdrawalV1>{};
    request.executionPayload.withdrawalsRoot = bcos::h256{};
    request.executionPayload.excessBlobGas = bcos::u256(0);
    request.executionPayload.blobGasUsed = bcos::u256(0);
    request.parentBeaconBlockRoot = bcos::h256{};

    BOOST_CHECK_THROW(
        bcos::task::syncWait(engine.newPayload(request, 4)), bcos::engine::UnsupportedFork);
}

BOOST_AUTO_TEST_SUITE_END()
