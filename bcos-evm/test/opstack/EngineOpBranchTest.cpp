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
// Final review batch 2 adds four more (all stub-driven, same reasoning):
//   (m) an execution escape matching NEITHER typed handler is still classified as -32603 (B2-1);
//   (n) re-delivering an already-accepted payload short-circuits to VALID without re-executing
//       (B2-2(a), op-geth eth/catalyst/api.go:872-876);
//   (o) a non-tip parent is refused explicitly instead of executed against the wrong base state
//       (B2-2(b));
//   (p)/(q) gasLimit > 2^63-1 and extraData > 32 bytes are rejected (B2-3/B2-4).
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
#include <boost/exception/diagnostic_information.hpp>
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

/// Runs `action` and returns the thrown exception's full diagnostic text (empty if it did not
/// throw). Needed because `OpExecutionInternalError` is now the single type covering several
/// distinct refusals (the unclassified-escape fallback, the non-tip-parent refusal, the
/// receipt-count invariant): asserting the type alone no longer identifies which path ran, so
/// these tests assert on the message marker as well (batch-1 review lesson).
template <class Action>
std::string thrownDiagnostic(Action&& action)
{
    try
    {
        action();
    }
    catch (const boost::exception& e)
    {
        // `errinfo_comment` is attached with `<<`, so it only shows up in the boost diagnostic,
        // not in `what()`.
        return boost::diagnostic_information(e);
    }
    catch (const std::exception& e)
    {
        return e.what();
    }
    return {};
}

// ---- exact-match validationError assertions (final review B3-6) ----
//
// Every assertion in this file used to be `validationError->find("<word>") != npos`. Substring
// matching is too loose here in two concrete, already-live ways:
//   - `find("blockHash")` is satisfied by ANY message containing the word — a future
//     "parentBlockHash ..." or "blockHash of the parent ..." message would keep a test written
//     for the header-reconstruction bucket green while the bucket itself moved;
//   - `find("blobGasUsed")` matches BOTH "blobGasUsed must be zero before Jovian (OP Isthmus)"
//     (the static-validation bucket, latestValidHash = null) AND "execution result does not
//     match payload field: blobGasUsed" (the comparison bucket, latestValidHash = parent). Those
//     are two different code paths with two different verdicts, and the substring told them
//     apart not at all.
// Exact equality pins which message — and therefore which bucket — actually produced the verdict.

/// The one prefix every step-5 comparison mismatch carries (`EngineServiceImpl.h:993-995`).
constexpr std::string_view c_comparisonMismatchPrefix =
    "execution result does not match payload field: ";

void expectValidationError(
    bcos::engine::PayloadStatus const& status, std::string_view expectedMessage)
{
    ASSERT_TRUE(status.validationError.has_value()) << "expected a validationError, got none";
    EXPECT_EQ(*status.validationError, expectedMessage);
}

