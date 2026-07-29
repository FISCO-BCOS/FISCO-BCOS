// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

// EngineOpBranchTest.cpp — op-validator-minimal-loop Task 5b (design §6.1/§6.2/§6.3,
// task-5-brief.md Task 5b Step 1). Unit-level coverage of the `newPayload` OP branch's decision
// points, one test per branch:
//   (a) timestamp x version gate (-38005), both directions;
//   (b) blockHash mismatch -> INVALID with latestValidHash = null (and *before* parentKnown);
//   (c) unknown parent -> SYNCING, with nothing written to storage;
//   (d) OP-mode forkchoiceUpdated carrying attributes -> -38003, head still advances;
//   (e) generic composition root still rejects V4 unchanged (regression re-assertion of Task 5a);
//   (f) OpConsensusError from the execution layer -> INVALID (a vector that passes every static
//       check and only then violates OP semantics, so the verdict provably comes from the
//       execution-side error classification and not from a static short-circuit);
//   (g) Isthmus blobGasUsed != 0 -> INVALID;
//   (h) getPayload in OP mode -> explicit "building is not OP-ized" error (design §6.3).
//
// NOT covered here, by design: the end-to-end VALID path (six-way comparison all-match + block
// registration). Producing a payload whose stateRoot/receiptsRoot/... match real execution cannot
// be done by hand-written literals — that is exactly what Task 6's golden-vector gate does, with
// op-geth-anchored values. The tests below therefore exercise every rejection branch and the
// ordering between them; VALID is Task 6's.
//
// Fixture composition follows EngineVersionGateTest.cpp (this directory's Task 5a file) verbatim
// in structure — StubMemPool / StubExecutor / local MultiLayerStorage fixture / manually
// assembled BlockFactory (NOT `bcos::test::createBlockFactory`, whose FakeBlock.h drags in
// <boost/test/unit_test.hpp> into this GTest binary). Deliberately NOT copied:
// EngineServiceTest.cpp:221-229's local-factory pattern — scheduler/executor/storage here are
// named locals that outlive the `EngineServiceImpl` referring to them.
//
// CMake note: this file is NOT yet wired into bcos-evm/test/CMakeLists.txt. It needs (1)
// `${CMAKE_SOURCE_DIR}` on the include path so `"engine/bcos-engine/EngineServiceImpl.h"`
// resolves, and (2) `engine/bcos-engine/EngineServiceImpl.cpp` compiled directly into this
// test's sources — "编入与链 engine 库二选一": compiling the .cpp in and linking the `engine`
// CMake target are mutually exclusive (duplicate symbols), and compiling it in is the choice, to
// avoid dragging `ledger`/mempool into this binary. Both land in Task 6's CMakeLists pass, inside
// the `if(TARGET bcos-framework)` gate.
//
// **未编译验证**: written and committed without cmake/ctest per the project's development-phase
// protocol (and not yet wired into CMakeLists.txt either); see task-5b-report.md for the
// API-precedent map and static walkthrough that substitute for it.

#include "engine/bcos-engine/EngineServiceImpl.h"

#include <bcos-codec/rlp/EthBlockHeader.h>
#include <bcos-concepts/ByteBuffer.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-evm/engine/OpEngineSeam.h>
#include <bcos-evm/engine/OpSchedulerImpl.h>
#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/storage2/MultiLayerStorage.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-framework/transaction-executor/TransactionExecutor.h>
#include <bcos-framework/transaction-scheduler/TransactionScheduler.h>
#include <bcos-ledger/LedgerMethods.h>
#include <bcos-tars-protocol/protocol/BlockFactoryImpl.h>
#include <bcos-tars-protocol/protocol/BlockHeaderFactoryImpl.h>
#include <bcos-tars-protocol/protocol/TransactionFactoryImpl.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h>
#include <bcos-task/Wait.h>
#include <bcos-transaction-scheduler/SchedulerSerialImpl.h>
#include <bcos-utilities/IOServicePool.h>
#include <gtest/gtest.h>
#include <boost/lexical_cast.hpp>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace
{

// ---- storage fixture (EngineVersionGateTest.cpp / OpSchedulerImplTest.cpp's StorageFixture) ----

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

    StorageFixture() : checkpointBackend(backendStorage), multiLayerStorage(checkpointBackend) {}
};

