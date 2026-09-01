/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 */

#include "engine/bcos-engine/EngineServiceImpl.h"

#include <bcos-rlp-protocol/EthBlockHeader.h>
#include <bcos-codec/rlp/Common.h>
#include <bcos-codec/rlp/RLPEncode.h>
#include <bcos-concepts/ByteBuffer.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-framework/engine/RawTransactionDispatch.h>
#include <bcos-framework/ledger/EVMAccount.h>
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/storage/Entry.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/storage2/MultiLayerStorage.h>
#include <bcos-framework/testutils/faker/FakeBlock.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-ledger/Ledger.h>
#include <bcos-mempool/MemPoolImpl.h>
#include <bcos-tars-protocol/protocol/TransactionImpl.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptImpl.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/lexical_cast.hpp>
#include <boost/test/unit_test.hpp>
#include <algorithm>

using namespace bcos;
using namespace bcos::engine;

namespace
{
// Whole-second milliseconds (1700000000s): every Eth header produced by finalizeEthBlockHeader
// must satisfy validateHeader's "timestamp is a whole number of seconds" check, so a fixture
// timestamp with sub-second milliseconds would make every build path throw.
constexpr std::uint64_t c_timestamp = 1700000000ULL * 1000ULL;
constexpr bcos::protocol::BlockNumber c_initialBlockNumber = 5;
constexpr bcos::protocol::BlockNumber c_trackedInitialBlockNumber = 10;
constexpr bcos::protocol::BlockNumber c_trackedNextBlockNumber = 11;
constexpr bcos::protocol::BlockNumber c_validationBlockNumber = 20;
constexpr bcos::protocol::BlockNumber c_reorgStartBlockNumber = 30;
constexpr bcos::protocol::BlockNumber c_reorgTargetBlockNumber = 32;
constexpr bcos::protocol::BlockNumber c_headOrderingBlockNumber = 40;
constexpr bcos::protocol::BlockNumber c_safeOrderingBlockNumber = 41;
constexpr bcos::protocol::BlockNumber c_finalizedOrderingBlockNumber = 42;
constexpr bcos::protocol::BlockNumber c_staleInitialBlockNumber = 50;
constexpr bcos::protocol::BlockNumber c_staleNextBlockNumber = 51;
constexpr bcos::protocol::BlockNumber c_staleThirdBlockNumber = 52;

using namespace bcos::txpool;
using namespace bcos::protocol;
using namespace bcos::crypto;

static bytes toBytes(std::string_view input)
{
    return {reinterpret_cast<const byte*>(input.data()),
        reinterpret_cast<const byte*>(input.data()) + input.size()};
}

class TestTransactionImpl : public bcostars::protocol::TransactionImpl
{
public:
    void markClean() { setTainted(false); }
};

static protocol::Transaction::Ptr makeTx(std::string_view senderBytes, int64_t nonce)
{
    auto tx = std::make_shared<TestTransactionImpl>();
    tx->mutableInner().data.to.assign(senderBytes.begin(), senderBytes.end());
    tx->setNonce(std::to_string(nonce));
    tx->forceSender(toBytes(senderBytes));
    Keccak256 hasher;
    tx->calculateHash(hasher);
    tx->markClean();
    tx->setImportTime(nonce);
    return tx;
}

/// A Web3-typed transaction with a real EIP-1559 signing payload and a 65-byte
/// signature, shaped exactly like the eth_sendRawTransaction ingress produces:
/// type=Web3Transaction, extraTransactionBytes = 0x02 || rlp(unsigned fields),
/// signature = r(32) || s(32) || yParity(1). calculateHash() runs the same raw-bytes
/// splice buildPayload uses, so constructing one also validates the payload shape.
static protocol::Transaction::Ptr makeWeb3Tx(std::string_view senderBytes, uint64_t nonce)
{
    bytes body;
    bcos::codec::rlp::encode(body, static_cast<uint64_t>(1));  // chainId
    bcos::codec::rlp::encode(body, nonce);
    bcos::codec::rlp::encode(body, static_cast<uint64_t>(1));      // maxPriorityFeePerGas
    bcos::codec::rlp::encode(body, static_cast<uint64_t>(1));      // maxFeePerGas
    bcos::codec::rlp::encode(body, static_cast<uint64_t>(21000));  // gasLimit
    bcos::codec::rlp::encode(body, Address("abcdabcdabcdabcdabcdabcdabcdabcdabcdabcd"));
    bcos::codec::rlp::encode(body, static_cast<uint64_t>(0));  // value
    bcos::codec::rlp::encode(body, bytes{});                   // data
    body.push_back(bcos::codec::rlp::LIST_HEAD_BASE);          // empty accessList
    bytes payload;
    payload.push_back(0x02);
    bcos::codec::rlp::encodeHeader(
        payload, bcos::codec::rlp::Header{.isList = true, .payloadLength = body.size()});
    payload.insert(payload.end(), body.begin(), body.end());

    auto tx = std::make_shared<TestTransactionImpl>();
    tx->mutableInner().type = static_cast<int>(bcos::protocol::TransactionType::Web3Transaction);
    tx->mutableInner().extraTransactionBytes.assign(payload.begin(), payload.end());
    bytes signature(65, 0);
    signature[31] = 0x12;  // r != 0
    signature[63] = 0x34;  // s != 0
    signature[64] = 0x01;  // yParity
    tx->mutableInner().signature.assign(signature.begin(), signature.end());
    tx->setNonce("0x" + std::to_string(nonce));
    tx->forceSender(toBytes(senderBytes));
    Keccak256 hasher;
    tx->calculateHash(hasher);
    tx->markClean();
    tx->setImportTime(static_cast<int64_t>(nonce));
    return tx;
}

using RealGlobalStateMutableStorage = bcos::storage2::memory_storage::MemoryStorage<
    bcos::executor_v1::StateKey, bcos::executor_v1::StateValue,
    bcos::storage2::memory_storage::Attribute(bcos::storage2::memory_storage::ORDERED |
                                              bcos::storage2::memory_storage::LOGICAL_DELETION)>;
using RealGlobalStateBackendStorage =
    bcos::storage2::memory_storage::MemoryStorage<bcos::executor_v1::StateKey,
        bcos::executor_v1::StateValue,
        bcos::storage2::memory_storage::Attribute(
            bcos::storage2::memory_storage::ORDERED | bcos::storage2::memory_storage::CONCURRENT),
        std::hash<bcos::executor_v1::StateKey>>;

// Minimal CheckpointStorage stub — only the interface needed by MultiLayerStorage
template <class Key, class Value, bcos::storage2::ReadWriteStorage<Key, Value> Storage>
struct TrivialCheckpointStorage
{
    using CheckpointName = bcos::h256;

    Storage& m_storage;
    explicit TrivialCheckpointStorage(Storage& s) : m_storage(s) {}
    Storage& open() { return m_storage; }
    [[noreturn]] Storage& open(CheckpointName const&) { std::abort(); }
    void createCheckpoint(Storage&, CheckpointName const&) {}
    void deleteCheckpoint(CheckpointName const&) {}
    std::optional<CheckpointName> latestCheckpointName() const { return std::nullopt; }
    std::optional<CheckpointName> oldestCheckpointName() const { return std::nullopt; }
};

using RealGlobalCheckpointBackend = TrivialCheckpointStorage<bcos::executor_v1::StateKey,
    bcos::executor_v1::StateValue, RealGlobalStateBackendStorage>;
using RealGlobalStateStorage = bcos::storage2::MultiLayerStorage<RealGlobalStateMutableStorage,
    void, RealGlobalCheckpointBackend>;

task::Task<void> writeBlockNumberToStorage(RealGlobalStateBackendStorage& backendStorage,
    const h256& blockHash, bcos::protocol::BlockNumber blockNumber)
{
    storage::Entry entry;
    entry.set(boost::lexical_cast<std::string>(blockNumber));
    co_await bcos::storage2::writeOne(backendStorage,
        bcos::executor_v1::StateKey{
            ledger::SYS_HASH_2_NUMBER, bcos::concepts::bytebuffer::toView(blockHash)},
        std::move(entry));
}

struct RealGlobalStateStorageFixture
{
    RealGlobalStateBackendStorage backendStorage;
    RealGlobalCheckpointBackend checkpointBackend{backendStorage};
    RealGlobalStateStorage storage{checkpointBackend};

    explicit RealGlobalStateStorageFixture(
        evmc_revision rev = EVMC_CANCUN, bool writeEvmcRevision = true)
    {
        // The Engine service runs only on executor_version=2 with an explicit EVMC
        // revision. The default models the "Karst" chain (CANCUN) these tests mostly
        // drive; tests that exercise older FCU method versions (V1/V2) pass SHANGHAI so
        // the attribute shape the request can express matches the chain fork.
        // buildPayload FAILS CLOSED when the revision is absent (it never falls back to
        // the compile-time default), so the missing-revision test passes writeEvmcRevision
        // = false to reach that branch.
        writeSysConfig(
            magic_enum::enum_name(ledger::SystemConfig::executor_version),
            std::to_string(ledger::ETHEREUM_EXECUTOR_VERSION));
        if (writeEvmcRevision)
        {
            writeSysConfig(
                ledger::SYSTEM_KEY_EVMC_REVISION, ledger::encodeEVMCRevisionConfig(rev, {}));
        }
    }

    void setBlockNumber(const h256& blockHash, bcos::protocol::BlockNumber blockNumber)
    {
        task::syncWait(writeBlockNumberToStorage(backendStorage, blockHash, blockNumber));
    }

    void setNonce(std::string_view sender, std::string nonce)
    {
        // The mempool stores senders as raw bytes and MemPoolImpl::seal/remove resolve the
        // account via the evmc_address overload (lower-case hex path), matching the executor.
        // Use the same path here so the sealed nonce the test relies on is visible to seal().
        evmc_address addr{};
        std::copy_n(sender.begin(), std::min(sender.size(), sizeof(addr.bytes)), addr.bytes);
        ledger::account::EVMAccount account{backendStorage, addr, false};
        task::syncWait(account.setNonce(std::move(nonce)));
    }

private:
    void writeSysConfig(std::string_view key, std::string value)
    {
        storage::Entry entry;
        entry.set(bcos::storage::serialize::encode(
            ledger::SystemConfigEntry{std::move(value), 0}));
        task::syncWait(storage2::writeOne(backendStorage,
            bcos::executor_v1::StateKey{ledger::SYS_CONFIG, key}, std::move(entry)));
    }
};

void setForkchoiceBlockNumbers(RealGlobalStateStorageFixture& storageFixture,
    const ForkchoiceState& forkchoiceState, bcos::protocol::BlockNumber headBlockNumber,
    bcos::protocol::BlockNumber safeBlockNumber, bcos::protocol::BlockNumber finalizedBlockNumber)
{
    storageFixture.setBlockNumber(forkchoiceState.headBlockHash, headBlockNumber);
    storageFixture.setBlockNumber(forkchoiceState.safeBlockHash, safeBlockNumber);
    storageFixture.setBlockNumber(forkchoiceState.finalizedBlockHash, finalizedBlockNumber);
}

// Stub types satisfying executor_v1::TransactionExecutor and
// scheduler_v1::TransactionScheduler concepts for unit testing.
// Stub executor and scheduler return empty results; blockFactory is a real
// instance used for block header creation and hash computation.
struct StubExecutor
{
    template <class Storage>
    struct ExecuteContext
    {
        task::Task<void> prepare() { co_return; }
        task::Task<void> execute() { co_return; }
        task::Task<protocol::TransactionReceipt::Ptr> finish() { co_return nullptr; }
    };

    template <class Storage>
    task::Task<protocol::TransactionReceipt::Ptr> executeTransaction(Storage&,
        const protocol::BlockHeader&, const protocol::Transaction&, int,
        const ledger::LedgerConfig&, bool)
    {
        co_return nullptr;
    }

