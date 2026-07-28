// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

// EngineVersionGateTest.cpp — op-validator-minimal-loop Task 5a (design §6.3, task-5-brief.md
// Task 5a Step 1). Covers exactly the two things this task delivers and nothing from Task 5b
// (the OP `newPayload` branch body, which does not exist yet):
//   (a) the GENERIC composition root's `isVersionSupported` gate is byte-for-byte unchanged —
//       version 4 is still rejected with `UnsupportedEngineApiVersion`, the same as before
//       `maxEngineVersion` existed (zero-drift, spec §6.3 "V4 放宽门控");
//   (b) the OP composition root (constructed with `maxEngineVersion = 4`) is no longer rejected
//       by the version gate — `newPayload(request, 4)` reaches past `isVersionSupported` into
//       the pre-existing generic validation body (Task 5b has not wired an OP branch there yet,
//       so what it lands on is `detail::validateExecutionPayload`'s ordinary V2+ withdrawals
//       check, not any OP-specific outcome — asserted as "did not throw
//       UnsupportedEngineApiVersion", per task-5-brief.md's own two allowed formulations).
//
// Fixture composition (brief-mandated, not this file's own choice):
//   - generic root: the REAL `bcos::scheduler_v1::SchedulerSerialImpl` (production scheduler,
//     not a trivial engine-test stub) + a local recreation of `MockExecutorSerial`
//     (transaction-scheduler/tests/testSchedulerSerial.cpp:20-46 — same per-file "keep a tiny
//     local copy" convention EngineServiceTest.cpp:84-98 and this directory's own
//     OpSchedulerImplTest.cpp:121-145/262-288 already establish; MockExecutorSerial's nested
//     `ExecuteContext::executeStep<N>()` shape is what SchedulerSerialImpl's pipeline actually
//     calls — StubExecutor from EngineServiceTest.cpp/OpSchedulerImplTest.cpp does NOT have that
//     shape and would not work here).
//   - OP root: `bcos::evm::engine::OpSchedulerImpl<ViewType>` (Task 4) + `StubExecutor`
//     (OpSchedulerImplTest.cpp:266-288 precedent — only needed to satisfy EngineServiceImpl's
//     `ExecutorType` concept constraint; OP mode's dummy `executeBlock` never touches it).
//   - storage: local MultiLayerStorage fixture, copied from OpSchedulerImplTest.cpp's own
//     `StorageFixture` (that file's own header comment traces this convention back through
///    EbT8nReplayTest.cpp to EngineServiceTest.cpp:84-98).
//   - BlockFactory: manually assembled from bcos-tars-protocol pieces, NOT
//     `bcos::test::createBlockFactory` (bcos-framework/testutils/faker/FakeBlock.h) — that
//     header unconditionally pulls in `<boost/test/unit_test.hpp>`, which this binary
//     (bcos-evm-opstack-tests, GTest-based, does not link Boost::unit_test_framework) must not
//     depend on. Neither constructed BlockFactory is ever exercised by the code paths under
//     test here (the version gate throws, or the withdrawals check returns, before
//     `m_blockFactory` is read) — it exists solely to satisfy the constructor's non-null check.
//
// Both `EngineServiceImpl` template parameters explicitly include the class's own new
// `c_opMode` static member via `static_assert`, giving the opMode compile-time judgment
// (task-5a "opMode 编译期判据") a machine-checked witness independent of the runtime
// assertions below.
//
// CMake note (task-5-brief.md Task 5b's own fixture-closure note applies identically here,
// since this file has the exact same build requirements): this file is NOT yet wired into
// bcos-evm/test/CMakeLists.txt. It needs (1) `${CMAKE_SOURCE_DIR}` on the include path so
// `"engine/bcos-engine/EngineServiceImpl.h"` resolves (mirroring engine/test/CMakeLists.txt's
// own `target_include_directories(... PRIVATE . ${CMAKE_SOURCE_DIR})`), and (2)
// `engine/bcos-engine/EngineServiceImpl.cpp` compiled directly into this test's sources (NOT
// linking the `engine` CMake target, which would pull in `ledger`/mempool/etc. and risks
// duplicate symbols) — both deferred to Task 6's `bcos-evm/test/CMakeLists.txt` pass, same
// "编入与链 engine 库二选一" guardrail Task 5b's brief already pins for `EngineOpBranchTest.cpp`.
//
// **未编译验证**: written and committed without cmake/ctest per the project's development-phase
// protocol (not yet wired into CMakeLists.txt either, see note above); see task-5a-report.md for
// the static walkthrough / API-precedent cross-check that substitutes for it.

#include "engine/bcos-engine/EngineServiceImpl.h"

