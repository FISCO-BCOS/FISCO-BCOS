// bcos-evm/test/opstack/OpNewPayloadRpcE2eTest.cpp
// L2 end-to-end real-chain comparison: real JSON params ->
// EngineHelper::parseNewPayloadRequest(V4) -> EngineService<OpSchedulerSeam>.newPayload(4)
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
#include <opstack-executor/OpCommon.h>
#include <opstack-executor/OpScheduler.h>
#include <opstack-executor/OpSchedulerSeam.h>
// EngineHelper.h's parseNewPayloadRequest declaration references
// bcos::protocol::TransactionFactory&, but EngineHelper.h does not declare that type
// itself (production relies on bcos-rpc unity-build include order). A single-TU
// direct compile must include TransactionFactory.h first or the declaration fails.
#include <bcos-framework/protocol/TransactionFactory.h>
#include <bcos-ledger/Ledger.h>  // real bcos::ledger::Ledger for the delegate's commit hook
#include <bcos-rpc/web3jsonrpc/utils/EngineHelper.h>
#include <bcos-table/src/LegacyStorageWrapper.h>  // LegacyStorageWrapper<BackendMemStorage>
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

// ── Composition-root stand-ins (the OP build path touches memPool hygiene; never populated) ──
struct StubMemPool
{
    template <class View>
    void remove(View&)
    {}
    template <class View, class OutputIt>
    void seal(int64_t, View&, OutputIt)
    {}
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

bcos::evm::opstack::OpForkFlags forkFlagsFor(bool jovian)
{
    return bcos::evm::opstack::OpForkFlags{.jovianActive = jovian};
}

using EngineOpScheduler = bcos::evm::engine::OpSchedulerSeam<ViewType>;
using OpEngineService =
    bcos::engine::EngineServiceImpl<StubMemPool, MLS, StubExecutor, EngineOpScheduler>;

/// Seed the SYS_TABLES meta-rows for the ledger SYS tables (each uses the SYS_VALUE field,
/// Ledger.cpp buildGenesisBlock). The delegate's commit hook runs prewriteBlockToBuffer, whose
/// asyncPrewriteBlock ends with asyncGetTotalTransactionCount opening SYS_CURRENT_STATE through
/// the Ledger's OWN m_stateStorage (here: a LegacyStorageWrapper over the MLS backend). Without
/// the SYS_TABLES row the open fails and the whole asyncPrewriteBlock errors out. The rows sort
/// before the /apps/ account range, so stateRoot's visitAccounts scan is unaffected.
void seedSysTables(MLS& multiLayerStorage)
{
    auto view = multiLayerStorage.fork();
    view.newMutable();
    constexpr std::string_view sysTables[] = {bcos::ledger::SYS_CURRENT_STATE,
        bcos::ledger::SYS_HASH_2_TX, bcos::ledger::SYS_HASH_2_NUMBER,
        bcos::ledger::SYS_NUMBER_2_HASH, bcos::ledger::SYS_NUMBER_2_BLOCK_HEADER,
        bcos::ledger::SYS_NUMBER_2_TXS, bcos::ledger::SYS_HASH_2_RECEIPT,
        bcos::ledger::SYS_BLOCK_NUMBER_2_NONCES};
    for (auto const& table : sysTables)
    {
        bcos::storage::Entry e;
        e.set(std::string(bcos::ledger::SYS_VALUE));
        bcos::task::syncWait(bcos::storage2::writeOne(
            view, StateKey{bcos::ledger::SYS_TABLES, std::string(table)}, std::move(e)));
    }
    bcos::task::syncWait(multiLayerStorage.mergeView(std::move(view)));
}

struct OpE2eFixture
{
    // Single-bucket CONCURRENT backend: seed/parent lands in the
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
    bcos::crypto::Hash::Ptr hashImpl;
    bcos::protocol::TransactionReceiptFactory::Ptr receiptFactory;
    EngineOpScheduler scheduler;  // engine seam SchedulerType (OpSchedulerSeam<ViewType>)
    bcos::protocol::BlockFactory::Ptr blockFactory{makeBlockFactory()};
    /// A real Ledger wired into the delegate's m_ledger (the commit hook now calls
    /// prewriteBlockToBuffer; every newPayload VALID submission commits through the delegate).
    /// prewriteBlockToBuffer writes through the commit hook's MutableStorage, so the Ledger's own
    /// m_stateStorage is only read by asyncGetTotalTransactionCount (SYS_CURRENT_STATE) — the
    /// seedSysTables call below makes that open succeed. LegacyStorageWrapper holds a reference;
    /// backendStorage is declared first and outlives it. The engine service's own ledger stays
    /// null (its local-payload persist path at EngineServiceImpl.h:714 is null-guarded and the
    /// forkchoice tests depend on that branch staying inert).
    std::shared_ptr<bcos::storage::LegacyStorageWrapper<BackendMemStorage>> legacyLedgerStorage;
    std::shared_ptr<bcos::ledger::Ledger> ledger;
    // Task 4: SchedulerSerialImpl (the delegate's per-tx driver) needs a live io pool.
    bcos::IOServicePool::Ptr ioServicePool{std::make_shared<bcos::IOServicePool>(1)};
    /// The engine's OP block-execution delegate (slot-3 OpScheduler<MLS>).
    /// A single OpSchedulerSeam serves the seam SchedulerType (route A executeOpBlock retired);
    /// OpScheduler holds no execution kernel — block execution is the delegate's route B.
    std::shared_ptr<bcos::executor_v1::opstack::OpScheduler<MLS>> opDelegate;
    OpEngineService service;

