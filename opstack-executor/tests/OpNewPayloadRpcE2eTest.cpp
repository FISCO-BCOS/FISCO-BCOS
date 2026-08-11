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

using OpScheduler = bcos::evm::engine::OpSchedulerImpl<ViewType>;
using OpEngineService =
    bcos::engine::EngineServiceImpl<StubMemPool, MLS, StubExecutor, OpScheduler>;

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
    OpScheduler scheduler;
    bcos::protocol::BlockFactory::Ptr blockFactory{makeBlockFactory()};
    OpEngineService service;

    explicit OpE2eFixture(bcos::evm::opstack::OpForkTimestamps forkTimestamps)
      : scheduler(receiptFactory, kChainId, forkTimestamps),
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
    // is no longer written. newPayload's registerOpBlock converts each raw EIP-2718
    // envelope to a tars Transaction and writes SYS_HASH_2_TX[txHash]
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

    // Submit A: VALID (registerOpBlock writes SYS_HASH_2_NUMBER[hashA]=1)
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

/// 从金样本派生内联无效向量。自洽损坏模型（spec §2a）：改头字段 → 重算 blockHash——
/// 用 productionHeaderOf 重建 OP 头（不读 payload.blockHash）后取 opHeaderHash 作为新 blockHash，
/// 保证 step-2 blockHash 检查通过、字段级命中（step-5 比较 / parentKnown / step-3c）可达。
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

    // 自洽：重算 blockHash（rebuild 头不含 payload.blockHash → opHeaderHash）
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
    // V4 静态校验要求 parentBeaconBlockRoot（EngineServiceImpl.cpp:340）；params[2] 恒真值
    sample.vector["_op_payload"]["parentBeaconBlockRoot"] = params[2u];
    if (spec.carryCanonical)
    {
        // -32603 canonical 兄弟 = 未损坏的 base payload（同父同高度、不同 blockHash）
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

/// 内联自测向量注册表（Task 2 自测；磁盘语料走 w6test::loadInvalidSample）。
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
    // Task 4 gate 回归：executor-consumer 向量的 validation_error_contains 是 T8n 执行层
    // throw 消息（"intrinsic gas too low"），引擎 RTTI-bypass 折叠成通用消息——若 runner
    // 未按 consumer 跳过消息断言，此向量必红。INVALID 分类 + latest_valid_hash=parent 仍须断言。
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

/// 分类驱动 runner（Task 2 主交付）。关键分支：
///  - SYNCING：跳过 registerVerifiedBlock（parent 未知是设计意图——登记后 parentKnown 通过
///    不再是 SYNCING）
///  - -38005/-32603：expect_throw 按 classification 选 UnsupportedFork / OpExecutionInternalError；
///    version 从 fisco.version 读（-38005 置 3），调 newPayload(request, version)
///  - -32603：两投——先投 canonical 子块（VALID，写 SYS_NUMBER_2_HASH 占用），再投同高度 sibling
///    （canonical 从向量 `_op_canonical` 读；Task 5 chain_fork_* carrier 同 schema）
///  - INVALID：断言 status + latestValidHash(=parent 或 null) + validationError 子串
void runInvalidVector(std::string const& id)
{
    auto sample =
        (id.rfind("inline_", 0) == 0) ? makeInlineInvalidSample(id) : w6test::loadInvalidSample(id);
    auto fixture = std::make_unique<OpE2eFixture>(forkTimestampsFor(sample.jovian));
    const auto& fisco = sample.vector["_op_expected"]["reject"]["fisco"];
    const auto classification = fisco["classification"].asString();
    // Task 4 consumer gate：executor-consumer 向量（非 decode 类）的 validation_error_contains
    // 是 T8n 执行层 throw 消息——引擎 RTTI-bypass 把它折叠成通用消息，子串断言必然失配。
    // consumer=="both"（blob）时 decode 消息是可靠 engine 面，仍须断言。缺省视 engine。
    const auto consumer = fisco.get("consumer", "engine").asString();

    if (classification == "SYNCING")
    {
        // ⚠️ 不登记 parent——parentHash 损坏/断链向量的意图就是 parent 未知
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

    // 非 SYNCING：先 seed pre + 登记 parent（parentHash 自洽损坏后是已知合法祖先）
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
        else  // -32603：两投——先投 canonical 子块（VALID，写 SYS_NUMBER_2_HASH 占用），再投 sibling
        {
            // canonical 兄弟必须随向量携带（Task 5 chain_fork_* carrier 同 schema）。缺 =
            // 畸形语料：单投 sibling 是 VALID 不抛，静默跳过会给误导性失败——响亮点名 schema 违例。
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

    // INVALID（默认路径）
    auto params = w6test::makeInvalidParamsJson(sample);
    auto request = bcos::rpc::parseNewPayloadRequest(
        params, *fixture->blockFactory->transactionFactory(), bcos::engine::ApiVersion::V4);
    auto status = bcos::task::syncWait(fixture->service.newPayload(request, 4));
    BOOST_CHECK_MESSAGE(static_cast<int>(status.status) ==
                            static_cast<int>(bcos::engine::PayloadValidationStatus::Invalid),
        id << ": expected INVALID, got " << static_cast<int>(status.status));
    // INVALID 向量必须声明 latest_valid_hash（"parent"|null）；缺 = 畸形语料，响亮失败
    // 而非把缺字段读成 null 断言错东西（stateRoot 损坏返回 parent 会假失败且信息误导）。
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

/// manifest 一行 → 无效向量 stem。⚠️ 必须剥掉 `.json` 后缀：manifest.txt 条目是 `xxx.json`，
/// 而 `loadInvalidSample`（GoldenSample.h）会再拼 `OP_T8N_VECTORS_DIR + "/" + id + ".json"`——
/// 不剥则 `vectors/invalid_xxx.json.json` 硬崩。返回空串表示该行不构成无效向量条目
/// （注释/空白/非 `invalid_` 前缀）。
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

/// manifest.txt 内 `invalid_*` 子集（Task 1 loadManifest 同款逻辑，条目存 stem 无 `.json`）。
/// Task 3 语料落地前为空集 → InvalidVectorsFromManifest 零迭代（前向兼容钩子）。
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

// B-7：单 fork isthmus 的 system-call 顺序可观测向量（W5 审查 A#1 定 id）。顺序错 →
// L1 读者 REVERT → stateRoot 失配 → VALID 断言+七项断言变红。
BOOST_AUTO_TEST_CASE(SystemCallOrderObservable)
{
    runGoldenVector("isthmus_system_call_order_observable");
}

// ── 全部 33 向量（16 isthmus + 17 jovian），每个一行 ──
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

// ── 引擎门 probe（Task 2 gate 1）：5 个预编译代表向量（over-cap/7702/Jovian/value/成功 output
// 五风险面）──
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

// 注：前 9 个样例 case 已覆盖 9 个向量；上面补全的 24 个 case 覆盖剩余 24 个，合计 33。
// 全量清单（16 isthmus + 17 jovian）：
//   isthmus: access_list, big_block_130tx, contract_create, contract_logs, deposit_failed,
//            deposit_mint, deposit_only, empty_account_cleanup, fee_env_observer,
//            message_passer_write, setcode_7702, setcode_7702_skips, system_contracts_real,
//            transfer_basic, transfer_multi, tx_reverted
//   jovian:  access_list, contract_create, contract_logs, da_mix, deposit_failed, deposit_mint,
//            deposit_only, empty_account_cleanup, fee_env_observer, first_block,
//            message_passer_write, setcode_7702, setcode_7702_skips, system_contracts_real,
//            transfer_basic, transfer_multi, tx_reverted
// 样例 case（9）：JovianDepositOnly/JovianTransferMulti/JovianDaMix/JovianFirstBlock/
//   IsthmusDepositOnly/IsthmusTransferMulti/IsthmusSetcode7702/IsthmusTxReverted/IsthmusBigBlock130tx
// 补全 case（24）：其余全部。⚠️ 命名冲突注意：BOOST_AUTO_TEST_CASE 名不可重复——
// 补全段的 24 个 case 名已刻意避开前 9 个样例 case 名（R2-D 实测：与既有 107 个 case 名、
// 计划内 33 个 case 名均零冲突）。
// 链式双块：一个 case（runChainedPair）同流执行 chainA+chainB（先 B SYNCING → A VALID → B VALID）。
BOOST_AUTO_TEST_CASE(ChainedAB)
{
    runChainedPair("chainA", "chainB");
}

// B-5c：jovian 链式对。runChainedPair 内断言 block2 VALID = step 3a-2 baseFee 一致性校验
// 通过即验证 max 分支（baseFee 按父块推导 + 上取整封顶）。
BOOST_AUTO_TEST_CASE(JovianChainedAB)
{
    runChainedPair("jovianChainA", "jovianChainB");
}
// ── Task 4 补全：24 个 precompile 矩阵向量（brief 附录 A id→用例名映射）──
// Task 2 的 5 个
// probe（bn256pair_norm/bls_pairing_overcap/wrap_eip7702/wrap_value_overcap/ecrecover）
// 已覆盖五风险面；此段补全其余 24 个，合计 29 个 precompile 用例。
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

// 最终校验：65 用例 = 63 单向量 + 2 链式对（chainA/B + jovianChainA/B，覆盖 4 个链式样本）。
// 63 单向量 = 34 基础向量（33 + isthmus_system_call_order_observable）
//   + 29 precompile（Task 2 probe 5 + Task 4 补全 24）。

// ── Task 2：E2E 无效向量 runner（分类驱动）──
// 内联自测向量（Task 2 不依赖 Task 3 语料文件）：分类驱动 runner 四分支全覆盖——
//   INVALID（stateRoot 自洽损坏 + latestValidHash=parent + "stateRoot"）
//   SYNCING（parentHash 断链 → 不登记 parent）
//   -38005（Isthmus+ timestamp + version≠4 → UnsupportedFork）
//   -32603（同父双子：先 canonical VALID 写 SYS_NUMBER_2_HASH 占用，再 sibling → 两投）
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

// Task 4 gate 回归：executor-consumer 向量跳过 validation_error_contains 断言（引擎
// RTTI-bypass 折叠 T8n 消息），但 INVALID 分类 + latest_valid_hash=parent 仍须断言。
BOOST_AUTO_TEST_CASE(ExecutorConsumerSkipsMessageAssertion)
{
    runInvalidVector("inline_invalid_executorStateRoot");
}

// Step 5：manifest 子集遍历（invalid_*.json）。Task 3 语料落地前为空集 → 零迭代（前向兼容）。
BOOST_AUTO_TEST_CASE(InvalidVectorsFromManifest)
{
    for (auto const& id : loadInvalidManifest())
    {
        runInvalidVector(id);
    }
}

// 回归（审查 Important）：manifest 条目是 `invalid_xxx.json`，loadInvalidSample 会再拼 `.json`。
// invalidStemFromManifestLine 必须剥后缀——否则 runInvalidVector 得 `vectors/invalid_xxx.json.json`
// → loadJsonFile 硬崩。证明 manifest→load 路径不再拼重。
BOOST_AUTO_TEST_CASE(InvalidManifestStemStripsJsonSuffix)
{
    // 真实 manifest 行形状 → stem 无后缀（loadInvalidSample 拼回后 = 原文件名）
    BOOST_CHECK_EQUAL(
        invalidStemFromManifestLine("invalid_inline_stateRoot.json"), "invalid_inline_stateRoot");
    BOOST_CHECK_EQUAL(
        invalidStemFromManifestLine("  invalid_foo_static_3.json  "), "invalid_foo_static_3");
    // 非 invalid_ 前缀 / 注释 / 空白 → 不进子集
    BOOST_CHECK_EQUAL(invalidStemFromManifestLine("jovian_deposit_only.json"), "");
    BOOST_CHECK_EQUAL(invalidStemFromManifestLine("# comment"), "");
    BOOST_CHECK_EQUAL(invalidStemFromManifestLine("   "), "");
    BOOST_CHECK_EQUAL(invalidStemFromManifestLine(""), "");
}

// ── Task 6 Step 2：覆盖矩阵断言（manifest 注册子集驱动）──────────────────
// 遍历 manifest 内 invalid_* 向量 + Task 2 内联向量，断言必达集全部被覆盖（缺 → FAILURE）：
//   每 classification（含 -38005——Task 2 version 向量满足；-32603 两投 runner 满足）、
//   每 latest_valid_hash 值（"parent"|null）、每 §4c 静态项（除 item 3/12——loader 不可表达
//   强制不入 manifest）、每 validation_error_contains 目标串。
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
    // Task 2 内联向量（-38005 / 两投 -32603 / SYNCING 由内联满足；不在 manifest）。
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
    // 必达：classification（四态全盖）
    for (auto const* c : {"INVALID", "SYNCING", "-38005", "-32603"})
        BOOST_CHECK_MESSAGE(classifications.count(c),
            "coverage: classification '" << c << "' has no vector");
    // 必达：latest_valid_hash（"parent" 与 null 两值）
    for (auto const* h : {"parent", "null"})
        BOOST_CHECK_MESSAGE(lvh.count(h),
            "coverage: latest_valid_hash '" << h << "' has no vector");
    // 必达：§4c 静态项 1..11（除 3/12——强制不入 manifest）
    for (int n : {1, 2, 4, 5, 6, 7, 8, 9, 10, 11})
        BOOST_CHECK_MESSAGE(staticItems.count(n),
            "coverage: static item " << n << " has no vector");
    BOOST_CHECK_MESSAGE(!staticItems.count(3),
        "coverage: static item 3 must NOT be manifest-registered (loader 不可表达)");
    BOOST_CHECK_MESSAGE(!staticItems.count(12),
        "coverage: static item 12 must NOT be manifest-registered (loader 不可表达)");
    // 必达：validation_error_contains 目标串全集（corrupt 字段 / 静态面 / invalid-tx 消息）
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

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(OpForkchoiceRpcE2eSuite)
// ── FCU 端到端 6 用例（/loop 审计缺口：真实 updateForkchoice OP 语义零覆盖）──
// 对照 op-geth v1.101702.2 eth/catalyst/api.go forkchoiceUpdated 判定：
//   head 已知 → VALID + LatestValidHash=head（api.go:316-322 valid()）
//   head 未知 → STATUS_SYNCING（api.go:238 网络拉取）
//   OP + attributes → -38003（checkOptimismPayloadAttributes 拒绝对,本仓库
//   UnsupportedOpPayloadAttributes） 单调性 finalized>head → -38002（api.go safe/finalized
//   校验,本仓库 InvalidForkchoiceState :263-280） head 递增必须恰好 +1（:318-323） 无 attributes →
//   head/safe/finalized 推进（updateTrackedBlockNumbers :1520-1525）

// ① head 已知 → VALID + LatestValidHash=head（对照 op-geth valid()）
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

// ② head 未知 → SYNCING（对照 op-geth STATUS_SYNCING；getBlockNumber 无值 → :253-262）
BOOST_AUTO_TEST_CASE(ForkchoiceHeadUnknownSyncing)
{
    auto fixture = std::make_unique<OpE2eFixture>(forkTimestampsFor(/*jovian=*/false));
    bcos::h256 unknownHash(0xdeadbeef);  // 未登记任何块
    auto [state, payloadId] = bcos::task::syncWait(fixture->service.updateForkchoice(
        bcos::engine::ForkchoiceState{unknownHash, unknownHash, unknownHash}, nullptr,
        /*version=*/3));
    (void)payloadId;
    BOOST_CHECK_EQUAL(static_cast<int>(state.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Syncing));
}

// ③ OP attributes → -38003（UnsupportedOpPayloadAttributes；对照 op-geth attributes 拒绝）。
// 注意：forkchoice 状态更新先生效、构建才被拒（EngineServiceImpl.h:356-359 设计 §6.2）。
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

// ④ finalized > head → InvalidForkchoiceState（-38002 单调性；对照 updateForkchoice :263-280）
BOOST_AUTO_TEST_CASE(ForkchoiceMonotonicityRejected)
{
    auto [fixture, blockHash, number] = runVectorAndGetBlockHash("jovian_deposit_only");
    // 手动登记一个更高编号"已知"块供 finalized 用（registerVerifiedBlock 写 SYS_HASH_2_NUMBER）
    bcos::h256 higherBlock("0x9999999999999999999999999999999999999999999999999999999999999999");
    registerVerifiedBlock(fixture->multiLayerStorage, higherBlock, number + 2);
    // finalized(编号 number+2) > head(编号 number) → :269-274 抛 InvalidForkchoiceState
    BOOST_CHECK_THROW(bcos::task::syncWait(fixture->service.updateForkchoice(
                          bcos::engine::ForkchoiceState{blockHash, blockHash, higherBlock}, nullptr,
                          /*version=*/3)),
        bcos::engine::InvalidForkchoiceState);
}

// ⑤ head 递增必须恰好 +1：跳号（缺中间块）→ 冲突；严格 +1 → VALID（对照 :318-323）
BOOST_AUTO_TEST_CASE(ForkchoiceHeadIncrement)
{
    auto [fixture, blockHash1, n1] = runVectorAndGetBlockHash("jovian_deposit_only");
    // 第一次 FCU(head=块1) → VALID（tracked head 设为块1,编号 n1）
    auto [s1, p1] = bcos::task::syncWait(fixture->service.updateForkchoice(
        bcos::engine::ForkchoiceState{blockHash1, blockHash1, blockHash1}, nullptr,
        /*version=*/3));
    (void)p1;
    BOOST_CHECK_EQUAL(static_cast<int>(s1.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));
    // 跳号：直接 FCU 指向编号 n1+2 的已登记块（未登记 n1+1）→ :318-323 "must increase by
    // exactly 1" 抛 InvalidForkchoiceState（抛在 m_trackedHeadBlock 赋值前,tracked 仍为 n1）
    bcos::h256 jumpBlock("0x8888888888888888888888888888888888888888888888888888888888888888");
    registerVerifiedBlock(fixture->multiLayerStorage, jumpBlock, n1 + 2);
    BOOST_CHECK_THROW(bcos::task::syncWait(fixture->service.updateForkchoice(
                          bcos::engine::ForkchoiceState{jumpBlock, jumpBlock, jumpBlock}, nullptr,
                          /*version=*/3)),
        bcos::engine::InvalidForkchoiceState);
    // 严格 +1：登记编号 n1+1 的块 → VALID（tracked head 从 n1 递增到 n1+1）
    bcos::h256 nextBlock("0x7777777777777777777777777777777777777777777777777777777777777777");
    registerVerifiedBlock(fixture->multiLayerStorage, nextBlock, n1 + 1);
    auto [s2, p2] = bcos::task::syncWait(fixture->service.updateForkchoice(
        bcos::engine::ForkchoiceState{nextBlock, nextBlock, nextBlock}, nullptr, /*version=*/3));
    (void)p2;
    BOOST_CHECK_EQUAL(static_cast<int>(s2.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));
}

// ⑥ 无 attributes → head 推进（getSafe/Finalized 反映；对照 updateForkchoice :334-338 +
// updateTrackedBlockNumbers :1520-1525）
BOOST_AUTO_TEST_CASE(ForkchoiceNoAttributesHeadAdvance)
{
    auto [fixture, blockHash, number] = runVectorAndGetBlockHash("jovian_deposit_only");
    auto [state, payloadId] = bcos::task::syncWait(fixture->service.updateForkchoice(
        bcos::engine::ForkchoiceState{blockHash, blockHash, blockHash}, nullptr, /*version=*/3));
    (void)payloadId;
    BOOST_CHECK_EQUAL(static_cast<int>(state.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));
    // safe/finalized 同步到 FCU 传入的编号（内存态;FCU 无持久化）
    auto safe = fixture->service.getSafeBlockNumber();
    auto finalized = fixture->service.getFinalizedBlockNumber();
    BOOST_REQUIRE(safe.has_value());
    BOOST_REQUIRE(finalized.has_value());
    BOOST_CHECK_EQUAL(*safe, number);
    BOOST_CHECK_EQUAL(*finalized, number);
}

BOOST_AUTO_TEST_SUITE_END()