    template <class Storage>
    task::Task<ExecuteContext<Storage>> createExecuteContext(Storage&, const protocol::BlockHeader&,
        const protocol::Transaction&, int, const ledger::LedgerConfig&, bool)
    {
        co_return ExecuteContext<Storage>{};
    }
};

struct StubScheduler
{
    template <class Storage, class Executor>
    task::Task<std::vector<protocol::TransactionReceipt::Ptr>> executeBlock(Storage&, Executor&,
        const protocol::BlockHeader&, ::ranges::input_range auto&&, const ledger::LedgerConfig&)
    {
        co_return {};
    }
};

struct BloomScheduler
{
    template <class Storage, class Executor>
    task::Task<std::vector<protocol::TransactionReceipt::Ptr>> executeBlock(Storage&, Executor&,
        const protocol::BlockHeader&, ::ranges::input_range auto&&, const ledger::LedgerConfig&)
    {
        Bloom bloom1{};
        bloom1[255] = static_cast<bcos::byte>(0x01);
        Bloom bloom2{};
        bloom2[255] = static_cast<bcos::byte>(0x02);

        Keccak256 hasher;
        auto receipt1 = std::make_shared<bcostars::protocol::TransactionReceiptImpl>();
        receipt1->setLogsBloom({bloom1.data(), bloom1.size()});
        receipt1->calculateHash(hasher);
        auto receipt2 = std::make_shared<bcostars::protocol::TransactionReceiptImpl>();
        receipt2->setLogsBloom({bloom2.data(), bloom2.size()});
        receipt2->calculateHash(hasher);

        co_return std::vector<protocol::TransactionReceipt::Ptr>{receipt1, receipt2};
    }
};

bcos::protocol::BlockFactory::Ptr testBlockFactory()
{
    static auto blockFactory =
        bcos::test::createBlockFactory(bcos::test::createNormalCryptoSuite());
    return blockFactory;
}

/// EngineServiceImpl stores the executor and scheduler BY REFERENCE (std::ref, see its
/// constructor), so a function-local stub would dangle the moment a factory returns. The
/// stubs are stateless, which is why that went unnoticed; keep one long-lived instance of
/// each so it stays true no matter what a stub grows later.
StubExecutor& sharedStubExecutor()
{
    static StubExecutor executor;
    return executor;
}

StubScheduler& sharedStubScheduler()
{
    static StubScheduler scheduler;
    return scheduler;
}

using BloomEngineServiceImpl =
    EngineServiceImpl<MemPoolImpl, RealGlobalStateStorage, StubExecutor, BloomScheduler>;

BloomEngineServiceImpl makeBloomEngineServiceImpl(
    MemPoolImpl& memPool, RealGlobalStateStorage& storage, BloomScheduler& scheduler)
{
    return BloomEngineServiceImpl(
        memPool, storage, sharedStubExecutor(), scheduler, testBlockFactory());
}

using TestEngineServiceImpl =
    EngineServiceImpl<MemPoolImpl, RealGlobalStateStorage, StubExecutor, StubScheduler>;

TestEngineServiceImpl makeEngineServiceImpl(MemPoolImpl& memPool, RealGlobalStateStorage& storage)
{
    return TestEngineServiceImpl(
        memPool, storage, sharedStubExecutor(), sharedStubScheduler(), testBlockFactory());
}

/// The production ledger with its one state-storage dependency short-circuited:
/// Ledger::asyncPrewriteBlock self-calls asyncGetTotalTransactionCount, which opens
/// SYS_CURRENT_STATE and fails the whole prewrite when that table is unknown
/// (Ledger.cpp:396 -> :1051). Everything asserted below — the
/// SYS_NUMBER_2_BLOCK_HEADER row — is written by the real header encode path above
/// that call, so this keeps the commit exercising production code rather than a fake.
class CommitLedger : public bcos::ledger::Ledger
{
public:
    using bcos::ledger::Ledger::Ledger;
    void asyncGetTotalTransactionCount(
        std::function<void(bcos::Error::Ptr, int64_t, int64_t, bcos::protocol::BlockNumber)>
            callback) override
    {
        callback(nullptr, 0, 0, 0);
    }
};

TestEngineServiceImpl makeCommittingEngineServiceImpl(MemPoolImpl& memPool,
    RealGlobalStateStorage& storage, bcos::ledger::LedgerInterface::Ptr ledger)
{
    return TestEngineServiceImpl(memPool, storage, sharedStubExecutor(), sharedStubScheduler(),
        testBlockFactory(), std::move(ledger));
}

/// Reads the block header the commit persisted, the way eth_getBlockByNumber reaches it
/// (SYS_NUMBER_2_BLOCK_HEADER keyed by the decimal block number; decode pattern from
/// bcos-ledger/bcos-ledger/LedgerMethods.h:204-215).
bcos::protocol::BlockHeader::Ptr readPersistedHeader(
    RealGlobalStateBackendStorage& backendStorage, bcos::protocol::BlockNumber blockNumber)
{
    auto entry = task::syncWait(bcos::storage2::readOne(
        backendStorage, bcos::executor_v1::StateKey{ledger::SYS_NUMBER_2_BLOCK_HEADER,
                            boost::lexical_cast<std::string>(blockNumber)}));
    if (!entry)
    {
        return nullptr;
    }
    auto field = entry->get();
    return testBlockFactory()->blockHeaderFactory()->createBlockHeader(
        bcos::bytesConstRef(reinterpret_cast<const bcos::byte*>(field.data()), field.size()));
}

ForkchoiceState makeForkchoiceState()
{
    return {h256("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"),
        h256("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"),
        h256("cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc")};
}

PayloadAttributes makePayloadAttributesV2()
{
    PayloadAttributes payloadAttributes;
    payloadAttributes.timestamp = c_timestamp;
    payloadAttributes.prevRandao =
        h256("1111111111111111111111111111111111111111111111111111111111111111");
    payloadAttributes.suggestedFeeRecipient = Address("1234567890abcdef1234567890abcdef12345678");
    // The withdrawals list is empty: finalizeEthBlockHeader commits the empty-trie root as
    // a placeholder, and validatePayloadAttributes rejects a NON-empty list paired with it
    // (the real withdrawals trie root is not computed yet).
    payloadAttributes.withdrawals = std::vector<WithdrawalV1>{};
    return payloadAttributes;
}

PayloadAttributes makePayloadAttributesV3()
{
    auto payloadAttributes = makePayloadAttributesV2();
    payloadAttributes.parentBeaconBlockRoot =
        h256("2222222222222222222222222222222222222222222222222222222222222222");
    return payloadAttributes;
}

/// Attributes op-node actually sends on a Karst chain: same as V3 except the withdrawals
/// operation list is empty, which Isthmus requires of the resulting ExecutionPayloadV4
/// (op-geth beacon/engine/types.go:324-326).
PayloadAttributes makeKarstPayloadAttributes()
{
    auto payloadAttributes = makePayloadAttributesV3();
    payloadAttributes.withdrawals = std::vector<WithdrawalV1>{};
    return payloadAttributes;
}

NewPayloadRequest makeNewPayloadRequestV3(const ExecutionPayload& executionPayload)
{
    NewPayloadRequest request;
    request.executionPayload = executionPayload;
    // expectedBlobVersionedHashes stays empty: L2 forbids blob transactions, so a real CL
    // never sends any and a non-empty list is INVALID from V3 up
    // (new_payload_v3_rejects_blob_versioned_hashes below).
    // Deliberately different from makePayloadAttributesV3()'s beacon root (0x2222...):
    // tests must be able to tell whether newPayload really overwrites the cached value
    // with the request's, not just re-reads what the attributes stored.
    request.parentBeaconBlockRoot =
        h256("5555555555555555555555555555555555555555555555555555555555555555");
    return request;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(EngineServiceTest)

BOOST_AUTO_TEST_CASE(exchange_capabilities_returns_supported_methods)
{
    MemPoolImpl memPool;
    RealGlobalStateStorageFixture globalStateStorageFixture;
    auto engineService = makeEngineServiceImpl(memPool, globalStateStorageFixture.storage);

    auto capabilities = task::syncWait(
        engineService.exchangeCapabilities({"engine_forkchoiceUpdatedV1", "unknown_method"}));

    // Everything implemented, not narrowed to the active fork (op-geth advertises its
    // whole method set and lets the CL pick). The Karst triple joins the pre-Karst
    // versions rather than replacing them.
    BOOST_CHECK_EQUAL(capabilities.size(), 13);
    auto contains = [&](std::string_view name) {
        return std::find(capabilities.begin(), capabilities.end(), name) != capabilities.end();
    };
    BOOST_CHECK(contains("engine_exchangeCapabilities"));
    BOOST_CHECK(contains("engine_forkchoiceUpdatedV1"));
    BOOST_CHECK(contains("engine_forkchoiceUpdatedV2"));
    BOOST_CHECK(contains("engine_forkchoiceUpdatedV3"));
    BOOST_CHECK(contains("engine_getPayloadV1"));
    BOOST_CHECK(contains("engine_getPayloadV2"));
    BOOST_CHECK(contains("engine_getPayloadV3"));
    BOOST_CHECK(contains("engine_getPayloadV4"));
    BOOST_CHECK(contains("engine_getPayloadV5"));
    BOOST_CHECK(contains("engine_newPayloadV1"));
    BOOST_CHECK(contains("engine_newPayloadV2"));
    BOOST_CHECK(contains("engine_newPayloadV3"));
    BOOST_CHECK(contains("engine_newPayloadV4"));
    // The one genuinely unimplemented version (the forkchoice window tops out at V3), so
    // not advertised; the endpoint answers -38005.
    BOOST_CHECK(!contains("engine_forkchoiceUpdatedV4"));
}

BOOST_AUTO_TEST_CASE(forkchoice_with_payload_attributes_builds_retrievable_payload)
{
    MemPoolImpl memPool;
    // A SHANGHAI chain: the V2 forkchoiceUpdated attribute shape (withdrawals, no blob
    // fields) matches this era; on the CANCUN default fixture the same request is rightly
    // rejected for lacking parentBeaconBlockRoot.
    RealGlobalStateStorageFixture globalStateStorageFixture(EVMC_SHANGHAI);
    auto forkchoiceState = makeForkchoiceState();
    setForkchoiceBlockNumbers(globalStateStorageFixture, forkchoiceState, c_initialBlockNumber,
        c_initialBlockNumber, c_initialBlockNumber);
    std::string sender("aaaaaaaaaaaaaaaaaaaa", 20);
    auto tx = makeTx(sender, 0);
    memPool.add(std::vector{tx});
    globalStateStorageFixture.setNonce(sender, "1");
    auto payloadAttributes = makePayloadAttributesV2();

    auto engineService = makeEngineServiceImpl(memPool, globalStateStorageFixture.storage);

    auto result =
        task::syncWait(engineService.updateForkchoice(forkchoiceState, &payloadAttributes, 2));

    BOOST_CHECK_EQUAL(static_cast<int>(result.payloadStatus.status),
        static_cast<int>(PayloadValidationStatus::Valid));
    BOOST_REQUIRE(result.payloadId.has_value());

    auto payload = task::syncWait(engineService.getPayload(*result.payloadId, 2));
    BOOST_CHECK_EQUAL(payload->executionPayload.parentHash, forkchoiceState.headBlockHash);
    BOOST_CHECK_EQUAL(payload->executionPayload.blockNumber, c_initialBlockNumber + 1);
    BOOST_CHECK_EQUAL(payload->executionPayload.timestamp, c_timestamp);
    BOOST_CHECK(payload->executionPayload.withdrawals.has_value());
    BOOST_CHECK(!payload->executionPayload.blobGasUsed.has_value());
    BOOST_CHECK(payload->executionPayload.transactions.empty());
    auto fetched = memPool.get(std::vector{tx->hash()});
    BOOST_CHECK_EQUAL(fetched.size(), 1);
    BOOST_CHECK(!fetched[0]);
}

// On a CANCUN chain the header-fork era outruns the V2 attribute shape, so a V2
// forkchoiceUpdated is refused with UnsupportedFork (the RPC layer maps it to -38005,
// matching geth's answer for the same CL/chain mismatch). The same attributes on a
// SHANGHAI chain build (see forkchoice_with_payload_attributes_builds_retrievable_payload).
BOOST_AUTO_TEST_CASE(forkchoice_v2_rejected_on_cancun_chain)
{
    MemPoolImpl memPool;
    RealGlobalStateStorageFixture globalStateStorageFixture;  // default CANCUN
    auto forkchoiceState = makeForkchoiceState();
    setForkchoiceBlockNumbers(globalStateStorageFixture, forkchoiceState, c_initialBlockNumber,
        c_initialBlockNumber, c_initialBlockNumber);
    auto engineService = makeEngineServiceImpl(memPool, globalStateStorageFixture.storage);

    auto payloadAttributes = makePayloadAttributesV2();
    // Pin the exact gate, not just the exception type: the same UnsupportedFork type is
    // thrown by the V2-attr and the missing-revision gates with different what() text.
    BOOST_CHECK_EXCEPTION(
        task::syncWait(engineService.updateForkchoice(forkchoiceState, &payloadAttributes, 2)),
        UnsupportedFork, [](const UnsupportedFork& e) {
            return std::string(e.what()).find("requires the V3 payload attributes") !=
                   std::string::npos;
        });
}

// The reverse direction of the fork/method-version gate: a V3 forkchoiceUpdated on a
// SHANGHAI chain would build a payload whose required blob pair is absent (the payload
// fields are filled by the chain-derived forkVersion, but getPayloadV3's response shape
// requires them). Reject with the same UnsupportedFork as the older-FCU case.
BOOST_AUTO_TEST_CASE(forkchoice_v3_rejected_on_shanghai_chain)
{
    MemPoolImpl memPool;
    RealGlobalStateStorageFixture globalStateStorageFixture(EVMC_SHANGHAI);
    auto forkchoiceState = makeForkchoiceState();
    setForkchoiceBlockNumbers(globalStateStorageFixture, forkchoiceState, c_initialBlockNumber,
        c_initialBlockNumber, c_initialBlockNumber);
    auto engineService = makeEngineServiceImpl(memPool, globalStateStorageFixture.storage);

    auto payloadAttributes = makePayloadAttributesV3();
    BOOST_CHECK_EXCEPTION(
        task::syncWait(engineService.updateForkchoice(forkchoiceState, &payloadAttributes, 3)),
        UnsupportedFork, [](const UnsupportedFork& e) {
            return std::string(e.what()).find("requires a CANCUN-or-later chain fork") !=
                   std::string::npos;
        });
}

// A v2 chain with no on-chain EVM revision must fail closed: hashing under an assumed fork
// (the compile-time default, OSAKA -> PRAGUE) would stamp requestsHash into every RLP hash.
// The gate throws UnsupportedFork with a distinguishing message; pin it.
BOOST_AUTO_TEST_CASE(forkchoice_rejected_when_evm_revision_missing)
{
    MemPoolImpl memPool;
    // executor_version=2 but NO SYSTEM_KEY_EVMC_REVISION row.
    RealGlobalStateStorageFixture globalStateStorageFixture(EVMC_CANCUN, /*writeEvmcRevision=*/false);
    auto forkchoiceState = makeForkchoiceState();
    setForkchoiceBlockNumbers(globalStateStorageFixture, forkchoiceState, c_initialBlockNumber,
        c_initialBlockNumber, c_initialBlockNumber);
    auto engineService = makeEngineServiceImpl(memPool, globalStateStorageFixture.storage);

    auto payloadAttributes = makePayloadAttributesV3();
    BOOST_CHECK_EXCEPTION(
        task::syncWait(engineService.updateForkchoice(forkchoiceState, &payloadAttributes, 3)),
        UnsupportedFork, [](const UnsupportedFork& e) {
            return std::string(e.what()).find("no on-chain EVM revision") != std::string::npos;
        });
}

// A non-empty withdrawals list is rejected at the ATTRIBUTE layer (payloadStatus INVALID,
// not UnsupportedFork): the withdrawals trie root is not computed, so the hashed header
// would commit the empty-trie root while geth derives DeriveSha(withdrawals) — a mismatch
// every peer would reject (T5).
BOOST_AUTO_TEST_CASE(forkchoice_rejects_non_empty_withdrawals)
{
    MemPoolImpl memPool;
    RealGlobalStateStorageFixture globalStateStorageFixture;
    auto forkchoiceState = makeForkchoiceState();
    setForkchoiceBlockNumbers(globalStateStorageFixture, forkchoiceState, c_initialBlockNumber,
        c_initialBlockNumber, c_initialBlockNumber);
    auto engineService = makeEngineServiceImpl(memPool, globalStateStorageFixture.storage);

    auto payloadAttributes = makePayloadAttributesV3();
    payloadAttributes.withdrawals = std::vector<WithdrawalV1>{
        WithdrawalV1{.index = 1, .validatorIndex = 2, .amount = 3, .address = Address{}}};
    auto result = task::syncWait(
        engineService.updateForkchoice(forkchoiceState, &payloadAttributes, 3));
    BOOST_CHECK_EQUAL(static_cast<int>(result.payloadStatus.status),
        static_cast<int>(PayloadValidationStatus::Invalid));
    BOOST_REQUIRE(result.payloadStatus.validationError.has_value());
    BOOST_CHECK_NE(result.payloadStatus.validationError->find("non-empty withdrawals"),
        std::string::npos);
}

BOOST_AUTO_TEST_CASE(forkchoice_v3_tracks_safe_and_finalized_block_numbers)
{
    MemPoolImpl memPool;
    RealGlobalStateStorageFixture globalStateStorageFixture;
    auto engineService = makeEngineServiceImpl(memPool, globalStateStorageFixture.storage);

    auto initialForkchoice = makeForkchoiceState();
    setForkchoiceBlockNumbers(globalStateStorageFixture, initialForkchoice,
        c_trackedInitialBlockNumber, c_trackedInitialBlockNumber, c_trackedInitialBlockNumber);
    auto payloadAttributes = makePayloadAttributesV3();
    auto initialResult =
        task::syncWait(engineService.updateForkchoice(initialForkchoice, &payloadAttributes, 3));
    BOOST_REQUIRE(initialResult.payloadId.has_value());
    auto builtPayload = task::syncWait(engineService.getPayload(*initialResult.payloadId, 3));
    globalStateStorageFixture.setBlockNumber(
        builtPayload->executionPayload.blockHash, c_trackedNextBlockNumber);

    auto request = makeNewPayloadRequestV3(builtPayload->executionPayload);
    auto newPayloadStatus = task::syncWait(engineService.newPayload(request, 3));
    BOOST_CHECK_EQUAL(static_cast<int>(newPayloadStatus.status),
        static_cast<int>(PayloadValidationStatus::Valid));

    ForkchoiceState trackedForkchoice{builtPayload->executionPayload.blockHash,
        builtPayload->executionPayload.blockHash, builtPayload->executionPayload.blockHash};
    auto trackedResult =
        task::syncWait(engineService.updateForkchoice(trackedForkchoice, nullptr, 3));
    BOOST_CHECK_EQUAL(static_cast<int>(trackedResult.payloadStatus.status),
        static_cast<int>(PayloadValidationStatus::Valid));

    auto safeBlockNumber = engineService.getSafeBlockNumber();
    auto finalizedBlockNumber = engineService.getFinalizedBlockNumber();
    BOOST_REQUIRE(safeBlockNumber.has_value());
    BOOST_REQUIRE(finalizedBlockNumber.has_value());
    BOOST_CHECK_EQUAL(*safeBlockNumber, c_trackedNextBlockNumber);
    BOOST_CHECK_EQUAL(*finalizedBlockNumber, c_trackedNextBlockNumber);
}

BOOST_AUTO_TEST_CASE(new_payload_rejects_missing_required_v3_fields)
{
    MemPoolImpl memPool;
    RealGlobalStateStorageFixture globalStateStorageFixture;
    auto forkchoiceState = makeForkchoiceState();
    setForkchoiceBlockNumbers(globalStateStorageFixture, forkchoiceState, c_validationBlockNumber,
        c_validationBlockNumber, c_validationBlockNumber);
    auto payloadAttributes = makePayloadAttributesV3();
    auto engineService = makeEngineServiceImpl(memPool, globalStateStorageFixture.storage);

    auto result =
        task::syncWait(engineService.updateForkchoice(forkchoiceState, &payloadAttributes, 3));
    BOOST_REQUIRE(result.payloadId.has_value());

    auto payload = task::syncWait(engineService.getPayload(*result.payloadId, 3));
    NewPayloadRequest request;
    request.executionPayload = payload->executionPayload;

    auto status = task::syncWait(engineService.newPayload(request, 3));

    BOOST_CHECK_EQUAL(
        static_cast<int>(status.status), static_cast<int>(PayloadValidationStatus::Invalid));
    BOOST_REQUIRE(status.validationError.has_value());
    BOOST_CHECK_NE(status.validationError->find("parentBeaconBlockRoot"), std::string::npos);
}

BOOST_AUTO_TEST_CASE(forkchoice_returns_syncing_when_head_block_number_missing)
{
    MemPoolImpl memPool;
    RealGlobalStateStorageFixture globalStateStorageFixture;
    auto forkchoiceState = makeForkchoiceState();
    globalStateStorageFixture.setBlockNumber(
        forkchoiceState.safeBlockHash, c_validationBlockNumber);
    globalStateStorageFixture.setBlockNumber(
        forkchoiceState.finalizedBlockHash, c_validationBlockNumber);
    std::string sender("bbbbbbbbbbbbbbbbbbbb", 20);
    auto tx = makeTx(sender, 0);
    memPool.add(std::vector{tx});
    globalStateStorageFixture.setNonce(sender, "1");
    auto engineService = makeEngineServiceImpl(memPool, globalStateStorageFixture.storage);

    auto result = task::syncWait(engineService.updateForkchoice(forkchoiceState, nullptr, 3));

    BOOST_CHECK_EQUAL(static_cast<int>(result.payloadStatus.status),
        static_cast<int>(PayloadValidationStatus::Syncing));
    BOOST_CHECK(!result.payloadId.has_value());
    auto fetched = memPool.get(std::vector{tx->hash()});
    BOOST_CHECK_EQUAL(fetched.size(), 1);
    BOOST_CHECK(fetched[0]);
}

BOOST_AUTO_TEST_CASE(forkchoice_returns_syncing_when_safe_block_number_missing)
{
    MemPoolImpl memPool;
    RealGlobalStateStorageFixture globalStateStorageFixture;
    auto forkchoiceState = makeForkchoiceState();
    globalStateStorageFixture.setBlockNumber(
        forkchoiceState.headBlockHash, c_validationBlockNumber);
    globalStateStorageFixture.setBlockNumber(
        forkchoiceState.finalizedBlockHash, c_validationBlockNumber);
    auto engineService = makeEngineServiceImpl(memPool, globalStateStorageFixture.storage);

    auto result = task::syncWait(engineService.updateForkchoice(forkchoiceState, nullptr, 3));

    BOOST_CHECK_EQUAL(static_cast<int>(result.payloadStatus.status),
        static_cast<int>(PayloadValidationStatus::Syncing));
    BOOST_CHECK(!result.payloadId.has_value());
}

BOOST_AUTO_TEST_CASE(forkchoice_rejects_non_sequential_head_block_number)
{
    MemPoolImpl memPool;
    RealGlobalStateStorageFixture globalStateStorageFixture;
    auto engineService = makeEngineServiceImpl(memPool, globalStateStorageFixture.storage);

    auto initialForkchoice = makeForkchoiceState();
    setForkchoiceBlockNumbers(globalStateStorageFixture, initialForkchoice, c_reorgStartBlockNumber,
        c_reorgStartBlockNumber, c_reorgStartBlockNumber);
    auto initialResult =
        task::syncWait(engineService.updateForkchoice(initialForkchoice, nullptr, 3));
    BOOST_CHECK_EQUAL(static_cast<int>(initialResult.payloadStatus.status),
        static_cast<int>(PayloadValidationStatus::Valid));

    ForkchoiceState reorgForkchoice{
        h256("dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd"),
        h256("dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd"),
        h256("dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd")};
    setForkchoiceBlockNumbers(globalStateStorageFixture, reorgForkchoice, c_reorgTargetBlockNumber,
        c_reorgTargetBlockNumber, c_reorgTargetBlockNumber);

    BOOST_CHECK_THROW(task::syncWait(engineService.updateForkchoice(reorgForkchoice, nullptr, 3)),
        bcos::engine::InvalidForkchoiceState);
}

BOOST_AUTO_TEST_CASE(forkchoice_rejects_safe_block_number_above_head)
{
    MemPoolImpl memPool;
    RealGlobalStateStorageFixture globalStateStorageFixture;
    auto engineService = makeEngineServiceImpl(memPool, globalStateStorageFixture.storage);

    ForkchoiceState forkchoiceState{
        h256("eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee"),
        h256("ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"),
        h256("0000000000000000000000000000000000000000000000000000000000000011")};
    globalStateStorageFixture.setBlockNumber(
        forkchoiceState.headBlockHash, c_headOrderingBlockNumber);
    globalStateStorageFixture.setBlockNumber(
        forkchoiceState.safeBlockHash, c_safeOrderingBlockNumber);
    globalStateStorageFixture.setBlockNumber(
        forkchoiceState.finalizedBlockHash, c_headOrderingBlockNumber);

    BOOST_CHECK_THROW(task::syncWait(engineService.updateForkchoice(forkchoiceState, nullptr, 3)),
        bcos::engine::InvalidForkchoiceState);
}

BOOST_AUTO_TEST_CASE(forkchoice_rejects_finalized_block_number_above_safe)
{
    MemPoolImpl memPool;
    RealGlobalStateStorageFixture globalStateStorageFixture;
    auto engineService = makeEngineServiceImpl(memPool, globalStateStorageFixture.storage);

    ForkchoiceState forkchoiceState{
        h256("1212121212121212121212121212121212121212121212121212121212121212"),
        h256("1313131313131313131313131313131313131313131313131313131313131313"),
        h256("1414141414141414141414141414141414141414141414141414141414141414")};
    globalStateStorageFixture.setBlockNumber(
        forkchoiceState.headBlockHash, c_finalizedOrderingBlockNumber);
    globalStateStorageFixture.setBlockNumber(
        forkchoiceState.safeBlockHash, c_headOrderingBlockNumber);
    globalStateStorageFixture.setBlockNumber(
        forkchoiceState.finalizedBlockHash, c_safeOrderingBlockNumber);

    BOOST_CHECK_THROW(task::syncWait(engineService.updateForkchoice(forkchoiceState, nullptr, 3)),
        bcos::engine::InvalidForkchoiceState);
}

BOOST_AUTO_TEST_CASE(forkchoice_ignores_stale_update_after_newer_head_wins)
{
    MemPoolImpl memPool;
    RealGlobalStateStorageFixture globalStateStorageFixture;
    auto engineService = makeEngineServiceImpl(memPool, globalStateStorageFixture.storage);

    ForkchoiceState firstForkchoice{
        h256("1515151515151515151515151515151515151515151515151515151515151515"),
        h256("1515151515151515151515151515151515151515151515151515151515151515"),
        h256("1515151515151515151515151515151515151515151515151515151515151515")};
    ForkchoiceState secondForkchoice{
        h256("1616161616161616161616161616161616161616161616161616161616161616"),
        h256("1616161616161616161616161616161616161616161616161616161616161616"),
        h256("1616161616161616161616161616161616161616161616161616161616161616")};
    ForkchoiceState thirdForkchoice{
        h256("1717171717171717171717171717171717171717171717171717171717171717"),
        h256("1717171717171717171717171717171717171717171717171717171717171717"),
        h256("1717171717171717171717171717171717171717171717171717171717171717")};

    globalStateStorageFixture.setBlockNumber(
        firstForkchoice.headBlockHash, c_staleInitialBlockNumber);
    globalStateStorageFixture.setBlockNumber(
        secondForkchoice.headBlockHash, c_staleNextBlockNumber);
    globalStateStorageFixture.setBlockNumber(
        thirdForkchoice.headBlockHash, c_staleThirdBlockNumber);

    auto firstResult = task::syncWait(engineService.updateForkchoice(firstForkchoice, nullptr, 3));
    BOOST_CHECK_EQUAL(static_cast<int>(firstResult.payloadStatus.status),
        static_cast<int>(PayloadValidationStatus::Valid));

    auto secondResult =
        task::syncWait(engineService.updateForkchoice(secondForkchoice, nullptr, 3));
    BOOST_CHECK_EQUAL(static_cast<int>(secondResult.payloadStatus.status),
        static_cast<int>(PayloadValidationStatus::Valid));

    auto staleResult = task::syncWait(engineService.updateForkchoice(firstForkchoice, nullptr, 3));
    BOOST_CHECK_EQUAL(static_cast<int>(staleResult.payloadStatus.status),
        static_cast<int>(PayloadValidationStatus::Valid));

    auto thirdResult = task::syncWait(engineService.updateForkchoice(thirdForkchoice, nullptr, 3));
    BOOST_CHECK_EQUAL(static_cast<int>(thirdResult.payloadStatus.status),
        static_cast<int>(PayloadValidationStatus::Valid));
}

/// A V3 payload that carries transactions and no blob hashes must be VALIDATED and
/// COMMITTED, not waved through with ACCEPTED. The old branch answered ACCEPTED on exactly
/// this shape: the CL saw its block taken while nothing was stored, so the next
/// forkchoiceUpdated naming that block as head could never succeed. op-geth's only
/// ACCEPTED is the parent-state-missing case (eth/catalyst/api.go:904-907), which here is
/// the SYNCING branch.
BOOST_AUTO_TEST_CASE(new_payload_v3_with_transactions_is_validated_not_accepted)
{
    MemPoolImpl memPool;
    RealGlobalStateStorageFixture globalStateStorageFixture;
    auto forkchoiceState = makeForkchoiceState();
    setForkchoiceBlockNumbers(globalStateStorageFixture, forkchoiceState, c_initialBlockNumber,
        c_initialBlockNumber, c_initialBlockNumber);
    auto engineService = makeEngineServiceImpl(memPool, globalStateStorageFixture.storage);

    auto payloadAttributes = makePayloadAttributesV3();
    auto result =
        task::syncWait(engineService.updateForkchoice(forkchoiceState, &payloadAttributes, 3));
    BOOST_REQUIRE(result.payloadId.has_value());
    auto payload = task::syncWait(engineService.getPayload(*result.payloadId, 3));

    globalStateStorageFixture.setBlockNumber(
        payload->executionPayload.blockHash, c_initialBlockNumber + 1);
    auto request = makeNewPayloadRequestV3(payload->executionPayload);
    request.executionPayload.transactions.push_back(
        {.raw = bytes{0x7e, 0x01, 0x02}, .decoded = nullptr});
    BOOST_REQUIRE(request.expectedBlobVersionedHashes.empty());
    BOOST_REQUIRE(!request.executionPayload.transactions.empty());

    auto status = task::syncWait(engineService.newPayload(request, 3));
    BOOST_CHECK_EQUAL(
        static_cast<int>(status.status), static_cast<int>(PayloadValidationStatus::Valid));

    // Stored, not merely acknowledged: the transactions came back out of the cache.
    auto committed = task::syncWait(engineService.getPayload(*result.payloadId, 3));
    BOOST_CHECK_EQUAL(committed->executionPayload.transactions.size(),
        request.executionPayload.transactions.size());
}

/// Non-empty expectedBlobVersionedHashes is INVALID from V3 up, not ACCEPTED: op-geth
/// compares the list against the payload's own transactions' blob hashes and answers
/// INVALID on a mismatch (beacon/engine/types.go:311-322 -> api.invalid), and an L2
/// payload never carries blob transactions.
BOOST_AUTO_TEST_CASE(new_payload_v3_rejects_blob_versioned_hashes)
{
    MemPoolImpl memPool;
    RealGlobalStateStorageFixture globalStateStorageFixture;
    auto forkchoiceState = makeForkchoiceState();
    setForkchoiceBlockNumbers(globalStateStorageFixture, forkchoiceState, c_initialBlockNumber,
        c_initialBlockNumber, c_initialBlockNumber);
    auto engineService = makeEngineServiceImpl(memPool, globalStateStorageFixture.storage);

    auto payloadAttributes = makePayloadAttributesV3();
    auto result =
        task::syncWait(engineService.updateForkchoice(forkchoiceState, &payloadAttributes, 3));
    BOOST_REQUIRE(result.payloadId.has_value());
    auto payload = task::syncWait(engineService.getPayload(*result.payloadId, 3));

    // An otherwise perfectly valid payload — only the blob-hash list is non-empty.
    auto request = makeNewPayloadRequestV3(payload->executionPayload);
    request.expectedBlobVersionedHashes = {
        h256("3333333333333333333333333333333333333333333333333333333333333333")};

    auto status = task::syncWait(engineService.newPayload(request, 3));
    BOOST_CHECK_EQUAL(
        static_cast<int>(status.status), static_cast<int>(PayloadValidationStatus::Invalid));
    BOOST_REQUIRE(status.validationError.has_value());
    BOOST_CHECK_NE(status.validationError->find("expectedBlobVersionedHashes"), std::string::npos);
}

/// Re-querying with getPayloadV5 a payloadId that was committed through newPayloadV3 must
/// answer the version error (-38005 at the RPC layer), not InternalError. A commit REWRITES
/// the cache entry with the request's payload, and a V3 request carries no withdrawalsRoot
/// — but the entry stays tagged version 3, so it passes the V5 window and used to blow up
/// in serializeExecutionPayload's "withdrawalsRoot missing" check as -32603.
BOOST_AUTO_TEST_CASE(get_payload_v5_rejects_a_v3_committed_entry_without_withdrawals_root)
{
    MemPoolImpl memPool;
    RealGlobalStateStorageFixture globalStateStorageFixture;
    auto forkchoiceState = makeForkchoiceState();
    setForkchoiceBlockNumbers(globalStateStorageFixture, forkchoiceState, c_initialBlockNumber,
        c_initialBlockNumber, c_initialBlockNumber);
    auto engineService = makeEngineServiceImpl(memPool, globalStateStorageFixture.storage);

    auto payloadAttributes = makePayloadAttributesV3();
    auto result =
        task::syncWait(engineService.updateForkchoice(forkchoiceState, &payloadAttributes, 3));
    BOOST_REQUIRE(result.payloadId.has_value());
    auto payload = task::syncWait(engineService.getPayload(*result.payloadId, 3));
    // The build always sets the field; a V3 wire request never does.
    BOOST_REQUIRE(payload->executionPayload.withdrawalsRoot.has_value());

    globalStateStorageFixture.setBlockNumber(
        payload->executionPayload.blockHash, c_initialBlockNumber + 1);
    auto request = makeNewPayloadRequestV3(payload->executionPayload);
    request.executionPayload.withdrawalsRoot = std::nullopt;
    auto status = task::syncWait(engineService.newPayload(request, 3));
    BOOST_CHECK_EQUAL(
        static_cast<int>(status.status), static_cast<int>(PayloadValidationStatus::Valid));

    BOOST_CHECK_EXCEPTION(
        task::syncWait(engineService.getPayload(*result.payloadId, 5)), IncompatiblePayloadVersion,
        [](const IncompatiblePayloadVersion& e) {
            // Pin the REWRITE-path message (the entry was rewritten by the V3 commit and
            // lost its withdrawalsRoot), not just the exception type — a swapped gate that
            // keeps IncompatiblePayloadVersion must still fail (T4).
            return std::string(e.what()).find("Payload does not carry the V4+ response shape") !=
                   std::string::npos;
        });
    // The V3 view of the same entry is still perfectly serviceable.
    auto v3 = task::syncWait(engineService.getPayload(*result.payloadId, 3));
    BOOST_CHECK(v3);
}

BOOST_AUTO_TEST_CASE(payload_carries_parent_beacon_block_root_and_withdrawals_root)
{
    MemPoolImpl memPool;
    RealGlobalStateStorageFixture globalStateStorageFixture;
    auto forkchoiceState = makeForkchoiceState();
    setForkchoiceBlockNumbers(globalStateStorageFixture, forkchoiceState, c_initialBlockNumber,
        c_initialBlockNumber, c_initialBlockNumber);
    auto payloadAttributes = makePayloadAttributesV3();
    auto engineService = makeEngineServiceImpl(memPool, globalStateStorageFixture.storage);

    auto result =
        task::syncWait(engineService.updateForkchoice(forkchoiceState, &payloadAttributes, 3));
    BOOST_REQUIRE(result.payloadId.has_value());

    // buildPayload stored the attributes' beacon root in the payload cache; getPayload
    // must return it. withdrawalsRoot carries the empty-trie root placeholder on
    // SHANGHAI+ chains (B4: required for the getPayloadV5 -> newPayloadV4 round trip) until
    // real-value header wiring lands — and the served value agrees with the hashed header.
    auto payload = task::syncWait(engineService.getPayload(*result.payloadId, 3));
    BOOST_REQUIRE(payload->parentBeaconBlockRoot.has_value());
    BOOST_CHECK_EQUAL(*payload->parentBeaconBlockRoot, *payloadAttributes.parentBeaconBlockRoot);
    BOOST_REQUIRE(payload->executionPayload.withdrawalsRoot.has_value());
    BOOST_CHECK_EQUAL(
        *payload->executionPayload.withdrawalsRoot, bcos::ledger::mpt::emptyRootHash());

    // newPayload carries both fields back in; the committed cache entry keeps them.
    // This exercises the structure layer only: withdrawalsRoot is set directly on the
    // struct, deliberately bypassing the JSON parser, whose V1-V3 dialect ignores the
    // field (it is an ExecutionPayloadV4+/Isthmus field — see EngineProtoAlignB1Test).
    globalStateStorageFixture.setBlockNumber(
        payload->executionPayload.blockHash, c_initialBlockNumber + 1);
    auto request = makeNewPayloadRequestV3(payload->executionPayload);
    auto const withdrawalsRoot =
        h256("4444444444444444444444444444444444444444444444444444444444444444");
    request.executionPayload.withdrawalsRoot = withdrawalsRoot;
    auto status = task::syncWait(engineService.newPayload(request, 3));
    BOOST_CHECK_EQUAL(
        static_cast<int>(status.status), static_cast<int>(PayloadValidationStatus::Valid));

    auto committed = task::syncWait(engineService.getPayload(*result.payloadId, 3));
    BOOST_REQUIRE(committed->parentBeaconBlockRoot.has_value());
    // The request carries a beacon root different from the attributes' — the cache must
    // now hold the request's value, proving newPayload overwrites rather than re-reads.
    BOOST_CHECK_NE(*request.parentBeaconBlockRoot, *payloadAttributes.parentBeaconBlockRoot);
    BOOST_CHECK_EQUAL(*committed->parentBeaconBlockRoot, *request.parentBeaconBlockRoot);
    BOOST_REQUIRE(committed->executionPayload.withdrawalsRoot.has_value());
    BOOST_CHECK_EQUAL(*committed->executionPayload.withdrawalsRoot, withdrawalsRoot);
}

BOOST_AUTO_TEST_CASE(build_payload_reassembles_web3_raw_bytes)
{
    MemPoolImpl memPool;
    // A SHANGHAI chain, matching the V2 forkchoiceUpdated attribute shape used below.
    RealGlobalStateStorageFixture globalStateStorageFixture(EVMC_SHANGHAI);
    auto forkchoiceState = makeForkchoiceState();
    setForkchoiceBlockNumbers(globalStateStorageFixture, forkchoiceState, c_initialBlockNumber,
        c_initialBlockNumber, c_initialBlockNumber);
    std::string sender("dddddddddddddddddddd", 20);
    auto tx = makeWeb3Tx(sender, 0);
    memPool.add(std::vector{tx});
    globalStateStorageFixture.setNonce(sender, "0");
    auto payloadAttributes = makePayloadAttributesV2();
    auto engineService = makeEngineServiceImpl(memPool, globalStateStorageFixture.storage);

    auto result =
        task::syncWait(engineService.updateForkchoice(forkchoiceState, &payloadAttributes, 2));
    BOOST_REQUIRE(result.payloadId.has_value());
    auto payload = task::syncWait(engineService.getPayload(*result.payloadId, 2));
    BOOST_REQUIRE_EQUAL(payload->executionPayload.transactions.size(), 1);
    auto const& engineTx = payload->executionPayload.transactions.front();

    // The wire form is the reassembled signed EIP-2718 raw envelope, not Tars encoding.
    BOOST_CHECK(bcos::engine::dispatchRawTransaction(bcos::ref(engineTx.raw)) ==
                bcos::engine::RawTransactionKind::DynamicFee);
    // keccak256(raw) is the canonical txHash — the exact equality op-node depends on
    // when it recomputes transaction hashes from getPayload's raw bytes.
    BOOST_CHECK_EQUAL(bcos::crypto::keccak256Hash(bcos::ref(engineTx.raw)), tx->hash());
    // Dual model: the decoded executable form is the sealed mempool transaction itself.
    BOOST_CHECK(engineTx.decoded == tx);

    // A native Tars encoding, by contrast, dispatches as Unsupported — pinning why
    // buildPayload excludes native transactions instead of emitting their Tars bytes
    // (such a wire form could never pass newPayload validation).
    bytes tarsEncoded;
    makeTx(sender, 1)->encode(tarsEncoded);
    BOOST_CHECK(bcos::engine::dispatchRawTransaction(bcos::ref(tarsEncoded)) ==
                bcos::engine::RawTransactionKind::Unsupported);
}

BOOST_AUTO_TEST_CASE(build_payload_excludes_native_transactions_full_loop)
{
    MemPoolImpl memPool;
    RealGlobalStateStorageFixture globalStateStorageFixture;
    auto forkchoiceState = makeForkchoiceState();
    setForkchoiceBlockNumbers(globalStateStorageFixture, forkchoiceState, c_initialBlockNumber,
        c_initialBlockNumber, c_initialBlockNumber);
    // One native Tars transaction and one Web3 transaction, both sealable.
    std::string nativeSender("eeeeeeeeeeeeeeeeeeee", 20);
    std::string web3Sender("ffffffffffffffffffff", 20);
    auto nativeTx = makeTx(nativeSender, 0);
    auto web3Tx = makeWeb3Tx(web3Sender, 0);
    memPool.add(std::vector{nativeTx, web3Tx});
    globalStateStorageFixture.setNonce(nativeSender, "0");
    globalStateStorageFixture.setNonce(web3Sender, "0");
    auto payloadAttributes = makePayloadAttributesV3();
    auto engineService = makeEngineServiceImpl(memPool, globalStateStorageFixture.storage);

    auto result =
        task::syncWait(engineService.updateForkchoice(forkchoiceState, &payloadAttributes, 3));
    BOOST_REQUIRE(result.payloadId.has_value());

    // The native transaction is excluded from the OP payload (no EIP-2718 wire form);
    // only the Web3 transaction remains, carried as its genuine raw envelope.
    auto payload = task::syncWait(engineService.getPayload(*result.payloadId, 3));
    BOOST_REQUIRE_EQUAL(payload->executionPayload.transactions.size(), 1);
    BOOST_CHECK(payload->executionPayload.transactions.front().decoded == web3Tx);
    BOOST_CHECK(bcos::engine::dispatchRawTransaction(
                    bcos::ref(payload->executionPayload.transactions.front().raw)) ==
                bcos::engine::RawTransactionKind::DynamicFee);

    // The full loop closes: the payload this service built passes its own newPayload
    // validation (the seal/validate asymmetry the exclusion removes).
    globalStateStorageFixture.setBlockNumber(
        payload->executionPayload.blockHash, c_initialBlockNumber + 1);
    auto request = makeNewPayloadRequestV3(payload->executionPayload);
    auto status = task::syncWait(engineService.newPayload(request, 3));
    BOOST_CHECK_EQUAL(
        static_cast<int>(status.status), static_cast<int>(PayloadValidationStatus::Valid));
}

BOOST_AUTO_TEST_CASE(new_payload_round_trips_deposit_raw_bytes)
{
    MemPoolImpl memPool;
    RealGlobalStateStorageFixture globalStateStorageFixture;
    auto forkchoiceState = makeForkchoiceState();
    setForkchoiceBlockNumbers(globalStateStorageFixture, forkchoiceState, c_initialBlockNumber,
        c_initialBlockNumber, c_initialBlockNumber);
    auto payloadAttributes = makePayloadAttributesV3();
    auto engineService = makeEngineServiceImpl(memPool, globalStateStorageFixture.storage);

    auto result =
        task::syncWait(engineService.updateForkchoice(forkchoiceState, &payloadAttributes, 3));
    BOOST_REQUIRE(result.payloadId.has_value());
    auto payload = task::syncWait(engineService.getPayload(*result.payloadId, 3));

    // dep-1: raw 0x7E deposit bytes. Only the first byte matters to the dispatch table;
    // byte fidelity is asserted on the full vector.
    bytes const depositRaw{0x7e, 0x01, 0x02, 0x03, 0x04, 0x05};
    bytes const legacyRaw{0xc9, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80};
    bytes const typedRaw{0x02, 0xf8, 0x01, 0x02};

    globalStateStorageFixture.setBlockNumber(
        payload->executionPayload.blockHash, c_initialBlockNumber + 1);
    auto request = makeNewPayloadRequestV3(payload->executionPayload);
    request.executionPayload.transactions.push_back({.raw = depositRaw, .decoded = nullptr});
    request.executionPayload.transactions.push_back({.raw = legacyRaw, .decoded = nullptr});
    request.executionPayload.transactions.push_back({.raw = typedRaw, .decoded = nullptr});

    // Deposit / legacy / typed all dispatch as admissible payload transactions.
    auto status = task::syncWait(engineService.newPayload(request, 3));
    BOOST_CHECK_EQUAL(
        static_cast<int>(status.status), static_cast<int>(PayloadValidationStatus::Valid));

    // getPayload returns exactly the bytes newPayload received (dep-1 byte-for-byte).
    auto committed = task::syncWait(engineService.getPayload(*result.payloadId, 3));
    BOOST_REQUIRE_EQUAL(committed->executionPayload.transactions.size(), 3);
    BOOST_CHECK(committed->executionPayload.transactions[0].raw == depositRaw);
    BOOST_CHECK(committed->executionPayload.transactions[1].raw == legacyRaw);
    BOOST_CHECK(committed->executionPayload.transactions[2].raw == typedRaw);
}

BOOST_AUTO_TEST_CASE(new_payload_rejects_blob_and_unknown_transaction_types)
{
    MemPoolImpl memPool;
    RealGlobalStateStorageFixture globalStateStorageFixture;
    auto engineService = makeEngineServiceImpl(memPool, globalStateStorageFixture.storage);

    // A single blob transaction invalidates the whole payload, even next to a valid one.
    NewPayloadRequest blobRequest;
    blobRequest.executionPayload.transactions.push_back(
        {.raw = bytes{0xc9, 0x80, 0x80}, .decoded = nullptr});
    blobRequest.executionPayload.transactions.push_back(
        {.raw = bytes{0x03, 0xaa, 0xbb}, .decoded = nullptr});
    auto blobStatus = task::syncWait(engineService.newPayload(blobRequest, 1));
    BOOST_CHECK_EQUAL(
        static_cast<int>(blobStatus.status), static_cast<int>(PayloadValidationStatus::Invalid));
    BOOST_REQUIRE(blobStatus.validationError.has_value());
    BOOST_CHECK_NE(blobStatus.validationError->find("blob"), std::string::npos);

    // 0x00 is not a valid EIP-2718 type.
    NewPayloadRequest zeroRequest;
    zeroRequest.executionPayload.transactions.push_back(
        {.raw = bytes{0x00, 0x01}, .decoded = nullptr});
    auto zeroStatus = task::syncWait(engineService.newPayload(zeroRequest, 1));
    BOOST_CHECK_EQUAL(
        static_cast<int>(zeroStatus.status), static_cast<int>(PayloadValidationStatus::Invalid));
    BOOST_REQUIRE(zeroStatus.validationError.has_value());
    BOOST_CHECK_NE(zeroStatus.validationError->find("unsupported"), std::string::npos);

    // Unknown reserved type byte.
    NewPayloadRequest unknownRequest;
    unknownRequest.executionPayload.transactions.push_back(
        {.raw = bytes{0x05, 0x01}, .decoded = nullptr});
    auto unknownStatus = task::syncWait(engineService.newPayload(unknownRequest, 1));
    BOOST_CHECK_EQUAL(
        static_cast<int>(unknownStatus.status), static_cast<int>(PayloadValidationStatus::Invalid));

    // Empty raw bytes.
    NewPayloadRequest emptyRequest;
    emptyRequest.executionPayload.transactions.push_back({.raw = bytes{}, .decoded = nullptr});
    auto emptyStatus = task::syncWait(engineService.newPayload(emptyRequest, 1));
    BOOST_CHECK_EQUAL(
        static_cast<int>(emptyStatus.status), static_cast<int>(PayloadValidationStatus::Invalid));
}

BOOST_AUTO_TEST_CASE(forkchoice_attributes_reject_blob_forced_transactions)
{
    MemPoolImpl memPool;
    RealGlobalStateStorageFixture globalStateStorageFixture;
    auto forkchoiceState = makeForkchoiceState();
    setForkchoiceBlockNumbers(globalStateStorageFixture, forkchoiceState, c_initialBlockNumber,
        c_initialBlockNumber, c_initialBlockNumber);
    auto engineService = makeEngineServiceImpl(memPool, globalStateStorageFixture.storage);

    // A blob transaction in the forced transaction list invalidates the whole FCU.
    auto blobAttributes = makePayloadAttributesV3();
    blobAttributes.transactions = std::vector<std::string>{"0x03aabb"};
    auto blobResult =
        task::syncWait(engineService.updateForkchoice(forkchoiceState, &blobAttributes, 3));
    BOOST_CHECK_EQUAL(static_cast<int>(blobResult.payloadStatus.status),
        static_cast<int>(PayloadValidationStatus::Invalid));
    BOOST_REQUIRE(blobResult.payloadStatus.validationError.has_value());
    BOOST_CHECK_NE(blobResult.payloadStatus.validationError->find("blob"), std::string::npos);
    BOOST_CHECK(!blobResult.payloadId.has_value());

    // Non-hex entries are rejected, not decoded.
    auto badHexAttributes = makePayloadAttributesV3();
    badHexAttributes.transactions = std::vector<std::string>{"0xzz"};
    auto badHexResult =
        task::syncWait(engineService.updateForkchoice(forkchoiceState, &badHexAttributes, 3));
    BOOST_CHECK_EQUAL(static_cast<int>(badHexResult.payloadStatus.status),
        static_cast<int>(PayloadValidationStatus::Invalid));

    // A deposit in the forced transaction list is admissible (dep-1 arrives this way)
    // AND actually lands in the built payload, byte-for-byte.
    auto depositAttributes = makePayloadAttributesV3();
    depositAttributes.transactions = std::vector<std::string>{"0x7e010203"};
    auto depositResult =
        task::syncWait(engineService.updateForkchoice(forkchoiceState, &depositAttributes, 3));
    BOOST_CHECK_EQUAL(static_cast<int>(depositResult.payloadStatus.status),
        static_cast<int>(PayloadValidationStatus::Valid));
    BOOST_REQUIRE(depositResult.payloadId.has_value());
    auto depositPayload = task::syncWait(engineService.getPayload(*depositResult.payloadId, 3));
    BOOST_REQUIRE_EQUAL(depositPayload->executionPayload.transactions.size(), 1);
    BOOST_CHECK(depositPayload->executionPayload.transactions.front().raw ==
                (bytes{0x7e, 0x01, 0x02, 0x03}));
}

BOOST_AUTO_TEST_CASE(forced_transactions_enter_payload_first)
{
    MemPoolImpl memPool;
    RealGlobalStateStorageFixture globalStateStorageFixture;
    auto forkchoiceState = makeForkchoiceState();
    setForkchoiceBlockNumbers(globalStateStorageFixture, forkchoiceState, c_initialBlockNumber,
        c_initialBlockNumber, c_initialBlockNumber);
    std::string sender("abababababababababab", 20);
    auto poolTx = makeWeb3Tx(sender, 0);
    memPool.add(std::vector{poolTx});
    globalStateStorageFixture.setNonce(sender, "0");
    auto engineService = makeEngineServiceImpl(memPool, globalStateStorageFixture.storage);

    // Two forced transactions (dep-1 first) plus one mempool transaction:
    // payload order = forced list order, then pool transactions.
    auto attributes = makePayloadAttributesV3();
    attributes.transactions = std::vector<std::string>{"0x7e0102030405", "0x02f8aabb"};
    auto result = task::syncWait(engineService.updateForkchoice(forkchoiceState, &attributes, 3));
    BOOST_REQUIRE(result.payloadId.has_value());

    auto payload = task::syncWait(engineService.getPayload(*result.payloadId, 3));
    BOOST_REQUIRE_EQUAL(payload->executionPayload.transactions.size(), 3);
    // Forced first, in the order the attributes gave them, byte-for-byte.
    BOOST_CHECK(payload->executionPayload.transactions[0].raw ==
                (bytes{0x7e, 0x01, 0x02, 0x03, 0x04, 0x05}));
    BOOST_CHECK(payload->executionPayload.transactions[0].decoded == nullptr);
    BOOST_CHECK(payload->executionPayload.transactions[1].raw == (bytes{0x02, 0xf8, 0xaa, 0xbb}));
    // The mempool transaction follows the forced list.
    BOOST_CHECK(payload->executionPayload.transactions[2].decoded == poolTx);
}

BOOST_AUTO_TEST_CASE(no_tx_pool_true_excludes_mempool_transactions)
{
    MemPoolImpl memPool;
    RealGlobalStateStorageFixture globalStateStorageFixture;
    auto forkchoiceState = makeForkchoiceState();
    setForkchoiceBlockNumbers(globalStateStorageFixture, forkchoiceState, c_initialBlockNumber,
        c_initialBlockNumber, c_initialBlockNumber);
    std::string sender("cdcdcdcdcdcdcdcdcdcd", 20);
    auto poolTx = makeWeb3Tx(sender, 0);
    memPool.add(std::vector{poolTx});
    globalStateStorageFixture.setNonce(sender, "0");
    auto engineService = makeEngineServiceImpl(memPool, globalStateStorageFixture.storage);

    // noTxPool=true with a forced deposit: the payload contains exactly the forced
    // list; the sealable mempool transaction must not appear and stays in the pool.
    auto attributes = makePayloadAttributesV3();
    attributes.noTxPool = true;
    attributes.transactions = std::vector<std::string>{"0x7e010203"};
    auto result = task::syncWait(engineService.updateForkchoice(forkchoiceState, &attributes, 3));
    BOOST_REQUIRE(result.payloadId.has_value());
    auto payload = task::syncWait(engineService.getPayload(*result.payloadId, 3));
    BOOST_REQUIRE_EQUAL(payload->executionPayload.transactions.size(), 1);
    BOOST_CHECK(
        payload->executionPayload.transactions.front().raw == (bytes{0x7e, 0x01, 0x02, 0x03}));
    auto retained = memPool.get(std::vector{poolTx->hash()});
    BOOST_REQUIRE_EQUAL(retained.size(), 1);
    BOOST_CHECK(retained[0]);

    // noTxPool=true with no forced transactions: an empty payload.
    auto emptyAttributes = makePayloadAttributesV3();
    emptyAttributes.noTxPool = true;
    auto emptyResult =
        task::syncWait(engineService.updateForkchoice(forkchoiceState, &emptyAttributes, 3));
    BOOST_REQUIRE(emptyResult.payloadId.has_value());
    auto emptyPayload = task::syncWait(engineService.getPayload(*emptyResult.payloadId, 3));
    BOOST_CHECK(emptyPayload->executionPayload.transactions.empty());
}

BOOST_AUTO_TEST_CASE(build_payload_aggregates_receipt_blooms)
{
    MemPoolImpl memPool;
    // A SHANGHAI chain, matching the V2 forkchoiceUpdated attribute shape used below.
    RealGlobalStateStorageFixture globalStateStorageFixture(EVMC_SHANGHAI);
    auto forkchoiceState = makeForkchoiceState();
    setForkchoiceBlockNumbers(globalStateStorageFixture, forkchoiceState, c_initialBlockNumber,
        c_initialBlockNumber, c_initialBlockNumber);
    std::string sender("cccccccccccccccccccc", 20);
    // Web3-shaped: only transactions with an EIP-2718 wire form enter OP payloads.
    auto tx = makeWeb3Tx(sender, 0);
    memPool.add(std::vector{tx});
    globalStateStorageFixture.setNonce(sender, "0");
    auto payloadAttributes = makePayloadAttributesV2();

    BloomScheduler bloomScheduler;
    auto engineService =
        makeBloomEngineServiceImpl(memPool, globalStateStorageFixture.storage, bloomScheduler);

    auto result =
        task::syncWait(engineService.updateForkchoice(forkchoiceState, &payloadAttributes, 2));

    BOOST_CHECK_EQUAL(static_cast<int>(result.payloadStatus.status),
        static_cast<int>(PayloadValidationStatus::Valid));
    BOOST_REQUIRE(result.payloadId.has_value());

    auto payload = task::syncWait(engineService.getPayload(*result.payloadId, 2));

    // Verify bloom aggregation: bloom1[255]=0x01 | bloom2[255]=0x02 = 0x03
    BOOST_CHECK_EQUAL(static_cast<int>(payload->executionPayload.logsBloom[255]), 0x03);
    // Other bytes remain zero (only the last byte was set in both blooms)
    for (size_t i = 0; i < 255; ++i)
    {
        BOOST_CHECK_EQUAL(static_cast<int>(payload->executionPayload.logsBloom[i]), 0);
    }
}

// ---- B4: Karst method surface (forkchoiceUpdatedV3 -> getPayloadV5 -> newPayloadV4) ----

BOOST_AUTO_TEST_CASE(karst_v3_build_v5_get_v4_commit_round_trip)
{
    MemPoolImpl memPool;
    RealGlobalStateStorageFixture globalStateStorageFixture;
    auto forkchoiceState = makeForkchoiceState();
    setForkchoiceBlockNumbers(globalStateStorageFixture, forkchoiceState, c_initialBlockNumber,
        c_initialBlockNumber, c_initialBlockNumber);
    auto payloadAttributes = makeKarstPayloadAttributes();
    auto engineService = makeEngineServiceImpl(memPool, globalStateStorageFixture.storage);

    auto result =
        task::syncWait(engineService.updateForkchoice(forkchoiceState, &payloadAttributes, 3));
    BOOST_REQUIRE(result.payloadId.has_value());

    // getPayloadV5 retrieves a payload built via forkchoiceUpdatedV3.
    auto payload = task::syncWait(engineService.getPayload(*result.payloadId, 5));
    BOOST_REQUIRE(payload);
    // V5 response shape: executionRequests present-but-empty, blobs bundle three empty
    // arrays, shouldOverrideBuilder=false, Isthmus withdrawalsRoot placeholder present.
    BOOST_REQUIRE(payload->executionRequests.has_value());
    BOOST_CHECK(payload->executionRequests->empty());
    BOOST_REQUIRE(payload->blobsBundle.has_value());
    BOOST_CHECK(payload->blobsBundle->commitments.empty());
    BOOST_CHECK(payload->blobsBundle->proofs.empty());
    BOOST_CHECK(payload->blobsBundle->blobs.empty());
    BOOST_CHECK(!payload->shouldOverrideBuilder);
    BOOST_REQUIRE(payload->executionPayload.withdrawalsRoot.has_value());
    BOOST_REQUIRE(payload->parentBeaconBlockRoot.has_value());

    // newPayloadV4 commits it: echoed beacon root + required-empty lists.
    NewPayloadRequest request;
    request.executionPayload = payload->executionPayload;
    request.parentBeaconBlockRoot = payload->parentBeaconBlockRoot;
    request.executionRequests = std::vector<bytes>{};
    auto status = task::syncWait(engineService.newPayload(request, 4));
    BOOST_CHECK_EQUAL(
        static_cast<int>(status.status), static_cast<int>(PayloadValidationStatus::Valid));
}

// Round-5 op-node breakpoint: a Jovian CL sends eip1559Params + minBaseFee and expects
// the built block to carry the 17-byte Jovian extraData, re-validated when it reads the
// header back (op-core/eip1559/eip1559.go ValidateJovianExtraData). The payload and the
// header the block hash was computed over must agree: buildPayload stamps the same bytes
// on both before calculateHash.
BOOST_AUTO_TEST_CASE(build_payload_stamps_jovian_extra_data_on_payload)
{
    MemPoolImpl memPool;
    RealGlobalStateStorageFixture globalStateStorageFixture;
    auto forkchoiceState = makeForkchoiceState();
    setForkchoiceBlockNumbers(globalStateStorageFixture, forkchoiceState, c_initialBlockNumber,
        c_initialBlockNumber, c_initialBlockNumber);
    auto payloadAttributes = makeKarstPayloadAttributes();
    // What op-node actually sent in round 5: all-zero params (SystemConfig defaults),
    // minBaseFee 0. The EL must translate 0,0 to the Canyon constants (250, 6).
    payloadAttributes.eip1559Params = bytes(8, 0);
    payloadAttributes.minBaseFee = 0;
    auto engineService = makeEngineServiceImpl(memPool, globalStateStorageFixture.storage);

    auto result =
        task::syncWait(engineService.updateForkchoice(forkchoiceState, &payloadAttributes, 3));
    BOOST_REQUIRE(result.payloadId.has_value());

    auto payload = task::syncWait(engineService.getPayload(*result.payloadId, 3));
    BOOST_REQUIRE(payload);
    BOOST_CHECK_EQUAL(toHexStringWithPrefix(payload->executionPayload.extraData),
        "0x01000000fa000000060000000000000000");

    // minBaseFee absent -> Holocene 9-byte form, version byte 0x00.
    auto holoceneAttributes = makeKarstPayloadAttributes();
    holoceneAttributes.eip1559Params = fromHexWithPrefix("0x000000fa00000006");
    auto holoceneResult =
        task::syncWait(engineService.updateForkchoice(forkchoiceState, &holoceneAttributes, 3));
    BOOST_REQUIRE(holoceneResult.payloadId.has_value());
    auto holocenePayload = task::syncWait(engineService.getPayload(*holoceneResult.payloadId, 3));
    BOOST_REQUIRE(holocenePayload);
    BOOST_CHECK_EQUAL(
        toHexStringWithPrefix(holocenePayload->executionPayload.extraData), "0x00000000fa00000006");
}

// The invariant op-node actually depends on is the PERSISTED HEADER's extraData: after
// the CL commits a block it re-reads the header over eth_getBlockByNumber and runs
// ValidateJovianExtraData on it (op-core/eip1559/eip1559.go:167-176, reached from
// op-node/rollup/derive/payload_util.go PayloadToSystemConfig and
// engine_consolidate.go). Asserting only the getPayload response would stay green if
// buildPayload ever stamped the payload but not the header — and op-node would reject
// every block. So commit the payload and read the header back out of storage.
BOOST_AUTO_TEST_CASE(committed_header_carries_the_same_jovian_extra_data_as_the_payload)
{
    MemPoolImpl memPool;
    RealGlobalStateStorageFixture globalStateStorageFixture;
    auto forkchoiceState = makeForkchoiceState();
    setForkchoiceBlockNumbers(globalStateStorageFixture, forkchoiceState, c_initialBlockNumber,
        c_initialBlockNumber, c_initialBlockNumber);
    auto payloadAttributes = makeKarstPayloadAttributes();
    payloadAttributes.eip1559Params = bytes(8, 0);
    payloadAttributes.minBaseFee = 0;

    auto ledger = std::make_shared<CommitLedger>(testBlockFactory(), nullptr, /*blockLimit=*/100);
    auto engineService =
        makeCommittingEngineServiceImpl(memPool, globalStateStorageFixture.storage, ledger);

    auto result =
        task::syncWait(engineService.updateForkchoice(forkchoiceState, &payloadAttributes, 3));
    BOOST_REQUIRE(result.payloadId.has_value());
    auto payload = task::syncWait(engineService.getPayload(*result.payloadId, 3));
    BOOST_REQUIRE(payload);

    globalStateStorageFixture.setBlockNumber(
        payload->executionPayload.blockHash, c_initialBlockNumber + 1);
    auto request = makeNewPayloadRequestV3(payload->executionPayload);
    auto status = task::syncWait(engineService.newPayload(request, 3));
    BOOST_REQUIRE_EQUAL(
        static_cast<int>(status.status), static_cast<int>(PayloadValidationStatus::Valid));

    auto persisted =
        readPersistedHeader(globalStateStorageFixture.backendStorage, c_initialBlockNumber + 1);
    BOOST_REQUIRE(persisted);
    // Byte-for-byte with the payload the CL was handed, and equal to the Jovian vector.
    BOOST_CHECK_EQUAL(toHexStringWithPrefix(persisted->extraData()),
        toHexStringWithPrefix(payload->executionPayload.extraData));
    BOOST_CHECK_EQUAL(
        toHexStringWithPrefix(persisted->extraData()), "0x01000000fa000000060000000000000000");
}

// A CL that alters the extraData of a payload this node built, while keeping the
// blockHash it was handed, must not get VALID: the node would otherwise commit its own
// (different) header and leave the CL believing its version was taken.
BOOST_AUTO_TEST_CASE(new_payload_rejects_altered_extra_data_under_a_built_block_hash)
{
    MemPoolImpl memPool;
    RealGlobalStateStorageFixture globalStateStorageFixture;
    auto forkchoiceState = makeForkchoiceState();
    setForkchoiceBlockNumbers(globalStateStorageFixture, forkchoiceState, c_initialBlockNumber,
        c_initialBlockNumber, c_initialBlockNumber);
    auto payloadAttributes = makeKarstPayloadAttributes();
    payloadAttributes.eip1559Params = bytes(8, 0);
    payloadAttributes.minBaseFee = 0;
    auto engineService = makeEngineServiceImpl(memPool, globalStateStorageFixture.storage);

    auto result =
        task::syncWait(engineService.updateForkchoice(forkchoiceState, &payloadAttributes, 3));
    BOOST_REQUIRE(result.payloadId.has_value());
    auto payload = task::syncWait(engineService.getPayload(*result.payloadId, 3));
    BOOST_REQUIRE(payload);

    globalStateStorageFixture.setBlockNumber(
        payload->executionPayload.blockHash, c_initialBlockNumber + 1);
    auto request = makeNewPayloadRequestV3(payload->executionPayload);
    // Same blockHash, different extraData: a well-formed Jovian shape, so this is the
    // local-build comparison talking, not the shape gate.
    request.executionPayload.extraData = fromHexWithPrefix("0x01000000fa000000060000000000000009");
    auto status = task::syncWait(engineService.newPayload(request, 3));
    BOOST_CHECK_EQUAL(static_cast<int>(status.status),
        static_cast<int>(PayloadValidationStatus::InvalidBlockHash));

    // The untouched payload still commits.
    auto honest = makeNewPayloadRequestV3(payload->executionPayload);
    BOOST_CHECK_EQUAL(static_cast<int>(task::syncWait(engineService.newPayload(honest, 3)).status),
        static_cast<int>(PayloadValidationStatus::Valid));
}

// A malformed extraData shape is rejected on the commit side regardless of whether the
// node built the block, mirroring op-geth's header verification
// (consensus/beacon/consensus.go:240-243 -> eip1559.ValidateOptimismExtraData).
BOOST_AUTO_TEST_CASE(new_payload_rejects_malformed_extra_data_shape)
{
    MemPoolImpl memPool;
    RealGlobalStateStorageFixture globalStateStorageFixture;
    auto forkchoiceState = makeForkchoiceState();
    setForkchoiceBlockNumbers(globalStateStorageFixture, forkchoiceState, c_initialBlockNumber,
        c_initialBlockNumber, c_initialBlockNumber);
    auto payloadAttributes = makeKarstPayloadAttributes();
    auto engineService = makeEngineServiceImpl(memPool, globalStateStorageFixture.storage);

    auto result =
        task::syncWait(engineService.updateForkchoice(forkchoiceState, &payloadAttributes, 3));
    BOOST_REQUIRE(result.payloadId.has_value());
    auto payload = task::syncWait(engineService.getPayload(*result.payloadId, 3));
    BOOST_REQUIRE(payload);

    auto request = makeNewPayloadRequestV3(payload->executionPayload);
    // 17 bytes carrying the Holocene version byte: neither shape accepts it.
    request.executionPayload.extraData = fromHexWithPrefix("0x00000000fa000000060000000000000000");
    auto status = task::syncWait(engineService.newPayload(request, 3));
    BOOST_CHECK_EQUAL(
        static_cast<int>(status.status), static_cast<int>(PayloadValidationStatus::Invalid));
}

BOOST_AUTO_TEST_CASE(get_payload_v5_accepts_only_v3_builds)
{
    MemPoolImpl memPool;
    // A SHANGHAI chain, matching the V2 forkchoiceUpdated attribute shape used below (the
    // CANCUN default fixture would reject the request for lacking parentBeaconBlockRoot).
    RealGlobalStateStorageFixture globalStateStorageFixture(EVMC_SHANGHAI);
    auto forkchoiceState = makeForkchoiceState();
    setForkchoiceBlockNumbers(globalStateStorageFixture, forkchoiceState, c_initialBlockNumber,
        c_initialBlockNumber, c_initialBlockNumber);
    auto payloadAttributes = makePayloadAttributesV2();
    auto engineService = makeEngineServiceImpl(memPool, globalStateStorageFixture.storage);

    // A V2-tagged build on a SHANGHAI chain carries withdrawals but no blob fields or
    // withdrawalsRoot; the V5 service window is keyed on the build's version tag, and
    // op-geth's GetPayloadV5 admits only PayloadV3 builds — it answers UnsupportedFork
    // otherwise; so does this. Serializing a V1/V2-tagged entry in the V5 response shape
    // would fabricate fields the tag does not commit to.
    auto result =
        task::syncWait(engineService.updateForkchoice(forkchoiceState, &payloadAttributes, 2));
    BOOST_REQUIRE(result.payloadId.has_value());
    BOOST_CHECK_EXCEPTION(
        task::syncWait(engineService.getPayload(*result.payloadId, 5)), IncompatiblePayloadVersion,
        [](const IncompatiblePayloadVersion& e) {
            return std::string(e.what()).find("incompatible with requested method version") !=
                   std::string::npos;
        });
    // getPayloadV4 has the same window: op-geth's GetPayloadV4 also admits only
    // PayloadV3 builds, and the V4 response shape needs the same three fields a V2 build
    // does not have.
    BOOST_CHECK_EXCEPTION(
        task::syncWait(engineService.getPayload(*result.payloadId, 4)), IncompatiblePayloadVersion,
        [](const IncompatiblePayloadVersion& e) {
            return std::string(e.what()).find("incompatible with requested method version") !=
                   std::string::npos;
        });
    // The same build is still retrievable through its own method version.
    BOOST_CHECK_NO_THROW(task::syncWait(engineService.getPayload(*result.payloadId, 2)));
}

BOOST_AUTO_TEST_CASE(new_payload_v4_rejects_nonempty_lists_and_missing_fields)
{
    MemPoolImpl memPool;
    RealGlobalStateStorageFixture globalStateStorageFixture;
    auto forkchoiceState = makeForkchoiceState();
    setForkchoiceBlockNumbers(globalStateStorageFixture, forkchoiceState, c_validationBlockNumber,
        c_validationBlockNumber, c_validationBlockNumber);
    auto payloadAttributes = makeKarstPayloadAttributes();
    auto engineService = makeEngineServiceImpl(memPool, globalStateStorageFixture.storage);
    auto result =
        task::syncWait(engineService.updateForkchoice(forkchoiceState, &payloadAttributes, 3));
    BOOST_REQUIRE(result.payloadId.has_value());
    auto payload = task::syncWait(engineService.getPayload(*result.payloadId, 5));

    auto makeRequest = [&] {
        NewPayloadRequest request;
        request.executionPayload = payload->executionPayload;
        request.parentBeaconBlockRoot = payload->parentBeaconBlockRoot;
        request.executionRequests = std::vector<bytes>{};
        return request;
    };
    auto expectInvalid = [&](NewPayloadRequest const& request, std::string_view needle) {
        auto status = task::syncWait(engineService.newPayload(request, 4));
        BOOST_CHECK_EQUAL(
            static_cast<int>(status.status), static_cast<int>(PayloadValidationStatus::Invalid));
        BOOST_REQUIRE(status.validationError.has_value());
        BOOST_CHECK_NE(status.validationError->find(needle), std::string::npos);
    };

    // L2 forbids blob transactions: a V4 request can never expect blob hashes.
    auto blobHashesRequest = makeRequest();
    blobHashesRequest.expectedBlobVersionedHashes = {
        h256("3333333333333333333333333333333333333333333333333333333333333333")};
    expectInvalid(blobHashesRequest, "expectedBlobVersionedHashes");

    // Karst carries no execution-layer requests.
    auto executionRequestsRequest = makeRequest();
    executionRequestsRequest.executionRequests = std::vector<bytes>{bytes{0x01}};
    expectInvalid(executionRequestsRequest, "executionRequests");

    // An absent executionRequests is rejected too, not silently accepted: the RPC layer
    // makes the fourth parameter mandatory, so an in-process caller must not get a laxer
    // contract than the wire (op-geth: "nil executionRequests post-prague").
    auto absentRequestsRequest = makeRequest();
    absentRequestsRequest.executionRequests = std::nullopt;
    expectInvalid(absentRequestsRequest, "executionRequests");

    // Isthmus requires a present-but-EMPTY withdrawals operation list at V4.
    auto nonEmptyWithdrawalsRequest = makeRequest();
    nonEmptyWithdrawalsRequest.executionPayload.withdrawals = std::vector<WithdrawalV1>{
        WithdrawalV1{.index = 1, .validatorIndex = 2, .amount = 3, .address = Address{}}};
    expectInvalid(nonEmptyWithdrawalsRequest, "withdrawals");

    // Isthmus payload shape: withdrawalsRoot is required at V4.
    auto missingRootRequest = makeRequest();
    missingRootRequest.executionPayload.withdrawalsRoot = std::nullopt;
    expectInvalid(missingRootRequest, "withdrawalsRoot");

    // A FOREIGN Isthmus withdrawalsRoot (anything other than withdrawalsRootFor(), i.e.
    // the empty-trie placeholder) is rejected: the hashed header commits this node's own
    // root, so a CL submitting a different value cannot reproduce blockHash (T5).
    auto foreignRootRequest = makeRequest();
    foreignRootRequest.executionPayload.withdrawalsRoot =
        h256("9999999999999999999999999999999999999999999999999999999999999999");
    expectInvalid(foreignRootRequest, "withdrawalsRoot");

    // parentBeaconBlockRoot is required at V3 and later.
    auto missingBeaconRequest = makeRequest();
    missingBeaconRequest.parentBeaconBlockRoot = std::nullopt;
    expectInvalid(missingBeaconRequest, "parentBeaconBlockRoot");
}

BOOST_AUTO_TEST_CASE(per_method_version_windows_and_unknown_payload)
{
    MemPoolImpl memPool;
    RealGlobalStateStorageFixture globalStateStorageFixture;
    auto engineService = makeEngineServiceImpl(memPool, globalStateStorageFixture.storage);

    // Unknown payloadId surfaces as a typed UnknownPayload (RPC maps it to -38001).
    BOOST_CHECK_THROW(
        task::syncWait(engineService.getPayload("0x0000000000000000", 5)), UnknownPayload);

    // Out-of-window versions surface as UnsupportedEngineApiVersion (RPC maps to -38005):
    // forkchoiceUpdated tops out at V3, newPayload at V4, getPayload at V5.
    auto forkchoiceState = makeForkchoiceState();
    BOOST_CHECK_THROW(task::syncWait(engineService.updateForkchoice(forkchoiceState, nullptr, 4)),
        UnsupportedEngineApiVersion);
    NewPayloadRequest request;
    BOOST_CHECK_THROW(
        task::syncWait(engineService.newPayload(request, 5)), UnsupportedEngineApiVersion);
    BOOST_CHECK_THROW(
        task::syncWait(engineService.getPayload("0x01", 6)), UnsupportedEngineApiVersion);
}

/// Build a BlockHeader carrying every field EthBlockHeader::validateHeader requires for a
/// CANCUN header, so finalizeEthBlockHeader's calculateRLPHash succeeds.
static bcos::protocol::BlockHeader::Ptr makeValidCancunHeader(
    bcos::protocol::BlockFactory::Ptr blockFactory, bcos::crypto::HashType parentHash)
{
    auto header = blockFactory->blockHeaderFactory()->createBlockHeader();
    header->setParentInfo(bcos::protocol::ParentInfo{
        .blockNumber = 9, .blockHash = parentHash});
    header->setNumber(10);
    // Internal BlockHeader timestamps are milliseconds: the whole-second value × 1000.
    header->setTimestamp(1700000000 * 1000LL);
    header->setCoinbase(bcos::Address("1234567890abcdef1234567890abcdef12345678"));
    header->setPrevRandao(
        bcos::h256("1111111111111111111111111111111111111111111111111111111111111111"));
    header->setGasLimit(bcos::u256(30000000));
    header->setGasUsed(bcos::u256(21000));
    header->setStateRoot(
        bcos::h256("4444444444444444444444444444444444444444444444444444444444444444"));
    header->setTxsRoot(
        bcos::h256("5555555555555555555555555555555555555555555555555555555555555555"));
    header->setReceiptsRoot(
        bcos::h256("6666666666666666666666666666666666666666666666666666666666666666"));
    bcos::Bloom bloom;
    bloom[0] = 0xab;
    header->setLogsBloom(bcos::bytesConstRef(bloom.data(), bloom.size()));
    return header;
}

BOOST_AUTO_TEST_CASE(ethBlockVersionForMapsEvmRevisions)
{
    using bcos::protocol::EthBlockVersion;
    // Revisions strictly below LONDON (FRONTIER..BERLIN) cannot occur on a PoS/Engine
    // path; they map to the minimal post-merge shape (LONDON).
    BOOST_CHECK_EQUAL(static_cast<int>(bcos::engine::detail::ethBlockVersionFor(EVMC_BERLIN)),
        static_cast<int>(EthBlockVersion::LONDON));
    BOOST_CHECK_EQUAL(static_cast<int>(bcos::engine::detail::ethBlockVersionFor(EVMC_LONDON)),
        static_cast<int>(EthBlockVersion::LONDON));
    BOOST_CHECK_EQUAL(static_cast<int>(bcos::engine::detail::ethBlockVersionFor(EVMC_PARIS)),
        static_cast<int>(EthBlockVersion::LONDON));
    BOOST_CHECK_EQUAL(static_cast<int>(bcos::engine::detail::ethBlockVersionFor(EVMC_SHANGHAI)),
        static_cast<int>(EthBlockVersion::SHANGHAI));
    BOOST_CHECK_EQUAL(static_cast<int>(bcos::engine::detail::ethBlockVersionFor(EVMC_CANCUN)),
        static_cast<int>(EthBlockVersion::CANCUN));
    // PRAGUE and OSAKA both hash as PRAGUE (OSAKA's header RLP adds no new fields).
    BOOST_CHECK_EQUAL(static_cast<int>(bcos::engine::detail::ethBlockVersionFor(EVMC_PRAGUE)),
        static_cast<int>(EthBlockVersion::PRAGUE));
    BOOST_CHECK_EQUAL(static_cast<int>(bcos::engine::detail::ethBlockVersionFor(EVMC_OSAKA)),
        static_cast<int>(EthBlockVersion::PRAGUE));
    // A future EVMC bump (a revision above OSAKA) must fail loudly: hashing under the
    // wrong fork rules would drop fork-gated fields and every peer would reject the
    // block, with no diagnostic.
    BOOST_CHECK_EXCEPTION(bcos::engine::detail::ethBlockVersionFor(EVMC_EXPERIMENTAL),
        UnsupportedFork, [](const UnsupportedFork& e) {
            return std::string(e.what()).find("unsupported EVM revision") != std::string::npos;
        });
}

BOOST_AUTO_TEST_CASE(finalizeEthBlockHeaderFillsEthFieldsAndHash)
{
    static auto blockFactory =
        bcos::test::createBlockFactory(bcos::test::createNormalCryptoSuite());
    auto parentHash = bcos::crypto::HashType(
        "0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    auto header = makeValidCancunHeader(blockFactory, parentHash);

    bcos::engine::ExecutionPayload payload;
    payload.logsBloom = bcos::Bloom{};
    payload.baseFeePerGas = bcos::u256(1000);
    payload.blobGasUsed = bcos::u256(0);
    payload.excessBlobGas = bcos::u256(0);

    auto beaconRoot = bcos::h256(
        "3333333333333333333333333333333333333333333333333333333333333333");
    bcos::engine::detail::finalizeEthBlockHeader(
        *header, payload, beaconRoot, bcos::protocol::EthBlockVersion::CANCUN);

    // The header is marked as an Eth CANCUN header.
    BOOST_CHECK(header->ethBlockVersion() == bcos::protocol::EthBlockVersion::CANCUN);
    // Post-merge constants.
    BOOST_CHECK_EQUAL(header->uncleHash().hexPrefixed(),
        "0x1dcc4de8dec75d7aab85b567b6ccd41ad312451b948a7413f0a142fd40d49347");
    BOOST_CHECK_EQUAL(header->difficulty(), bcos::u256(0));
    BOOST_CHECK_EQUAL(header->nonce(), bcos::h64(0));
    // Base fee taken from the payload.
    BOOST_REQUIRE(header->baseFee().has_value());
    BOOST_CHECK_EQUAL(*header->baseFee(), bcos::u256(1000));
    // SHANGHAI+ withdrawals root is the empty-trie root placeholder.
    BOOST_REQUIRE(header->withdrawalsRoot().has_value());
    BOOST_CHECK_EQUAL(*header->withdrawalsRoot(), bcos::ledger::mpt::emptyRootHash());
    // CANCUN blob fields + beacon root.
    BOOST_REQUIRE(header->blobGasUsed().has_value());
    BOOST_REQUIRE(header->excessBlobGas().has_value());
    BOOST_REQUIRE(header->parentBeaconBlockRoot().has_value());
    BOOST_CHECK_EQUAL(*header->parentBeaconBlockRoot(), beaconRoot);
    // The RLP hash was computed and injected (not a synthetic / zero hash).
    BOOST_CHECK_NE(header->hash(), bcos::crypto::HashType{});
}

BOOST_AUTO_TEST_CASE(finalizeEthBlockHeaderVersionGatesFields)
{
    static auto blockFactory =
        bcos::test::createBlockFactory(bcos::test::createNormalCryptoSuite());
    auto parentHash = bcos::crypto::HashType(
        "0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");

    bcos::engine::ExecutionPayload payload;
    payload.logsBloom = bcos::Bloom{};
    payload.baseFeePerGas = bcos::u256(1000);

    // V1/LONDON: baseFee present, no withdrawalsRoot / blob fields / beacon root.
    {
        auto header = makeValidCancunHeader(blockFactory, parentHash);
        bcos::engine::detail::finalizeEthBlockHeader(
            *header, payload, std::nullopt, bcos::protocol::EthBlockVersion::LONDON);
        BOOST_CHECK(header->ethBlockVersion() == bcos::protocol::EthBlockVersion::LONDON);
        BOOST_REQUIRE(header->baseFee().has_value());
        BOOST_CHECK(!header->withdrawalsRoot().has_value());
        BOOST_CHECK(!header->blobGasUsed().has_value());
        BOOST_CHECK(!header->excessBlobGas().has_value());
        BOOST_CHECK(!header->parentBeaconBlockRoot().has_value());
    }

    // V2/SHANGHAI: + withdrawalsRoot, no blob fields / beacon root.
    {
        auto header = makeValidCancunHeader(blockFactory, parentHash);
        bcos::engine::detail::finalizeEthBlockHeader(
            *header, payload, std::nullopt, bcos::protocol::EthBlockVersion::SHANGHAI);
        BOOST_CHECK(header->ethBlockVersion() == bcos::protocol::EthBlockVersion::SHANGHAI);
        BOOST_REQUIRE(header->withdrawalsRoot().has_value());
        BOOST_CHECK(!header->blobGasUsed().has_value());
        BOOST_CHECK(!header->excessBlobGas().has_value());
        BOOST_CHECK(!header->parentBeaconBlockRoot().has_value());
    }

    // V3/CANCUN: everything present.
    {
        auto header = makeValidCancunHeader(blockFactory, parentHash);
        auto beaconRoot = bcos::h256(
            "3333333333333333333333333333333333333333333333333333333333333333");
        payload.blobGasUsed = bcos::u256(0);
        payload.excessBlobGas = bcos::u256(0);
        bcos::engine::detail::finalizeEthBlockHeader(
            *header, payload, beaconRoot, bcos::protocol::EthBlockVersion::CANCUN);
        BOOST_CHECK(header->ethBlockVersion() == bcos::protocol::EthBlockVersion::CANCUN);
        BOOST_REQUIRE(header->withdrawalsRoot().has_value());
        BOOST_REQUIRE(header->blobGasUsed().has_value());
        BOOST_REQUIRE(header->excessBlobGas().has_value());
        BOOST_REQUIRE(header->parentBeaconBlockRoot().has_value());
    }

    // V4/PRAGUE: everything plus the EIP-7685 empty requests hash — finalize must not throw
    // (regression: PRAGUE previously produced a header that failed validateHeader).
    {
        auto header = makeValidCancunHeader(blockFactory, parentHash);
        auto beaconRoot = bcos::h256(
            "3333333333333333333333333333333333333333333333333333333333333333");
        payload.blobGasUsed = bcos::u256(0);
        payload.excessBlobGas = bcos::u256(0);
        BOOST_CHECK_NO_THROW(bcos::engine::detail::finalizeEthBlockHeader(*header, payload,
            beaconRoot, bcos::protocol::EthBlockVersion::PRAGUE));
        BOOST_CHECK(header->ethBlockVersion() == bcos::protocol::EthBlockVersion::PRAGUE);
        BOOST_REQUIRE(header->requestsHash().has_value());
        BOOST_CHECK_EQUAL(header->requestsHash()->hex(),
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
        BOOST_CHECK_NE(header->hash(), bcos::crypto::HashType{});
    }
}

BOOST_AUTO_TEST_CASE(buildPayloadEmptyBlockInjectsRlpHash)
{
    MemPoolImpl memPool;
    RealGlobalStateStorageFixture globalStateStorageFixture;
    auto forkchoiceState = makeForkchoiceState();
    setForkchoiceBlockNumbers(globalStateStorageFixture, forkchoiceState, c_initialBlockNumber,
        c_initialBlockNumber, c_initialBlockNumber);
    auto engineService = makeEngineServiceImpl(memPool, globalStateStorageFixture.storage);

    // Empty block: no transactions, no mempool seeding.
    auto payloadAttributes = makePayloadAttributesV3();
    auto result =
        task::syncWait(engineService.updateForkchoice(forkchoiceState, &payloadAttributes, 3));
    BOOST_REQUIRE(result.payloadId.has_value());

    auto payload = task::syncWait(engineService.getPayload(*result.payloadId, 3));
    BOOST_REQUIRE(payload);
    // The block hash must be the injected RLP hash — deterministic and not the synthetic
    // placeholder (which would have been derived from just the payloadId).
    auto const& blockHash = payload->executionPayload.blockHash;
    BOOST_CHECK_NE(blockHash, bcos::engine::detail::syntheticHash(*result.payloadId));
    BOOST_CHECK_NE(blockHash, bcos::crypto::HashType{});

    // Strong anchor: recompute keccak256(rlp(header)) from the payload fields via the
    // EthBlockHeader bridge and compare against the block hash. The old anchor (≠ synthetic
    // hash, ≠ zero) stayed green even when finalizeEthBlockHeader hashed a wrong timestamp,
    // so it could not catch the merge-broken unit conversion.
    static auto blockFactory =
        bcos::test::createBlockFactory(bcos::test::createNormalCryptoSuite());
    auto header = blockFactory->blockHeaderFactory()->createBlockHeader();
    auto const& executionPayload = payload->executionPayload;
    header->setParentInfo(bcos::protocol::ParentInfo{
        .blockNumber = static_cast<int64_t>(executionPayload.blockNumber) - 1,
        .blockHash = executionPayload.parentHash});
    header->setNumber(static_cast<int64_t>(executionPayload.blockNumber));
    // Internal BlockHeader milliseconds; the bridge converts to seconds at encode.
    header->setTimestamp(static_cast<int64_t>(executionPayload.timestamp));
    header->setCoinbase(executionPayload.feeRecipient);
    header->setUncleHash(bcos::crypto::HashType(
        "0x1dcc4de8dec75d7aab85b567b6ccd41ad312451b948a7413f0a142fd40d49347"));
    header->setPrevRandao(executionPayload.prevRandao);
    header->setNonce(bcos::h64(0));
    header->setDifficulty(bcos::u256(0));
    header->setGasLimit(executionPayload.gasLimit);
    header->setGasUsed(executionPayload.gasUsed);
    header->setStateRoot(executionPayload.stateRoot);
    header->setReceiptsRoot(bcos::ledger::mpt::emptyRootHash());
    header->setTxsRoot(bcos::ledger::mpt::emptyRootHash());
    header->setLogsBloom(bcos::bytesConstRef(
        executionPayload.logsBloom.data(), executionPayload.logsBloom.size()));
    header->setBaseFee(executionPayload.baseFeePerGas);
    header->setWithdrawalsRoot(bcos::ledger::mpt::emptyRootHash());
    header->setBlobGasUsed(executionPayload.blobGasUsed.value_or(bcos::u256(0)));
    header->setExcessBlobGas(executionPayload.excessBlobGas.value_or(bcos::u256(0)));
    header->setParentBeaconBlockRoot(
        payloadAttributes.parentBeaconBlockRoot.value_or(bcos::h256{}));
    header->setEthBlockVersion(bcos::protocol::EthBlockVersion::CANCUN);

    bcos::protocol::EthBlockHeader ethHeader(*header);
    bcos::bytes rlp;
    ethHeader.rlpEncode(rlp);
    BOOST_CHECK_EQUAL(
        blockHash.hex(), bcos::crypto::keccak256Hash(bcos::ref(rlp)).hex());
}

BOOST_AUTO_TEST_SUITE_END()