    explicit OpE2eFixture(bcos::evm::opstack::OpForkFlags forkFlags)
      : hashImpl(makeCryptoSuite()->hashImpl()),
        receiptFactory(makeReceiptFactory()),
        scheduler(forkFlags),
        legacyLedgerStorage(
            std::make_shared<bcos::storage::LegacyStorageWrapper<BackendMemStorage>>(
                backendStorage)),
        ledger(std::make_shared<bcos::ledger::Ledger>(blockFactory, legacyLedgerStorage, 1000)),
        opDelegate(std::make_shared<bcos::executor_v1::opstack::OpScheduler<MLS>>(receiptFactory,
            hashImpl, kChainId, forkFlags, blockFactory, multiLayerStorage, ledger, ioServicePool)),
        service(memPool, multiLayerStorage, executor, scheduler, blockFactory,
            /*ledger=*/nullptr, bcos::engine::c_defaultBlockTxCountLimit, /*maxEngineVersion=*/4,
            opDelegate)
    {
        seedSysTables(multiLayerStorage);
    }
};

/// Produced header: production-mapping reconstruction (val-loop GateFixture's
/// productionHeaderOf pattern). `bcos::engine::detail::rebuildOpEthHeader`
/// (EngineServiceImpl.cpp:470 or so: 17 fields verbatim from payload + txRoot + 3
/// constants via applyOpHeaderConstants). OP block hash uses `opHeaderHash()` =
/// keccak256(encodeOpHeader()), NOT BlockHeader::hash() (empty dataHash / factory
/// TARS-order backfill).
bcos::protocol::BlockHeader::Ptr productionHeaderOf(
    bcos::protocol::BlockFactory::Ptr const& blockFactory,
    bcos::engine::NewPayloadRequest const& request)
{
    auto const& payload = request.executionPayload;
    const auto transactionsRoot = EngineOpScheduler::computeTxRoot(*payload.rawTransactions);
    return bcos::engine::detail::rebuildOpEthHeader(blockFactory->blockHeaderFactory(), payload,
        transactionsRoot, *request.parentBeaconBlockRoot);
}

/// Parent pre-registration: the OP path's step-3 parentKnown
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
    // mergeBackStorage merges the oldest layer (FIFO).
    // Drain the stack — parent pre-registration lands in the backend immediately, and
    // with an empty stack before each block push, mergeView persists right away so the
    // backend assertions can pass.
    bcos::task::syncWait(multiLayerStorage.mergeView(std::move(view)));
}

// ── seven assertions ──
void assertSevenFields(std::string const& id, bcos::protocol::BlockHeader::Ptr const& produced,
    bcostars::protocol::BlockHeaderImpl::Ptr const& goldenHeader, bcos::h256 const& goldenBlockHash)
{
    // 1. blockHash: EthBlockHeader::computeHash = keccak256(rlpEncode) must equal
    //    golden.blockHash (op-geth's block.Hash() = keccak(RLP(21 fields)) definition). The
    //    3 post-merge constants are in the header (producer populates them).
    // Warning: this repo's Boost.Test macros do not support `<< id << msg` chaining;
    // uniformly use BOOST_CHECK_MESSAGE(id << msg).
    BOOST_CHECK_MESSAGE(bcos::protocol::EthBlockHeader::computeHash(*produced) == goldenBlockHash,
        id << ": blockHash");
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
    // Main assertion: byte-exact equality of EthBlockHeader::rlpEncode (covers the full RLP
    // encoding of all fields)
    bcos::bytes producedEncoded, goldenEncoded;
    bcos::protocol::EthBlockHeader(*produced).rlpEncode(producedEncoded);
    bcos::protocol::EthBlockHeader(*goldenHeader).rlpEncode(goldenEncoded);
    BOOST_CHECK_MESSAGE(producedEncoded == goldenEncoded, id << ": encodeOpHeader");
}

