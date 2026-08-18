/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 */

#include "engine/bcos-engine/EngineServiceImpl.h"

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
#include <bcos-mempool/MemPoolImpl.h>
#include <bcos-tars-protocol/protocol/TransactionImpl.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptImpl.h>
#include <bcos-task/Wait.h>
#include <boost/lexical_cast.hpp>
#include <boost/test/unit_test.hpp>
#include <algorithm>

using namespace bcos;
using namespace bcos::engine;

namespace
{
constexpr std::uint64_t c_timestamp = 123456;
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

using BloomEngineServiceImpl =
    EngineServiceImpl<MemPoolImpl, RealGlobalStateStorage, StubExecutor, BloomScheduler>;

BloomEngineServiceImpl makeBloomEngineServiceImpl(
    MemPoolImpl& memPool, RealGlobalStateStorage& storage, BloomScheduler& scheduler)
{
    StubExecutor executor;
    static auto blockFactory =
        bcos::test::createBlockFactory(bcos::test::createNormalCryptoSuite());
    return BloomEngineServiceImpl(memPool, storage, executor, scheduler, blockFactory);
}

using TestEngineServiceImpl =
    EngineServiceImpl<MemPoolImpl, RealGlobalStateStorage, StubExecutor, StubScheduler>;

TestEngineServiceImpl makeEngineServiceImpl(MemPoolImpl& memPool, RealGlobalStateStorage& storage)
{
    StubExecutor executor;
    StubScheduler scheduler;
    static auto blockFactory =
        bcos::test::createBlockFactory(bcos::test::createNormalCryptoSuite());
    return TestEngineServiceImpl(memPool, storage, executor, scheduler, blockFactory);
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
    WithdrawalV1 withdrawal;
    withdrawal.index = 1;
    withdrawal.validatorIndex = 2;
    withdrawal.address = Address("abcdefabcdefabcdefabcdefabcdefabcdefabcd");
    withdrawal.amount = 3;
    payloadAttributes.withdrawals = std::vector<WithdrawalV1>{std::move(withdrawal)};
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
    RealGlobalStateStorageFixture globalStateStorageFixture;
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

    BOOST_CHECK_THROW(
        task::syncWait(engineService.getPayload(*result.payloadId, 5)), IncompatiblePayloadVersion);
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
    // must return it. withdrawalsRoot carries the zero placeholder on V3+ builds (B4:
    // required for the getPayloadV5 -> newPayloadV4 round trip) until real-value header
    // wiring lands.
    auto payload = task::syncWait(engineService.getPayload(*result.payloadId, 3));
    BOOST_REQUIRE(payload->parentBeaconBlockRoot.has_value());
    BOOST_CHECK_EQUAL(*payload->parentBeaconBlockRoot, *payloadAttributes.parentBeaconBlockRoot);
    BOOST_REQUIRE(payload->executionPayload.withdrawalsRoot.has_value());
    BOOST_CHECK_EQUAL(*payload->executionPayload.withdrawalsRoot, h256{});

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
    RealGlobalStateStorageFixture globalStateStorageFixture;
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
    RealGlobalStateStorageFixture globalStateStorageFixture;
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

BOOST_AUTO_TEST_CASE(get_payload_v5_accepts_only_v3_builds)
{
    MemPoolImpl memPool;
    RealGlobalStateStorageFixture globalStateStorageFixture;
    auto forkchoiceState = makeForkchoiceState();
    setForkchoiceBlockNumbers(globalStateStorageFixture, forkchoiceState, c_initialBlockNumber,
        c_initialBlockNumber, c_initialBlockNumber);
    auto payloadAttributes = makePayloadAttributesV2();
    auto engineService = makeEngineServiceImpl(memPool, globalStateStorageFixture.storage);

    // A V2 build carries neither blobGasUsed/excessBlobGas nor withdrawalsRoot, so
    // serializing it in the V5 response shape would fabricate them. op-geth's GetPayloadV5
    // allows only PayloadV3 builds and answers UnsupportedFork otherwise; so does this.
    auto result =
        task::syncWait(engineService.updateForkchoice(forkchoiceState, &payloadAttributes, 2));
    BOOST_REQUIRE(result.payloadId.has_value());
    BOOST_CHECK_THROW(
        task::syncWait(engineService.getPayload(*result.payloadId, 5)), IncompatiblePayloadVersion);
    // getPayloadV4 has the same window: op-geth's GetPayloadV4 also admits only
    // PayloadV3 builds, and the V4 response shape needs the same three fields a V2 build
    // does not have.
    BOOST_CHECK_THROW(
        task::syncWait(engineService.getPayload(*result.payloadId, 4)), IncompatiblePayloadVersion);
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

BOOST_AUTO_TEST_SUITE_END()
