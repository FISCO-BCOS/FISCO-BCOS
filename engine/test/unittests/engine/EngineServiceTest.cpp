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
static protocol::Transaction::Ptr makeWeb3Tx(
    std::string_view senderBytes, uint64_t nonce, uint64_t gasLimit = 21000)
{
    bytes body;
    bcos::codec::rlp::encode(body, static_cast<uint64_t>(1));  // chainId
    bcos::codec::rlp::encode(body, nonce);
    bcos::codec::rlp::encode(body, static_cast<uint64_t>(1));  // maxPriorityFeePerGas
    bcos::codec::rlp::encode(body, static_cast<uint64_t>(1));  // maxFeePerGas
    bcos::codec::rlp::encode(body, gasLimit);
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
    // Transaction::gasLimit() reads the Tars field, not the signing payload — the
    // eth_sendRawTransaction ingress fills both, so keep them in step here too.
    tx->mutableInner().data.gasLimit = static_cast<int64_t>(gasLimit);
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

    /// Seed the SYS_CONFIG tx_gas_limit row getLedgerConfig reads. Without it that read
    /// falls back to "0" (LedgerMethods.h getOrDefault) and buildPayload treats the block
    /// gas budget as unconfigured.
    void setGasLimit(uint64_t gasLimit)
    {
        storage::Entry entry;
        entry.set(bcos::storage::serialize::encode(
            ledger::SystemConfigEntry{std::to_string(gasLimit), 0}));
        task::syncWait(storage2::writeOne(backendStorage,
            bcos::executor_v1::StateKey{ledger::SYS_CONFIG, ledger::SYSTEM_KEY_TX_GAS_LIMIT},
            std::move(entry)));
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

NewPayloadRequest makeNewPayloadRequestV3(const ExecutionPayload& executionPayload)
{
    NewPayloadRequest request;
    request.executionPayload = executionPayload;
    request.expectedBlobVersionedHashes = {
        h256("3333333333333333333333333333333333333333333333333333333333333333")};
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

    BOOST_CHECK_EQUAL(capabilities.size(), 10);
    BOOST_CHECK(std::find(capabilities.begin(), capabilities.end(), "engine_getPayloadV3") !=
                capabilities.end());
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
    // must return it. withdrawalsRoot stays unset until real-value header wiring lands.
    auto payload = task::syncWait(engineService.getPayload(*result.payloadId, 3));
    BOOST_REQUIRE(payload->parentBeaconBlockRoot.has_value());
    BOOST_CHECK_EQUAL(*payload->parentBeaconBlockRoot, *payloadAttributes.parentBeaconBlockRoot);
    BOOST_CHECK(!payload->executionPayload.withdrawalsRoot.has_value());

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

/// A stub executor that additionally provides the OP deposit entry: buildPayload's
/// Step 2c-0 gate (if constexpr requires executeDeposit) compiles the deposit lane
/// in, must call it for raw-only 0x7e entries — and only those — and prepends the
/// returned receipt.
struct DepositRecordingExecutor : StubExecutor
{
    std::vector<bytes> executedDeposits;
    /// gasUsed each executed deposit reports. Raised by the gas-budget tests so the
    /// deposits consume (or overrun) the whole block gas limit.
    u256 depositGasUsed{21000};

    template <class Storage>
    task::Task<protocol::TransactionReceipt::Ptr> executeDeposit(Storage& /*storage*/,
        const protocol::BlockHeader& /*blockHeader*/, bcos::bytesConstRef rawDeposit,
        const ledger::LedgerConfig& /*ledgerConfig*/)
    {
        executedDeposits.emplace_back(rawDeposit.begin(), rawDeposit.end());
        auto inner = std::make_shared<bcostars::TransactionReceipt>();
        inner->data.gasUsed = depositGasUsed.str();
        auto receipt = std::make_shared<bcostars::protocol::TransactionReceiptImpl>(
            [inner]() mutable { return inner.get(); });
        Keccak256 hasher;
        receipt->calculateHash(hasher);
        co_return receipt;
    }
};

BOOST_AUTO_TEST_CASE(build_payload_executes_forced_deposits)
{
    MemPoolImpl memPool;
    RealGlobalStateStorageFixture globalStateStorageFixture;
    auto forkchoiceState = makeForkchoiceState();
    setForkchoiceBlockNumbers(globalStateStorageFixture, forkchoiceState, c_initialBlockNumber,
        c_initialBlockNumber, c_initialBlockNumber);

    // One forced deposit and one forced non-deposit (typed raw): only the deposit
    // reaches executeDeposit; the typed raw entry still has no execution wiring.
    auto attributes = makePayloadAttributesV3();
    attributes.transactions = std::vector<std::string>{"0x7e0102030405", "0x02f8aabb"};

    DepositRecordingExecutor executor;
    StubScheduler scheduler;
    static auto blockFactory =
        bcos::test::createBlockFactory(bcos::test::createNormalCryptoSuite());
    EngineServiceImpl<MemPoolImpl, RealGlobalStateStorage, DepositRecordingExecutor, StubScheduler>
        engineService(
            memPool, globalStateStorageFixture.storage, executor, scheduler, blockFactory);

    auto result = task::syncWait(engineService.updateForkchoice(forkchoiceState, &attributes, 3));
    BOOST_REQUIRE(result.payloadId.has_value());
    auto payload = task::syncWait(engineService.getPayload(*result.payloadId, 3));

    // Exactly the deposit's raw bytes were executed.
    BOOST_REQUIRE_EQUAL(executor.executedDeposits.size(), 1);
    BOOST_CHECK(executor.executedDeposits[0] == (bytes{0x7e, 0x01, 0x02, 0x03, 0x04, 0x05}));
    // The deposit receipt participates in the block totals (StubScheduler
    // contributes no receipts, so the gas total is the deposit's alone).
    BOOST_CHECK_EQUAL(payload->executionPayload.gasUsed, u256(21000));
    // Both forced entries still ride the payload raw, byte-for-byte.
    BOOST_REQUIRE_EQUAL(payload->executionPayload.transactions.size(), 2);
    BOOST_CHECK(payload->executionPayload.transactions[0].raw ==
                (bytes{0x7e, 0x01, 0x02, 0x03, 0x04, 0x05}));
}

/// Mixed block: a deposit that consumes most of the block gas limit and a pool
/// transaction coexist. The deposit's gas enters the block totals (Step 2f sums over
/// deposits AND scheduler receipts), the block stays inside its own limit, and nothing is
/// dropped -- the guard judges the total, it does not ration admission.
BOOST_AUTO_TEST_CASE(build_payload_counts_deposit_gas_in_the_block_totals)
{
    MemPoolImpl memPool;
    RealGlobalStateStorageFixture globalStateStorageFixture;
    auto forkchoiceState = makeForkchoiceState();
    setForkchoiceBlockNumbers(globalStateStorageFixture, forkchoiceState, c_initialBlockNumber,
        c_initialBlockNumber, c_initialBlockNumber);

    constexpr uint64_t c_blockGasLimit = ledger::DEFAULT_GAS_LIMIT;
    globalStateStorageFixture.setGasLimit(c_blockGasLimit);
    std::string sender("9999999999aaaaaaaaaa", 20);
    auto poolTx = makeWeb3Tx(sender, 0, /*gasLimit=*/21000);
    memPool.add(std::vector{poolTx});
    globalStateStorageFixture.setNonce(sender, "0");

    auto attributes = makePayloadAttributesV3();
    attributes.transactions = std::vector<std::string>{"0x7e0102030405"};

    DepositRecordingExecutor executor;
    executor.depositGasUsed = u256(c_blockGasLimit - 1000);
    StubScheduler scheduler;
    static auto blockFactory =
        bcos::test::createBlockFactory(bcos::test::createNormalCryptoSuite());
    EngineServiceImpl<MemPoolImpl, RealGlobalStateStorage, DepositRecordingExecutor, StubScheduler>
        engineService(
            memPool, globalStateStorageFixture.storage, executor, scheduler, blockFactory);

    auto result = task::syncWait(engineService.updateForkchoice(forkchoiceState, &attributes, 3));
    BOOST_REQUIRE(result.payloadId.has_value());
    auto payload = task::syncWait(engineService.getPayload(*result.payloadId, 3));

    // Both entries ride the payload: the forced deposit and the sealed transaction.
    BOOST_REQUIRE_EQUAL(executor.executedDeposits.size(), 1);
    BOOST_REQUIRE_EQUAL(payload->executionPayload.transactions.size(), 2);
    BOOST_CHECK(payload->executionPayload.transactions[0].decoded == nullptr);
    BOOST_CHECK(payload->executionPayload.transactions[1].decoded == poolTx);
    // The deposit's gas is in the block totals, and the block stays legal.
    BOOST_CHECK_EQUAL(payload->executionPayload.gasLimit, c_blockGasLimit);
    BOOST_CHECK_EQUAL(payload->executionPayload.gasUsed, u256(c_blockGasLimit - 1000));
    BOOST_CHECK(payload->executionPayload.gasUsed <= payload->executionPayload.gasLimit);
}

/// Anti-starvation regression. A transaction declaring the ENTIRE block gas limit must not
/// cost any other transaction its slot. This is why buildPayload only judges the block's
/// total and never reserves a transaction's declared gasLimit up front: nothing bounds a
/// declared gasLimit on the way in (EthEndpoint checks signature and chainId, MemPoolImpl
/// hash and nonce), and such a transaction is rejected WITHOUT executing, so its nonce
/// never advances and MemPoolImpl::remove — which prunes strictly by nonce — can never
/// evict it. A reserve-up-front scheme would let one free eth_sendRawTransaction consume
/// the budget of every future block forever.
BOOST_AUTO_TEST_CASE(build_payload_does_not_let_one_huge_gaslimit_starve_other_senders)
{
    MemPoolImpl memPool;
    RealGlobalStateStorageFixture globalStateStorageFixture;
    auto forkchoiceState = makeForkchoiceState();
    setForkchoiceBlockNumbers(globalStateStorageFixture, forkchoiceState, c_initialBlockNumber,
        c_initialBlockNumber, c_initialBlockNumber);
    globalStateStorageFixture.setGasLimit(ledger::DEFAULT_GAS_LIMIT);

    // The hog declares the entire block gas limit; the ordinary transaction is a plain
    // 21000-gas transfer from a DIFFERENT sender.
    std::string hogSender("11111111111111111111", 20);
    std::string plainSender("22222222222222222222", 20);
    auto hogTx = makeWeb3Tx(hogSender, 0, /*gasLimit=*/ledger::DEFAULT_GAS_LIMIT);
    auto plainTx = makeWeb3Tx(plainSender, 0, /*gasLimit=*/21000);
    memPool.add(std::vector{hogTx, plainTx});
    globalStateStorageFixture.setNonce(hogSender, "0");
    globalStateStorageFixture.setNonce(plainSender, "0");

    auto attributes = makePayloadAttributesV3();
    auto engineService = makeEngineServiceImpl(memPool, globalStateStorageFixture.storage);

    auto result = task::syncWait(engineService.updateForkchoice(forkchoiceState, &attributes, 3));
    BOOST_REQUIRE(result.payloadId.has_value());
    auto payload = task::syncWait(engineService.getPayload(*result.payloadId, 3));

    // BOTH transactions ride the payload. The hog's declared 3e9 gas costs the other
    // sender nothing, because no budget is reserved from a declaration — only the
    // resulting total is judged. seal() visits senders through a hashed index, so the
    // order is not fixed; assert membership rather than position.
    auto const& transactions = payload->executionPayload.transactions;
    BOOST_REQUIRE_EQUAL(transactions.size(), 2);
    bool hogPresent = false;
    bool plainPresent = false;
    for (auto const& engineTx : transactions)
    {
        hogPresent = hogPresent || engineTx.decoded == hogTx;
        plainPresent = plainPresent || engineTx.decoded == plainTx;
    }
    BOOST_CHECK(hogPresent);
    BOOST_CHECK(plainPresent);
    // And the block still respects its own limit.
    BOOST_CHECK(payload->executionPayload.gasUsed <= payload->executionPayload.gasLimit);
}

/// Forced deposits that alone overrun the block gas limit are a block-level error: the
/// builder refuses the payload instead of emitting a block whose gasUsed exceeds its own
/// gasLimit. op-geth reaches the same verdict — SubGas's ErrGasLimitReached is excluded
/// from the failed-deposit salvage branch (core/state_transition.go:486).
BOOST_AUTO_TEST_CASE(build_payload_rejects_deposits_over_the_block_gas_limit)
{
    MemPoolImpl memPool;
    RealGlobalStateStorageFixture globalStateStorageFixture;
    auto forkchoiceState = makeForkchoiceState();
    setForkchoiceBlockNumbers(globalStateStorageFixture, forkchoiceState, c_initialBlockNumber,
        c_initialBlockNumber, c_initialBlockNumber);

    globalStateStorageFixture.setGasLimit(ledger::DEFAULT_GAS_LIMIT);
    auto attributes = makePayloadAttributesV3();
    // Two deposits, each reporting two thirds of the block gas limit.
    attributes.transactions = std::vector<std::string>{"0x7e0102030405", "0x7e0607080910"};

    DepositRecordingExecutor executor;
    executor.depositGasUsed = u256(ledger::DEFAULT_GAS_LIMIT / 3 * 2);
    StubScheduler scheduler;
    static auto blockFactory =
        bcos::test::createBlockFactory(bcos::test::createNormalCryptoSuite());
    EngineServiceImpl<MemPoolImpl, RealGlobalStateStorage, DepositRecordingExecutor, StubScheduler>
        engineService(
            memPool, globalStateStorageFixture.storage, executor, scheduler, blockFactory);

    BOOST_CHECK_THROW(
        task::syncWait(engineService.updateForkchoice(forkchoiceState, &attributes, 3)),
        bcos::engine::BlockGasLimitExceeded);
}

BOOST_AUTO_TEST_SUITE_END()