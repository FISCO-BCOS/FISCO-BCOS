// bcos-evm/test/opstack/OpNewPayloadRpcE2eTest.cpp
// W6 L2 end-to-end real-chain comparison: real JSON params ->
// EngineHelper::parseNewPayloadRequest(V4) -> EngineService<OpSchedulerImpl>.newPayload(4)
// -> executeOpBlock -> seven assertions vs golden. The fixture composition mirrors the
// val-loop EngineNewPayloadGateTest GateFixture (member order storage->memPool->executor->
// receiptFactory->scheduler->blockFactory->service).
#include "support/GoldenSample.h"
#include "support/SeedPreState.h"
#include <bcos-concepts/ByteBuffer.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/storage/Entry.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/storage2/MultiLayerStorage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <opstack-executor/OpEngineSeam.h>
#include <opstack-executor/OpSchedulerImpl.h>
// EngineHelper.h's parseNewPayloadRequest declaration references
// bcos::protocol::TransactionFactory&, but EngineHelper.h does not declare that type
// itself (production relies on bcos-rpc unity-build include order). A single-TU
// direct compile must include TransactionFactory.h first or the declaration fails.
#include <bcos-framework/protocol/TransactionFactory.h>
#include <bcos-rpc/web3jsonrpc/utils/EngineHelper.h>
#include <bcos-tars-protocol/protocol/BlockFactoryImpl.h>
#include <bcos-tars-protocol/protocol/BlockHeaderFactoryImpl.h>
#include <bcos-tars-protocol/protocol/TransactionFactoryImpl.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <bcos-utilities/IOServicePool.h>
#include <engine/bcos-engine/EngineServiceImpl.h>
#include <json/json.h>
#include <boost/lexical_cast.hpp>
#include <boost/test/unit_test.hpp>
#include <algorithm>
#include <fstream>
#include <set>
#include <string>
#include <vector>

using bcos::executor_v1::StateKey;
using bcos::executor_v1::StateValue;
namespace memory_storage = bcos::storage2::memory_storage;

namespace
{

// ── storage fixture (same as the Task 2 tests) ──
template <class Key, class Value, bcos::storage2::ReadWriteStorage<Key, Value> Storage>
struct TrivialCheckpointStorage
{
    using CheckpointName = bcos::h256;
    Storage& m_storage;
    explicit TrivialCheckpointStorage(Storage& storage) noexcept : m_storage(storage) {}
    Storage& open() & { return m_storage; }
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
using ViewType = typename MLS::ViewType;

// ── Composition-root stand-ins (val-loop GateFixture style: OP mode bypasses memPool/executor) ──
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
        bcos::task::Task<bcos::protocol::TransactionReceipt::Ptr> finish() { co_return {}; }
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

bcos::protocol::TransactionReceiptFactory::Ptr makeReceiptFactory()
{
    return std::make_shared<bcostars::protocol::TransactionReceiptFactoryImpl>(makeCryptoSuite());
}

constexpr uint64_t kChainId = 0x2105;

bcos::evm::opstack::OpForkTimestamps forkTimestampsFor(bool jovian)
{
    return bcos::evm::opstack::OpForkTimestamps{
        .isthmusTime = 0,
        .jovianTime = jovian ? 0 : std::numeric_limits<uint64_t>::max(),
    };
}

using OpScheduler = bcos::evm::engine::OpSchedulerImpl<ViewType, MLS>;
using OpEngineService =
    bcos::engine::EngineServiceImpl<StubMemPool, MLS, StubExecutor, OpScheduler>;

/// Envelope -> tars converter for the two-phase commit path. A file-scope function (not an inline
/// member lambda) so the fixture's member-init list can reference it by name — members initialize
/// in declaration order, and a converter declared after the scheduler would read an unconstructed
/// object. `bcos::engine::detail::opEnvelopeToTars`'s forward declaration is visible via
/// EngineServiceImpl.h (included above). Exercised by the three-phase commit path.
std::optional<bcostars::Transaction> realConverter(
    bcos::bytes const& env, bcos::crypto::HashType const& h)
{
    return bcos::engine::detail::opEnvelopeToTars(env, h);
}

struct OpE2eFixture
{
    // Single-bucket CONCURRENT backend: after the C2 fix, seed/parent lands in the
    // backend via mergeView, and stateRoot's range(SYS_TABLES) scan relies on the
    // backend's RANGE_SEEK semantics. With multiple buckets (default
    // hardware_concurrency*2+1), MemoryStorage's range only seeks bucket 0, so buckets
    // 1+ return non-SYS_TABLES keys, visitAccounts breaks early, and stateRoot is empty.
    // A single bucket keeps the range correct.
    BackendMemStorage backendStorage{1};
    CheckpointBackend checkpointBackend{backendStorage};
    MLS multiLayerStorage{checkpointBackend};
    StubMemPool memPool;
    StubExecutor executor;
    bcos::protocol::TransactionReceiptFactory::Ptr receiptFactory{makeReceiptFactory()};
    // blockFactory must be declared before scheduler: members initialize in declaration order and
    // the scheduler constructor takes blockFactory as an argument.
    bcos::protocol::BlockFactory::Ptr blockFactory{makeBlockFactory()};
    OpScheduler scheduler;
    OpEngineService service;

