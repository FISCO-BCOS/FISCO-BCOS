/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 */

#include "engine/bcos-engine/EngineService.h"

#include <bcos-concepts/ByteBuffer.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-framework/ledger/EVMAccount.h>
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/storage/Entry.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/storage2/MultiLayerStorage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-mempool/MemPoolImpl.h>
#include <bcos-tars-protocol/protocol/TransactionImpl.h>
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

using RealGlobalCheckpointBackend = TrivialCheckpointStorage<
    bcos::executor_v1::StateKey, bcos::executor_v1::StateValue, RealGlobalStateBackendStorage>;
using RealGlobalStateStorage = bcos::storage2::MultiLayerStorage<RealGlobalStateMutableStorage,
    void, RealGlobalCheckpointBackend>;

task::Task<void> writeBlockNumberToStorage(RealGlobalStateBackendStorage& backendStorage,
    const h256& blockHash, bcos::protocol::BlockNumber blockNumber)
{
    storage::Entry entry;
    entry.importFields({boost::lexical_cast<std::string>(blockNumber)});
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
        ledger::account::EVMAccount account{backendStorage, sender, false};
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

using TestEngineService = EngineService<MemPoolImpl, RealGlobalStateStorage>;
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
    request.parentBeaconBlockRoot =
        h256("2222222222222222222222222222222222222222222222222222222222222222");
    return request;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(EngineServiceTest)

BOOST_AUTO_TEST_CASE(exchange_capabilities_returns_supported_methods)
{
    MemPoolImpl memPool;
    RealGlobalStateStorageFixture globalStateStorageFixture;
    TestEngineService engineService(memPool, globalStateStorageFixture.storage);

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

    TestEngineService engineService(memPool, globalStateStorageFixture.storage);

    auto result =
        task::syncWait(engineService.updateForkchoice(forkchoiceState, &payloadAttributes, 2));

    BOOST_CHECK_EQUAL(static_cast<int>(result.payloadStatus.status),
        static_cast<int>(PayloadValidationStatus::Valid));
    BOOST_REQUIRE(result.payloadId.has_value());

    auto payload = task::syncWait(engineService.getPayload(*result.payloadId, 2));
    BOOST_CHECK_EQUAL(payload.executionPayload.parentHash, forkchoiceState.headBlockHash);
    BOOST_CHECK_EQUAL(payload.executionPayload.blockNumber, 0);
    BOOST_CHECK_EQUAL(payload.executionPayload.timestamp, c_timestamp);
    BOOST_CHECK(payload.executionPayload.withdrawals.has_value());
    BOOST_CHECK(!payload.executionPayload.blobGasUsed.has_value());
    auto fetched = memPool.get(std::vector{tx->hash()});
    BOOST_CHECK_EQUAL(fetched.size(), 1);
    BOOST_CHECK(!fetched[0]);
}

BOOST_AUTO_TEST_CASE(forkchoice_v3_tracks_safe_and_finalized_block_numbers)
{
    MemPoolImpl memPool;
    RealGlobalStateStorageFixture globalStateStorageFixture;
    TestEngineService engineService(memPool, globalStateStorageFixture.storage);

    auto initialForkchoice = makeForkchoiceState();
    setForkchoiceBlockNumbers(globalStateStorageFixture, initialForkchoice,
        c_trackedInitialBlockNumber, c_trackedInitialBlockNumber, c_trackedInitialBlockNumber);
    auto payloadAttributes = makePayloadAttributesV3();
    auto initialResult =
        task::syncWait(engineService.updateForkchoice(initialForkchoice, &payloadAttributes, 3));
    BOOST_REQUIRE(initialResult.payloadId.has_value());
    auto builtPayload = task::syncWait(engineService.getPayload(*initialResult.payloadId, 3));
    globalStateStorageFixture.setBlockNumber(
        builtPayload.executionPayload.blockHash, c_trackedNextBlockNumber);

    auto request = makeNewPayloadRequestV3(builtPayload.executionPayload);
    auto newPayloadStatus = task::syncWait(engineService.newPayload(request, 3));
    BOOST_CHECK_EQUAL(static_cast<int>(newPayloadStatus.status),
        static_cast<int>(PayloadValidationStatus::Valid));

    ForkchoiceState trackedForkchoice{builtPayload.executionPayload.blockHash,
        builtPayload.executionPayload.blockHash, builtPayload.executionPayload.blockHash};
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
    TestEngineService engineService(memPool, globalStateStorageFixture.storage);

    auto result =
        task::syncWait(engineService.updateForkchoice(forkchoiceState, &payloadAttributes, 3));
    BOOST_REQUIRE(result.payloadId.has_value());

    auto payload = task::syncWait(engineService.getPayload(*result.payloadId, 3));
    NewPayloadRequest request;
    request.executionPayload = payload.executionPayload;

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
    TestEngineService engineService(memPool, globalStateStorageFixture.storage);

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
    TestEngineService engineService(memPool, globalStateStorageFixture.storage);

    auto result = task::syncWait(engineService.updateForkchoice(forkchoiceState, nullptr, 3));

    BOOST_CHECK_EQUAL(static_cast<int>(result.payloadStatus.status),
        static_cast<int>(PayloadValidationStatus::Syncing));
    BOOST_CHECK(!result.payloadId.has_value());
}

BOOST_AUTO_TEST_CASE(forkchoice_rejects_non_sequential_head_block_number)
{
    MemPoolImpl memPool;
    RealGlobalStateStorageFixture globalStateStorageFixture;
    TestEngineService engineService(memPool, globalStateStorageFixture.storage);

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
    TestEngineService engineService(memPool, globalStateStorageFixture.storage);

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
    TestEngineService engineService(memPool, globalStateStorageFixture.storage);

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
    TestEngineService engineService(memPool, globalStateStorageFixture.storage);

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

BOOST_AUTO_TEST_SUITE_END()