/// One vector end-to-end: seed pre -> register parent -> makeParamsJson ->
/// parseNewPayloadRequest(V4) -> newPayload(4) -> assertions.
void runGoldenVector(std::string const& id)
{
    auto sample = w6test::loadVectorSample(id);
    auto fixture = std::make_unique<OpE2eFixture>(forkFlagsFor(sample.jovian));
    opstack_test::seedPreState(fixture->multiLayerStorage, sample.vector["pre"]);
    // Warning: parent pre-registration (gap A): without it -> SYNCING instead of VALID. parentHash
    // is decoded from the golden header
    const auto goldenHeader = w6test::decodeGoldenHeader(sample);
    registerVerifiedBlock(fixture->multiLayerStorage, goldenHeader->parentInfo().blockHash, 0);

    auto params = w6test::makeParamsJson(sample);
    auto request = bcos::rpc::parseNewPayloadRequest(params, bcos::engine::ApiVersion::V4);

    auto status = bcos::task::syncWait(fixture->service.newPayload(request, 4));
    // Warning: PayloadValidationStatus is an enum class without operator<<; must compare
    // via static_cast<int> (same as existing engine tests, e.g. EngineServiceTest.cpp:312).
    BOOST_REQUIRE_MESSAGE(static_cast<int>(status.status) ==
                              static_cast<int>(bcos::engine::PayloadValidationStatus::Valid),
        id << ": expected VALID, got " << static_cast<int>(status.status)
           << (status.validationError ? " : " + *status.validationError : ""));

    // #19: after a VALID payload the head pointer must advance (same-view write).
    {
        auto headView = fixture->multiLayerStorage.fork();
        const auto head = bcos::task::syncWait(
            bcos::ledger::getCurrentBlockNumber(headView, bcos::ledger::fromStorage));
        BOOST_CHECK_MESSAGE(head == request.executionPayload.blockNumber,
            id << ": SYS_CURRENT_STATE head must equal the VALID payload's number, got " << head
               << " want " << request.executionPayload.blockNumber);
    }

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
    BOOST_REQUIRE(request.executionPayload.rawTransactions.has_value());  // missing field fails
                                                                          // cleanly
    auto const& rawTxs = *request.executionPayload.rawTransactions;
    auto& hashImpl = *fixture->blockFactory->cryptoSuite()->hashImpl();
    auto view = fixture->multiLayerStorage.fork();
    for (std::size_t i = 0; i < rawTxs.size(); ++i)
    {
        auto txHash = hashImpl.hash(rawTxs[i]);
        // SYS_HASH_2_TX present + round-trip (tars decode back to Transaction, hash==txHash pins
        // D4)
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
        auto rawEntry = bcos::task::syncWait(bcos::storage2::readOne(
            view, bcos::executor_v1::StateKey{EngineOpScheduler::c_ethRawTxTable,
                      bcos::concepts::bytebuffer::toView(txHash)}));
        BOOST_CHECK_MESSAGE(
            !rawEntry.has_value(), id << ": tx #" << i << " s_eth_hash_2_rawtx absent");
    }
}

/// Chained two-block: seed only A's pre -> register A's
/// parent(0) -> submit B first (SYNCING) -> submit A (VALID) -> submit B again (VALID).
/// FCU deliberately omitted (see implementation hint #4).
void runChainedPair(std::string const& aId, std::string const& bId)
{
    auto sampleA = w6test::loadChainedSample(aId);
    auto sampleB = w6test::loadChainedSample(bId);
    BOOST_REQUIRE(sampleA.jovian == sampleB.jovian);  // chained pair shares one fork (isthmus or
                                                      // jovian)
    auto fixture = std::make_unique<OpE2eFixture>(forkFlagsFor(sampleA.jovian));

    // Seed only A's pre (B's pre is A's postState; never re-seed)
    opstack_test::seedPreState(fixture->multiLayerStorage, sampleA.vector["pre"]);
    const auto goldenHeaderA = w6test::decodeGoldenHeader(sampleA);
    // Register A's parent (trusted genesis height 0)
    registerVerifiedBlock(fixture->multiLayerStorage, goldenHeaderA->parentInfo().blockHash, 0);

    auto requestA = bcos::rpc::parseNewPayloadRequest(
        w6test::makeParamsJson(sampleA), bcos::engine::ApiVersion::V4);
    auto requestB = bcos::rpc::parseNewPayloadRequest(
        w6test::makeParamsJson(sampleB), bcos::engine::ApiVersion::V4);

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

    // #19: after A is VALID the head pointer must equal A's number (same-view write).
    {
        auto headView = fixture->multiLayerStorage.fork();
        const auto head = bcos::task::syncWait(
            bcos::ledger::getCurrentBlockNumber(headView, bcos::ledger::fromStorage));
        BOOST_CHECK_MESSAGE(head == requestA.executionPayload.blockNumber,
            aId << ": SYS_CURRENT_STATE head must equal A's number, got " << head << " want "
                << requestA.executionPayload.blockNumber);
    }

    // Submit B again: parentKnown hits A -> VALID
    auto statusB = bcos::task::syncWait(fixture->service.newPayload(requestB, 4));
    BOOST_REQUIRE_MESSAGE(static_cast<int>(statusB.status) ==
                              static_cast<int>(bcos::engine::PayloadValidationStatus::Valid),
        bId << ": B expected VALID after A, got " << static_cast<int>(statusB.status));

    // #19: after B is VALID the head pointer must advance to B's number.
    {
        auto headView = fixture->multiLayerStorage.fork();
        const auto head = bcos::task::syncWait(
            bcos::ledger::getCurrentBlockNumber(headView, bcos::ledger::fromStorage));
        BOOST_CHECK_MESSAGE(head == requestB.executionPayload.blockNumber,
            bId << ": SYS_CURRENT_STATE head must equal B's number, got " << head << " want "
                << requestB.executionPayload.blockNumber);
    }

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
    auto fixture = std::make_unique<OpE2eFixture>(forkFlagsFor(sample.jovian));
    opstack_test::seedPreState(fixture->multiLayerStorage, sample.vector["pre"]);
    const auto goldenHeader = w6test::decodeGoldenHeader(sample);
    registerVerifiedBlock(fixture->multiLayerStorage, goldenHeader->parentInfo().blockHash, 0);
    auto params = w6test::makeParamsJson(sample);
    auto req = bcos::rpc::parseNewPayloadRequest(params, bcos::engine::ApiVersion::V4);
    auto status = bcos::task::syncWait(fixture->service.newPayload(req, /*version=*/4));
    BOOST_REQUIRE_MESSAGE(static_cast<int>(status.status) ==
                              static_cast<int>(bcos::engine::PayloadValidationStatus::Valid),
        id << ": seed newPayload expected VALID, got " << static_cast<int>(status.status));
    return {std::move(fixture), bcos::h256(std::string(sample.golden["blockHash"].asString())),
        goldenHeader->number()};
}