    explicit OpE2eFixture(bcos::evm::opstack::OpForkTimestamps forkTimestamps)
      : scheduler(receiptFactory, kChainId, forkTimestamps, blockFactory, multiLayerStorage,
            realConverter),
        service(memPool, multiLayerStorage, executor, scheduler, blockFactory,
            /*ledger=*/nullptr, bcos::engine::c_defaultBlockTxCountLimit, /*maxEngineVersion=*/4)
    {}
};

/// Produced header: production-mapping reconstruction (val-loop GateFixture's
/// productionHeaderOf pattern). `bcos::engine::detail::rebuildOpEthHeader`
/// (EngineServiceImpl.cpp:470 or so: 17 fields verbatim from payload + txRoot + 3
/// constants). OP block hash uses `opHeaderHash(c)` = keccak256(encodeOpHeader()), NOT
/// BlockHeader::hash() (empty dataHash / factory TARS-order backfill).
bcos::protocol::BlockHeader::Ptr productionHeaderOf(
    bcos::protocol::BlockFactory::Ptr const& blockFactory,
    bcos::engine::NewPayloadRequest const& request)
{
    auto const& payload = request.executionPayload;
    const auto transactionsRoot = OpScheduler::computeTxRoot(*payload.rawTransactions);
    return bcos::engine::detail::rebuildOpEthHeader(blockFactory->blockHeaderFactory(), payload,
        transactionsRoot, *request.parentBeaconBlockRoot);
}

/// Parent pre-registration (R3/R5 fatal-gap A fix): the OP path's step-3 parentKnown
/// checks SYS_HASH_2_NUMBER. All 33 isolated vectors are block 1, so the parent must be
/// pre-registered as a trusted genesis, otherwise newPayload returns SYNCING. The write
/// encoding must match production: key = raw 32-byte hash, value = decimal number string
/// (gate test registerVerifiedBlock, EngineNewPayloadGateTest.cpp:188-198).
void registerVerifiedBlock(MLS& multiLayerStorage, bcos::h256 const& blockHash, int64_t number)
{
    auto view = multiLayerStorage.fork();
    view.newMutable();
    bcos::storage::Entry entry;
    entry.set(boost::lexical_cast<std::string>(number));
    bcos::task::syncWait(bcos::storage2::writeOne(view,
        StateKey{bcos::ledger::SYS_HASH_2_NUMBER, bcos::concepts::bytebuffer::toView(blockHash)},
        std::move(entry)));
    // C2 review fix (P1 CRITICAL): mergeBackStorage merges the oldest layer (FIFO).
    // Drain the stack — parent pre-registration lands in the backend immediately, and
    // with an empty stack before each block push, mergeView persists right away so the
    // backend assertions can pass.
    bcos::task::syncWait(multiLayerStorage.mergeView(std::move(view)));
}

// ── seven assertions ──
void assertSevenFields(std::string const& id, bcos::protocol::BlockHeader::Ptr const& produced,
    bcostars::protocol::BlockHeaderImpl::Ptr const& goldenHeader, bcos::h256 const& goldenBlockHash)
{
    const auto c = bcos::engine::detail::opHeaderConst();
    // 1. blockHash: produced opHeaderHash = keccak256(encodeOpHeader()) must equal
    //    golden.blockHash (op-geth's block.Hash() = keccak(RLP(21 fields)) definition).
    // Warning: this repo's Boost.Test macros do not support `<< id << msg` chaining;
    // uniformly use BOOST_CHECK_MESSAGE(id << msg).
    BOOST_CHECK_MESSAGE(produced->opHeaderHash(c) == goldenBlockHash, id << ": blockHash");
    BOOST_CHECK_MESSAGE(produced->stateRoot() == goldenHeader->stateRoot(), id << ": stateRoot");
    BOOST_CHECK_MESSAGE(
        produced->receiptsRoot() == goldenHeader->receiptsRoot(), id << ": receiptsRoot");
    BOOST_CHECK_MESSAGE(
        produced->withdrawalsRoot() == goldenHeader->withdrawalsRoot(), id << ": withdrawalsRoot");
    BOOST_CHECK_MESSAGE(produced->gasUsed() == goldenHeader->gasUsed(), id << ": gasUsed");
    BOOST_CHECK_MESSAGE(produced->txsRoot() == goldenHeader->txsRoot(), id << ": txRoot");
    // Warning: bytesConstRef(RefDataContainer)'s operator== is a "pointer+length"
    // shallow compare, not a content compare (RefDataContainer.h:84-87). produced/golden
    // hold the same 256-byte bloom in different storage, so the pointers always differ —
    // must use std::equal for byte-wise content compare (encodeOpHeader byte-level
    // assertion already covers content; this keeps the seven-field slot separately).
    BOOST_CHECK_MESSAGE(std::equal(produced->logsBloom().begin(), produced->logsBloom().end(),
                            goldenHeader->logsBloom().begin(), goldenHeader->logsBloom().end()),
        id << ": logsBloom");
    // Main assertion: byte-exact equality of encodeOpHeader (covers the full RLP encoding of all fields)
    BOOST_CHECK_MESSAGE(
        produced->encodeOpHeader(c) == goldenHeader->encodeOpHeader(c), id << ": encodeOpHeader");
}

/// One vector end-to-end: seed pre -> register parent -> makeParamsJson ->
/// parseNewPayloadRequest(V4) -> newPayload(4) -> assertions.
void runGoldenVector(std::string const& id)
{
    auto sample = w6test::loadVectorSample(id);
    auto fixture = std::make_unique<OpE2eFixture>(forkTimestampsFor(sample.jovian));
    w6test::seedPreState(fixture->multiLayerStorage, sample.vector["pre"]);
    // Warning: parent pre-registration (gap A): without it -> SYNCING instead of VALID. parentHash is decoded from the golden header
    const auto goldenHeader = w6test::decodeGoldenHeader(sample);
    registerVerifiedBlock(fixture->multiLayerStorage, goldenHeader->parentInfo().blockHash, 0);

    auto params = w6test::makeParamsJson(sample);
    auto request = bcos::rpc::parseNewPayloadRequest(
        params, *fixture->blockFactory->transactionFactory(), bcos::engine::ApiVersion::V4);

    auto status = bcos::task::syncWait(fixture->service.newPayload(request, 4));
    // Warning: PayloadValidationStatus is an enum class without operator<<; must compare
    // via static_cast<int> (same as existing engine tests, e.g. EngineServiceTest.cpp:312).
    BOOST_REQUIRE_MESSAGE(static_cast<int>(status.status) ==
                              static_cast<int>(bcos::engine::PayloadValidationStatus::Valid),
        id << ": expected VALID, got " << static_cast<int>(status.status)
           << (status.validationError ? " : " + *status.validationError : ""));

    // produced header (production-mapping reconstruction) + golden header (reuse the
    // goldenHeader decoded above for parent registration; redeclaring it in the same
    // block would be a compile error, caught by R2-A)
    auto produced = productionHeaderOf(fixture->blockFactory, request);
    const auto goldenBlockHash = bcos::h256(std::string(sample.golden["blockHash"].asString()));
    assertSevenFields(id, produced, goldenHeader, goldenBlockHash);

    // Plan-B write side: OP txs land in SYS_HASH_2_TX (tars encoding); s_eth_hash_2_rawtx
    // is no longer written. newPayload's two-phase commit (commitBlock -> opstackRegisterBlock)
    // converts each raw EIP-2718 envelope to a tars Transaction and writes SYS_HASH_2_TX[txHash]
    // (extraTransactionHash=txHash pins D4); the raw envelope is no longer stored in
    // s_eth_hash_2_rawtx (D1). 0x04 (EIP-7702) has been a first-class TransactionType
    // (EIP7702=4) since upstream #5411, so opEnvelopeToTars converts it and it lands in
    // SYS_HASH_2_TX like any other typed tx (no longer absent; D7 is obsolete).
    BOOST_REQUIRE(request.executionPayload.rawTransactions.has_value());  // P3-1: missing field fails cleanly
    auto const& rawTxs = *request.executionPayload.rawTransactions;
    auto& hashImpl = *fixture->blockFactory->cryptoSuite()->hashImpl();
    auto view = fixture->multiLayerStorage.fork();
    for (std::size_t i = 0; i < rawTxs.size(); ++i)
    {
        auto txHash = hashImpl.hash(rawTxs[i]);
        // SYS_HASH_2_TX present + round-trip (tars decode back to Transaction, hash==txHash pins D4)
        auto txEntry = bcos::task::syncWait(
            bcos::storage2::readOne(view, bcos::executor_v1::StateKey{bcos::ledger::SYS_HASH_2_TX,
                                              bcos::concepts::bytebuffer::toView(txHash)}));
        BOOST_REQUIRE_MESSAGE(txEntry.has_value(), id << ": tx #" << i << " SYS_HASH_2_TX present");
        auto txBytes = bcos::bytesConstRef(
            reinterpret_cast<bcos::byte const*>(txEntry->get().data()), txEntry->get().size());
        auto tx = fixture->blockFactory->transactionFactory()->createTransaction(
            txBytes, /*checkSig=*/false, /*checkHash=*/false, /*tainted=*/false);
        BOOST_CHECK_MESSAGE(
            tx->hash() == txHash, id << ": tx #" << i << " round-trip hash==txHash");
        // C2: data must land in the backend (m_latestBackend) — restart-recovery semantics.
        auto backendEntry =
            bcos::task::syncWait(bcos::storage2::readOne(fixture->multiLayerStorage.latestBackend(),
                bcos::executor_v1::StateKey{
                    bcos::ledger::SYS_HASH_2_TX, bcos::concepts::bytebuffer::toView(txHash)}));
        BOOST_CHECK_MESSAGE(
            backendEntry.has_value(), id << ": tx #" << i << " SYS_HASH_2_TX in backend");
        // rawtx table absent (D1: not retained)
        auto rawEntry = bcos::task::syncWait(
            bcos::storage2::readOne(view, bcos::executor_v1::StateKey{OpScheduler::c_ethRawTxTable,
                                              bcos::concepts::bytebuffer::toView(txHash)}));
        BOOST_CHECK_MESSAGE(
            !rawEntry.has_value(), id << ": tx #" << i << " s_eth_hash_2_rawtx absent");
    }
}

/// Chained two-block (chainA/B, R2-C verified flow): seed only A's pre -> register A's
/// parent(0) -> submit B first (SYNCING) -> submit A (VALID) -> submit B again (VALID).
/// FCU deliberately omitted (see implementation hint #4).
void runChainedPair(std::string const& aId, std::string const& bId)
{
    auto sampleA = w6test::loadChainedSample(aId);
    auto sampleB = w6test::loadChainedSample(bId);
    BOOST_REQUIRE(sampleA.jovian == sampleB.jovian);  // chained pair shares one fork (isthmus or jovian)
    auto fixture = std::make_unique<OpE2eFixture>(forkTimestampsFor(sampleA.jovian));

    // Seed only A's pre (B's pre is A's postState; never re-seed)
    w6test::seedPreState(fixture->multiLayerStorage, sampleA.vector["pre"]);
    const auto goldenHeaderA = w6test::decodeGoldenHeader(sampleA);
    // Register A's parent (trusted genesis height 0)
    registerVerifiedBlock(fixture->multiLayerStorage, goldenHeaderA->parentInfo().blockHash, 0);

    auto requestA = bcos::rpc::parseNewPayloadRequest(w6test::makeParamsJson(sampleA),
        *fixture->blockFactory->transactionFactory(), bcos::engine::ApiVersion::V4);
    auto requestB = bcos::rpc::parseNewPayloadRequest(w6test::makeParamsJson(sampleB),
        *fixture->blockFactory->transactionFactory(), bcos::engine::ApiVersion::V4);

    // Submit B first: parent(A) not yet registered -> SYNCING
    auto earlyB = bcos::task::syncWait(fixture->service.newPayload(requestB, 4));
    BOOST_CHECK_MESSAGE(static_cast<int>(earlyB.status) ==
                            static_cast<int>(bcos::engine::PayloadValidationStatus::Syncing),
        bId << ": first B should be SYNCING (parent A unknown)");

    // Submit A: VALID (commitBlock -> opstackRegisterBlock writes SYS_HASH_2_NUMBER[hashA]=1)
    auto statusA = bcos::task::syncWait(fixture->service.newPayload(requestA, 4));
    BOOST_REQUIRE_MESSAGE(static_cast<int>(statusA.status) ==
                              static_cast<int>(bcos::engine::PayloadValidationStatus::Valid),
        aId << ": A expected VALID, got " << static_cast<int>(statusA.status));

    // Submit B again: parentKnown hits A -> VALID
    auto statusB = bcos::task::syncWait(fixture->service.newPayload(requestB, 4));
    BOOST_REQUIRE_MESSAGE(static_cast<int>(statusB.status) ==
                              static_cast<int>(bcos::engine::PayloadValidationStatus::Valid),
        bId << ": B expected VALID after A, got " << static_cast<int>(statusB.status));

    // Seven assertions for each (productionHeaderOf rebuilt from request, independent of execution)
    auto producedA = productionHeaderOf(fixture->blockFactory, requestA);
    const auto goldenBlockHashA = bcos::h256(std::string(sampleA.golden["blockHash"].asString()));
    assertSevenFields(aId, producedA, goldenHeaderA, goldenBlockHashA);
    auto producedB = productionHeaderOf(fixture->blockFactory, requestB);
    auto goldenHeaderB = w6test::decodeGoldenHeader(sampleB);
    const auto goldenBlockHashB = bcos::h256(std::string(sampleB.golden["blockHash"].asString()));
    assertSevenFields(bId, producedB, goldenHeaderB, goldenBlockHashB);
}

/// Seeds and submits one vector block, returning {fixture, blockHash, blockNumber} for
/// FCU case reuse. Mirrors runGoldenVector's flow but keeps the fixture and block
/// identity — updateForkchoice seeds head/safe/finalized with that block. Warning:
/// parseNewPayloadRequest's real signature is the 3-arg
/// `bcos::rpc::parseNewPayloadRequest(params, txFactory, version)` (EngineHelper.h:68-70);
/// the task brief's 1-arg `bcos::engine::parseNewPayloadRequest(params)` is a stale draft.
std::tuple<std::unique_ptr<OpE2eFixture>, bcos::h256, bcos::protocol::BlockNumber>
runVectorAndGetBlockHash(std::string const& id)
{
    auto sample = w6test::loadVectorSample(id);
    auto fixture = std::make_unique<OpE2eFixture>(forkTimestampsFor(sample.jovian));
    w6test::seedPreState(fixture->multiLayerStorage, sample.vector["pre"]);
    const auto goldenHeader = w6test::decodeGoldenHeader(sample);
    registerVerifiedBlock(fixture->multiLayerStorage, goldenHeader->parentInfo().blockHash, 0);
    auto params = w6test::makeParamsJson(sample);
    auto req = bcos::rpc::parseNewPayloadRequest(
        params, *fixture->blockFactory->transactionFactory(), bcos::engine::ApiVersion::V4);
    auto status = bcos::task::syncWait(fixture->service.newPayload(req, /*version=*/4));
    BOOST_REQUIRE_MESSAGE(static_cast<int>(status.status) ==
                              static_cast<int>(bcos::engine::PayloadValidationStatus::Valid),
        id << ": seed newPayload expected VALID, got " << static_cast<int>(status.status));
    return {std::move(fixture), bcos::h256(std::string(sample.golden["blockHash"].asString())),
        goldenHeader->number()};
}

// ═══════════════════ Task 2: invalid-vector runner (classification-driven) ════
// Consumer-first (atomicity): Task 2 self-tests use inline vectors (no dependence on
// Task 3 corpus files); the runner also accepts on-disk invalid_*.json (landed after
// Task 3 generation). Reject schema (task-2-brief): fisco.consumer / classification
// ("INVALID"|"SYNCING"|"-38005"|"-32603") / latest_valid_hash("parent"|null) /
// validation_error_contains(optional) / expect_throw("UnsupportedFork"|"OpExecutionInternalError") /
// version(optional, -38005 sets 3).

/// Parses the parent hash from `_op_payload.parentHash` (needed for INVALID's latestValidHash=parent assertion).
bcos::h256 parseParentHashFromPayload(w6test::InvalidSample const& sample)
{
    return bcos::h256(std::string(sample.vector["_op_payload"]["parentHash"].asString()));
}

/// Construction spec for an inline invalid vector: derives a "self-consistent corrupt"
/// payload from an existing golden sample.
struct InlineInvalidSpec
{
    std::string baseId;          // golden-sample base (valid deposit/transfer shape)
    std::string corruptField;    // "stateRoot"|"parentHash"|"feeRecipient"|"" (no corruption)
    std::string classification;  // "INVALID"|"SYNCING"|"-38005"|"-32603"
    std::string validationContains = "";  // optional validation_error_contains
    std::uint32_t version = 4;            // -38005 uses 3 (version!=4 triggers UnsupportedFork)
    bool carryCanonical = false;          // -32603 carries an uncorrupted canonical sibling (two-pour)
    std::string consumer = "engine";      // Task 4 gate regression: executor skips message assertion
};

/// Derives an inline invalid vector from a golden sample. Self-consistent corruption
/// model: mutate a header field -> recompute blockHash — rebuild the OP header with
/// productionHeaderOf (does not read payload.blockHash) and take opHeaderHash as the
/// new blockHash, so the step-2 blockHash check passes and field-level hits (step-5
/// compare / parentKnown / step-3c) stay reachable.
w6test::InvalidSample buildInlineInvalidSample(std::string const& id, InlineInvalidSpec const& spec)
{
    auto base = w6test::loadVectorSample(spec.baseId);
    auto blockFactory = makeBlockFactory();
    auto params = w6test::makeParamsJson(base);
    auto& ep = params[0u];

    if (spec.corruptField == "stateRoot")
    {
        auto b = bcos::fromHex(ep["stateRoot"].asString());
        b[0] ^= 0xff;
        ep["stateRoot"] = "0x" + bcos::toHex(b);
    }
    else if (spec.corruptField == "parentHash")
    {
        ep["parentHash"] = "0x0000000000000000000000000000000000000000000000000000000000000001";
    }
    else if (spec.corruptField == "feeRecipient")
    {
        auto b = bcos::fromHex(ep["feeRecipient"].asString());
        b[0] ^= 0xff;
        ep["feeRecipient"] = "0x" + bcos::toHex(b);
    }
    else if (!spec.corruptField.empty())
    {
        throw std::runtime_error(
            "buildInlineInvalidSample: unknown corruptField " + spec.corruptField);
    }

    // Self-consistent: recompute blockHash (the rebuilt header excludes payload.blockHash -> opHeaderHash)
    auto request = bcos::rpc::parseNewPayloadRequest(
        params, *blockFactory->transactionFactory(), bcos::engine::ApiVersion::V4);
    auto header = productionHeaderOf(blockFactory, request);
    ep["blockHash"] =
        w6test::hexPrefixedH256(header->opHeaderHash(bcos::engine::detail::opHeaderConst()));

    w6test::InvalidSample sample;
    sample.hardfork = base.jovian ? "jovian" : "isthmus";
    sample.jovian = base.jovian;
    sample.vector["_info"]["hardfork"] = sample.hardfork;
    sample.vector["pre"] = base.vector["pre"];
    sample.vector["_op_payload"] = ep;
    // V4 static validation requires parentBeaconBlockRoot (EngineServiceImpl.cpp:340); params[2] always holds the real value
    sample.vector["_op_payload"]["parentBeaconBlockRoot"] = params[2u];
    if (spec.carryCanonical)
    {
        // -32603 canonical sibling = the uncorrupted base payload (same parent/height, different blockHash)
        auto canonicalParams = w6test::makeParamsJson(base);
        sample.vector["_op_canonical"] = canonicalParams[0u];
        sample.vector["_op_canonical"]["parentBeaconBlockRoot"] = canonicalParams[2u];
    }

    auto& fisco = sample.vector["_op_expected"]["reject"]["fisco"];
    fisco["consumer"] = spec.consumer;
    fisco["classification"] = spec.classification;
    if (spec.classification == "INVALID")
    {
        fisco["latest_valid_hash"] = "parent";
    }
    else if (spec.classification == "-38005")
    {
        fisco["latest_valid_hash"] = Json::Value(Json::nullValue);
        fisco["expect_throw"] = "UnsupportedFork";
        fisco["version"] = spec.version;
    }
    else if (spec.classification == "-32603")
    {
        fisco["latest_valid_hash"] = Json::Value(Json::nullValue);
        fisco["expect_throw"] = "OpExecutionInternalError";
    }
    if (!spec.validationContains.empty())
    {
        fisco["validation_error_contains"] = spec.validationContains;
    }
    return sample;
}

/// Inline self-test vector registry (Task 2 self-test; on-disk corpus uses w6test::loadInvalidSample).
w6test::InvalidSample makeInlineInvalidSample(std::string const& id)
{
    if (id == "inline_invalid_stateRoot")
    {
        return buildInlineInvalidSample(id, InlineInvalidSpec{
                                                .baseId = "jovian_deposit_only",
                                                .corruptField = "stateRoot",
                                                .classification = "INVALID",
                                                .validationContains = "stateRoot",
                                            });
    }
    if (id == "inline_invalid_parentUnknown")
    {
        return buildInlineInvalidSample(id, InlineInvalidSpec{
                                                .baseId = "jovian_deposit_only",
                                                .corruptField = "parentHash",
                                                .classification = "SYNCING",
                                            });
    }
    if (id == "inline_invalid_unsupportedFork")
    {
        return buildInlineInvalidSample(id, InlineInvalidSpec{
                                                .baseId = "jovian_deposit_only",
                                                .corruptField = "",
                                                .classification = "-38005",
                                                .version = 3u,
                                            });
    }
    if (id == "inline_invalid_siblingFork")
    {
        return buildInlineInvalidSample(id, InlineInvalidSpec{
                                                .baseId = "jovian_deposit_only",
                                                .corruptField = "feeRecipient",
                                                .classification = "-32603",
                                                .carryCanonical = true,
                                            });
    }
    // Task 4 gate regression: an executor-consumer vector's validation_error_contains is
    // a T8n execution-layer throw message ("intrinsic gas too low"), which the engine's
    // RTTI-bypass folds into a generic message — if the runner does not skip the message
    // assertion per consumer, this vector would go red. INVALID classification +
    // latest_valid_hash=parent must still be asserted.
    if (id == "inline_invalid_executorStateRoot")
    {
        return buildInlineInvalidSample(id, InlineInvalidSpec{
                                                .baseId = "jovian_deposit_only",
                                                .corruptField = "stateRoot",
                                                .classification = "INVALID",
                                                .validationContains = "intrinsic gas too low",
                                                .consumer = "executor",
                                            });
    }
    throw std::runtime_error("makeInlineInvalidSample: unknown inline id " + id);
}

/// Classification-driven runner (Task 2 main deliverable). Key branches:
///  - SYNCING: skips registerVerifiedBlock (unknown parent is the intent — registering
///    would let parentKnown pass and it would no longer be SYNCING)
///  - -38005/-32603: expect_throw picks UnsupportedFork / OpExecutionInternalError by
///    classification; version read from fisco.version (-38005 sets 3), call
///    newPayload(request, version)
///  - -32603: two-pour — submit the canonical child first (VALID, writes SYS_NUMBER_2_HASH
///    occupancy), then the same-height sibling (canonical read from vector `_op_canonical`;
///    Task 5 chain_fork_* carriers use the same schema)
///  - INVALID: assert status + latestValidHash(=parent or null) + validationError substring
void runInvalidVector(std::string const& id)
{
    auto sample =
        (id.rfind("inline_", 0) == 0) ? makeInlineInvalidSample(id) : w6test::loadInvalidSample(id);
    auto fixture = std::make_unique<OpE2eFixture>(forkTimestampsFor(sample.jovian));
    const auto& fisco = sample.vector["_op_expected"]["reject"]["fisco"];
    const auto classification = fisco["classification"].asString();
    // Task 4 consumer gate: for executor-consumer vectors (non-decode class), the
    // validation_error_contains is a T8n execution-layer throw message, which the engine's
    // RTTI-bypass folds into a generic message, so a substring assertion would always
    // mismatch. For consumer=="both" (blob) the decode message is a reliable engine-side
    // string and must still be asserted. Default treated as engine.
    const auto consumer = fisco.get("consumer", "engine").asString();

    if (classification == "SYNCING")
    {
        // Warning: do not register the parent — a corrupted/broken parentHash vector intends parent unknown
        w6test::seedPreState(fixture->multiLayerStorage, sample.vector["pre"]);
        auto params = w6test::makeInvalidParamsJson(sample);
        auto request = bcos::rpc::parseNewPayloadRequest(
            params, *fixture->blockFactory->transactionFactory(), bcos::engine::ApiVersion::V4);
        auto status = bcos::task::syncWait(fixture->service.newPayload(request, 4));
        BOOST_CHECK_MESSAGE(static_cast<int>(status.status) ==
                                static_cast<int>(bcos::engine::PayloadValidationStatus::Syncing),
            id << ": expected SYNCING, got " << static_cast<int>(status.status));
        return;
    }

    // Non-SYNCING: seed pre + register parent first (after self-consistent corruption, parentHash is a known valid ancestor)
    w6test::seedPreState(fixture->multiLayerStorage, sample.vector["pre"]);
    const auto parentHash = parseParentHashFromPayload(sample);
    registerVerifiedBlock(fixture->multiLayerStorage, parentHash, 0);

    if (classification == "-38005" || classification == "-32603")
    {
        auto params = w6test::makeInvalidParamsJson(sample);
        const auto version = fisco.isMember("version") ? fisco["version"].asUInt() : 4u;
        auto request =
            bcos::rpc::parseNewPayloadRequest(params, *fixture->blockFactory->transactionFactory(),
                static_cast<bcos::engine::ApiVersion>(version));
        if (classification == "-38005")
        {
            BOOST_CHECK_THROW(bcos::task::syncWait(fixture->service.newPayload(request, version)),
                bcos::engine::UnsupportedFork);
        }
        else  // -32603: two-pour — submit canonical child first (VALID, writes SYS_NUMBER_2_HASH occupancy), then sibling
        {
            // The canonical sibling must travel with the vector (Task 5 chain_fork_*
            // carriers share the schema). Missing = malformed corpus: a single sibling
            // submit is VALID and does not throw, so a silent skip would give a
            // misleading failure — fail loudly with a named schema violation.
            BOOST_REQUIRE_MESSAGE(sample.vector.isMember("_op_canonical"),
                id << ": -32603 vector must carry _op_canonical (two-pour canonical sibling)");
            w6test::InvalidSample canonicalSample;
            canonicalSample.vector["_info"]["hardfork"] = sample.hardfork;
            canonicalSample.vector["_op_payload"] = sample.vector["_op_canonical"];
            canonicalSample.jovian = sample.jovian;
            auto canonicalParams = w6test::makeInvalidParamsJson(canonicalSample);
            auto canonicalReq = bcos::rpc::parseNewPayloadRequest(canonicalParams,
                *fixture->blockFactory->transactionFactory(), bcos::engine::ApiVersion::V4);
            auto canonicalStatus =
                bcos::task::syncWait(fixture->service.newPayload(canonicalReq, 4));
            BOOST_REQUIRE_MESSAGE(
                static_cast<int>(canonicalStatus.status) ==
                    static_cast<int>(bcos::engine::PayloadValidationStatus::Valid),
                id << ": canonical expected VALID, got " << static_cast<int>(canonicalStatus.status)
                   << (canonicalStatus.validationError ? " : " + *canonicalStatus.validationError :
                                                         ""));
            BOOST_CHECK_THROW(bcos::task::syncWait(fixture->service.newPayload(request, 4)),
                bcos::engine::OpExecutionInternalError);
        }
        return;
    }

    // INVALID (default path)
    auto params = w6test::makeInvalidParamsJson(sample);
    auto request = bcos::rpc::parseNewPayloadRequest(
        params, *fixture->blockFactory->transactionFactory(), bcos::engine::ApiVersion::V4);
    auto status = bcos::task::syncWait(fixture->service.newPayload(request, 4));
    BOOST_CHECK_MESSAGE(static_cast<int>(status.status) ==
                            static_cast<int>(bcos::engine::PayloadValidationStatus::Invalid),
        id << ": expected INVALID, got " << static_cast<int>(status.status));
    // An INVALID vector must declare latest_valid_hash ("parent"|null); missing =
    // malformed corpus, fail loudly rather than reading the missing field as null and
    // asserting the wrong thing (stateRoot corruption returning parent would false-fail misleadingly).
    if (!fisco.isMember("latest_valid_hash"))
    {
        BOOST_ERROR(id << ": malformed vector — fisco.latest_valid_hash missing for INVALID");
    }
    else if (fisco["latest_valid_hash"].isNull())
    {
        BOOST_CHECK_MESSAGE(
            !status.latestValidHash.has_value(), id << ": latestValidHash should be null");
    }
    else
    {
        BOOST_CHECK_MESSAGE(
            status.latestValidHash.has_value() && *status.latestValidHash == parentHash,
            id << ": latestValidHash should be parent");
    }
    if (consumer != "executor" && fisco.isMember("validation_error_contains"))
    {
        const auto expected = fisco["validation_error_contains"].asString();
        BOOST_CHECK_MESSAGE(
            status.validationError && status.validationError->find(expected) != std::string::npos,
            id << ": validationError missing '" << expected
               << "', got: " << (status.validationError ? *status.validationError : "<none>"));
    }
}

/// One manifest line -> invalid-vector stem. Warning: must strip the `.json` suffix —
/// manifest.txt entries are `xxx.json`, and `loadInvalidSample` (GoldenSample.h) re-appends
/// `OP_T8N_VECTORS_DIR + "/" + id + ".json"`, so not stripping would hard-crash on
/// `vectors/invalid_xxx.json.json`. Returns an empty string when the line is not an
/// invalid-vector entry (comment/blank/non-`invalid_` prefix).
std::string invalidStemFromManifestLine(std::string line)
{
    const auto b = line.find_first_not_of(" \t\r");
    if (b == std::string::npos)
        return {};
    const auto e = line.find_last_not_of(" \t\r");
    line = line.substr(b, e - b + 1);
    if (line.empty() || line[0] == '#')
        return {};
    if (line.rfind("invalid_", 0) != 0)
        return {};
    if (line.size() > 5 && line.rfind(".json") == line.size() - 5)
        line = line.substr(0, line.size() - 5);
    return line;
}

/// The `invalid_*` subset of manifest.txt (same logic as Task 1 loadManifest; entries are
/// stored as stems without `.json`). Empty before Task 3 corpus lands -> zero iterations
/// of InvalidVectorsFromManifest (forward-compat hook).
std::set<std::string> loadInvalidManifest()
{
    std::set<std::string> names;
    std::ifstream input(std::string(OP_T8N_VECTORS_DIR) + "/manifest.txt");
    if (!input.is_open())
    {
        BOOST_ERROR("manifest.txt missing");
        return names;
    }
    std::string line;
    while (std::getline(input, line))
    {
        auto stem = invalidStemFromManifestLine(line);
        if (!stem.empty())
            names.insert(stem);
    }
    return names;
}

}  // namespace

BOOST_AUTO_TEST_SUITE(OpNewPayloadRpcE2eSuite)

BOOST_AUTO_TEST_CASE(JovianDepositOnly)
{
    runGoldenVector("jovian_deposit_only");
}

BOOST_AUTO_TEST_CASE(JovianTransferMulti)
{
    runGoldenVector("jovian_transfer_multi");
}

BOOST_AUTO_TEST_CASE(JovianDaMix)
{
    runGoldenVector("jovian_da_mix");
}

BOOST_AUTO_TEST_CASE(JovianFirstBlock)
{
    runGoldenVector("jovian_first_block");
}

BOOST_AUTO_TEST_CASE(IsthmusDepositOnly)
{
    runGoldenVector("isthmus_deposit_only");
}

BOOST_AUTO_TEST_CASE(IsthmusTransferMulti)
{
    runGoldenVector("isthmus_transfer_multi");
}

BOOST_AUTO_TEST_CASE(IsthmusSetcode7702)
{
    runGoldenVector("isthmus_setcode_7702");
}

BOOST_AUTO_TEST_CASE(IsthmusTxReverted)
{
    runGoldenVector("isthmus_tx_reverted");
}

BOOST_AUTO_TEST_CASE(IsthmusBigBlock130tx)
{
    runGoldenVector("isthmus_big_block_130tx");
}

// B-7: single-fork isthmus system-call-order observable vector (id set by W5 review A#1).
// Wrong order -> L1 reader REVERT -> stateRoot mismatch -> VALID assertion + seven-field
// assertions turn red.
BOOST_AUTO_TEST_CASE(SystemCallOrderObservable)
{
    runGoldenVector("isthmus_system_call_order_observable");
}

// ── all 33 vectors (16 isthmus + 17 jovian), one per line ──
BOOST_AUTO_TEST_CASE(IsthmusAccessList)
{
    runGoldenVector("isthmus_access_list");
}
BOOST_AUTO_TEST_CASE(IsthmusContractCreate)
{
    runGoldenVector("isthmus_contract_create");
}
BOOST_AUTO_TEST_CASE(IsthmusContractLogs)
{
    runGoldenVector("isthmus_contract_logs");
}
BOOST_AUTO_TEST_CASE(IsthmusDepositFailed)
{
    runGoldenVector("isthmus_deposit_failed");
}
BOOST_AUTO_TEST_CASE(IsthmusDepositMint)
{
    runGoldenVector("isthmus_deposit_mint");
}
BOOST_AUTO_TEST_CASE(IsthmusEmptyAccountCleanup)
{
    runGoldenVector("isthmus_empty_account_cleanup");
}
BOOST_AUTO_TEST_CASE(IsthmusFeeEnvObserver)
{
    runGoldenVector("isthmus_fee_env_observer");
}
BOOST_AUTO_TEST_CASE(IsthmusMessagePasserWrite)
{
    runGoldenVector("isthmus_message_passer_write");
}
BOOST_AUTO_TEST_CASE(IsthmusSetcode7702Skips)
{
    runGoldenVector("isthmus_setcode_7702_skips");
}
BOOST_AUTO_TEST_CASE(IsthmusSystemContractsReal)
{
    runGoldenVector("isthmus_system_contracts_real");
}
BOOST_AUTO_TEST_CASE(IsthmusTransferBasic)
{
    runGoldenVector("isthmus_transfer_basic");
}
BOOST_AUTO_TEST_CASE(JovianAccessList)
{
    runGoldenVector("jovian_access_list");
}
BOOST_AUTO_TEST_CASE(JovianContractCreate)
{
    runGoldenVector("jovian_contract_create");
}
BOOST_AUTO_TEST_CASE(JovianContractLogs)
{
    runGoldenVector("jovian_contract_logs");
}
BOOST_AUTO_TEST_CASE(JovianDepositFailed)
{
    runGoldenVector("jovian_deposit_failed");
}
BOOST_AUTO_TEST_CASE(JovianDepositMint)
{
    runGoldenVector("jovian_deposit_mint");
}
BOOST_AUTO_TEST_CASE(JovianEmptyAccountCleanup)
{
    runGoldenVector("jovian_empty_account_cleanup");
}
BOOST_AUTO_TEST_CASE(JovianFeeEnvObserver)
{
    runGoldenVector("jovian_fee_env_observer");
}
BOOST_AUTO_TEST_CASE(JovianMessagePasserWrite)
{
    runGoldenVector("jovian_message_passer_write");
}
BOOST_AUTO_TEST_CASE(JovianSetcode7702)
{
    runGoldenVector("jovian_setcode_7702");
}
BOOST_AUTO_TEST_CASE(JovianSetcode7702Skips)
{
    runGoldenVector("jovian_setcode_7702_skips");
}
BOOST_AUTO_TEST_CASE(JovianSystemContractsReal)
{
    runGoldenVector("jovian_system_contracts_real");
}
BOOST_AUTO_TEST_CASE(JovianTransferBasic)
{
    runGoldenVector("jovian_transfer_basic");
}
BOOST_AUTO_TEST_CASE(JovianTxReverted)
{
    runGoldenVector("jovian_tx_reverted");
}

// ── Engine-gate probe (Task 2 gate 1): 5 representative precompile vectors ─────────
// (five risk faces: over-cap / 7702 / Jovian / value / successful output)
BOOST_AUTO_TEST_CASE(IsthmusPrecompileBn256PairNorm)
{
    runGoldenVector("isthmus_precompile_bn256pair_norm");
}
BOOST_AUTO_TEST_CASE(IsthmusPrecompileBlsPairingOvercap)
{
    runGoldenVector("isthmus_precompile_bls_pairing_overcap");
}
BOOST_AUTO_TEST_CASE(IsthmusPrecompileWrapEip7702)
{
    runGoldenVector("isthmus_precompile_wrap_eip7702");
}
BOOST_AUTO_TEST_CASE(JovianPrecompileWrapValueOvercap)
{
    runGoldenVector("jovian_precompile_wrap_value_overcap");
}
BOOST_AUTO_TEST_CASE(IsthmusPrecompileEcrecover)
{
    runGoldenVector("isthmus_precompile_ecrecover");
}

// Note: the first 9 sample cases cover 9 vectors; the 24 above-completed cases cover the
// remaining 24, totaling 33. Full list (16 isthmus + 17 jovian):
//   isthmus: access_list, big_block_130tx, contract_create, contract_logs, deposit_failed,
//            deposit_mint, deposit_only, empty_account_cleanup, fee_env_observer,
//            message_passer_write, setcode_7702, setcode_7702_skips, system_contracts_real,
//            transfer_basic, transfer_multi, tx_reverted
//   jovian:  access_list, contract_create, contract_logs, da_mix, deposit_failed, deposit_mint,
//            deposit_only, empty_account_cleanup, fee_env_observer, first_block,
//            message_passer_write, setcode_7702, setcode_7702_skips, system_contracts_real,
//            transfer_basic, transfer_multi, tx_reverted
// Sample cases (9): JovianDepositOnly/JovianTransferMulti/JovianDaMix/JovianFirstBlock/
//   IsthmusDepositOnly/IsthmusTransferMulti/IsthmusSetcode7702/IsthmusTxReverted/IsthmusBigBlock130tx
// Completion cases (24): all the rest. Warning: BOOST_AUTO_TEST_CASE names must not repeat —
// the 24 completion-case names deliberately avoid the 9 sample-case names (verified by
// R2-D: zero collisions with the existing 107 case names or the planned 33).
// Chained pair: one case (runChainedPair) runs chainA+chainB in one flow (B SYNCING -> A VALID -> B VALID).
BOOST_AUTO_TEST_CASE(ChainedAB)
{
    runChainedPair("chainA", "chainB");
}

// B-5c: jovian chained pair. runChainedPair's block2 VALID assertion = step 3a-2 baseFee
// consistency check passing, which exercises the max branch (baseFee derived from the
// parent block + capped by rounding up).
BOOST_AUTO_TEST_CASE(JovianChainedAB)
{
    runChainedPair("jovianChainA", "jovianChainB");
}
// ── Task 4 completion: 24 precompile matrix vectors (brief appendix A id->case mapping) ──
// Task 2's 5 probes (bn256pair_norm/bls_pairing_overcap/wrap_eip7702/wrap_value_overcap/
// ecrecover) already covered the five risk faces; this section completes the remaining 24,
// totaling 29 precompile cases.
BOOST_AUTO_TEST_CASE(IsthmusPrecompileBlake2f)
{
    runGoldenVector("isthmus_precompile_blake2f");
}
BOOST_AUTO_TEST_CASE(IsthmusPrecompileBn256Add)
{
    runGoldenVector("isthmus_precompile_bn256add");
}
BOOST_AUTO_TEST_CASE(IsthmusPrecompileBn256Mul)
{
    runGoldenVector("isthmus_precompile_bn256mul");
}
BOOST_AUTO_TEST_CASE(IsthmusPrecompileBlsG1Add)
{
    runGoldenVector("isthmus_precompile_bls_g1add");
}
BOOST_AUTO_TEST_CASE(IsthmusPrecompileBlsG1Msm)
{
    runGoldenVector("isthmus_precompile_bls_g1msm");
}
BOOST_AUTO_TEST_CASE(IsthmusPrecompileBlsG1MsmOvercap)
{
    runGoldenVector("isthmus_precompile_bls_g1msm_overcap");
}
BOOST_AUTO_TEST_CASE(IsthmusPrecompileBlsG2Add)
{
    runGoldenVector("isthmus_precompile_bls_g2add");
}
BOOST_AUTO_TEST_CASE(IsthmusPrecompileBlsG2Msm)
{
    runGoldenVector("isthmus_precompile_bls_g2msm");
}
BOOST_AUTO_TEST_CASE(IsthmusPrecompileBlsG2MsmOvercap)
{
    runGoldenVector("isthmus_precompile_bls_g2msm_overcap");
}
BOOST_AUTO_TEST_CASE(IsthmusPrecompileBlsMapG1)
{
    runGoldenVector("isthmus_precompile_bls_map_g1");
}
BOOST_AUTO_TEST_CASE(IsthmusPrecompileBlsMapG2)
{
    runGoldenVector("isthmus_precompile_bls_map_g2");
}
BOOST_AUTO_TEST_CASE(IsthmusPrecompileBlsPairing)
{
    runGoldenVector("isthmus_precompile_bls_pairing");
}
BOOST_AUTO_TEST_CASE(IsthmusPrecompileExpmod)
{
    runGoldenVector("isthmus_precompile_expmod");
}
BOOST_AUTO_TEST_CASE(IsthmusPrecompileIdentity)
{
    runGoldenVector("isthmus_precompile_identity");
}
BOOST_AUTO_TEST_CASE(IsthmusPrecompilePointEvaluation)
{
    runGoldenVector("isthmus_precompile_point_evaluation");
}
BOOST_AUTO_TEST_CASE(IsthmusPrecompileRipemd160)
{
    runGoldenVector("isthmus_precompile_ripemd160");
}
BOOST_AUTO_TEST_CASE(IsthmusPrecompileSha256)
{
    runGoldenVector("isthmus_precompile_sha256");
}
BOOST_AUTO_TEST_CASE(IsthmusPrecompileWrap63of64)
{
    runGoldenVector("isthmus_precompile_wrap_63of64");
}
BOOST_AUTO_TEST_CASE(IsthmusPrecompileWrapReturndata)
{
    runGoldenVector("isthmus_precompile_wrap_returndata");
}
BOOST_AUTO_TEST_CASE(JovianPrecompileBlsG1MsmOvercap)
{
    runGoldenVector("jovian_precompile_bls_g1msm_overcap");
}
BOOST_AUTO_TEST_CASE(JovianPrecompileBlsG2MsmOvercap)
{
    runGoldenVector("jovian_precompile_bls_g2msm_overcap");
}
BOOST_AUTO_TEST_CASE(JovianPrecompileBlsPairingOvercap)
{
    runGoldenVector("jovian_precompile_bls_pairing_overcap");
}
BOOST_AUTO_TEST_CASE(JovianPrecompileBn256PairOvercap)
{
    runGoldenVector("jovian_precompile_bn256pair_overcap");
}
BOOST_AUTO_TEST_CASE(JovianPrecompileWrapValueRevert)
{
    runGoldenVector("jovian_precompile_wrap_value_revert");
}

// Final tally: 65 cases = 63 single vectors + 2 chained pairs (chainA/B + jovianChainA/B,
// covering 4 chained samples). 63 single = 34 base vectors (33 +
// isthmus_system_call_order_observable) + 29 precompile (Task 2 probes 5 + Task 4 completion 24).

// ── Task 2: E2E invalid-vector runner (classification-driven) ──
// Inline self-test vectors (Task 2 does not depend on Task 3 corpus files): the
// classification-driven runner covers all four branches —
//   INVALID (stateRoot self-consistent corruption + latestValidHash=parent + "stateRoot")
//   SYNCING (parentHash broken chain -> do not register parent)
//   -38005 (Isthmus+ timestamp + version!=4 -> UnsupportedFork)
//   -32603 (same-parent twins: canonical VALID writes SYS_NUMBER_2_HASH occupancy, then sibling -> two-pour)
BOOST_AUTO_TEST_CASE(InvalidStateRootRejected)
{
    runInvalidVector("inline_invalid_stateRoot");
}

BOOST_AUTO_TEST_CASE(InvalidParentUnknownSyncing)
{
    runInvalidVector("inline_invalid_parentUnknown");
}

BOOST_AUTO_TEST_CASE(InvalidUnsupportedForkVersion3)
{
    runInvalidVector("inline_invalid_unsupportedFork");
}

BOOST_AUTO_TEST_CASE(InvalidSiblingForkRejected)
{
    runInvalidVector("inline_invalid_siblingFork");
}

// Task 4 gate regression: executor-consumer vectors skip the validation_error_contains
// assertion (engine RTTI-bypass folds T8n messages), but INVALID classification +
// latest_valid_hash=parent must still be asserted.
BOOST_AUTO_TEST_CASE(ExecutorConsumerSkipsMessageAssertion)
{
    runInvalidVector("inline_invalid_executorStateRoot");
}

// Step 5: manifest subset iteration (invalid_*.json). Empty before Task 3 corpus lands -> zero iterations (forward compat).
BOOST_AUTO_TEST_CASE(InvalidVectorsFromManifest)
{
    for (auto const& id : loadInvalidManifest())
    {
        runInvalidVector(id);
    }
}

// Regression (review Important): manifest entries are `invalid_xxx.json`, and
// loadInvalidSample re-appends `.json`. invalidStemFromManifestLine must strip the suffix —
// otherwise runInvalidVector gets `vectors/invalid_xxx.json.json` -> loadJsonFile hard crash.
// Proves the manifest->load path no longer double-appends.
BOOST_AUTO_TEST_CASE(InvalidManifestStemStripsJsonSuffix)
{
    // Real manifest line shape -> stem without suffix (loadInvalidSample re-appends to the original filename)
    BOOST_CHECK_EQUAL(
        invalidStemFromManifestLine("invalid_inline_stateRoot.json"), "invalid_inline_stateRoot");
    BOOST_CHECK_EQUAL(
        invalidStemFromManifestLine("  invalid_foo_static_3.json  "), "invalid_foo_static_3");
    // non-invalid_ prefix / comment / blank -> not in the subset
    BOOST_CHECK_EQUAL(invalidStemFromManifestLine("jovian_deposit_only.json"), "");
    BOOST_CHECK_EQUAL(invalidStemFromManifestLine("# comment"), "");
    BOOST_CHECK_EQUAL(invalidStemFromManifestLine("   "), "");
    BOOST_CHECK_EQUAL(invalidStemFromManifestLine(""), "");
}

// ── Task 6 Step 2: coverage-matrix assertion (manifest-registered subset driven) ─────
// Iterates manifest invalid_* vectors + Task 2 inline vectors, asserting every required
// set member is covered (missing -> FAILURE): each classification (incl. -38005 — satisfied
// by the Task 2 version vector; -32603 satisfied by the two-pour runner), each
// latest_valid_hash value ("parent"|null), each static item (except 3/12 — inexpressible
// through the loader, forced out of manifest), each validation_error_contains target string.
BOOST_AUTO_TEST_CASE(CoverageMatrixFromManifest)
{
    const auto names = loadInvalidManifest();
    BOOST_REQUIRE_MESSAGE(!names.empty(), "coverage: manifest has no invalid_* vectors");
    std::set<std::string> classifications;
    std::set<std::string> lvh;  // "parent" | "null" | "absent"
    std::set<int> staticItems;
    std::set<std::string> errStrings;
    for (auto const& id : names)
    {
        auto sample = w6test::loadInvalidSample(id);
        auto const& fisco = sample.vector["_op_expected"]["reject"]["fisco"];
        classifications.insert(fisco["classification"].asString());
        if (fisco.isMember("latest_valid_hash"))
            lvh.insert(fisco["latest_valid_hash"].isNull() ? "null" :
                                                             fisco["latest_valid_hash"].asString());
        else
            lvh.insert("absent");
        if (fisco.isMember("validation_error_contains"))
            errStrings.insert(fisco["validation_error_contains"].asString());
        const auto pos = id.rfind("_static_");
        if (pos != std::string::npos)
            staticItems.insert(std::stoi(id.substr(pos + 8)));
    }
    // Task 2 inline vectors (-38005 / two-pour -32603 / SYNCING satisfied inline; not in manifest).
    for (auto const* id : {"inline_invalid_stateRoot", "inline_invalid_parentUnknown",
             "inline_invalid_unsupportedFork", "inline_invalid_siblingFork"})
    {
        auto sample = makeInlineInvalidSample(id);
        auto const& fisco = sample.vector["_op_expected"]["reject"]["fisco"];
        classifications.insert(fisco["classification"].asString());
        if (fisco.isMember("latest_valid_hash"))
            lvh.insert(fisco["latest_valid_hash"].isNull() ? "null" :
                                                             fisco["latest_valid_hash"].asString());
    }
    // Required: classification (all four states covered)
    for (auto const* c : {"INVALID", "SYNCING", "-38005", "-32603"})
        BOOST_CHECK_MESSAGE(classifications.count(c),
            "coverage: classification '" << c << "' has no vector");
    // Required: latest_valid_hash (both "parent" and null values)
    for (auto const* h : {"parent", "null"})
        BOOST_CHECK_MESSAGE(lvh.count(h),
            "coverage: latest_valid_hash '" << h << "' has no vector");
    // Required: static items 1..11 (except 3/12 — forced out of manifest)
    for (int n : {1, 2, 4, 5, 6, 7, 8, 9, 10, 11})
        BOOST_CHECK_MESSAGE(staticItems.count(n),
            "coverage: static item " << n << " has no vector");
    BOOST_CHECK_MESSAGE(!staticItems.count(3),
        "coverage: static item 3 must NOT be manifest-registered (loader inexpressible)");
    BOOST_CHECK_MESSAGE(!staticItems.count(12),
        "coverage: static item 12 must NOT be manifest-registered (loader inexpressible)");
    // Required: full set of validation_error_contains target strings (corrupt fields / static faces / invalid-tx messages)
    static const char* kRequiredErrors[] = {
        "stateRoot", "gasUsed", "receiptsRoot",
        "blockHash does not match the reconstructed block header",
        "extraData must be exactly 9 bytes on the OP path (Isthmus)",
        "extraData must be exactly 17 bytes on the OP path (Jovian)",
        "executionPayload.rawTransactions is required on the OP path",
        "withdrawals must be present and empty on the OP path",
        "parentBeaconBlockRoot must be a 32-byte hash for newPayloadV4",
        "withdrawalsRoot is required on the OP path (Isthmus+)",
        "excessBlobGas must be present and zero on the OP path",
        "blobGasUsed must be zero before Jovian (OP Isthmus)",
        "blockNumber must not be negative",
        "gasLimit exceeds the maximum block gas limit (2^63-1)",
        "DA footprint (blobGasUsed) exceeds the block gas limit",
        "intrinsic gas too low", "nonce too low", "nonce too high",
        "insufficient funds for gas * price + value",
        "max fee per gas less than block base fee",
        "sender not an eoa",
        "set code transaction must not be a create transaction",
        "empty authorization list",
        "unsupported tx type byte 0x3",
    };
    for (auto const* s : kRequiredErrors)
        BOOST_CHECK_MESSAGE(errStrings.count(s),
            "coverage: validation_error_contains '" << s << "' has no vector");
}

// ── Task 4 (OP driver orchestration alignment): notifier timing ──
// The block-number notifier is fired by the scheduler's commitBlock (two-phase phase 2)
// AFTER the view is merged. It must fire ONLY on a genuine VALID commit — never on the
// compare-INVALID branch (resetPending) and never on the step-3b known-block short-circuit.
// Design task-4-brief Step 7; mirrors op-geth's "announce on block import, not on validation".
BOOST_AUTO_TEST_CASE(NotifierFiresOnlyOnValidCommit)
{
    int notified = 0;
    const auto armNotifier = [&](OpE2eFixture& f) {
        f.scheduler.setBlockNumberNotifier(
            [&notified](bcos::protocol::BlockNumber) { ++notified; });
    };

    // 1) stateRoot-corrupt INVALID vector (inline_invalid_stateRoot pattern): the step-5
    //    compare rejects and calls resetPending — no commit, no notifier.
    {
        auto sample = makeInlineInvalidSample("inline_invalid_stateRoot");
        auto fixture = std::make_unique<OpE2eFixture>(forkTimestampsFor(sample.jovian));
        armNotifier(*fixture);
        w6test::seedPreState(fixture->multiLayerStorage, sample.vector["pre"]);
        registerVerifiedBlock(
            fixture->multiLayerStorage, parseParentHashFromPayload(sample), 0);
        auto params = w6test::makeInvalidParamsJson(sample);
        auto request = bcos::rpc::parseNewPayloadRequest(
            params, *fixture->blockFactory->transactionFactory(), bcos::engine::ApiVersion::V4);
        auto status = bcos::task::syncWait(fixture->service.newPayload(request, 4));
        BOOST_REQUIRE_MESSAGE(
            static_cast<int>(status.status) ==
                static_cast<int>(bcos::engine::PayloadValidationStatus::Invalid),
            "NotifierFiresOnlyOnValidCommit: corrupt stateRoot expected INVALID, got "
                << static_cast<int>(status.status));
    }
    BOOST_CHECK_EQUAL(notified, 0);  // compare-INVALID must not fire

    // 2) Re-deliver a block that is already VALID-committed (validated before the notifier
    //    was armed): step-3b short-circuits before execution/commit — no notifier.
    {
        auto sample = w6test::loadVectorSample("jovian_deposit_only");
        auto fixture = std::make_unique<OpE2eFixture>(forkTimestampsFor(sample.jovian));
        w6test::seedPreState(fixture->multiLayerStorage, sample.vector["pre"]);
        const auto goldenHeader = w6test::decodeGoldenHeader(sample);
        registerVerifiedBlock(
            fixture->multiLayerStorage, goldenHeader->parentInfo().blockHash, 0);
        auto params = w6test::makeParamsJson(sample);
        auto request = bcos::rpc::parseNewPayloadRequest(
            params, *fixture->blockFactory->transactionFactory(), bcos::engine::ApiVersion::V4);
        // First delivery (notifier NOT yet armed): VALID, committed.
        auto first = bcos::task::syncWait(fixture->service.newPayload(request, 4));
        BOOST_REQUIRE_MESSAGE(
            static_cast<int>(first.status) ==
                static_cast<int>(bcos::engine::PayloadValidationStatus::Valid),
            "NotifierFiresOnlyOnValidCommit: seed delivery expected VALID");
        // Arm, then re-deliver the SAME block -> 3b short-circuit, no commit.
        armNotifier(*fixture);
        auto again = bcos::task::syncWait(fixture->service.newPayload(request, 4));
        BOOST_REQUIRE_MESSAGE(
            static_cast<int>(again.status) ==
                static_cast<int>(bcos::engine::PayloadValidationStatus::Valid),
            "NotifierFiresOnlyOnValidCommit: re-delivery expected VALID");
    }
    BOOST_CHECK_EQUAL(notified, 0);  // known-block short-circuit must not fire

    // 3) A fresh valid deposit-only block: commitBlock merges the view then fires the
    //    notifier exactly once.
    {
        auto sample = w6test::loadVectorSample("jovian_deposit_only");
        auto fixture = std::make_unique<OpE2eFixture>(forkTimestampsFor(sample.jovian));
        armNotifier(*fixture);
        w6test::seedPreState(fixture->multiLayerStorage, sample.vector["pre"]);
        const auto goldenHeader = w6test::decodeGoldenHeader(sample);
        registerVerifiedBlock(
            fixture->multiLayerStorage, goldenHeader->parentInfo().blockHash, 0);
        auto params = w6test::makeParamsJson(sample);
        auto request = bcos::rpc::parseNewPayloadRequest(
            params, *fixture->blockFactory->transactionFactory(), bcos::engine::ApiVersion::V4);
        auto status = bcos::task::syncWait(fixture->service.newPayload(request, 4));
        BOOST_REQUIRE_MESSAGE(
            static_cast<int>(status.status) ==
                static_cast<int>(bcos::engine::PayloadValidationStatus::Valid),
            "NotifierFiresOnlyOnValidCommit: fresh valid expected VALID, got "
                << static_cast<int>(status.status));
    }
    BOOST_CHECK_EQUAL(notified, 1);  // exactly one commit across the whole case
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(OpForkchoiceRpcE2eSuite)
// ── FCU end-to-end 6 cases (/loop audit gap: zero coverage of real updateForkchoice OP semantics) ──
// Mirrors op-geth v1.101702.2 eth/catalyst/api.go forkchoiceUpdated semantics:
//   head known -> VALID + LatestValidHash=head (api.go:316-322 valid())
//   head unknown -> STATUS_SYNCING (api.go:238 network pull)
//   OP + attributes -> -38003 (checkOptimismPayloadAttributes rejects; here
//   UnsupportedOpPayloadAttributes)  monotonicity finalized>head -> -38002 (api.go
//   safe/finalized checks; here InvalidForkchoiceState :263-280)  head increment must be
//   exactly +1 (:318-323)  no attributes -> head/safe/finalized advance
//   (updateTrackedBlockNumbers :1520-1525)

// ① head known -> VALID + LatestValidHash=head (mirrors op-geth valid())
BOOST_AUTO_TEST_CASE(ForkchoiceHeadKnownValid)
{
    auto [fixture, blockHash, number] = runVectorAndGetBlockHash("jovian_deposit_only");
    (void)number;
    auto [state, payloadId] = bcos::task::syncWait(fixture->service.updateForkchoice(
        bcos::engine::ForkchoiceState{blockHash, blockHash, blockHash}, nullptr, /*version=*/3));
    (void)payloadId;
    BOOST_CHECK_EQUAL(static_cast<int>(state.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));
    BOOST_CHECK(state.latestValidHash.has_value());
    BOOST_CHECK_EQUAL(*state.latestValidHash, blockHash);
}

// ② head unknown -> SYNCING (mirrors op-geth STATUS_SYNCING; getBlockNumber has no value -> :253-262)
BOOST_AUTO_TEST_CASE(ForkchoiceHeadUnknownSyncing)
{
    auto fixture = std::make_unique<OpE2eFixture>(forkTimestampsFor(/*jovian=*/false));
    bcos::h256 unknownHash(0xdeadbeef);  // no block registered
    auto [state, payloadId] = bcos::task::syncWait(fixture->service.updateForkchoice(
        bcos::engine::ForkchoiceState{unknownHash, unknownHash, unknownHash}, nullptr,
        /*version=*/3));
    (void)payloadId;
    BOOST_CHECK_EQUAL(static_cast<int>(state.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Syncing));
}

// ③ OP attributes -> -38003 (UnsupportedOpPayloadAttributes; mirrors op-geth attribute rejection).
// Note: the forkchoice state update takes effect first, then the build is rejected
// (EngineServiceImpl.h:356-359).
BOOST_AUTO_TEST_CASE(ForkchoiceAttributesRejected)
{
    auto [fixture, blockHash, number] = runVectorAndGetBlockHash("jovian_deposit_only");
    (void)number;
    bcos::engine::PayloadAttributes attrs;
    BOOST_CHECK_THROW(bcos::task::syncWait(fixture->service.updateForkchoice(
                          bcos::engine::ForkchoiceState{blockHash, blockHash, blockHash}, &attrs,
                          /*version=*/3)),
        bcos::engine::UnsupportedOpPayloadAttributes);
}

// ④ finalized > head -> InvalidForkchoiceState (-38002 monotonicity; mirrors updateForkchoice :263-280)
BOOST_AUTO_TEST_CASE(ForkchoiceMonotonicityRejected)
{
    auto [fixture, blockHash, number] = runVectorAndGetBlockHash("jovian_deposit_only");
    // Manually register a higher-numbered "known" block for finalized (registerVerifiedBlock writes SYS_HASH_2_NUMBER)
    bcos::h256 higherBlock("0x9999999999999999999999999999999999999999999999999999999999999999");
    registerVerifiedBlock(fixture->multiLayerStorage, higherBlock, number + 2);
    // finalized (number+2) > head (number) -> throws InvalidForkchoiceState at :269-274
    BOOST_CHECK_THROW(bcos::task::syncWait(fixture->service.updateForkchoice(
                          bcos::engine::ForkchoiceState{blockHash, blockHash, higherBlock}, nullptr,
                          /*version=*/3)),
        bcos::engine::InvalidForkchoiceState);
}

// ⑤ head increment must be exactly +1: skipping (missing middle block) -> conflict; strict +1 -> VALID (mirrors :318-323)
BOOST_AUTO_TEST_CASE(ForkchoiceHeadIncrement)
{
    auto [fixture, blockHash1, n1] = runVectorAndGetBlockHash("jovian_deposit_only");
    // First FCU(head=block1) -> VALID (tracked head set to block1, number n1)
    auto [s1, p1] = bcos::task::syncWait(fixture->service.updateForkchoice(
        bcos::engine::ForkchoiceState{blockHash1, blockHash1, blockHash1}, nullptr,
        /*version=*/3));
    (void)p1;
    BOOST_CHECK_EQUAL(static_cast<int>(s1.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));
    // Skipping: FCU straight to the registered block n1+2 (n1+1 not registered) ->
    // :318-323 "must increase by exactly 1" throws InvalidForkchoiceState (thrown before
    // m_trackedHeadBlock is assigned; tracked remains n1)
    bcos::h256 jumpBlock("0x8888888888888888888888888888888888888888888888888888888888888888");
    registerVerifiedBlock(fixture->multiLayerStorage, jumpBlock, n1 + 2);
    BOOST_CHECK_THROW(bcos::task::syncWait(fixture->service.updateForkchoice(
                          bcos::engine::ForkchoiceState{jumpBlock, jumpBlock, jumpBlock}, nullptr,
                          /*version=*/3)),
        bcos::engine::InvalidForkchoiceState);
    // Strict +1: register block n1+1 -> VALID (tracked head advances from n1 to n1+1)
    bcos::h256 nextBlock("0x7777777777777777777777777777777777777777777777777777777777777777");
    registerVerifiedBlock(fixture->multiLayerStorage, nextBlock, n1 + 1);
    auto [s2, p2] = bcos::task::syncWait(fixture->service.updateForkchoice(
        bcos::engine::ForkchoiceState{nextBlock, nextBlock, nextBlock}, nullptr, /*version=*/3));
    (void)p2;
    BOOST_CHECK_EQUAL(static_cast<int>(s2.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));
}

// ⑥ no attributes -> head advance (getSafe/Finalized reflect it; mirrors updateForkchoice
// :334-338 + updateTrackedBlockNumbers :1520-1525)
BOOST_AUTO_TEST_CASE(ForkchoiceNoAttributesHeadAdvance)
{
    auto [fixture, blockHash, number] = runVectorAndGetBlockHash("jovian_deposit_only");
    auto [state, payloadId] = bcos::task::syncWait(fixture->service.updateForkchoice(
        bcos::engine::ForkchoiceState{blockHash, blockHash, blockHash}, nullptr, /*version=*/3));
    (void)payloadId;
    BOOST_CHECK_EQUAL(static_cast<int>(state.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));
    // safe/finalized sync to the FCU-passed numbers (in-memory; FCU has no persistence)
    auto safe = fixture->service.getSafeBlockNumber();
    auto finalized = fixture->service.getFinalizedBlockNumber();
    BOOST_REQUIRE(safe.has_value());
    BOOST_REQUIRE(finalized.has_value());
    BOOST_CHECK_EQUAL(*safe, number);
    BOOST_CHECK_EQUAL(*finalized, number);
}

BOOST_AUTO_TEST_SUITE_END()