/// Pre-registers a block hash -> number mapping, i.e. declares that block "already verified" per
/// design §6.1 step 5's operational definition (裁定 C2: presence in `SYS_HASH_2_NUMBER`). The
/// spec calls this fixture seeding an explicit, test-only exemption from the invariant that only
/// the VALID branch writes that table — the equivalent of a trusted-genesis premise.
///
/// Encoding is the production one (`BaselineScheduler.h:207-220`): key = the hash's raw 32 bytes,
/// value = the number as a decimal string. It has to be, or `getBlockNumber(..., fromStorage)`
/// would not find it.
void registerVerifiedBlock(StorageFixture& fixture, bcos::h256 const& blockHash, int64_t number)
{
    auto view = fixture.multiLayerStorage.fork();
    view.newMutable();
    bcos::storage::Entry entry;
    entry.set(boost::lexical_cast<std::string>(number));
    bcos::task::syncWait(bcos::storage2::writeOne(view,
        StateKey{bcos::ledger::SYS_HASH_2_NUMBER, bcos::concepts::bytebuffer::toView(blockHash)},
        std::move(entry)));
    fixture.multiLayerStorage.pushView(std::move(view));
}

bool isBlockRegistered(StorageFixture& fixture, bcos::h256 const& blockHash)
{
    auto view = fixture.multiLayerStorage.fork();
    return bcos::task::syncWait(
        bcos::ledger::getBlockNumber(view, blockHash, bcos::ledger::fromStorage))
        .has_value();
}

// ---- stand-ins (EngineVersionGateTest.cpp) ----

/// The OP branch never touches the mempool: `handleOpNewPayload` does not, and the only generic
/// path that does (`updateForkchoice`'s build segment) is inside the `if constexpr (!c_opMode)`
/// branch and is therefore not even instantiated here.
struct StubMemPool
{
};

/// Satisfies `EngineServiceImpl`'s ExecutorType concept constraint only; OP mode never routes a
/// transaction through it (OpSchedulerImpl's dummy `executeBlock` is never called, and
/// `executeOpBlock` drives evmone directly).
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

/// Generic composition root's executor (transaction-scheduler/tests/testSchedulerSerial.cpp:20-46
/// shape — SchedulerSerialImpl's pipeline calls `ExecuteContext::executeStep<N>()`, which
/// StubExecutor above does not have).
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

