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
// Tests (a)-(h) drive the REAL `OpSchedulerImpl`. Three further groups need outcomes real
// execution cannot be steered into from hand-written literals, and are driven by `StubOpScheduler`
// (see its comment — the seam is pure duck typing, so a stand-in is a first-class client of it):
//   (i) end-to-end VALID + the four-table block registration asserted table by table;
//   (j) each of the six comparison-surface fields mismatched individually;
//   (k) OpStorageError -> -32603, never INVALID;
//   (l) non-consecutive blockNumber -> INVALID (task-5b review I2).
//
// What still cannot be tested here at all: whether `rebuildOpEthHeader`'s payload-field ->
// header-field mapping is the RIGHT one. Every blockHash in this file is computed by that same
// mapping, so a systematically wrong mapping passes silently (see `rebuiltHeader`'s comment).
// Pinning it is Task 6's job, with op-geth golden blockHash values — a hard constraint on that
// task, not an optional extra.
//
// Fixture composition follows EngineVersionGateTest.cpp (this directory's Task 5a file) verbatim
// in structure — StubMemPool / StubExecutor / local MultiLayerStorage fixture / manually
// assembled BlockFactory (NOT `bcos::test::createBlockFactory`, whose FakeBlock.h drags in
// <boost/test/unit_test.hpp> into this GTest binary). Deliberately NOT copied:
// EngineServiceTest.cpp:221-229's local-factory pattern — scheduler/executor/storage here are
// named locals that outlive the `EngineServiceImpl` referring to them.
//
// CMake note: Task 6 wired this file into bcos-evm/test/CMakeLists.txt, inside the
// `if(TARGET bcos-framework)` gate, with (1) `${CMAKE_SOURCE_DIR}` on the include path so
// `"engine/bcos-engine/EngineServiceImpl.h"` resolves, and (2)
// `engine/bcos-engine/EngineServiceImpl.cpp` compiled directly into this test's sources —
// "编入与链 engine 库二选一": compiling the .cpp in and linking the `engine` CMake target are
// mutually exclusive (duplicate symbols), and compiling it in is the choice, to avoid dragging
// `engine`'s dependency closure into this binary.
//
// The Task 6 gate promised below now exists: `EngineNewPayloadGateTest.cpp`, in this directory.
//
// **未编译验证**: written and committed without cmake/ctest per the project's development-phase
// protocol; see task-5b-report.md for the API-precedent map and static walkthrough that
// substitute for it.

#include "engine/bcos-engine/EngineServiceImpl.h"

#include <bcos-codec/rlp/EthBlockHeader.h>
#include <bcos-concepts/ByteBuffer.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-evm/engine/OpEngineSeam.h>
#include <bcos-evm/engine/OpSchedulerImpl.h>
#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/protocol/LogEntry.h>
#include <bcos-framework/protocol/TransactionReceiptFactory.h>
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
#include <stdexcept>
#include <string>
#include <string_view>
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

std::optional<bcos::protocol::BlockNumber> registeredBlockNumber(
    StorageFixture& fixture, bcos::h256 const& blockHash)
{
    auto view = fixture.multiLayerStorage.fork();
    return bcos::task::syncWait(
        bcos::ledger::getBlockNumber(view, blockHash, bcos::ledger::fromStorage));
}

bool isBlockRegistered(StorageFixture& fixture, bcos::h256 const& blockHash)
{
    return registeredBlockNumber(fixture, blockHash).has_value();
}

/// Raw table read-back (`storage2::readOne` + `StateKeyView`, the shape
/// `bcos-ledger/LedgerMethods.h:110-119` uses on the write side's mirror image).
std::optional<bcos::storage::Entry> readEntry(
    StorageFixture& fixture, std::string_view table, std::string_view key)
{
    auto view = fixture.multiLayerStorage.fork();
    return bcos::task::syncWait(
        bcos::storage2::readOne(view, bcos::executor_v1::StateKeyView{table, key}));
}