#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-evm/engine/OpSchedulerImpl.h>
#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-framework/ledger/LedgerConfig.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/storage2/MultiLayerStorage.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-framework/transaction-executor/TransactionExecutor.h>
#include <bcos-framework/transaction-scheduler/TransactionScheduler.h>
#include <bcos-tars-protocol/protocol/BlockFactoryImpl.h>
#include <bcos-tars-protocol/protocol/BlockHeaderFactoryImpl.h>
#include <bcos-tars-protocol/protocol/TransactionFactoryImpl.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h>
#include <bcos-task/Wait.h>
#include <bcos-transaction-scheduler/SchedulerSerialImpl.h>
#include <bcos-utilities/IOServicePool.h>
#include <gtest/gtest.h>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

namespace
{

// ---- MultiLayerStorage fixture (OpSchedulerImplTest.cpp's own StorageFixture, copied per this
// directory's established "each test module keeps its own tiny copy" convention). ----

using bcos::executor_v1::StateKey;
using bcos::executor_v1::StateValue;
namespace memory_storage = bcos::storage2::memory_storage;

template <class Key, class Value, bcos::storage2::ReadWriteStorage<Key, Value> Storage>
struct TrivialCheckpointStorage
{
    using CheckpointName = bcos::h256;

    Storage& m_storage;
    explicit TrivialCheckpointStorage(Storage& storage) noexcept : m_storage(storage) {}
    Storage& open() & { return m_storage; }
    [[noreturn]] Storage& open(CheckpointName const& /*unused*/) &
    {
        std::abort();  // this fixture never needs historical checkpoints.
    }
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

struct StorageFixture
{
    BackendMemStorage backendStorage;
    CheckpointBackend checkpointBackend;
    MLS multiLayerStorage;
    ViewType view;

    StorageFixture()
      : checkpointBackend(backendStorage),
        multiLayerStorage(checkpointBackend),
        view(multiLayerStorage.fork())
    {
        view.newMutable();
    }
};

// ---- MemPool: never exercised by either test below (only `updateForkchoice` touches
// `m_memPool`, and neither test calls it — the version gate in `newPayload`/`handleNewPayload`
// short-circuits, or the fallthrough validation error, both happen before any mempool access),
// so an empty stand-in is sufficient; EngineServiceImpl's class template places no `requires`
// constraint on MemPoolType (only ExecutorType/SchedulerType are concept-constrained). ----
struct StubMemPool
{
};

// ---- generic composition root: real SchedulerSerialImpl + a local MockExecutorSerial
// recreation (transaction-scheduler/tests/testSchedulerSerial.cpp:20-46 precedent, task-5-brief
// mandated combo). ----
struct MockExecutorSerial
{
    template <class Storage>
    struct ExecuteContext
    {
        template <int step>
        bcos::task::Task<bcos::protocol::TransactionReceipt::Ptr> executeStep()
        {
            co_return {};
        }
    };

    auto createExecuteContext(auto& storage, bcos::protocol::BlockHeader const& blockHeader,
        bcos::protocol::Transaction const& transaction, int32_t contextID,
        bcos::ledger::LedgerConfig const& ledgerConfig, bool call)
        -> bcos::task::Task<ExecuteContext<std::decay_t<decltype(storage)>>>
    {
        (void)storage;
        (void)blockHeader;
        (void)transaction;
        (void)contextID;
        (void)ledgerConfig;
        (void)call;
        co_return {};
    }