bcos::crypto::CryptoSuite::Ptr makeCryptoSuite()
{
    // Keccak256 is not optional in OP mode: the block registration derives transaction hashes as
    // keccak over the raw EIP-2718 envelope.
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

bcos::protocol::TransactionReceiptFactory::Ptr makeReceiptFactory()
{
    return std::make_shared<bcostars::protocol::TransactionReceiptFactoryImpl>(makeCryptoSuite());
}

constexpr uint64_t kChainId = 0x2105;  // OpSchedulerImplTest.cpp's kChainId, same corpus family.
constexpr uint64_t kIsthmusTime = 1000;
constexpr uint64_t kJovianTime = 1000000;
constexpr uint64_t kIsthmusTimestamp = 2000;    // in [isthmusTime, jovianTime) -> Isthmus
constexpr uint64_t kPreIsthmusTimestamp = 999;  // < isthmusTime -> pre-Isthmus

using OpScheduler = bcos::evm::engine::OpSchedulerImpl<ViewType>;
using OpEngineService =
    bcos::engine::EngineServiceImpl<StubMemPool, MLS, StubExecutor, OpScheduler>;
using GenericEngineService = bcos::engine::EngineServiceImpl<StubMemPool, MLS, MockExecutorSerial,
    bcos::scheduler_v1::SchedulerSerialImpl>;

static_assert(OpEngineService::c_opMode, "OP composition root must be detected as OP mode");
static_assert(!GenericEngineService::c_opMode, "generic composition root must not be OP mode");

bcos::h256 hashOf(std::string const& seed)
{
    bcos::crypto::Keccak256 keccak;
    return keccak.hash(seed);
}

/// A structurally well-formed OP payload: every static check of design §6.1 step 2 passes except
/// the blockHash comparison, which the caller closes by calling `sealWithBlockHash` below.
bcos::engine::NewPayloadRequest makeOpRequest(
    std::vector<bcos::bytes> rawTransactions, uint64_t timestamp, bcos::h256 const& parentHash)
{
    bcos::engine::NewPayloadRequest request;
    auto& payload = request.executionPayload;
    payload.parentHash = parentHash;
    payload.feeRecipient = bcos::Address{};
    payload.stateRoot = hashOf("stateRoot");
    payload.receiptsRoot = hashOf("receiptsRoot");
    payload.logsBloom = bcos::Bloom{};
    payload.prevRandao = hashOf("prevRandao");
    payload.blockNumber = 1;
    payload.gasLimit = bcos::u256(30000000);
    payload.gasUsed = bcos::u256(0);
    payload.timestamp = timestamp;
    // Holocene+ Isthmus shape: 0x00 version byte ‖ uint32 denominator ‖ uint32 elasticity
    // (design §5.1). The engine does not shape-check extraData this cycle (§6.4 欠账) — this is
    // realistic filler, emitted into the header verbatim.
    payload.extraData = bcos::bytes{0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x02};
    payload.baseFeePerGas = bcos::u256(1000);
    payload.blockHash = bcos::h256{};  // filled in by sealWithBlockHash
    payload.withdrawals = std::vector<bcos::engine::WithdrawalV1>{};
    payload.blobGasUsed = bcos::u256(0);
    payload.excessBlobGas = bcos::u256(0);
    payload.rawTransactions = std::move(rawTransactions);
    payload.withdrawalsRoot = hashOf("withdrawalsRoot");
    request.parentBeaconBlockRoot = hashOf("parentBeaconBlockRoot");
    return request;
}

/// Sets `blockHash` to the hash the OP branch will recompute for this payload.
///
/// It asks the production reconstruction (`detail::rebuildOpEthHeader`) rather than re-deriving
/// the 21-field header here on purpose: this file's subject is the *branch structure* (which
/// verdict, which latestValidHash, in which order), not the header encoding — that is Task 3's
/// 33-vector golden gate (EthBlockHeaderTest.cpp) and Task 6's engine gate, both anchored to
/// op-geth. Re-deriving the header here would duplicate a formula that is already golden-checked
/// elsewhere while adding no independent signal to these tests.
void sealWithBlockHash(bcos::engine::NewPayloadRequest& request)
{
    auto& payload = request.executionPayload;
    const auto transactionsRoot = bcos::evm::engine::computeOpTxRoot(*payload.rawTransactions);
    payload.blockHash = bcos::engine::detail::rebuildOpEthHeader(
        payload, transactionsRoot, *request.parentBeaconBlockRoot)
                            .hash();
}

/// Everything the OP composition root needs, as named members so that scheduler/executor/storage
/// all outlive the service holding references to them (the lifetime trap called out in the file
/// header).
struct OpFixture
{
    StorageFixture storage;
    StubMemPool memPool;
    StubExecutor executor;
    bcos::protocol::TransactionReceiptFactory::Ptr receiptFactory{makeReceiptFactory()};
    OpScheduler scheduler{receiptFactory, kChainId,
        bcos::evm::opstack::OpForkTimestamps{.isthmusTime = kIsthmusTime,
            .jovianTime = kJovianTime}};
    bcos::protocol::BlockFactory::Ptr blockFactory{makeBlockFactory()};
    OpEngineService service{memPool, storage.multiLayerStorage, executor, scheduler, blockFactory,
        bcos::engine::c_defaultBlockTxCountLimit, /*maxEngineVersion=*/4};
};

}  // namespace

// (a) Step 1, design §6.1: the timestamp's fork and the method version must agree, both ways.
TEST(EngineOpBranch, TimestampVersionGateRejectsMismatch)
{
    OpFixture fixture;

    // Isthmus payload on V3 -> -38005 (Isthmus+ requires newPayloadV4).
    auto isthmusOnV3 = makeOpRequest({}, kIsthmusTimestamp, hashOf("parent"));
    EXPECT_THROW(bcos::task::syncWait(fixture.service.newPayload(isthmusOnV3, 3)),
        bcos::engine::UnsupportedFork);

    // pre-Isthmus payload on V4 -> -38005 (the direction `OpForkSchedule::configAt` cannot
    // answer, since it resolves sub-isthmusTime timestamps to the Isthmus config as well —
    // task-4 review item M4; this assertion is what pins the gate to the raw threshold instead).
    auto preIsthmusOnV4 = makeOpRequest({}, kPreIsthmusTimestamp, hashOf("parent"));
    EXPECT_THROW(bcos::task::syncWait(fixture.service.newPayload(preIsthmusOnV4, 4)),
        bcos::engine::UnsupportedFork);

    // Control: the matching combination passes the gate (it is rejected later, for an unrelated
    // reason — parent is unknown — which is exactly what proves the gate itself let it through).
    auto isthmusOnV4 = makeOpRequest({}, kIsthmusTimestamp, hashOf("parent"));
    sealWithBlockHash(isthmusOnV4);
    auto status = bcos::task::syncWait(fixture.service.newPayload(isthmusOnV4, 4));
    EXPECT_EQ(status.status, bcos::engine::PayloadValidationStatus::Syncing);
}