// ═══════════════════ Invalid-vector runner (classification-driven) ════
// Consumer-first (atomicity): self-tests use inline vectors (no dependence on corpus
// files); the runner also accepts on-disk invalid_*.json. Reject schema:
// fisco.consumer / classification ("INVALID"|"SYNCING"|"-38005"|"-32603") /
// latest_valid_hash("parent"|null) / validation_error_contains(optional) /
// expect_throw("UnsupportedFork"|"OpExecutionInternalError") / version(optional, -38005 sets 3).

/// Parses the parent hash from `_op_payload.parentHash` (needed for INVALID's
/// latestValidHash=parent assertion).
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
    bool carryCanonical = false;      // -32603 carries an uncorrupted canonical sibling (two-pour)
    std::string consumer = "engine";  // Task 4 gate regression: executor skips message assertion
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

    // Self-consistent: recompute blockHash (the rebuilt header excludes payload.blockHash ->
    // opHeaderHash)
    auto request = bcos::rpc::parseNewPayloadRequest(params, bcos::engine::ApiVersion::V4);
    auto header = productionHeaderOf(blockFactory, request);
    ep["blockHash"] = w6test::hexPrefixedH256(bcos::protocol::EthBlockHeader::computeHash(*header));

    w6test::InvalidSample sample;
    sample.hardfork = base.jovian ? "jovian" : "isthmus";
    sample.jovian = base.jovian;
    sample.vector["_info"]["hardfork"] = sample.hardfork;
    sample.vector["pre"] = base.vector["pre"];
    sample.vector["_op_payload"] = ep;
    // V4 static validation requires parentBeaconBlockRoot (EngineServiceImpl.cpp:340); params[2]
    // always holds the real value
    sample.vector["_op_payload"]["parentBeaconBlockRoot"] = params[2u];
    if (spec.carryCanonical)
    {
        // -32603 canonical sibling = the uncorrupted base payload (same parent/height, different
        // blockHash)
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

/// Inline self-test vector registry (on-disk corpus uses
/// w6test::loadInvalidSample).
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
    // An executor-consumer vector's validation_error_contains is
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

/// Classification-driven runner. Key branches:
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
    auto fixture = std::make_unique<OpE2eFixture>(forkFlagsFor(sample.jovian));
    const auto& fisco = sample.vector["_op_expected"]["reject"]["fisco"];
    const auto classification = fisco["classification"].asString();
    // Consumer gate: for executor-consumer vectors (non-decode class), the
    // validation_error_contains is a T8n execution-layer throw message, which the engine's
    // RTTI-bypass folds into a generic message, so a substring assertion would always
    // mismatch. For consumer=="both" (blob) the decode message is a reliable engine-side
    // string and must still be asserted. Default treated as engine.
    const auto consumer = fisco.get("consumer", "engine").asString();

    if (classification == "SYNCING")
    {
        // Warning: do not register the parent — a corrupted/broken parentHash vector intends parent
        // unknown
        opstack_test::seedPreState(fixture->multiLayerStorage, sample.vector["pre"]);
        auto params = w6test::makeInvalidParamsJson(sample);
        try
        {
            auto request = bcos::rpc::parseNewPayloadRequest(params, bcos::engine::ApiVersion::V4);
            auto status = bcos::task::syncWait(fixture->service.newPayload(request, 4));
            BOOST_CHECK_MESSAGE(
                static_cast<int>(status.status) ==
                    static_cast<int>(bcos::engine::PayloadValidationStatus::Syncing),
                id << ": expected SYNCING, got " << static_cast<int>(status.status));
        }
        catch (const bcos::rpc::JsonRpcException&)
        {
            // RPC-level shape rejection before reaching the engine — acceptable for
            // SYNCING vectors that test missing fields.
        }
        return;
    }

    // Non-SYNCING: seed pre + register parent first (after self-consistent corruption, parentHash
    // is a known valid ancestor)
    opstack_test::seedPreState(fixture->multiLayerStorage, sample.vector["pre"]);
    const auto parentHash = parseParentHashFromPayload(sample);
    registerVerifiedBlock(fixture->multiLayerStorage, parentHash, 0);

    if (classification == "-38005" || classification == "-32603")
    {
        auto params = w6test::makeInvalidParamsJson(sample);
        const auto version = fisco.isMember("version") ? fisco["version"].asUInt() : 4u;
        bcos::engine::NewPayloadRequest request;
        try
        {
            request = bcos::rpc::parseNewPayloadRequest(
                params, static_cast<bcos::engine::ApiVersion>(version));
        }
        catch (const bcos::rpc::JsonRpcException&)
        {
            // RPC-level shape rejection before reaching the engine — acceptable for
            // -38005/-32603 vectors that test missing fields.
            return;
        }
        if (classification == "-38005")
        {
            BOOST_CHECK_THROW(bcos::task::syncWait(fixture->service.newPayload(request, version)),
                bcos::engine::UnsupportedFork);
        }
        else  // -32603: two-pour — submit canonical child first (VALID, writes SYS_NUMBER_2_HASH
              // occupancy), then sibling
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
            auto canonicalReq =
                bcos::rpc::parseNewPayloadRequest(canonicalParams, bcos::engine::ApiVersion::V4);
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
    // parseNewPayloadRequest may throw at the RPC shape level (requireNewPayloadV4ParamShape /
    // requireExecutionPayloadV4Fields) before reaching the engine. Vectors that deliberately
    // omit parentBeaconBlockRoot or withdrawalsRoot trigger this path. Catch and match against
    // the expected validation_error_contains.
    auto params = w6test::makeInvalidParamsJson(sample);
    try
    {
        auto request = bcos::rpc::parseNewPayloadRequest(params, bcos::engine::ApiVersion::V4);
        auto status = bcos::task::syncWait(fixture->service.newPayload(request, 4));
        BOOST_CHECK_MESSAGE(static_cast<int>(status.status) ==
                                static_cast<int>(bcos::engine::PayloadValidationStatus::Invalid),
            id << ": expected INVALID, got " << static_cast<int>(status.status));
        // An INVALID vector must declare latest_valid_hash ("parent"|null); missing =
        // malformed corpus, fail loudly rather than reading the missing field as null and
        // asserting the wrong thing (stateRoot corruption returning parent would false-fail
        // misleadingly).
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
            BOOST_CHECK_MESSAGE(status.validationError &&
                                    status.validationError->find(expected) != std::string::npos,
                id << ": validationError missing '" << expected
                   << "', got: " << (status.validationError ? *status.validationError : "<none>"));
        }
    }
    catch (const bcos::rpc::JsonRpcException&)
    {
        // RPC-level shape rejection (requireNewPayloadV4ParamShape /
        // requireExecutionPayloadV4Fields) before the engine runs. The vector
        // was designed for engine-level validation, but the RPC layer catches
        // the malformed payload first. Accept any InvalidParams rejection as
        // the expected rejection for this vector.
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

/// The `invalid_*` subset of manifest.txt (same logic as loadManifest; entries are
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

// Single-fork isthmus system-call-order observable vector.
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

// ── Engine-gate probe: 5 representative precompile vectors ─────────
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

// BOOST_AUTO_TEST_CASE names must not repeat: the completion-case names below deliberately avoid
// the 9 sample-case names above.
// Chained pair: one case (runChainedPair) runs chainA+chainB in one flow (B SYNCING -> A VALID -> B
// VALID).
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

// Scalar-change pair: proves cross-block scalar propagation (baseFeeScalar
// changes from 1368 to 2736 between block A and block B). The generator's
// self-check already asserts L1 fee difference; this test verifies FISCO's
// execution engine reproduces the same golden.
BOOST_AUTO_TEST_CASE(ScalarChangeAB)
{
    runChainedPair("scalarChangeA", "scalarChangeB");
}
// ── Precompile matrix completion: 24 vectors ──
// The 5 probes (bn256pair_norm/bls_pairing_overcap/wrap_eip7702/wrap_value_overcap/ecrecover)
// covered the five risk faces; this section completes the remaining 24, totaling 29 precompile
// cases.
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
// isthmus_system_call_order_observable) + 29 precompile (probes 5 + completion 24).

// ── E2E invalid-vector runner (classification-driven) ──
// Inline self-test vectors (no dependence on corpus files): the
// classification-driven runner covers all four branches —
//   INVALID (stateRoot self-consistent corruption + latestValidHash=parent + "stateRoot")
//   SYNCING (parentHash broken chain -> do not register parent)
//   -38005 (Isthmus+ timestamp + version!=4 -> UnsupportedFork)
//   -32603 (same-parent twins: canonical VALID writes SYS_NUMBER_2_HASH occupancy, then sibling ->
//   two-pour)
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

// Executor-consumer vectors skip the validation_error_contains assertion (engine RTTI-bypass
// folds T8n messages), but INVALID classification + latest_valid_hash=parent must still be
// asserted.
BOOST_AUTO_TEST_CASE(ExecutorConsumerSkipsMessageAssertion)
{
    runInvalidVector("inline_invalid_executorStateRoot");
}

// Manifest subset iteration (invalid_*.json). Empty corpus -> zero iterations (forward compat).
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
    // Real manifest line shape -> stem without suffix (loadInvalidSample re-appends to the original
    // filename)
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

// ── Coverage-matrix assertion (manifest-registered subset driven) ─────
// Iterates manifest invalid_* vectors + inline vectors, asserting every required
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
    // Inline vectors (-38005 / two-pour -32603 / SYNCING satisfied inline; not in manifest).
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
        BOOST_CHECK_MESSAGE(
            classifications.count(c), "coverage: classification '" << c << "' has no vector");
    // Required: latest_valid_hash (both "parent" and null values)
    for (auto const* h : {"parent", "null"})
        BOOST_CHECK_MESSAGE(
            lvh.count(h), "coverage: latest_valid_hash '" << h << "' has no vector");
    // Required: static items 1..11 (except 3/12 — forced out of manifest)
    for (int n : {1, 2, 4, 5, 6, 7, 8, 9, 10, 11})
        BOOST_CHECK_MESSAGE(
            staticItems.count(n), "coverage: static item " << n << " has no vector");
    BOOST_CHECK_MESSAGE(!staticItems.count(3),
        "coverage: static item 3 must NOT be manifest-registered (loader inexpressible)");
    BOOST_CHECK_MESSAGE(!staticItems.count(12),
        "coverage: static item 12 must NOT be manifest-registered (loader inexpressible)");
    // Required: full set of validation_error_contains target strings (corrupt fields / static faces
    // / invalid-tx messages)
    static const char* kRequiredErrors[] = {
        "stateRoot",
        "gasUsed",
        "receiptsRoot",
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
        "intrinsic gas too low",
        "nonce too low",
        "nonce too high",
        "insufficient funds for gas * price + value",
        "max fee per gas less than block base fee",
        "sender not an eoa",
        "set code transaction must not be a create transaction",
        "empty authorization list",
        "unsupported tx type byte 0x03",
    };
    for (auto const* s : kRequiredErrors)
        BOOST_CHECK_MESSAGE(
            errStrings.count(s), "coverage: validation_error_contains '" << s << "' has no vector");
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(OpForkchoiceRpcE2eSuite)
// ── FCU end-to-end cases ──
// ── Mirrors op-geth v1.101702.2 eth/catalyst/api.go forkchoiceUpdated semantics:
//   head known -> VALID + LatestValidHash=head (api.go:316-322 valid())
//   head unknown -> STATUS_SYNCING (api.go:238 network pull)
//   OP + attrs -> version<3 throws UnsupportedFork (-38005); attrs content invalid ->
//   STATUS_INVALID before any state change (ForkchoiceAttrs*Invalid cases, op-geth
//   checkOptimismPayloadAttributes api_optimism.go:40-65); valid attrs -> Tier-2 build
//   monotonicity finalized>head -> -38002 (api.go
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

// ② head unknown -> SYNCING (mirrors op-geth STATUS_SYNCING; getBlockNumber has no value ->
// :253-262)
BOOST_AUTO_TEST_CASE(ForkchoiceHeadUnknownSyncing)
{
    auto fixture = std::make_unique<OpE2eFixture>(forkFlagsFor(/*jovian=*/false));
    bcos::h256 unknownHash(0xdeadbeef);  // no block registered
    auto [state, payloadId] = bcos::task::syncWait(fixture->service.updateForkchoice(
        bcos::engine::ForkchoiceState{unknownHash, unknownHash, unknownHash}, nullptr,
        /*version=*/3));
    (void)payloadId;
    BOOST_CHECK_EQUAL(static_cast<int>(state.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Syncing));
}

// MJ-2: OP-mode FCU attrs deep validation (op-geth checkOptimismPayloadAttributes,
// eth/catalyst/api_optimism.go:40-65). Invalid attrs return STATUS_INVALID *before* any
// forkchoice state change or build -- never a silent fallback.
namespace
{
bcos::engine::PayloadAttributes makeJovianAttrs()
{
    bcos::engine::PayloadAttributes attrs;
    attrs.timestamp = 2'000'000'000'000;  // strictly after the golden parent (ms domain)
    attrs.prevRandao = bcos::crypto::HashType{};
    attrs.suggestedFeeRecipient = bcos::Address{};
    attrs.gasLimit = 30'000'000;
    attrs.eip1559Params = bcos::bytes{0, 0, 0, 8, 0, 0, 0, 2};  // denominator=8, elasticity=2
    attrs.minBaseFee = 0;
    attrs.withdrawals = std::vector<bcos::engine::WithdrawalV1>{};
    attrs.parentBeaconBlockRoot = bcos::crypto::HashType{};
    return attrs;
}
}  // namespace

BOOST_AUTO_TEST_CASE(ForkchoiceAttrsMissingGasLimitInvalid)
{
    auto [fixture, blockHash, number] = runVectorAndGetBlockHash("jovian_deposit_only");
    (void)number;
    auto attrs = makeJovianAttrs();
    attrs.gasLimit = std::nullopt;
    auto [state, payloadId] = bcos::task::syncWait(fixture->service.updateForkchoice(
        bcos::engine::ForkchoiceState{blockHash, blockHash, blockHash}, &attrs, /*version=*/4));
    (void)payloadId;
    BOOST_CHECK_EQUAL(static_cast<int>(state.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Invalid));
    BOOST_REQUIRE(state.validationError.has_value());
    BOOST_CHECK(state.validationError->find("gasLimit") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(ForkchoiceAttrsMissingMinBaseFeeInvalid)
{
    auto [fixture, blockHash, number] = runVectorAndGetBlockHash("jovian_deposit_only");
    (void)number;
    auto attrs = makeJovianAttrs();
    attrs.minBaseFee = std::nullopt;
    auto [state, payloadId] = bcos::task::syncWait(fixture->service.updateForkchoice(
        bcos::engine::ForkchoiceState{blockHash, blockHash, blockHash}, &attrs, /*version=*/4));
    (void)payloadId;
    BOOST_CHECK_EQUAL(static_cast<int>(state.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Invalid));
    BOOST_REQUIRE(state.validationError.has_value());
    BOOST_CHECK(state.validationError->find("minBaseFee") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(ForkchoiceAttrsMinBaseFeeBeforeJovianInvalid)
{
    // Isthmus fixture: minBaseFee must be null pre-Jovian (jovian/exec-engine.md:59-79).
    // OpE2eFixture has no genesis-hash helper — register a known block instead
    // (pattern: ForkchoiceMonotonicityRejected).
    auto fixture = std::make_unique<OpE2eFixture>(forkFlagsFor(/*jovian=*/false));
    bcos::h256 knownBlock("0x5555555555555555555555555555555555555555555555555555555555555555");
    registerVerifiedBlock(fixture->multiLayerStorage, knownBlock, /*number=*/0);
    auto attrs = makeJovianAttrs();
    auto [state, payloadId] = bcos::task::syncWait(fixture->service.updateForkchoice(
        bcos::engine::ForkchoiceState{knownBlock, knownBlock, knownBlock}, &attrs, /*version=*/4));
    (void)payloadId;
    BOOST_CHECK_EQUAL(static_cast<int>(state.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Invalid));
    BOOST_REQUIRE(state.validationError.has_value());
    BOOST_CHECK(state.validationError->find("minBaseFee") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(ForkchoiceAttrsNonEmptyWithdrawalsInvalid)
{
    // MJ-2: OP attrs withdrawals must be present AND empty (op-geth api_optimism.go:55-58
    // rejects non-empty; buildOpPayload must never silently normalize them away).
    auto [fixture, blockHash, number] = runVectorAndGetBlockHash("jovian_deposit_only");
    (void)number;
    auto attrs = makeJovianAttrs();
    attrs.withdrawals = std::vector<bcos::engine::WithdrawalV1>{bcos::engine::WithdrawalV1{}};
    auto [state, payloadId] = bcos::task::syncWait(fixture->service.updateForkchoice(
        bcos::engine::ForkchoiceState{blockHash, blockHash, blockHash}, &attrs, /*version=*/4));
    (void)payloadId;
    BOOST_CHECK_EQUAL(static_cast<int>(state.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Invalid));
    BOOST_REQUIRE(state.validationError.has_value());
    BOOST_CHECK(state.validationError->find("withdrawals") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(ForkchoiceAttrsMissingEip1559ParamsInvalid)
{
    // MJ-2: Holocene+ OP face requires eip1559Params (op-geth api_optimism.go:40-65);
    // a missing value is refused, never silently defaulted.
    auto [fixture, blockHash, number] = runVectorAndGetBlockHash("jovian_deposit_only");
    (void)number;
    auto attrs = makeJovianAttrs();
    attrs.eip1559Params = std::nullopt;
    auto [state, payloadId] = bcos::task::syncWait(fixture->service.updateForkchoice(
        bcos::engine::ForkchoiceState{blockHash, blockHash, blockHash}, &attrs, /*version=*/4));
    (void)payloadId;
    BOOST_CHECK_EQUAL(static_cast<int>(state.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Invalid));
    BOOST_REQUIRE(state.validationError.has_value());
    BOOST_CHECK(state.validationError->find("eip1559Params") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(ForkchoiceAttrsWrongSizeEip1559ParamsInvalid)
{
    // MJ-2: eip1559Params must be exactly 8 bytes (Holocene denominator|elasticity), and a
    // partial-zero pair (denominator=0 with elasticity!=0, or vice versa) is rejected at FCU
    // time (op-geth ValidateHolocene1559Params) -- both zero is allowed (= prior constants).
    auto [fixture, blockHash, number] = runVectorAndGetBlockHash("jovian_deposit_only");
    (void)number;
    // 4-byte value: size != 8.
    {
        auto attrs = makeJovianAttrs();
        attrs.eip1559Params = bcos::bytes{0, 0, 0, 8};
        auto [state, payloadId] = bcos::task::syncWait(fixture->service.updateForkchoice(
            bcos::engine::ForkchoiceState{blockHash, blockHash, blockHash}, &attrs,
            /*version=*/4));
        (void)payloadId;
        BOOST_CHECK_EQUAL(static_cast<int>(state.status),
            static_cast<int>(bcos::engine::PayloadValidationStatus::Invalid));
        BOOST_REQUIRE(state.validationError.has_value());
        BOOST_CHECK(state.validationError->find("eip1559Params") != std::string::npos);
    }
    // Partial-zero: denominator=0, elasticity=2 -> rejected (never a silent default build).
    {
        auto attrs = makeJovianAttrs();
        attrs.eip1559Params = bcos::bytes{0, 0, 0, 0, 0, 0, 0, 2};
        auto [state, payloadId] = bcos::task::syncWait(fixture->service.updateForkchoice(
            bcos::engine::ForkchoiceState{blockHash, blockHash, blockHash}, &attrs,
            /*version=*/4));
        (void)payloadId;
        BOOST_CHECK_EQUAL(static_cast<int>(state.status),
            static_cast<int>(bcos::engine::PayloadValidationStatus::Invalid));
        BOOST_REQUIRE(state.validationError.has_value());
        BOOST_CHECK(state.validationError->find("eip1559Params") != std::string::npos);
    }
}
// V3+ (op-node sends FCU V3 with attrs for Isthmus+ builds). The version gate now runs
// BEFORE attrs validation (updateForkchoice), so V2 attrs hit -38005 first, never the
// validation verdicts exercised by the ForkchoiceAttrs*Invalid cases above.
BOOST_AUTO_TEST_CASE(ForkchoiceAttributesVersionGate)
{
    auto [fixture, blockHash, number] = runVectorAndGetBlockHash("jovian_deposit_only");
    (void)number;
    bcos::engine::PayloadAttributes attrs;
    attrs.timestamp = 2'000'000'000'000;
    attrs.prevRandao = bcos::crypto::HashType{};
    attrs.suggestedFeeRecipient = bcos::Address{};
    BOOST_CHECK_THROW(bcos::task::syncWait(fixture->service.updateForkchoice(
                          bcos::engine::ForkchoiceState{blockHash, blockHash, blockHash}, &attrs,
                          /*version=*/2)),
        bcos::engine::UnsupportedFork);
}

// ④ finalized > head -> InvalidForkchoiceState (-38002 monotonicity; mirrors updateForkchoice
// :263-280)
BOOST_AUTO_TEST_CASE(ForkchoiceMonotonicityRejected)
{
    auto [fixture, blockHash, number] = runVectorAndGetBlockHash("jovian_deposit_only");
    // Manually register a higher-numbered "known" block for finalized (registerVerifiedBlock writes
    // SYS_HASH_2_NUMBER)
    bcos::h256 higherBlock("0x9999999999999999999999999999999999999999999999999999999999999999");
    registerVerifiedBlock(fixture->multiLayerStorage, higherBlock, number + 2);
    // finalized (number+2) > head (number) -> throws InvalidForkchoiceState at :269-274
    BOOST_CHECK_THROW(bcos::task::syncWait(fixture->service.updateForkchoice(
                          bcos::engine::ForkchoiceState{blockHash, blockHash, higherBlock}, nullptr,
                          /*version=*/3)),
        bcos::engine::InvalidForkchoiceState);
}

// ⑤ head increment must be exactly +1: skipping (missing middle block) -> conflict; strict +1 ->
// VALID (mirrors :318-323)
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

// ── B4-3: Engine API boundary tests (spec §6 P1, 08-18) ──

// safe/finalized can advance independently from head: safe=block2, finalized=block1 (both
// known) is VALID — only finalized > head is forbidden by the monotonicity check.
BOOST_AUTO_TEST_CASE(ForkchoiceSafeFinalizedIndependent)
{
    auto [fixture, blockHash1, n1] = runVectorAndGetBlockHash("jovian_deposit_only");
    // Register block n1+1 so we can use two different known blocks.
    bcos::h256 block2("0xaabbccdd00112233445566778899aabbccddeeff00112233445566778899aabb");
    registerVerifiedBlock(fixture->multiLayerStorage, block2, n1 + 1);
    // FCU: head=block2(n+1), safe=block2(n+1), finalized=block1(n) — valid, finalized < head.
    auto [state1, pid1] = bcos::task::syncWait(fixture->service.updateForkchoice(
        bcos::engine::ForkchoiceState{block2, block2, blockHash1}, nullptr, /*version=*/3));
    (void)pid1;
    BOOST_CHECK_EQUAL(static_cast<int>(state1.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));
    auto safe = fixture->service.getSafeBlockNumber();
    auto finalized = fixture->service.getFinalizedBlockNumber();
    BOOST_REQUIRE(safe.has_value());
    BOOST_REQUIRE(finalized.has_value());
    BOOST_CHECK_EQUAL(*safe, n1 + 1);
    BOOST_CHECK_EQUAL(*finalized, n1);
}

// safe < head is VALID (safe being behind head is normal during sync); only finalized > head
// is invalid (monotonicity, tested by ForkchoiceMonotonicityRejected).
BOOST_AUTO_TEST_CASE(ForkchoiceSafeBelowHeadIsValid)
{
    auto [fixture, blockHash, n1] = runVectorAndGetBlockHash("jovian_deposit_only");
    bcos::h256 block0("0x0000000000000000000000000000000000000000000000000000000000000001");
    registerVerifiedBlock(fixture->multiLayerStorage, block0, n1 - 1);
    // head=block(n), safe=block(n-1), finalized=block(n-1) — safe and finalized below head = VALID.
    auto [state, pid] = bcos::task::syncWait(fixture->service.updateForkchoice(
        bcos::engine::ForkchoiceState{blockHash, block0, block0}, nullptr, /*version=*/3));
    (void)pid;
    BOOST_CHECK_EQUAL(static_cast<int>(state.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));
}

BOOST_AUTO_TEST_SUITE_END()
