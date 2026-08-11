// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

// OpCallSchedulerTest — unit tests for the OP-mode RPC-facing scheduler adapter
// (opstack-executor/OpCallScheduler.h). Focused on what the adapter itself owns:
//  1. the loud refusal of the block-execution methods (executeBlock/commitBlock/
//     preExecuteBlock) — the fix for executor_version>=3 silently saturating to the v2 ethereum
//     scheduler, where those forwarded to the ethereum executor;
//  2. the eth_call error path: a ledger fault (empty storage -> getCurrentBlockNumber -1 ->
//     getBlockData "Wrong argument") surfaces as a callback Error, never a crash or a swallowed
//     wrong answer.
// The full OP eth_call happy path (fee / baseFee / block-hash injection parity with processOpBlock)
// needs a seeded ledger (evmcRevision config + block tables + L1Block slots) and is deferred to a
// follow-up; OpstackExecutor's own execution semantics are covered by OpstackExecutorTest.

#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/storage2/MultiLayerStorage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-tars-protocol/protocol/BlockFactoryImpl.h>
#include <bcos-tars-protocol/protocol/BlockHeaderFactoryImpl.h>
#include <bcos-tars-protocol/protocol/TransactionFactoryImpl.h>
#include <bcos-tars-protocol/protocol/TransactionImpl.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h>
#include <bcos-utilities/Common.h>
#include <opstack-executor/OpCallScheduler.h>
#include <boost/test/unit_test.hpp>
#include <memory>

using bcos::executor_v1::StateKey;
using bcos::executor_v1::StateValue;
namespace memory_storage = bcos::storage2::memory_storage;

namespace bcos::executor_v1::opstack
{
namespace
{
// Minimal CheckpointStorage stub — same pattern as OpSchedulerImplSmokeTest's per-file local copy
// (do not cross-include another module's test-private header).
template <class Key, class Value, bcos::storage2::ReadWriteStorage<Key, Value> Storage>
struct TrivialCheckpointStorage
{
    using CheckpointName = bcos::h256;

    Storage& m_storage;
    explicit TrivialCheckpointStorage(Storage& storage) noexcept : m_storage(storage) {}
    // By value (not `Storage&`): MultiLayerStorage's OpenedStorage must be a VALUE type — a
    // reference OpenedStorage makes the fork view's `BackendStorage*` a pointer-to-reference,
    // which is ill-formed and breaks the ReadableStorage<StateKey> concept the ledger functions
    // (getCurrentBlockNumber/getLedgerConfig/getBlockData) require. MemoryStorage is not
    // copyable, so return a move of the (empty, read-only) initial state; the error-path test
    // never reads through the moved-from backend.
    Storage open() & { return std::move(m_storage); }
    [[noreturn]] Storage& open(CheckpointName const&) & { std::abort(); }
    void createCheckpoint(Storage&, CheckpointName const&) {}
    void deleteCheckpoint(CheckpointName const&) {}
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

bcos::crypto::CryptoSuite::Ptr makeCryptoSuite()
{
    return std::make_shared<bcos::crypto::CryptoSuite>(
        std::make_shared<bcos::crypto::Keccak256>(), nullptr, nullptr);
}

bcos::protocol::BlockFactory::Ptr makeBlockFactory()
{
    auto cryptoSuite = makeCryptoSuite();
    auto blockHeaderFactory =
        std::make_shared<bcostars::protocol::BlockHeaderFactoryImpl>(cryptoSuite);
    auto transactionFactory =
        std::make_shared<bcostars::protocol::TransactionFactoryImpl>(cryptoSuite);
    auto receiptFactory =
        std::make_shared<bcostars::protocol::TransactionReceiptFactoryImpl>(cryptoSuite);
    return std::make_shared<bcostars::protocol::BlockFactoryImpl>(
        cryptoSuite, blockHeaderFactory, transactionFactory, receiptFactory);
}

using OpCallSchedulerT = OpCallScheduler<MLS>;

OpCallSchedulerT makeScheduler(MLS& storage)
{
    auto blockFactory = makeBlockFactory();
    return OpCallSchedulerT(blockFactory->receiptFactory(), makeCryptoSuite()->hashImpl(), 0x2105,
        bcos::evm::opstack::OpForkTimestamps{.isthmusTime = 1000, .jovianTime = 2000},
        blockFactory, storage);
}

/// A minimal Transaction whose content is never dereferenced on the empty-storage error path.
bcos::protocol::Transaction::Ptr makeAnyTransaction()
{
    auto tarsTx = std::make_shared<bcostars::Transaction>();
    return std::make_shared<bcostars::protocol::TransactionImpl>(
        [tarsTx]() { return tarsTx.get(); });
}
}  // namespace

BOOST_AUTO_TEST_SUITE(OpCallSchedulerSuite)

BOOST_AUTO_TEST_CASE(BlockExecutionMethodsRefuse)
{
    BackendMemStorage backendStorage;
    CheckpointBackend checkpointBackend(backendStorage);
    MLS multiLayerStorage(checkpointBackend);
    auto scheduler = makeScheduler(multiLayerStorage);

    // OP block execution is engine-driven (newPayload -> executeOpBlock); these must fail loudly
    // instead of running any executor silently (the old version-3 saturation path).
    scheduler.executeBlock(
        nullptr, false, [](bcos::Error::Ptr error, bcos::protocol::BlockHeader::Ptr, bool) {
            BOOST_REQUIRE(error != nullptr);
        });
    scheduler.commitBlock(
        nullptr, [](bcos::Error::Ptr error, bcos::ledger::LedgerConfig::Ptr) {
            BOOST_REQUIRE(error != nullptr);
        });
    scheduler.preExecuteBlock(nullptr, false, [](bcos::Error::Ptr error) {
        BOOST_REQUIRE(error != nullptr);
    });
}

BOOST_AUTO_TEST_CASE(CallOnEmptyStorageReturnsError)
{
    BackendMemStorage backendStorage;
    CheckpointBackend checkpointBackend(backendStorage);
    MLS multiLayerStorage(checkpointBackend);
    auto scheduler = makeScheduler(multiLayerStorage);

    // Empty storage: getCurrentBlockNumber -> -1, then getBlockData(-1) throws (Wrong argument).
    // The adapter must surface that as a callback Error — the "storage fault is an Error, not a
    // crash" contract — and must call the callback exactly once.
    bool called = false;
    scheduler.call(makeAnyTransaction(),
        [&called](bcos::Error::Ptr error, bcos::protocol::TransactionReceipt::Ptr) {
            called = true;
            BOOST_REQUIRE(error != nullptr);
        });
    BOOST_REQUIRE(called);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::executor_v1::opstack