// (b) Step 2, design §6.1: a payload whose blockHash does not match the reconstructed header is
// INVALID with latestValidHash = null — and the check runs BEFORE parentKnown, which is why this
// test never registers the parent yet does not get SYNCING.
TEST(EngineOpBranch, BlockHashMismatchIsInvalidWithNullLatestValidHash)
{
    OpFixture fixture;

    auto request = makeOpRequest({}, kIsthmusTimestamp, hashOf("parent"));
    sealWithBlockHash(request);
    const auto honestBlockHash = request.executionPayload.blockHash;
    request.executionPayload.blockHash = hashOf("tampered-block-hash");
    ASSERT_NE(request.executionPayload.blockHash, honestBlockHash);

    auto status = bcos::task::syncWait(fixture.service.newPayload(request, 4));

    EXPECT_EQ(status.status, bcos::engine::PayloadValidationStatus::Invalid);
    // Never `InvalidBlockHash`: that Engine API status is deprecated since Shanghai and the OP
    // branch deliberately does not use it (design §6.1 step 2).
    EXPECT_NE(status.status, bcos::engine::PayloadValidationStatus::InvalidBlockHash);
    EXPECT_FALSE(status.latestValidHash.has_value());
    ASSERT_TRUE(status.validationError.has_value());
    EXPECT_NE(status.validationError->find("blockHash"), std::string::npos);
}

// (c) Step 3, design §6.1 (rev.2): an unknown parent yields SYNCING — decided by a storage
// lookup, not by the in-memory map the generic path uses — and writes nothing.
TEST(EngineOpBranch, UnknownParentIsSyncingAndWritesNothing)
{
    OpFixture fixture;

    auto request = makeOpRequest({}, kIsthmusTimestamp, hashOf("unknown-parent"));
    sealWithBlockHash(request);

    auto status = bcos::task::syncWait(fixture.service.newPayload(request, 4));

    EXPECT_EQ(status.status, bcos::engine::PayloadValidationStatus::Syncing);
    EXPECT_FALSE(status.latestValidHash.has_value());
    EXPECT_FALSE(status.validationError.has_value());
    // "不入库": neither the block nor its parent may have been registered by this call.
    EXPECT_FALSE(isBlockRegistered(fixture.storage, request.executionPayload.blockHash));
    EXPECT_FALSE(isBlockRegistered(fixture.storage, request.executionPayload.parentHash));
}

// (d) Design §6.2: OP mode refuses attributes with -38003, but the forkchoice state update itself
// is NOT rolled back. Two assertions, as the brief requires: the refusal, and the head having
// advanced anyway.
TEST(EngineOpBranch, ForkchoiceWithAttributesRefusedButHeadStillAdvances)
{
    OpFixture fixture;

    const auto headHash = hashOf("head");
    registerVerifiedBlock(fixture.storage, headHash, 1);

    bcos::engine::ForkchoiceState forkchoiceState{
        .headBlockHash = headHash,
        .safeBlockHash = headHash,
        .finalizedBlockHash = headHash,
    };
    bcos::engine::PayloadAttributes attributes;
    attributes.timestamp = kIsthmusTimestamp;
    attributes.withdrawals = std::vector<bcos::engine::WithdrawalV1>{};
    attributes.parentBeaconBlockRoot = hashOf("parentBeaconBlockRoot");

    EXPECT_THROW(
        bcos::task::syncWait(fixture.service.updateForkchoice(forkchoiceState, &attributes, 3)),
        bcos::engine::UnsupportedOpPayloadAttributes);

    // Head advanced despite the refusal: `getSafeBlockNumber()` is only ever set by
    // `updateTrackedBlockNumbers`, which runs in the same locked section as the head update — so
    // an engaged value here proves the forkchoice update took effect before the throw.
    auto safeBlockNumber = fixture.service.getSafeBlockNumber();
    ASSERT_TRUE(safeBlockNumber.has_value());
    EXPECT_EQ(*safeBlockNumber, 1);

    // And a subsequent attribute-less forkchoiceUpdated on the same head is accepted (it would
    // throw InvalidForkchoiceState if the tracked head had been left behind/rolled back).
    auto result =
        bcos::task::syncWait(fixture.service.updateForkchoice(forkchoiceState, nullptr, 3));
    EXPECT_EQ(result.payloadStatus.status, bcos::engine::PayloadValidationStatus::Valid);
}