    bcos::task::Task<bcos::protocol::TransactionReceipt::Ptr> executeTransaction(auto& storage,
        bcos::protocol::BlockHeader const& blockHeader,
        bcos::protocol::Transaction const& transaction, int contextID,
        bcos::ledger::LedgerConfig const& /*unused*/, bool /*unused*/)
    {
        (void)storage;
        (void)blockHeader;
        (void)transaction;
        (void)contextID;
        co_return nullptr;
    }
};

// ---- OP composition root: StubExecutor (OpSchedulerImplTest.cpp:266-288 precedent) -- only
// needed to satisfy EngineServiceImpl's ExecutorType concept constraint; OpSchedulerImpl's dummy
// `executeBlock` (Task 4) never touches it. ----
struct StubExecutor
{
    template <class Storage>
    struct ExecuteContext
    {
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

// ---- BlockFactory: manual assembly, NOT bcos::test::createBlockFactory (FakeBlock.h drags in
// <boost/test/unit_test.hpp>, unwanted in this GTest binary — see file header comment). Neither
// test below actually reads m_blockFactory's contents; it exists to satisfy the constructor's
// non-null check. ----
bcos::protocol::BlockFactory::Ptr makeBlockFactory()
{
    auto cryptoSuite = std::make_shared<bcos::crypto::CryptoSuite>(
        std::make_shared<bcos::crypto::Keccak256>(), nullptr, nullptr);
    auto blockHeaderFactory =
        std::make_shared<bcostars::protocol::BlockHeaderFactoryImpl>(cryptoSuite);
    auto transactionFactory =
        std::make_shared<bcostars::protocol::TransactionFactoryImpl>(cryptoSuite);
    auto receiptFactory =
        std::make_shared<bcostars::protocol::TransactionReceiptFactoryImpl>(cryptoSuite);
    return std::make_shared<bcostars::protocol::BlockFactoryImpl>(
        cryptoSuite, blockHeaderFactory, transactionFactory, receiptFactory);
}

bcos::protocol::TransactionReceiptFactory::Ptr makeReceiptFactory()
{
    auto cryptoSuite = std::make_shared<bcos::crypto::CryptoSuite>(
        std::make_shared<bcos::crypto::Keccak256>(), nullptr, nullptr);
    return std::make_shared<bcostars::protocol::TransactionReceiptFactoryImpl>(cryptoSuite);
}

constexpr uint64_t kChainId = 0x2105;  // OpSchedulerImplTest.cpp's kChainId, same corpus family.

}  // namespace

// (a) Generic composition root: version 4 is rejected exactly as before this task
// (UnsupportedEngineApiVersion) — zero drift (spec §6.3 "通用组合根对 V4 行为与现状逐字节一致").
TEST(EngineVersionGate, GenericCompositionRootRejectsV4Unchanged)
{
    StorageFixture fixture;
    StubMemPool memPool;
    MockExecutorSerial executor;
    auto ioServicePool = std::make_shared<bcos::IOServicePool>(1, "engineVersionGateTest");
    bcos::scheduler_v1::SchedulerSerialImpl scheduler(ioServicePool);
    auto blockFactory = makeBlockFactory();

    using GenericEngineService = bcos::engine::EngineServiceImpl<StubMemPool, MLS,
        MockExecutorSerial, bcos::scheduler_v1::SchedulerSerialImpl>;
    static_assert(!GenericEngineService::c_opMode,
        "generic composition root must not be detected as OP mode (task-5a opMode probe)");

    GenericEngineService engineService(
        memPool, fixture.multiLayerStorage, executor, scheduler, blockFactory);

    // Default-constructed request: the version gate is the very first thing `newPayload` checks
    // (engine/bcos-engine/EngineServiceImpl.h's `handleNewPayload`), so no field needs to be
    // populated for this assertion.
    bcos::engine::NewPayloadRequest request;

    EXPECT_THROW(bcos::task::syncWait(engineService.newPayload(request, 4)),
        bcos::engine::UnsupportedEngineApiVersion);
}

// (b) OP composition root (maxEngineVersion=4): version 4 is no longer rejected by the version
// gate. Task 5b (not this task) still owns the actual OP `newPayload` branch body, so what the
// call lands on here is the pre-existing generic static-validation fallthrough (the withdrawals
// check) -- asserted narrowly as "did not throw UnsupportedEngineApiVersion", the exact
// task-5-brief.md-sanctioned formulation for this task's boundary.
TEST(EngineVersionGate, OpCompositionRootNotRejectedByVersionGate)
{
    StorageFixture fixture;
    StubMemPool memPool;
    StubExecutor executor;
    auto receiptFactory = makeReceiptFactory();
    bcos::evm::engine::OpSchedulerImpl<ViewType> scheduler(receiptFactory, kChainId,
        bcos::evm::opstack::OpForkTimestamps{.isthmusTime = 0, .jovianTime = 100000});
    auto blockFactory = makeBlockFactory();

    using OpEngineService = bcos::engine::EngineServiceImpl<StubMemPool, MLS, StubExecutor,
        bcos::evm::engine::OpSchedulerImpl<ViewType>>;
    static_assert(OpEngineService::c_opMode,
        "OP composition root must be detected as OP mode (task-5a opMode probe)");

    OpEngineService engineService(memPool, fixture.multiLayerStorage, executor, scheduler,
        blockFactory, bcos::engine::c_defaultBlockTxCountLimit, /*maxEngineVersion=*/4);

    // Default-constructed request: `withdrawals` stays nullopt, so once the (now-passing)
    // version gate lets execution through, `detail::validateExecutionPayload` deterministically
    // returns the V2+ "withdrawals are required" error -- a fixed, non-OP-specific fallthrough
    // outcome, not an accidental one (unlike relying on both `parentHash`/`headBlockHash`
    // defaulting to the same zero hash, which this test deliberately avoids).
    bcos::engine::NewPayloadRequest request;

    bool threwUnsupportedVersion = false;
    std::optional<bcos::engine::PayloadStatus> status;
    try
    {
        status = bcos::task::syncWait(engineService.newPayload(request, 4));
    }
    catch (const bcos::engine::UnsupportedEngineApiVersion&)
    {
        threwUnsupportedVersion = true;
    }

    EXPECT_FALSE(threwUnsupportedVersion);
    ASSERT_TRUE(status.has_value());
    EXPECT_EQ(static_cast<int>(status->status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Invalid));
    ASSERT_TRUE(status->validationError.has_value());
    EXPECT_NE(status->validationError->find("withdrawals"), std::string::npos);
}