/// `Entry::get()` returns a `string_view` over raw stored bytes; this is how a `bcos::bytes`
/// expectation is compared against it without a hex round-trip.
std::string_view asStringView(bcos::bytes const& data)
{
    return {reinterpret_cast<const char*>(data.data()), data.size()};
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

/// The 21-field header the OP branch will reconstruct for this payload.
///
/// It asks the production reconstruction (`detail::rebuildOpEthHeader`) rather than re-deriving
/// the header here on purpose: this file's subject is the *branch structure* (which verdict,
/// which latestValidHash, in which order), not the header encoding — that is Task 3's 33-vector
/// golden gate (EthBlockHeaderTest.cpp) and Task 6's engine gate, both anchored to op-geth.
/// Re-deriving the header here would duplicate a formula that is already golden-checked elsewhere
/// while adding no independent signal to these tests.
///
/// **T6 hard constraint follows from this** (task-5b review I4): because every blockHash in this
/// file is self-computed by the very mapping under test, NOTHING here can catch a wrong
/// payload-field -> header-field mapping in `rebuildOpEthHeader` (e.g. swapping prevRandao and
/// parentBeaconBlockRoot: both sides move together and the comparison still passes). Task 6's
/// gate MUST therefore feed op-geth golden `blockHash` values into `newPayload` and assert VALID
/// — that is the only place the mapping itself is pinned externally.
bcos::codec::rlp::EthBlockHeader rebuiltHeader(bcos::engine::NewPayloadRequest const& request)
{
    auto const& payload = request.executionPayload;
    const auto transactionsRoot = bcos::evm::engine::computeOpTxRoot(*payload.rawTransactions);
    return bcos::engine::detail::rebuildOpEthHeader(
        payload, transactionsRoot, *request.parentBeaconBlockRoot);
}

/// Sets `blockHash` to the hash the OP branch will recompute for this payload.
void sealWithBlockHash(bcos::engine::NewPayloadRequest& request)
{
    request.executionPayload.blockHash = rebuiltHeader(request).hash();
}

// ---- StubOpScheduler: the seam driven by a scriptable stand-in ----
//
// The engine's OP branch reaches the OP side only through dependent names on `SchedulerType`
// (`BlockEnv`, `ExecuteResult`, `ConsensusError`, `StorageError`, `c_ethBlockHeaderTable`,
// `commitmentsOf`, `computeTxRoot`, `isIsthmusActiveAt`, `isJovianActiveAt`, `executeOpBlock`) —
// i.e. the seam is pure duck typing, with no inheritance and no bcos-evm dependency on the engine
// side. That is exactly what lets this stub stand in for `OpSchedulerImpl` and drive the branches
// that real execution cannot reach without op-geth golden values (task-5b review I1): the
// end-to-end VALID path with its four-table registration, each of the six comparison-surface
// mismatches individually, and the OpStorageError -> -32603 classification.
//
// `computeTxRoot` deliberately forwards to the REAL `computeOpTxRoot`: the transactionsRoot feeds
// the header reconstruction whose hash the payload must match, so faking it would only mean
// faking both sides of the blockHash check.
struct StubCommitments
{
    bcos::h256 receiptsRoot;
    bcos::h2048 logsBloom;
    bcos::h256 withdrawalsRoot;
    bcos::h256 stateRoot;
    bcos::u256 gasUsed;
    bcos::h256 txRoot;
    std::optional<uint64_t> blobGasUsed;
    std::optional<bcos::h256> requestsHash;
};

struct StubOpScheduler
{
    /// Field names and order must match `bcos::evm::engine::OpBlockEnv` — the engine
    /// aggregate-initializes this type with designated initializers.
    struct BlockEnv
    {
        const bcos::protocol::BlockHeader& fiscoHeader;
        bcos::h256 parentHash;
        bcos::h256 prevRandao;
        bcos::u256 baseFeePerGas;
        bcos::Address feeRecipient;
        bcos::h256 parentBeaconBlockRoot;
        uint64_t gasLimit;
        bcos::bytes extraData;
        uint64_t blobGasUsed;
    };

    /// Carries the commitments inline because `commitmentsOf` is called *statically* by the
    /// engine (`SchedulerType::commitmentsOf(result)`) and so cannot read instance state.
    struct ExecuteResult
    {
        std::vector<bcos::protocol::TransactionReceipt::Ptr> receipts;
        StubCommitments commitments;
    };

    struct ConsensusError : std::runtime_error
    {
        using std::runtime_error::runtime_error;
    };
    struct StorageError : std::runtime_error
    {
        using std::runtime_error::runtime_error;
    };

    static constexpr std::string_view c_ethBlockHeaderTable =
        bcos::evm::engine::SYS_ETH_BLOCK_HEADER;

    // ---- script ----
    ExecuteResult result;
    std::optional<std::string> consensusErrorToThrow;
    std::optional<std::string> storageErrorToThrow;
    // ---- observation ----
    unsigned executeOpBlockCalls = 0;

    static StubCommitments commitmentsOf(const ExecuteResult& executeResult)
    {
        return executeResult.commitments;
    }

    static bcos::h256 computeTxRoot(::ranges::input_range auto const& rawTxBytes)
    {
        return bcos::evm::engine::computeOpTxRoot(rawTxBytes);
    }

    [[nodiscard]] bool isIsthmusActiveAt(uint64_t timestamp) const noexcept
    {
        return timestamp >= kIsthmusTime;
    }
    [[nodiscard]] bool isJovianActiveAt(uint64_t timestamp) const noexcept
    {
        return timestamp >= kJovianTime;
    }

    /// Concept satisfaction only (`scheduler_v1::TransactionScheduler`), never called in OP mode —
    /// same role and same throw-before-any-suspend shape as `OpSchedulerImpl::executeBlock`.
    bcos::task::Task<std::vector<bcos::protocol::TransactionReceipt::Ptr>> executeBlock(ViewType&,
        auto&, bcos::protocol::BlockHeader const&, ::ranges::input_range auto const&,
        bcos::ledger::LedgerConfig const&)
    {
        throw std::logic_error("StubOpScheduler::executeBlock: OP mode must not call this");
        co_return {};  // unreachable; satisfies the coroutine's declared return type
    }

    /// Shape must match `OpSchedulerImpl::executeOpBlock` exactly (one invented template
    /// parameter, for `rawTxBytes`), or `EngineServiceImpl::c_opMode`'s
    /// `&SchedulerType::template executeOpBlock<std::vector<bcos::bytes>>` probe would not
    /// resolve — the static_assert below is what pins that.
    bcos::task::Task<ExecuteResult> executeOpBlock(
        ViewType& /*storage*/, BlockEnv const& /*env*/, ::ranges::input_range auto const&)
    {
        ++executeOpBlockCalls;
        if (consensusErrorToThrow.has_value())
        {
            throw ConsensusError(*consensusErrorToThrow);
        }
        if (storageErrorToThrow.has_value())
        {
            throw StorageError(*storageErrorToThrow);
        }
        co_return result;
    }
};

using StubEngineService =
    bcos::engine::EngineServiceImpl<StubMemPool, MLS, StubExecutor, StubOpScheduler>;
static_assert(StubEngineService::c_opMode,
    "StubOpScheduler must satisfy the same opMode probe as OpSchedulerImpl");

bcos::protocol::TransactionReceipt::Ptr makeReceipt(
    bcos::protocol::TransactionReceiptFactory::Ptr const& receiptFactory, uint64_t gasUsed)
{
    // Same call shape as OpReceiptMap.h's `mapOpReceipt`.
    return receiptFactory->createReceipt(bcos::u256(gasUsed), std::string{},
        bcos::protocol::LogEntries{}, /*status=*/0, bcos::bytesConstRef{}, /*blockNumber=*/0);
}

/// The commitments a correct execution of `request` would produce: every one of the six
/// comparison-surface fields equal to what the payload claims, plus the §5.1 `requestsHash`
/// (engaged, so the VALID path exercises that comparison too) and no `blobGasUsed` (pre-Jovian).
StubCommitments matchingCommitments(
    bcos::engine::NewPayloadRequest const& request, bcos::codec::rlp::EthBlockHeader const& header)
{
    auto const& payload = request.executionPayload;
    return StubCommitments{
        .receiptsRoot = payload.receiptsRoot,
        .logsBloom = bcos::engine::detail::toEthLogsBloom(payload.logsBloom),
        .withdrawalsRoot = *payload.withdrawalsRoot,
        .stateRoot = payload.stateRoot,
        .gasUsed = payload.gasUsed,
        .txRoot = bcos::evm::engine::computeOpTxRoot(*payload.rawTransactions),
        .blobGasUsed = std::nullopt,
        .requestsHash = header.requestsHash,
    };
}

/// Stub-driven composition root. Same lifetime discipline as `OpFixture` below.
struct StubFixture
{
    StorageFixture storage;
    StubMemPool memPool;
    StubExecutor executor;
    bcos::protocol::TransactionReceiptFactory::Ptr receiptFactory{makeReceiptFactory()};
    StubOpScheduler scheduler;
    bcos::protocol::BlockFactory::Ptr blockFactory{makeBlockFactory()};
    StubEngineService service{memPool, storage.multiLayerStorage, executor, scheduler, blockFactory,
        bcos::engine::c_defaultBlockTxCountLimit, /*maxEngineVersion=*/4};
};

/// Builds a sealed, parent-registered, stub-backed VALID scenario: one raw transaction, one
/// receipt, commitments that match the payload exactly.
struct ValidScenario
{
    bcos::engine::NewPayloadRequest request;
    bcos::codec::rlp::EthBlockHeader header;
    bcos::bytes rawTransaction;
    bcos::protocol::TransactionReceipt::Ptr receipt;
};

ValidScenario prepareValidScenario(StubFixture& fixture, bcos::h256 const& parentHash)
{
    registerVerifiedBlock(fixture.storage, parentHash, 0);

    // Arbitrary envelope bytes: the stub never decodes them. They still matter — they are what
    // the transactionsRoot and the receipt's storage key are derived from.
    ValidScenario scenario;
    scenario.rawTransaction = bcos::bytes{0x7e, 0x01, 0x02, 0x03};
    scenario.request = makeOpRequest({scenario.rawTransaction}, kIsthmusTimestamp, parentHash);
    sealWithBlockHash(scenario.request);
    scenario.header = rebuiltHeader(scenario.request);
    scenario.receipt = makeReceipt(fixture.receiptFactory, 21000);

    fixture.scheduler.result.receipts = {scenario.receipt};
    fixture.scheduler.result.commitments = matchingCommitments(scenario.request, scenario.header);
    return scenario;
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

    // `Invalid`, never the deprecated-since-Shanghai `InvalidBlockHash` the generic path still
    // uses for this condition — the equality assertion above already pins that (design §6.1
    // step 2).
    EXPECT_EQ(status.status, bcos::engine::PayloadValidationStatus::Invalid);
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

// ===================== stub-driven branches (task-5b review I1) =====================
// The three groups below are unreachable with the real scheduler without op-geth golden values
// (a hand-written payload cannot match real execution's stateRoot/receiptsRoot), and Task 6's
// golden gate cannot reach two of them either (a golden vector is by construction consistent, so
// it exercises neither an individual field mismatch nor a storage fault). Hence the stub.

// (i) Step 6, design §6.1: the end-to-end VALID path and its four-table block registration.
TEST(EngineOpBranch, ValidPayloadRegistersAllFourTables)
{
    StubFixture fixture;
    const auto parentHash = hashOf("parent");
    auto scenario = prepareValidScenario(fixture, parentHash);
    auto const& payload = scenario.request.executionPayload;

    auto status = bcos::task::syncWait(fixture.service.newPayload(scenario.request, 4));

    ASSERT_EQ(status.status, bcos::engine::PayloadValidationStatus::Valid);
    ASSERT_TRUE(status.latestValidHash.has_value());
    EXPECT_EQ(*status.latestValidHash, payload.blockHash);
    EXPECT_FALSE(status.validationError.has_value());
    EXPECT_EQ(fixture.scheduler.executeOpBlockCalls, 1U);

    const auto blockNumberStr = boost::lexical_cast<std::string>(payload.blockNumber);

    // 1. s_hash_2_number — and specifically through the same accessor the OP branch's own
    //    parentKnown step uses, so this doubles as proof that a registered block is a valid
    //    parent for the next one (design §6.1 step 5's "parent 已验证" definition).
    auto registeredNumber = registeredBlockNumber(fixture.storage, payload.blockHash);
    ASSERT_TRUE(registeredNumber.has_value());
    EXPECT_EQ(*registeredNumber, payload.blockNumber);

    // 2. s_number_2_hash — key = number decimal string, value = the hash's raw 32 bytes.
    auto numberToHash = readEntry(fixture.storage, bcos::ledger::SYS_NUMBER_2_HASH, blockNumberStr);
    ASSERT_TRUE(numberToHash.has_value());
    EXPECT_EQ(numberToHash->get(), asStringView(payload.blockHash.asBytes()));

    // 3. s_eth_block_header (the new OP-only table) — key = number, value = the 21-field RLP.
    auto headerEntry =
        readEntry(fixture.storage, bcos::evm::engine::SYS_ETH_BLOCK_HEADER, blockNumberStr);
    ASSERT_TRUE(headerEntry.has_value());
    const auto encodedHeader = scenario.header.encode();
    EXPECT_EQ(headerEntry->get(), asStringView(encodedHeader));
    // The stored header is the one whose keccak is the accepted blockHash — i.e. the registry is
    // self-consistent with the verdict.
    EXPECT_EQ(scenario.header.hash(), payload.blockHash);

    // 4. s_hash_2_receipt — key = keccak(raw EIP-2718 envelope), value = encoded receipt.
    const auto txHash =
        fixture.blockFactory->cryptoSuite()->hashImpl()->hash(scenario.rawTransaction);
    auto receiptEntry = readEntry(fixture.storage, bcos::ledger::SYS_HASH_2_RECEIPT,
        bcos::concepts::bytebuffer::toView(txHash));
    ASSERT_TRUE(receiptEntry.has_value());
    bcos::bytes encodedReceipt;
    scenario.receipt->encode(encodedReceipt);
    EXPECT_EQ(receiptEntry->get(), asStringView(encodedReceipt));
}

// (j) Step 4/5, design §6.1 "六项比对面": each of the six fields, perturbed one at a time, must
// produce INVALID + latestValidHash = parent + a validationError naming that field. One
// perturbation per case, so a comparison that is silently missing from the chain shows up as
// exactly one failing case instead of being masked by its neighbours.
TEST(EngineOpBranch, EachComparisonSurfaceFieldMismatchIsNamed)
{
    struct MismatchCase
    {
        const char* fieldName;
        void (*perturb)(StubCommitments&);
    };

    const MismatchCase cases[] = {
        {"receiptsRoot", [](StubCommitments& c) { c.receiptsRoot = bcos::h256{}; }},
        {"logsBloom",
            [](StubCommitments& c) {
                bcos::h2048 bloom;
                bloom.data()[0] = 0x01;
                c.logsBloom = bloom;
            }},
        {"withdrawalsRoot", [](StubCommitments& c) { c.withdrawalsRoot = bcos::h256{}; }},
        {"stateRoot", [](StubCommitments& c) { c.stateRoot = bcos::h256{}; }},
        {"gasUsed", [](StubCommitments& c) { c.gasUsed = bcos::u256(1); }},
        {"transactionsRoot", [](StubCommitments& c) { c.txRoot = bcos::h256{}; }},
    };

    for (auto const& mismatchCase : cases)
    {
        SCOPED_TRACE(mismatchCase.fieldName);

        StubFixture fixture;
        const auto parentHash = hashOf("parent");
        auto scenario = prepareValidScenario(fixture, parentHash);
        mismatchCase.perturb(fixture.scheduler.result.commitments);

        auto status = bcos::task::syncWait(fixture.service.newPayload(scenario.request, 4));

        EXPECT_EQ(status.status, bcos::engine::PayloadValidationStatus::Invalid);
        ASSERT_TRUE(status.latestValidHash.has_value());
        EXPECT_EQ(*status.latestValidHash, parentHash);
        ASSERT_TRUE(status.validationError.has_value());
        EXPECT_NE(status.validationError->find(mismatchCase.fieldName), std::string::npos);
        // A rejected block must leave the registry untouched.
        EXPECT_FALSE(
            isBlockRegistered(fixture.storage, scenario.request.executionPayload.blockHash));
    }
}

// (k) Step 4, design §6.1: an `OpStorageError` is a fault in *this node's* storage, not a verdict
// on the payload — it must surface as JSON-RPC -32603 and must NEVER become INVALID (that would
// make the node vote against a block it merely failed to read). No golden vector can reach this
// branch, since a consistent vector never faults storage.
TEST(EngineOpBranch, StorageErrorFromExecutionIsInternalErrorNotInvalid)
{
    StubFixture fixture;
    const auto parentHash = hashOf("parent");
    auto scenario = prepareValidScenario(fixture, parentHash);
    fixture.scheduler.storageErrorToThrow = "ledger bridge poisoned";

    EXPECT_THROW(bcos::task::syncWait(fixture.service.newPayload(scenario.request, 4)),
        bcos::engine::OpExecutionInternalError);
    // Nothing registered: a storage fault leaves no trace of the block.
    EXPECT_FALSE(isBlockRegistered(fixture.storage, scenario.request.executionPayload.blockHash));
}

// (l) Step 3, task-5b review I2: a payload naming a verified parent but claiming a
// non-consecutive height is INVALID. Without this check it would have overwritten the
// number-keyed registration entries of an existing height.
TEST(EngineOpBranch, NonConsecutiveBlockNumberIsInvalid)
{
    StubFixture fixture;
    const auto parentHash = hashOf("parent");
    auto scenario = prepareValidScenario(fixture, parentHash);  // parent registered at number 0

    scenario.request.executionPayload.blockNumber = 7;  // parent is 0, so the only valid one is 1
    sealWithBlockHash(scenario.request);                // keep the blockHash self-consistent
    fixture.scheduler.result.commitments =
        matchingCommitments(scenario.request, rebuiltHeader(scenario.request));

    auto status = bcos::task::syncWait(fixture.service.newPayload(scenario.request, 4));

    EXPECT_EQ(status.status, bcos::engine::PayloadValidationStatus::Invalid);
    ASSERT_TRUE(status.latestValidHash.has_value());
    EXPECT_EQ(*status.latestValidHash, parentHash);
    ASSERT_TRUE(status.validationError.has_value());
    EXPECT_NE(status.validationError->find("blockNumber"), std::string::npos);
    // Rejected before execution — the height check precedes step 4.
    EXPECT_EQ(fixture.scheduler.executeOpBlockCalls, 0U);
    EXPECT_FALSE(isBlockRegistered(fixture.storage, scenario.request.executionPayload.blockHash));
}