/// Comparison-surface mismatch: the message must be the prefix above followed by EXACTLY the
/// named field — so naming a different field, or a message that merely mentions the field
/// somewhere, both fail.
void expectComparisonMismatch(bcos::engine::PayloadStatus const& status, std::string_view field)
{
    expectValidationError(status, std::string(c_comparisonMismatchPrefix) + std::string(field));
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
    // `Keccak256` overrides only `hash(bytesConstRef)`, which HIDES the base `crypto::Hash`'s
    // other `hash` overloads (including the `std::string const&` one) for calls made through the
    // derived type. Hence the explicit `bytesConstRef` -- its `std::string const&` constructor is
    // explicit, so it will not be applied implicitly.
    return keccak.hash(bcos::bytesConstRef(seed));
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
        /// Makes the STATIC `commitmentsOf` throw an unclassified exception, i.e. injects an
        /// escape into step 5 — outside `executeOpBlock`'s own try, which is the point. The flag
        /// lives in the result rather than in the scheduler because `commitmentsOf` is called
        /// statically and cannot read instance state.
        bool throwFromCommitmentsOf = false;
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
    /// Neither `ConsensusError` nor `StorageError` — stands in for the real scheduler's
    /// unclassified escapes (decode-loop `bad_alloc`, evmone-side `std::runtime_error` out of
    /// seal/stateRoot/txRoot, ...), all of which are raised OUTSIDE `executeOpBlock`'s own try.
    std::optional<std::string> unclassifiedErrorToThrow;
    // ---- observation ----
    unsigned executeOpBlockCalls = 0;

    static StubCommitments commitmentsOf(const ExecuteResult& executeResult)
    {
        if (executeResult.throwFromCommitmentsOf)
        {
            throw std::runtime_error("commitmentsOf blew up (comparison phase)");
        }
        return executeResult.commitments;
    }

    /// `s_throwFromComputeTxRoot` injects an escape into step 2 (the earliest unguarded window,
    /// before parentKnown). Static because `computeTxRoot` is; `ComputeTxRootThrowGuard` below
    /// keeps it from leaking between tests.
    static inline bool s_throwFromComputeTxRoot = false;

    static bcos::h256 computeTxRoot(::ranges::input_range auto const& rawTxBytes)
    {
        if (s_throwFromComputeTxRoot)
        {
            throw std::runtime_error("computeTxRoot blew up (static validation phase)");
        }
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
        if (unclassifiedErrorToThrow.has_value())
        {
            throw std::runtime_error(*unclassifiedErrorToThrow);
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

/// RAII switch for the static step-2 injection above.
struct ComputeTxRootThrowGuard
{
    ComputeTxRootThrowGuard() { StubOpScheduler::s_throwFromComputeTxRoot = true; }
    ~ComputeTxRootThrowGuard() { StubOpScheduler::s_throwFromComputeTxRoot = false; }
    ComputeTxRootThrowGuard(const ComputeTxRootThrowGuard&) = delete;
    ComputeTxRootThrowGuard& operator=(const ComputeTxRootThrowGuard&) = delete;
};

/// A receipt whose `encode()` throws — the injection point for step 6, inside `registerOpBlock`.
/// It stands in for the real hazards there (`bad_alloc` from `Entry::set`/`writeOne`, a tars
/// encoding error, ...), none of which are reachable on demand; `encode()` is the one call in
/// that function whose implementation the test owns. Deriving from the tars implementation keeps
/// the other 25 `TransactionReceipt` members real.
struct ThrowingEncodeReceipt : public bcostars::protocol::TransactionReceiptImpl
{
    void encode(bcos::bytes& /*encodedData*/) const override
    {
        throw std::runtime_error("receipt encode blew up (registration phase)");
    }
};

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

/// Builds the block that FOLLOWS an already-accepted scenario: parent = that block's hash, height
/// +1, caller-chosen timestamp. Unlike `prepareValidScenario`'s first block, this one's parent
/// header IS in `s_eth_block_header` (the accepted block registered it), which is what makes the
/// timestamp-monotonicity check live.
ValidScenario prepareFollowOnScenario(
    StubFixture& fixture, ValidScenario const& acceptedParent, uint64_t timestamp)
{
    ValidScenario scenario;
    scenario.rawTransaction = bcos::bytes{0x7e, 0x11, 0x22, 0x33};
    scenario.request = makeOpRequest(
        {scenario.rawTransaction}, timestamp, acceptedParent.request.executionPayload.blockHash);
    scenario.request.executionPayload.blockNumber =
        acceptedParent.request.executionPayload.blockNumber + 1;
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
    expectValidationError(status, "blockHash does not match the reconstructed block header");
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
    // Names the execution stage, not a static field — i.e. it did not短路 in step 2. The
    // message is only prefix-anchored (not exact-matched) because its tail is the decoder's
    // own `what()`, which this test does not own; the positive marker is the execution-stage
    // prefix and the negative marker is the static bucket's (B3-6/B3-13a pairing).
    ASSERT_TRUE(status.validationError.has_value());
    EXPECT_TRUE(status.validationError->starts_with("OP block execution rejected the payload: "))
        << *status.validationError;
    EXPECT_EQ(status.validationError->find(c_comparisonMismatchPrefix), std::string::npos)
        << *status.validationError;
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
    // Exact, because "blobGasUsed" alone also matches the step-5 comparison message — a
    // different bucket with a different latestValidHash (B3-6).
    expectValidationError(status, "blobGasUsed must be zero before Jovian (OP Isthmus)");
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

// (j) Step 4/5, design §6.1: every comparison in step 5, perturbed one at a time, must produce
// INVALID + latestValidHash = parent + a validationError naming that field. One perturbation per
// case, so a comparison that is silently missing from the chain shows up as exactly one failing
// case instead of being masked by its neighbours.
//
// Eight rows, not six (final review B3-1). The six named "六项比对面" (design §4.1) are the first
// six; the last two are design §5.1's additional comparisons, which the final review's mutation
// experiment proved were reached by NO test at all — short-circuiting both of them to constant
// false left the whole 50-case engine suite green. The §4.1/§5.1 split is a documentation
// distinction, not a coverage licence: all eight decide verdicts.
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
        // §5.1 #7 — the Jovian DA-footprint slot. The comparison is gated on the OPTIONAL being
        // engaged, not on the fork: `matchingCommitments` leaves it `nullopt` (pre-Jovian), so
        // engaging it with a value that differs from the payload's slot (0 under Isthmus, pinned
        // there by static validation) is exactly the "seal reports a DA footprint the header does
        // not claim" condition. `jovian_da_mix` in the golden gate covers only the agreeing
        // direction; this covers the refusing one.
        {"blobGasUsed", [](StubCommitments& c) { c.blobGasUsed = std::optional<uint64_t>{1}; }},
        // §5.1 #8 — drift between this library's copy of the OP empty-requests constant (used to
        // rebuild the header) and the OP side's own `OP_EMPTY_REQUESTS_HASH`. Displacing the
        // seal's copy is precisely the drift the comparison exists to catch.
        {"requestsHash",
            [](StubCommitments& c) {
                auto drifted = c.requestsHash.value_or(bcos::h256{});
                drifted.data()[0] ^= 0x01;
                c.requestsHash = drifted;
            }},
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
        expectComparisonMismatch(status, mismatchCase.fieldName);
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
    expectValidationError(status, "blockNumber must be exactly one greater than the parent's");
    // Rejected before execution — the height check precedes step 4.
    EXPECT_EQ(fixture.scheduler.executeOpBlockCalls, 0U);
    EXPECT_FALSE(isBlockRegistered(fixture.storage, scenario.request.executionPayload.blockHash));
}

// ═════════════════ final review batch 2: consensus safety (B2-1 … B2-4) ═════════════════

// (m) B2-1: an exception that is neither `ConsensusError` nor `StorageError` must still be
// classified. Most of `executeOpBlock` runs outside its own try (the raw-tx decode loop, the
// seal/stateRoot step, the txRoot/receipt step), and this repo additionally has a known
// typed-catch RTTI bypass (evmone is `-fno-rtti`), so "an escape that matches neither handler" is
// a live path, not a hypothetical. Uncaught it would leave the call with NO classification at
// all — neither INVALID nor -32603 — which is the one outcome design §4.3 rules out.
TEST(EngineOpBranch, UnclassifiedExecutionEscapeIsInternalErrorNotEscaping)
{
    StubFixture fixture;
    const auto parentHash = hashOf("parent");
    auto scenario = prepareValidScenario(fixture, parentHash);
    fixture.scheduler.unclassifiedErrorToThrow = "evmone MPT blew up";

    EXPECT_THROW(bcos::task::syncWait(fixture.service.newPayload(scenario.request, 4)),
        bcos::engine::OpExecutionInternalError);

    // Type alone is not discriminating (five different refusals share it) — and neither is the
    // bare phrase "unclassified exception", which `handleOpNewPayload`'s outer classification
    // barrier also uses. Assert the EXECUTION-phase marker specifically, and assert the barrier's
    // marker is absent: without both halves this test would stay green if the handler below were
    // deleted and the escape merely fell through to the barrier instead (a false green the
    // batch-1 review warned about, and one this file actually hit before being tightened).
    const auto diagnostic = thrownDiagnostic(
        [&] { bcos::task::syncWait(fixture.service.newPayload(scenario.request, 4)); });
    EXPECT_NE(
        diagnostic.find("OP block execution threw an unclassified exception"), std::string::npos)
        << diagnostic;
    EXPECT_EQ(diagnostic.find("outside block execution"), std::string::npos) << diagnostic;
    // Not INVALID, and nothing registered: an unknown local failure must not become a vote
    // against the block, nor leave a half-written block behind.
    EXPECT_FALSE(isBlockRegistered(fixture.storage, scenario.request.executionPayload.blockHash));
}

// (n) B2-2(a): re-delivering an already-accepted payload returns VALID without re-executing —
// op-geth `eth/catalyst/api.go:872-876`. Re-delivery is routine (CL timeout re-sends, op-node
// replaying unsafe blocks after a restart); before this short-circuit the second delivery
// re-executed on top of its own committed state and answered INVALID, i.e. this node would have
// called a block it had just accepted bad.
TEST(EngineOpBranch, AlreadyKnownBlockShortCircuitsToValidWithoutReExecuting)
{
    StubFixture fixture;
    const auto parentHash = hashOf("parent");
    auto scenario = prepareValidScenario(fixture, parentHash);
    auto const& payload = scenario.request.executionPayload;

    auto first = bcos::task::syncWait(fixture.service.newPayload(scenario.request, 4));
    ASSERT_EQ(first.status, bcos::engine::PayloadValidationStatus::Valid);
    ASSERT_EQ(fixture.scheduler.executeOpBlockCalls, 1U);

    auto second = bcos::task::syncWait(fixture.service.newPayload(scenario.request, 4));

    EXPECT_EQ(second.status, bcos::engine::PayloadValidationStatus::Valid);
    ASSERT_TRUE(second.latestValidHash.has_value());
    EXPECT_EQ(*second.latestValidHash, payload.blockHash);
    EXPECT_FALSE(second.validationError.has_value());
    // The load-bearing assertion: the verdict came from the registry, not from a second
    // execution. (Without the short-circuit the call still reaches `executeOpBlock`, so a count
    // of 2 here is exactly what a regression looks like.)
    EXPECT_EQ(fixture.scheduler.executeOpBlockCalls, 1U);
}

// (o) B2-2(b): a payload whose parent is NOT the current chain tip is refused explicitly. The
// forked view is always the tip's state, so executing such a block would run against the wrong
// base state and report a guaranteed mismatch as INVALID — a wrong verdict dressed as a consensus
// judgement. Answering -32603 says the truthful thing: this node cannot evaluate it. Serving
// arbitrary-parent base state needs a blockHash -> MLS layer mapping (spec §6.4).
TEST(EngineOpBranch, NonTipParentIsRefusedExplicitly)
{
    StubFixture fixture;
    const auto parentHash = hashOf("parent");
    auto accepted = prepareValidScenario(fixture, parentHash);

    ASSERT_EQ(bcos::task::syncWait(fixture.service.newPayload(accepted.request, 4)).status,
        bcos::engine::PayloadValidationStatus::Valid);

    // A sibling of the accepted block: same parent, same height, different content (hence a
    // different blockHash, so the known-block short-circuit does not apply).
    auto sibling =
        makeOpRequest({bcos::bytes{0x7e, 0x09, 0x09, 0x09}}, kIsthmusTimestamp, parentHash);
    sealWithBlockHash(sibling);
    ASSERT_NE(sibling.executionPayload.blockHash, accepted.request.executionPayload.blockHash);
    fixture.scheduler.result.commitments = matchingCommitments(sibling, rebuiltHeader(sibling));

    EXPECT_THROW(bcos::task::syncWait(fixture.service.newPayload(sibling, 4)),
        bcos::engine::OpExecutionInternalError);
    const auto diagnostic =
        thrownDiagnostic([&] { bcos::task::syncWait(fixture.service.newPayload(sibling, 4)); });
    EXPECT_NE(diagnostic.find("non-tip parent"), std::string::npos) << diagnostic;
    // Refused before execution, and nothing registered for the sibling.
    EXPECT_EQ(fixture.scheduler.executeOpBlockCalls, 1U);
    EXPECT_FALSE(isBlockRegistered(fixture.storage, sibling.executionPayload.blockHash));
}

// (p) B2-3: `gasLimit` above 2^63-1 is rejected. The execution side narrows it once more with a
// plain `static_cast<int64_t>` into `BlockInfo::gas_limit`, so an unchecked value becomes a
// NEGATIVE block gas pool — same unchecked-signed-narrowing class as batch 1's deposit gas_limit
// finding. op-geth pins the same bound (`params.MaxGasLimit`).
TEST(EngineOpBranch, GasLimitAboveInt64MaxIsInvalid)
{
    StubFixture fixture;
    const auto parentHash = hashOf("parent");
    auto scenario = prepareValidScenario(fixture, parentHash);

    scenario.request.executionPayload.gasLimit = bcos::u256(1) << 63;  // 2^63, one past the bound
    sealWithBlockHash(scenario.request);  // self-consistent hash: only the bound can reject it

    auto status = bcos::task::syncWait(fixture.service.newPayload(scenario.request, 4));

    EXPECT_EQ(status.status, bcos::engine::PayloadValidationStatus::Invalid);
    EXPECT_FALSE(status.latestValidHash.has_value());  // static-validation bucket
    // Exact: "gasLimit" also appears in the uint64-range message, a different bound.
    expectValidationError(status, "gasLimit exceeds the maximum block gas limit (2^63-1)");
    EXPECT_EQ(fixture.scheduler.executeOpBlockCalls, 0U);
}

// (q) B2-4: `extraData` longer than 32 bytes is rejected — op-geth
// `beacon/engine/types.go:294-296`. Distinct from the OP *shape* check parked in §6.4 (Isthmus 9
// bytes / Jovian 17 bytes): this is the chain-agnostic length bound that keeps an unbounded
// caller-supplied blob out of the header RLP.
TEST(EngineOpBranch, ExtraDataAboveThirtyTwoBytesIsInvalid)
{
    StubFixture fixture;
    const auto parentHash = hashOf("parent");
    auto scenario = prepareValidScenario(fixture, parentHash);

    scenario.request.executionPayload.extraData = bcos::bytes(33, 0xab);  // one byte too many
    sealWithBlockHash(scenario.request);

    auto status = bcos::task::syncWait(fixture.service.newPayload(scenario.request, 4));

    EXPECT_EQ(status.status, bcos::engine::PayloadValidationStatus::Invalid);
    EXPECT_FALSE(status.latestValidHash.has_value());
    expectValidationError(status, "extraData exceeds the 32-byte maximum");
    EXPECT_EQ(fixture.scheduler.executeOpBlockCalls, 0U);
    // 32 bytes exactly is still accepted (the bound is inclusive, as in op-geth).
    StubFixture atBound;
    auto boundary = prepareValidScenario(atBound, parentHash);
    boundary.request.executionPayload.extraData = bcos::bytes(32, 0xab);
    sealWithBlockHash(boundary.request);
    atBound.scheduler.result.commitments =
        matchingCommitments(boundary.request, rebuiltHeader(boundary.request));
    EXPECT_EQ(bcos::task::syncWait(atBound.service.newPayload(boundary.request, 4)).status,
        bcos::engine::PayloadValidationStatus::Valid);
}

// ══════ final review batch 2, second pass: the classification barrier (I-1) ══════
//
// The first pass' `catch (...)` guarded only the `executeOpBlock` call. Everything else in the OP
// branch still ran outside any handler, so an escape from static validation, from the comparison
// surface, or from block registration left `handleOpNewPayload` entirely and reached the caller as
// NEITHER INVALID NOR -32603 — the outcome design §4.3 rules out. The three tests below inject one
// unclassified escape into each of those windows.
//
// Each asserts the barrier's own marker ("outside block execution") rather than only the exception
// type: `OpExecutionInternalError` now covers five distinct refusals, so the type alone identifies
// nothing (batch-1 lesson). The execution-phase fallback keeps its own marker and is asserted
// separately by `UnclassifiedExecutionEscapeIsInternalErrorNotEscaping` above — that pairing is
// what proves the two windows stay distinguishable.

// (r) I-1, window 1: step 2 (`computeTxRoot` / `rebuildOpEthHeader` / `hash()`), before
// parentKnown.
TEST(EngineOpBranch, StaticValidationPhaseEscapeIsInternalError)
{
    StubFixture fixture;
    const auto parentHash = hashOf("parent");
    auto scenario = prepareValidScenario(fixture, parentHash);

    ComputeTxRootThrowGuard guard;

    EXPECT_THROW(bcos::task::syncWait(fixture.service.newPayload(scenario.request, 4)),
        bcos::engine::OpExecutionInternalError);
    const auto diagnostic = thrownDiagnostic(
        [&] { bcos::task::syncWait(fixture.service.newPayload(scenario.request, 4)); });
    EXPECT_NE(diagnostic.find("outside block execution"), std::string::npos) << diagnostic;
    EXPECT_EQ(fixture.scheduler.executeOpBlockCalls, 0U);
    EXPECT_FALSE(isBlockRegistered(fixture.storage, scenario.request.executionPayload.blockHash));
}

// (s) I-1, window 2: step 5 (`commitmentsOf` and the comparison surface), after execution.
TEST(EngineOpBranch, ComparisonPhaseEscapeIsInternalError)
{
    StubFixture fixture;
    const auto parentHash = hashOf("parent");
    auto scenario = prepareValidScenario(fixture, parentHash);
    fixture.scheduler.result.throwFromCommitmentsOf = true;

    EXPECT_THROW(bcos::task::syncWait(fixture.service.newPayload(scenario.request, 4)),
        bcos::engine::OpExecutionInternalError);
    const auto diagnostic = thrownDiagnostic(
        [&] { bcos::task::syncWait(fixture.service.newPayload(scenario.request, 4)); });
    EXPECT_NE(diagnostic.find("outside block execution"), std::string::npos) << diagnostic;
    // Execution DID run (this window is downstream of it) — and the block is still not registered.
    EXPECT_GT(fixture.scheduler.executeOpBlockCalls, 0U);
    EXPECT_FALSE(isBlockRegistered(fixture.storage, scenario.request.executionPayload.blockHash));
}

// (t) I-1, window 3: step 6 (`registerOpBlock`) — the case the review called out by name, where a
// `bad_alloc` out of `storage2::writeOne`/`Entry::set`, or a tars encoding error, previously
// escaped after the block had already been judged VALID.
TEST(EngineOpBranch, RegistrationPhaseEscapeIsInternalError)
{
    StubFixture fixture;
    const auto parentHash = hashOf("parent");
    auto scenario = prepareValidScenario(fixture, parentHash);
    // Everything compares equal, so the payload reaches step 6 as a VALID block; only the
    // registration itself fails.
    fixture.scheduler.result.receipts = {std::make_shared<ThrowingEncodeReceipt>()};

    EXPECT_THROW(bcos::task::syncWait(fixture.service.newPayload(scenario.request, 4)),
        bcos::engine::OpExecutionInternalError);
    const auto diagnostic = thrownDiagnostic(
        [&] { bcos::task::syncWait(fixture.service.newPayload(scenario.request, 4)); });
    EXPECT_NE(diagnostic.find("outside block execution"), std::string::npos) << diagnostic;
    // A block whose registration failed must not be visible as registered: the writes that did
    // land are discarded with the un-pushed view.
    EXPECT_FALSE(isBlockRegistered(fixture.storage, scenario.request.executionPayload.blockHash));
}

// ═══════════ final review batch 3: the receipt-count invariant (B3-3) ═══════════
//
// `registerOpBlock` refuses to register a block whose execution returned a receipt count
// differing from the transaction count, and refuses a null receipt — instead of the
// `std::min()`-truncate + `continue`-on-null shape that task-5b review M2 explicitly rejected
// (truncating would drop receipts from the registry while still reporting the block VALID).
//
// That design decision had NO test holding it: the final review restored the rejected shape and
// the entire 206-case suite stayed green. These two cases fix the decision in place.
//
// Marker discipline (B3-13a): both assert the registration invariant's OWN message AND assert the
// classification barrier's marker is absent. Without the negative half, deleting the invariant
// would still leave the test green whenever the resulting damage happened to trip the barrier
// instead — the exact false green batches 1 and 2 both hit.

TEST(EngineOpBranch, ReceiptCountMismatchIsRefusedNotSilentlyTruncated)
{
    StubFixture fixture;
    const auto parentHash = hashOf("parent");
    auto scenario = prepareValidScenario(fixture, parentHash);
    // One transaction in the payload, zero receipts back from execution. Under the rejected
    // `std::min()` design this registers the block as VALID with an empty receipt index.
    ASSERT_EQ(scenario.request.executionPayload.rawTransactions->size(), 1U);
    fixture.scheduler.result.receipts.clear();

    EXPECT_THROW(bcos::task::syncWait(fixture.service.newPayload(scenario.request, 4)),
        bcos::engine::OpExecutionInternalError);
    const auto diagnostic = thrownDiagnostic(
        [&] { bcos::task::syncWait(fixture.service.newPayload(scenario.request, 4)); });
    EXPECT_NE(
        diagnostic.find("receipt count differing from the transaction count"), std::string::npos)
        << diagnostic;
    EXPECT_EQ(diagnostic.find("unclassified exception"), std::string::npos) << diagnostic;

    // Not VALID and not registered: a block whose receipts cannot be indexed is not accepted.
    EXPECT_FALSE(isBlockRegistered(fixture.storage, scenario.request.executionPayload.blockHash));
    const auto txHash =
        fixture.blockFactory->cryptoSuite()->hashImpl()->hash(scenario.rawTransaction);
    EXPECT_FALSE(readEntry(fixture.storage, bcos::ledger::SYS_HASH_2_RECEIPT,
        bcos::concepts::bytebuffer::toView(txHash))
                     .has_value());
}

TEST(EngineOpBranch, NullReceiptIsRefusedNotSkipped)
{
    StubFixture fixture;
    const auto parentHash = hashOf("parent");
    auto scenario = prepareValidScenario(fixture, parentHash);
    // Count matches (1 == 1), so only the per-entry null check can reject this. Under the
    // rejected `continue`-on-null design the block registers VALID with a missing receipt.
    fixture.scheduler.result.receipts = {nullptr};

    EXPECT_THROW(bcos::task::syncWait(fixture.service.newPayload(scenario.request, 4)),
        bcos::engine::OpExecutionInternalError);
    const auto diagnostic = thrownDiagnostic(
        [&] { bcos::task::syncWait(fixture.service.newPayload(scenario.request, 4)); });
    EXPECT_NE(diagnostic.find("returned a null receipt"), std::string::npos) << diagnostic;
    EXPECT_EQ(diagnostic.find("unclassified exception"), std::string::npos) << diagnostic;
    EXPECT_FALSE(isBlockRegistered(fixture.storage, scenario.request.executionPayload.blockHash));
}

// ══════════ final review batch 4: B4-1 timestamp monotonicity / header read path ══════════
//
// These are the first tests of the `s_eth_block_header` READ path: until B4-1 that table was
// written and never read anywhere in the tree. The parent header is not seeded by hand — it gets
// there because the previous block was accepted and registered, so these also re-prove the
// registration/read round-trip end to end.

// (u) Equal timestamps are rejected: op-geth's condition is `<=`, not `<`
// (eth/catalyst/api.go:891-894), because equal timestamps make block ordering ambiguous and,
// here, leave the fork selector stationary across a height change.
TEST(EngineOpBranch, EqualParentTimestampIsInvalid)
{
    StubFixture fixture;
    const auto parentHash = hashOf("parent");
    auto accepted = prepareValidScenario(fixture, parentHash);
    ASSERT_EQ(bcos::task::syncWait(fixture.service.newPayload(accepted.request, 4)).status,
        bcos::engine::PayloadValidationStatus::Valid);
    const auto executeCallsAfterParent = fixture.scheduler.executeOpBlockCalls;

    auto child = prepareFollowOnScenario(
        fixture, accepted, accepted.request.executionPayload.timestamp);  // equal, not earlier

    auto status = bcos::task::syncWait(fixture.service.newPayload(child.request, 4));

    EXPECT_EQ(status.status, bcos::engine::PayloadValidationStatus::Invalid);
    ASSERT_TRUE(status.latestValidHash.has_value());
    EXPECT_EQ(*status.latestValidHash, accepted.request.executionPayload.blockHash);
    ASSERT_TRUE(status.validationError.has_value());
    EXPECT_NE(status.validationError->find("timestamp"), std::string::npos);
    // Rejected before execution, and not registered.
    EXPECT_EQ(fixture.scheduler.executeOpBlockCalls, executeCallsAfterParent);
    EXPECT_FALSE(isBlockRegistered(fixture.storage, child.request.executionPayload.blockHash));
}

// (v) A timestamp EARLIER than the parent's is rejected — the case with teeth, since it is the one
// that could roll the active fork backwards (Jovian -> Isthmus) and with it the DA accounting and
// baseFee semantics. The child here is still Isthmus-active, so it clears the -38005 version gate
// and is refused by the monotonicity rule specifically.
TEST(EngineOpBranch, EarlierThanParentTimestampIsInvalid)
{
    StubFixture fixture;
    const auto parentHash = hashOf("parent");
    auto accepted = prepareValidScenario(fixture, parentHash);
    ASSERT_EQ(bcos::task::syncWait(fixture.service.newPayload(accepted.request, 4)).status,
        bcos::engine::PayloadValidationStatus::Valid);
    ASSERT_GT(accepted.request.executionPayload.timestamp, kIsthmusTime + 1);

    auto child = prepareFollowOnScenario(fixture, accepted, kIsthmusTime + 1);

    auto status = bcos::task::syncWait(fixture.service.newPayload(child.request, 4));

    EXPECT_EQ(status.status, bcos::engine::PayloadValidationStatus::Invalid);
    ASSERT_TRUE(status.validationError.has_value());
    EXPECT_NE(status.validationError->find("timestamp"), std::string::npos);
}

// (w) The positive control: a strictly increasing timestamp is accepted, and the child registers
// its own header in turn. Without this the two rejections above would also pass if the check had
// been implemented as "always reject a child whose parent header exists".
TEST(EngineOpBranch, IncreasingTimestampIsAcceptedAndRegisters)
{
    StubFixture fixture;
    const auto parentHash = hashOf("parent");
    auto accepted = prepareValidScenario(fixture, parentHash);
    ASSERT_EQ(bcos::task::syncWait(fixture.service.newPayload(accepted.request, 4)).status,
        bcos::engine::PayloadValidationStatus::Valid);

    auto child =
        prepareFollowOnScenario(fixture, accepted, accepted.request.executionPayload.timestamp + 1);

    auto status = bcos::task::syncWait(fixture.service.newPayload(child.request, 4));

    ASSERT_EQ(status.status, bcos::engine::PayloadValidationStatus::Valid);
    EXPECT_EQ(*status.latestValidHash, child.request.executionPayload.blockHash);
    // The child's own header is now readable, i.e. the read path this test exercises is fed by
    // the write path under test.
    auto childHeaderEntry = readEntry(fixture.storage, bcos::evm::engine::SYS_ETH_BLOCK_HEADER,
        boost::lexical_cast<std::string>(child.request.executionPayload.blockNumber));
    ASSERT_TRUE(childHeaderEntry.has_value());
    EXPECT_EQ(childHeaderEntry->get(), asStringView(child.header.encode()));
}

// (x) The documented skip: a block whose parent was seeded as a trusted starting point (only
// `SYS_HASH_2_NUMBER`, no stored header) is accepted regardless of how its timestamp compares,
// because there is nothing to compare against. This is the genesis/first-block contract — and it
// is what keeps the 33 golden vectors and the chained pair working, so it is asserted rather than
// left implicit.
TEST(EngineOpBranch, MissingParentHeaderSkipsTimestampCheck)
{
    StubFixture fixture;
    const auto parentHash = hashOf("parent");
    // Deliberately the earliest timestamp the fork gate still admits: if the check were applied
    // with any default parent timestamp of 0 or above, this is where it would misfire.
    auto scenario = prepareValidScenario(fixture, parentHash);
    scenario.request.executionPayload.timestamp = kIsthmusTime;
    sealWithBlockHash(scenario.request);
    scenario.header = rebuiltHeader(scenario.request);
    fixture.scheduler.result.commitments = matchingCommitments(scenario.request, scenario.header);

    ASSERT_FALSE(
        readEntry(fixture.storage, bcos::evm::engine::SYS_ETH_BLOCK_HEADER, "0").has_value());
    EXPECT_EQ(bcos::task::syncWait(fixture.service.newPayload(scenario.request, 4)).status,
        bcos::engine::PayloadValidationStatus::Valid);
}

// ══════════════ final review batch 4: B4-3 opMode silent-failure guardrail ══════════════

/// A scheduler that is EXACTLY `StubOpScheduler` except that `executeOpBlock` has one extra
/// deduced parameter — the smallest realistic signature drift, and the kind a future refactor
/// could introduce without any diagnostic. `c_opMode`'s probe
/// (`&SchedulerType::template executeOpBlock<std::vector<bcos::bytes>>`) stops resolving, so the
/// class silently reverts to the generic engine path.
struct SignatureDriftedOpScheduler
{
    struct ExecuteResult
    {
    };

    bcos::task::Task<std::vector<bcos::protocol::TransactionReceipt::Ptr>> executeBlock(ViewType&,
        auto&, bcos::protocol::BlockHeader const&, ::ranges::input_range auto const&,
        bcos::ledger::LedgerConfig const&)
    {
        throw std::logic_error("SignatureDriftedOpScheduler::executeBlock must not be called");
        co_return {};
    }

    /// The drift: a second deduced parameter. Everything else about this class is unchanged.
    bcos::task::Task<ExecuteResult> executeOpBlock(
        ViewType&, auto const&, ::ranges::input_range auto const&)
    {
        co_return ExecuteResult{};
    }
};

using DriftedEngineService =
    bcos::engine::EngineServiceImpl<StubMemPool, MLS, StubExecutor, SignatureDriftedOpScheduler>;
static_assert(!DriftedEngineService::c_opMode,
    "the drifted signature must be what makes c_opMode collapse — if this fires, the test no "
    "longer reproduces the scenario it claims to");

// (y) B4-3: with `c_opMode` silently false, a V4 request must be REFUSED, not rubber-stamped.
// Before this guardrail the same request was answered VALID with the payload's own blockHash and
// the block was never executed — the failure mode a validator must not have. The composition root
// here deliberately passes `maxEngineVersion = 4`, exactly as the OP root does: that argument is
// what would otherwise unlock the generic path.
TEST(EngineOpBranch, DriftedOpModeProbeRefusesV4InsteadOfRubberStamping)
{
    StorageFixture storage;
    StubMemPool memPool;
    StubExecutor executor;
    SignatureDriftedOpScheduler scheduler;
    auto blockFactory = makeBlockFactory();
    DriftedEngineService service(memPool, storage.multiLayerStorage, executor, scheduler,
        blockFactory, bcos::engine::c_defaultBlockTxCountLimit, /*maxEngineVersion=*/4);

    // A payload shaped exactly like the one op-node sends: present-and-empty withdrawals, blob gas
    // fields present — i.e. one that the GENERIC static validation accepts.
    auto request = makeOpRequest({bcos::bytes{0x7e, 0x01}}, kIsthmusTimestamp, hashOf("parent"));
    sealWithBlockHash(request);

    EXPECT_THROW(bcos::task::syncWait(service.newPayload(request, 4)),
        bcos::engine::UnsupportedEngineApiVersion);

    // And the refusal must not be an accident of `maxEngineVersion`: V3 (which the generic gate
    // does admit) still reaches the generic body, proving the V4 refusal above is the guardrail
    // and not the version gate. The V3 payload is Isthmus-timestamped, which the generic path has
    // no opinion about; it lands on the generic parentBeaconBlockRoot rule instead.
    auto v3Status = bcos::task::syncWait(service.newPayload(request, 3));
    EXPECT_NE(v3Status.status, bcos::engine::PayloadValidationStatus::Valid);
}