// (e) Design §6.3: the generic composition root's version gate is unchanged by any of this —
// re-asserted here (Task 5a owns it) as a regression guard on the OP branch's `if constexpr`
// dispatch, which must be unreachable from the generic root.
TEST(EngineOpBranch, GenericCompositionRootStillRejectsV4)
{
    StorageFixture storage;
    StubMemPool memPool;
    MockExecutorSerial executor;
    auto ioServicePool = std::make_shared<bcos::IOServicePool>(1, "engineOpBranchTest");
    bcos::scheduler_v1::SchedulerSerialImpl scheduler(ioServicePool);
    auto blockFactory = makeBlockFactory();

    GenericEngineService service(
        memPool, storage.multiLayerStorage, executor, scheduler, blockFactory);

    bcos::engine::NewPayloadRequest request;
    EXPECT_THROW(bcos::task::syncWait(service.newPayload(request, 4)),
        bcos::engine::UnsupportedEngineApiVersion);
}

// (f) Step 4, design §6.1 (裁定 C7): a vector that passes every static check and only violates OP
// semantics at execution time must still come back INVALID — proving the verdict travels through
// the execution-side error classification (OpConsensusError -> INVALID) rather than through a
// static short-circuit.
//
// The vector: a single raw "transaction" whose EIP-2718 type byte is 0xff. Nothing in the static
// checks looks inside the transaction list (the blockHash covers the *bytes*, and this payload's
// blockHash is computed over exactly these bytes, so it matches), while
// `OpSchedulerImpl::executeOpBlock`'s decode step rejects the type byte outright with
// `OpConsensusError`. latestValidHash is the parent, since step 3 established it as verified.
TEST(EngineOpBranch, ConsensusErrorFromExecutionMapsToInvalid)
{
    OpFixture fixture;

    const auto parentHash = hashOf("parent");
    registerVerifiedBlock(fixture.storage, parentHash, 0);

    auto request = makeOpRequest({bcos::bytes{0xff}}, kIsthmusTimestamp, parentHash);
    sealWithBlockHash(request);

    auto status = bcos::task::syncWait(fixture.service.newPayload(request, 4));

    EXPECT_EQ(status.status, bcos::engine::PayloadValidationStatus::Invalid);
    ASSERT_TRUE(status.latestValidHash.has_value());
    EXPECT_EQ(*status.latestValidHash, parentHash);
    ASSERT_TRUE(status.validationError.has_value());
    // Names the execution stage, not a static field — i.e. it did not短路 in step 2.
    EXPECT_NE(status.validationError->find("OP block execution rejected"), std::string::npos);
    // A rejected block is not registered.
    EXPECT_FALSE(isBlockRegistered(fixture.storage, request.executionPayload.blockHash));
}

// (g) Step 2, design §5.1/§6.1 (裁定 C7): under Isthmus the header's blobGasUsed slot is a real
// blob-gas counter and OP blocks carry no blobs, so a non-zero value is INVALID. (From Jovian on
// the same slot is the DA footprint and is checked by seal comparison instead — which is why the
// check is fork-gated rather than unconditional.)
TEST(EngineOpBranch, NonZeroBlobGasUsedIsInvalidUnderIsthmus)
{
    OpFixture fixture;

    const auto parentHash = hashOf("parent");
    registerVerifiedBlock(fixture.storage, parentHash, 0);

    auto request = makeOpRequest({}, kIsthmusTimestamp, parentHash);
    request.executionPayload.blobGasUsed = bcos::u256(1);
    sealWithBlockHash(request);  // blockHash consistent, so only the OP constraint can reject it

    auto status = bcos::task::syncWait(fixture.service.newPayload(request, 4));

    EXPECT_EQ(status.status, bcos::engine::PayloadValidationStatus::Invalid);
    EXPECT_FALSE(status.latestValidHash.has_value());
    ASSERT_TRUE(status.validationError.has_value());
    EXPECT_NE(status.validationError->find("blobGasUsed"), std::string::npos);
}

// (h) Design §6.3: `getPayload` in OP mode reports an explicit "block building is not OP-ized"
// error rather than the misleading `UnknownPayload`.
TEST(EngineOpBranch, GetPayloadIsRefusedInOpMode)
{
    OpFixture fixture;

    const bcos::engine::PayloadID payloadId{"0x1"};
    EXPECT_THROW(bcos::task::syncWait(fixture.service.getPayload(payloadId, 4)),
        bcos::engine::OpPayloadBuildingUnsupported);
}
