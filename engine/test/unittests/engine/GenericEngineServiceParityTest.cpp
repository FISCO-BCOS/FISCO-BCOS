/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 */

#include "engine/bcos-engine/EngineServiceImpl.h"
#include "engine/bcos-engine/GenericEngineService.h"

#include <bcos-codec/rlp/Common.h>
#include <bcos-codec/rlp/RLPEncode.h>
#include <bcos-concepts/ByteBuffer.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-framework/engine/EngineService.h>
#include <bcos-framework/engine/RawTransactionDispatch.h>
#include <bcos-framework/ledger/EVMAccount.h>
#include <bcos-framework/storage/Entry.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/storage2/MultiLayerStorage.h>
#include <bcos-framework/testutils/faker/FakeBlock.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-mempool/MemPoolImpl.h>
#include <bcos-tars-protocol/protocol/TransactionImpl.h>
#include <bcos-task/Wait.h>
#include <boost/lexical_cast.hpp>
#include <boost/test/unit_test.hpp>

#include <algorithm>

using namespace bcos;
using namespace bcos::engine;
using namespace bcos::txpool;
using namespace bcos::protocol;
using namespace bcos::crypto;

namespace generic_parity_test
{
constexpr std::uint64_t c_timestamp = 123456;
constexpr bcos::protocol::BlockNumber c_initialBlockNumber = 5;
constexpr bcos::protocol::BlockNumber c_trackedInitialBlockNumber = 10;
constexpr bcos::protocol::BlockNumber c_trackedNextBlockNumber = 11;
constexpr bcos::protocol::BlockNumber c_validationBlockNumber = 20;
constexpr bcos::protocol::BlockNumber c_headOrderingBlockNumber = 40;
constexpr bcos::protocol::BlockNumber c_safeOrderingBlockNumber = 41;
constexpr bcos::protocol::BlockNumber c_staleInitialBlockNumber = 50;
constexpr bcos::protocol::BlockNumber c_staleNextBlockNumber = 51;
constexpr bcos::protocol::BlockNumber c_staleThirdBlockNumber = 52;
constexpr bcos::protocol::BlockNumber c_rebuildBaseBlockNumber = 60;

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

using LegacyService =
    EngineServiceImpl<MemPoolImpl, RealGlobalStateStorage, StubExecutor, StubScheduler>;
using NewService =
    GenericEngineService<MemPoolImpl, RealGlobalStateStorage, StubExecutor, StubScheduler>;

static_assert(EngineServiceConcept<LegacyService>);
static_assert(EngineServiceConcept<NewService>);

class TestTransactionImpl : public bcostars::protocol::TransactionImpl
{
public:
    void markClean() { setTainted(false); }
};

static bytes toBytes(std::string_view input)
{
    return {reinterpret_cast<const byte*>(input.data()),
        reinterpret_cast<const byte*>(input.data()) + input.size()};
}

static protocol::Transaction::Ptr makeWeb3Tx(std::string_view senderBytes, uint64_t nonce)
{
    bytes body;
    bcos::codec::rlp::encode(body, static_cast<uint64_t>(1));
    bcos::codec::rlp::encode(body, nonce);
    bcos::codec::rlp::encode(body, static_cast<uint64_t>(1));
    bcos::codec::rlp::encode(body, static_cast<uint64_t>(1));
    bcos::codec::rlp::encode(body, static_cast<uint64_t>(21000));
    bcos::codec::rlp::encode(body, Address("abcdabcdabcdabcdabcdabcdabcdabcdabcdabcd"));
    bcos::codec::rlp::encode(body, static_cast<uint64_t>(0));
    bcos::codec::rlp::encode(body, bytes{});
    body.push_back(bcos::codec::rlp::LIST_HEAD_BASE);
    bytes payload;
    payload.push_back(0x02);
    bcos::codec::rlp::encodeHeader(
        payload, bcos::codec::rlp::Header{.isList = true, .payloadLength = body.size()});
    payload.insert(payload.end(), body.begin(), body.end());

    auto tx = std::make_shared<TestTransactionImpl>();
    tx->mutableInner().type = static_cast<int>(bcos::protocol::TransactionType::Web3Transaction);
    tx->mutableInner().extraTransactionBytes.assign(payload.begin(), payload.end());
    bytes signature(65, 0);
    signature[31] = 0x12;
    signature[63] = 0x34;
    signature[64] = 0x01;
    tx->mutableInner().signature.assign(signature.begin(), signature.end());
    tx->setNonce("0x" + std::to_string(nonce));
    tx->forceSender(toBytes(senderBytes));
    Keccak256 hasher;
    tx->calculateHash(hasher);
    tx->markClean();
    tx->setImportTime(static_cast<int64_t>(nonce));
    return tx;
}

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

task::Task<void> writeCanonicalHashToStorage(RealGlobalStateBackendStorage& backendStorage,
    bcos::protocol::BlockNumber blockNumber, const h256& blockHash)
{
    storage::Entry entry;
    entry.set(blockHash.asBytes());
    co_await bcos::storage2::writeOne(backendStorage,
        bcos::executor_v1::StateKey{
            ledger::SYS_NUMBER_2_HASH, boost::lexical_cast<std::string>(blockNumber)},
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

    void setCanonicalBlock(const h256& blockHash, bcos::protocol::BlockNumber blockNumber)
    {
        setBlockNumber(blockHash, blockNumber);
        task::syncWait(writeCanonicalHashToStorage(backendStorage, blockNumber, blockHash));
    }

    void setNonce(std::string_view sender, std::string nonce)
    {
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

struct ServicePair
{
    MemPoolImpl legacyMemPool;
    MemPoolImpl newMemPool;
    RealGlobalStateStorageFixture legacyStorage;
    RealGlobalStateStorageFixture newStorage;
    StubExecutor legacyExecutor;
    StubExecutor newExecutor;
    StubScheduler legacyScheduler;
    StubScheduler newScheduler;
    LegacyService legacy;
    NewService fresh;

    ServicePair()
      : legacy(legacyMemPool, legacyStorage.storage, legacyExecutor, legacyScheduler,
            bcos::test::createBlockFactory(bcos::test::createNormalCryptoSuite())),
        fresh(newMemPool, newStorage.storage, newExecutor, newScheduler,
            bcos::test::createBlockFactory(bcos::test::createNormalCryptoSuite()))
    {}
};

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
    request.parentBeaconBlockRoot =
        h256("5555555555555555555555555555555555555555555555555555555555555555");
    return request;
}

void checkForkchoiceParity(
    ForkchoiceUpdatedResult const& legacyResult, ForkchoiceUpdatedResult const& newResult)
{
    BOOST_CHECK_EQUAL(static_cast<int>(legacyResult.payloadStatus.status),
        static_cast<int>(newResult.payloadStatus.status));
    BOOST_CHECK(
        legacyResult.payloadStatus.latestValidHash == newResult.payloadStatus.latestValidHash);
    BOOST_CHECK(
        legacyResult.payloadStatus.validationError == newResult.payloadStatus.validationError);
    BOOST_CHECK(legacyResult.payloadId == newResult.payloadId);
}

void checkStatusParity(PayloadStatus const& legacyStatus, PayloadStatus const& newStatus)
{
    BOOST_CHECK_EQUAL(static_cast<int>(legacyStatus.status), static_cast<int>(newStatus.status));
    BOOST_CHECK(legacyStatus.latestValidHash == newStatus.latestValidHash);
    BOOST_CHECK(legacyStatus.validationError == newStatus.validationError);
}

void checkEngineTransactionParity(EngineTransaction const& legacyTx, EngineTransaction const& newTx)
{
    BOOST_CHECK(legacyTx.raw == newTx.raw);
    if (legacyTx.decoded && newTx.decoded)
    {
        BOOST_CHECK_EQUAL(legacyTx.decoded->hash(), newTx.decoded->hash());
        BOOST_CHECK_EQUAL(legacyTx.decoded->type(), newTx.decoded->type());
    }
    else
    {
        BOOST_CHECK(legacyTx.decoded == newTx.decoded);
    }
}

void checkExecutionPayloadParity(
    ExecutionPayload const& legacyPayload, ExecutionPayload const& newPayload)
{
    BOOST_CHECK(legacyPayload.logsBloom == newPayload.logsBloom);
    BOOST_CHECK_EQUAL(legacyPayload.parentHash, newPayload.parentHash);
    BOOST_CHECK_EQUAL(legacyPayload.stateRoot, newPayload.stateRoot);
    BOOST_CHECK_EQUAL(legacyPayload.receiptsRoot, newPayload.receiptsRoot);
    BOOST_CHECK_EQUAL(legacyPayload.prevRandao, newPayload.prevRandao);
    BOOST_CHECK_EQUAL(legacyPayload.gasLimit, newPayload.gasLimit);
    BOOST_CHECK_EQUAL(legacyPayload.gasUsed, newPayload.gasUsed);
    BOOST_CHECK_EQUAL(legacyPayload.baseFeePerGas, newPayload.baseFeePerGas);
    BOOST_CHECK_EQUAL(legacyPayload.blockHash, newPayload.blockHash);
    BOOST_REQUIRE_EQUAL(legacyPayload.transactions.size(), newPayload.transactions.size());
    for (std::size_t i = 0; i < legacyPayload.transactions.size(); ++i)
    {
        checkEngineTransactionParity(legacyPayload.transactions[i], newPayload.transactions[i]);
    }
    BOOST_CHECK(legacyPayload.extraData == newPayload.extraData);
    BOOST_CHECK_EQUAL(legacyPayload.feeRecipient, newPayload.feeRecipient);
    BOOST_CHECK_EQUAL(legacyPayload.timestamp, newPayload.timestamp);
    BOOST_CHECK_EQUAL(legacyPayload.blockNumber, newPayload.blockNumber);
    BOOST_CHECK_EQUAL(legacyPayload.withdrawals.has_value(), newPayload.withdrawals.has_value());
    if (legacyPayload.withdrawals.has_value())
    {
        BOOST_REQUIRE(newPayload.withdrawals.has_value());
        BOOST_REQUIRE_EQUAL(legacyPayload.withdrawals->size(), newPayload.withdrawals->size());
        for (std::size_t i = 0; i < legacyPayload.withdrawals->size(); ++i)
        {
            BOOST_CHECK_EQUAL(
                (*legacyPayload.withdrawals)[i].index, (*newPayload.withdrawals)[i].index);
            BOOST_CHECK_EQUAL((*legacyPayload.withdrawals)[i].validatorIndex,
                (*newPayload.withdrawals)[i].validatorIndex);
            BOOST_CHECK_EQUAL(
                (*legacyPayload.withdrawals)[i].address, (*newPayload.withdrawals)[i].address);
            BOOST_CHECK_EQUAL(
                (*legacyPayload.withdrawals)[i].amount, (*newPayload.withdrawals)[i].amount);
        }
    }
    BOOST_CHECK(legacyPayload.blobGasUsed == newPayload.blobGasUsed);
    BOOST_CHECK(legacyPayload.excessBlobGas == newPayload.excessBlobGas);
    BOOST_CHECK(legacyPayload.withdrawalsRoot == newPayload.withdrawalsRoot);
}

void checkGetPayloadParity(GetPayloadData const& legacyPayload, GetPayloadData const& newPayload)
{
    checkExecutionPayloadParity(legacyPayload.executionPayload, newPayload.executionPayload);
    BOOST_CHECK_EQUAL(legacyPayload.blockValue, newPayload.blockValue);
    BOOST_CHECK(legacyPayload.blobsBundle.has_value() == newPayload.blobsBundle.has_value());
    if (legacyPayload.blobsBundle.has_value())
    {
        BOOST_REQUIRE(newPayload.blobsBundle.has_value());
        BOOST_CHECK(legacyPayload.blobsBundle->commitments == newPayload.blobsBundle->commitments);
        BOOST_CHECK(legacyPayload.blobsBundle->proofs == newPayload.blobsBundle->proofs);
        BOOST_CHECK(legacyPayload.blobsBundle->blobs == newPayload.blobsBundle->blobs);
    }
    BOOST_CHECK_EQUAL(legacyPayload.shouldOverrideBuilder, newPayload.shouldOverrideBuilder);
    BOOST_CHECK(
        legacyPayload.executionRequests.has_value() == newPayload.executionRequests.has_value());
    if (legacyPayload.executionRequests.has_value())
    {
        BOOST_REQUIRE(newPayload.executionRequests.has_value());
        BOOST_REQUIRE_EQUAL(
            legacyPayload.executionRequests->size(), newPayload.executionRequests->size());
        for (std::size_t i = 0; i < legacyPayload.executionRequests->size(); ++i)
        {
            BOOST_CHECK(
                (*legacyPayload.executionRequests)[i] == (*newPayload.executionRequests)[i]);
        }
    }
    BOOST_CHECK(legacyPayload.parentBeaconBlockRoot == newPayload.parentBeaconBlockRoot);
}

template <typename Exception>
void checkBothThrow(auto&& legacyAction, auto&& newAction)
{
    BOOST_CHECK_THROW(legacyAction(), Exception);
    BOOST_CHECK_THROW(newAction(), Exception);
}

}  // namespace generic_parity_test

BOOST_AUTO_TEST_SUITE(GenericEngineServiceParityTest)

using namespace generic_parity_test;

BOOST_AUTO_TEST_CASE(generic_capabilities_match)
{
    ServicePair pair;
    auto legacyCaps = task::syncWait(pair.legacy.exchangeCapabilities({}));
    auto newCaps = task::syncWait(pair.fresh.exchangeCapabilities({}));
    BOOST_CHECK_EQUAL_COLLECTIONS(
        legacyCaps.begin(), legacyCaps.end(), newCaps.begin(), newCaps.end());
}

BOOST_AUTO_TEST_CASE(generic_unknown_head_matches)
{
    ServicePair pair;
    auto forkchoiceState = makeForkchoiceState();
    pair.legacyStorage.setBlockNumber(forkchoiceState.safeBlockHash, c_validationBlockNumber);
    pair.legacyStorage.setBlockNumber(forkchoiceState.finalizedBlockHash, c_validationBlockNumber);
    pair.newStorage.setBlockNumber(forkchoiceState.safeBlockHash, c_validationBlockNumber);
    pair.newStorage.setBlockNumber(forkchoiceState.finalizedBlockHash, c_validationBlockNumber);

    auto legacyResult = task::syncWait(pair.legacy.updateForkchoice(forkchoiceState, nullptr, 3));
    auto newResult = task::syncWait(pair.fresh.updateForkchoice(forkchoiceState, nullptr, 3));
    checkForkchoiceParity(legacyResult, newResult);
}

BOOST_AUTO_TEST_CASE(generic_safe_finalized_validation_matches)
{
    ServicePair pair;
    ForkchoiceState forkchoiceState{
        h256("eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee"),
        h256("ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"),
        h256("0000000000000000000000000000000000000000000000000000000000000011")};
    pair.legacyStorage.setBlockNumber(forkchoiceState.headBlockHash, c_headOrderingBlockNumber);
    pair.legacyStorage.setBlockNumber(forkchoiceState.safeBlockHash, c_safeOrderingBlockNumber);
    pair.legacyStorage.setBlockNumber(
        forkchoiceState.finalizedBlockHash, c_headOrderingBlockNumber);
    pair.newStorage.setBlockNumber(forkchoiceState.headBlockHash, c_headOrderingBlockNumber);
    pair.newStorage.setBlockNumber(forkchoiceState.safeBlockHash, c_safeOrderingBlockNumber);
    pair.newStorage.setBlockNumber(forkchoiceState.finalizedBlockHash, c_headOrderingBlockNumber);

    checkBothThrow<InvalidForkchoiceState>(
        [&] { return task::syncWait(pair.legacy.updateForkchoice(forkchoiceState, nullptr, 3)); },
        [&] { return task::syncWait(pair.fresh.updateForkchoice(forkchoiceState, nullptr, 3)); });
}

BOOST_AUTO_TEST_CASE(generic_stale_head_swallow_matches)
{
    ServicePair pair;
    ForkchoiceState firstForkchoice{
        h256("1515151515151515151515151515151515151515151515151515151515151515"),
        h256("1515151515151515151515151515151515151515151515151515151515151515"),
        h256("1515151515151515151515151515151515151515151515151515151515151515")};
    ForkchoiceState secondForkchoice{
        h256("1616161616161616161616161616161616161616161616161616161616161616"),
        h256("1616161616161616161616161616161616161616161616161616161616161616"),
        h256("1616161616161616161616161616161616161616161616161616161616161616")};

    setForkchoiceBlockNumbers(pair.legacyStorage, firstForkchoice, c_staleInitialBlockNumber,
        c_staleInitialBlockNumber, c_staleInitialBlockNumber);
    setForkchoiceBlockNumbers(pair.legacyStorage, secondForkchoice, c_staleNextBlockNumber,
        c_staleNextBlockNumber, c_staleNextBlockNumber);
    setForkchoiceBlockNumbers(pair.newStorage, firstForkchoice, c_staleInitialBlockNumber,
        c_staleInitialBlockNumber, c_staleInitialBlockNumber);
    setForkchoiceBlockNumbers(pair.newStorage, secondForkchoice, c_staleNextBlockNumber,
        c_staleNextBlockNumber, c_staleNextBlockNumber);

    task::syncWait(pair.legacy.updateForkchoice(firstForkchoice, nullptr, 3));
    task::syncWait(pair.fresh.updateForkchoice(firstForkchoice, nullptr, 3));
    task::syncWait(pair.legacy.updateForkchoice(secondForkchoice, nullptr, 3));
    task::syncWait(pair.fresh.updateForkchoice(secondForkchoice, nullptr, 3));

    auto legacyStale = task::syncWait(pair.legacy.updateForkchoice(firstForkchoice, nullptr, 3));
    auto newStale = task::syncWait(pair.fresh.updateForkchoice(firstForkchoice, nullptr, 3));
    checkForkchoiceParity(legacyStale, newStale);
}

BOOST_AUTO_TEST_CASE(generic_rebuild_on_parent_matches)
{
    ServicePair pair;
    auto parentForkchoice = makeForkchoiceState();
    setForkchoiceBlockNumbers(pair.legacyStorage, parentForkchoice, c_rebuildBaseBlockNumber,
        c_rebuildBaseBlockNumber, c_rebuildBaseBlockNumber);
    setForkchoiceBlockNumbers(pair.newStorage, parentForkchoice, c_rebuildBaseBlockNumber,
        c_rebuildBaseBlockNumber, c_rebuildBaseBlockNumber);
    pair.legacyStorage.setCanonicalBlock(parentForkchoice.headBlockHash, c_rebuildBaseBlockNumber);
    pair.newStorage.setCanonicalBlock(parentForkchoice.headBlockHash, c_rebuildBaseBlockNumber);

    auto payloadAttributes = makePayloadAttributesV3();
    auto legacyBuild =
        task::syncWait(pair.legacy.updateForkchoice(parentForkchoice, &payloadAttributes, 3));
    auto newBuild =
        task::syncWait(pair.fresh.updateForkchoice(parentForkchoice, &payloadAttributes, 3));
    checkForkchoiceParity(legacyBuild, newBuild);
    BOOST_REQUIRE(legacyBuild.payloadId.has_value());
    BOOST_REQUIRE(newBuild.payloadId.has_value());

    auto legacyPayload = task::syncWait(pair.legacy.getPayload(*legacyBuild.payloadId, 3));
    auto newPayload = task::syncWait(pair.fresh.getPayload(*newBuild.payloadId, 3));
    checkGetPayloadParity(*legacyPayload, *newPayload);

    pair.legacyStorage.setBlockNumber(
        legacyPayload->executionPayload.blockHash, c_rebuildBaseBlockNumber + 1);
    pair.newStorage.setBlockNumber(
        newPayload->executionPayload.blockHash, c_rebuildBaseBlockNumber + 1);
    pair.legacyStorage.setCanonicalBlock(
        legacyPayload->executionPayload.blockHash, c_rebuildBaseBlockNumber + 1);
    pair.newStorage.setCanonicalBlock(
        newPayload->executionPayload.blockHash, c_rebuildBaseBlockNumber + 1);

    auto request = makeNewPayloadRequestV3(legacyPayload->executionPayload);
    checkStatusParity(task::syncWait(pair.legacy.newPayload(request, 3)),
        task::syncWait(
            pair.fresh.newPayload(makeNewPayloadRequestV3(newPayload->executionPayload), 3)));

    ForkchoiceState tipForkchoice{legacyPayload->executionPayload.blockHash,
        legacyPayload->executionPayload.blockHash, legacyPayload->executionPayload.blockHash};
    task::syncWait(pair.legacy.updateForkchoice(tipForkchoice, nullptr, 3));
    task::syncWait(pair.fresh.updateForkchoice(
        ForkchoiceState{newPayload->executionPayload.blockHash,
            newPayload->executionPayload.blockHash, newPayload->executionPayload.blockHash},
        nullptr, 3));

    auto rebuildAttributes = makePayloadAttributesV3();
    rebuildAttributes.timestamp = c_timestamp + 1;
    auto legacyRebuild =
        task::syncWait(pair.legacy.updateForkchoice(parentForkchoice, &rebuildAttributes, 3));
    auto newRebuild =
        task::syncWait(pair.fresh.updateForkchoice(parentForkchoice, &rebuildAttributes, 3));
    checkForkchoiceParity(legacyRebuild, newRebuild);
    BOOST_REQUIRE(legacyRebuild.payloadId.has_value());
    BOOST_REQUIRE(newRebuild.payloadId.has_value());

    auto legacyRebuilt = task::syncWait(pair.legacy.getPayload(*legacyRebuild.payloadId, 3));
    auto newRebuilt = task::syncWait(pair.fresh.getPayload(*newRebuild.payloadId, 3));
    checkGetPayloadParity(*legacyRebuilt, *newRebuilt);
}

BOOST_AUTO_TEST_CASE(generic_payload_id_and_get_payload_match)
{
    ServicePair pair;
    auto forkchoiceState = makeForkchoiceState();
    setForkchoiceBlockNumbers(pair.legacyStorage, forkchoiceState, c_initialBlockNumber,
        c_initialBlockNumber, c_initialBlockNumber);
    setForkchoiceBlockNumbers(pair.newStorage, forkchoiceState, c_initialBlockNumber,
        c_initialBlockNumber, c_initialBlockNumber);
    std::string sender("dddddddddddddddddddd", 20);
    auto legacyTx = makeWeb3Tx(sender, 0);
    auto newTx = makeWeb3Tx(sender, 0);
    pair.legacyMemPool.add(std::vector{legacyTx});
    pair.newMemPool.add(std::vector{newTx});
    pair.legacyStorage.setNonce(sender, "0");
    pair.newStorage.setNonce(sender, "0");

    auto payloadAttributes = makePayloadAttributesV3();
    auto legacyResult =
        task::syncWait(pair.legacy.updateForkchoice(forkchoiceState, &payloadAttributes, 3));
    auto newResult =
        task::syncWait(pair.fresh.updateForkchoice(forkchoiceState, &payloadAttributes, 3));
    checkForkchoiceParity(legacyResult, newResult);

    auto legacyPayload = task::syncWait(pair.legacy.getPayload(*legacyResult.payloadId, 3));
    auto newPayload = task::syncWait(pair.fresh.getPayload(*newResult.payloadId, 3));
    checkGetPayloadParity(*legacyPayload, *newPayload);
}

BOOST_AUTO_TEST_CASE(generic_cache_eviction_matches)
{
    ServicePair pair;
    auto forkchoiceState = makeForkchoiceState();
    setForkchoiceBlockNumbers(pair.legacyStorage, forkchoiceState, c_initialBlockNumber,
        c_initialBlockNumber, c_initialBlockNumber);
    setForkchoiceBlockNumbers(pair.newStorage, forkchoiceState, c_initialBlockNumber,
        c_initialBlockNumber, c_initialBlockNumber);

    std::vector<PayloadID> legacyIds;
    std::vector<PayloadID> newIds;
    auto currentHead = forkchoiceState;
    for (int i = 0; i < 65; ++i)
    {
        auto attrs = makePayloadAttributesV3();
        attrs.timestamp = c_timestamp + static_cast<std::uint64_t>(i);
        auto legacyResult = task::syncWait(pair.legacy.updateForkchoice(currentHead, &attrs, 3));
        auto newResult = task::syncWait(pair.fresh.updateForkchoice(currentHead, &attrs, 3));
        checkForkchoiceParity(legacyResult, newResult);
        BOOST_REQUIRE(legacyResult.payloadId.has_value());
        legacyIds.push_back(*legacyResult.payloadId);
        newIds.push_back(*newResult.payloadId);

        auto legacyPayload = task::syncWait(pair.legacy.getPayload(*legacyResult.payloadId, 3));
        auto newPayload = task::syncWait(pair.fresh.getPayload(*newResult.payloadId, 3));
        checkGetPayloadParity(*legacyPayload, *newPayload);

        pair.legacyStorage.setBlockNumber(
            legacyPayload->executionPayload.blockHash, c_initialBlockNumber + 1 + i);
        pair.newStorage.setBlockNumber(
            newPayload->executionPayload.blockHash, c_initialBlockNumber + 1 + i);
        auto request = makeNewPayloadRequestV3(legacyPayload->executionPayload);
        task::syncWait(pair.legacy.newPayload(request, 3));
        task::syncWait(
            pair.fresh.newPayload(makeNewPayloadRequestV3(newPayload->executionPayload), 3));

        currentHead = ForkchoiceState{legacyPayload->executionPayload.blockHash,
            legacyPayload->executionPayload.blockHash, legacyPayload->executionPayload.blockHash};
    }

    BOOST_CHECK_THROW(task::syncWait(pair.legacy.getPayload(legacyIds.front(), 3)), UnknownPayload);
    BOOST_CHECK_THROW(task::syncWait(pair.fresh.getPayload(newIds.front(), 3)), UnknownPayload);
    BOOST_CHECK_NO_THROW(task::syncWait(pair.legacy.getPayload(legacyIds.back(), 3)));
    BOOST_CHECK_NO_THROW(task::syncWait(pair.fresh.getPayload(newIds.back(), 3)));
}

BOOST_AUTO_TEST_CASE(generic_v3_v5_v4_round_trip_matches)
{
    ServicePair pair;
    auto forkchoiceState = makeForkchoiceState();
    setForkchoiceBlockNumbers(pair.legacyStorage, forkchoiceState, c_initialBlockNumber,
        c_initialBlockNumber, c_initialBlockNumber);
    setForkchoiceBlockNumbers(pair.newStorage, forkchoiceState, c_initialBlockNumber,
        c_initialBlockNumber, c_initialBlockNumber);
    auto payloadAttributes = makeKarstPayloadAttributes();

    auto legacyBuild =
        task::syncWait(pair.legacy.updateForkchoice(forkchoiceState, &payloadAttributes, 3));
    auto newBuild =
        task::syncWait(pair.fresh.updateForkchoice(forkchoiceState, &payloadAttributes, 3));
    checkForkchoiceParity(legacyBuild, newBuild);

    auto legacyPayload = task::syncWait(pair.legacy.getPayload(*legacyBuild.payloadId, 5));
    auto newPayload = task::syncWait(pair.fresh.getPayload(*newBuild.payloadId, 5));
    checkGetPayloadParity(*legacyPayload, *newPayload);

    NewPayloadRequest legacyRequest;
    legacyRequest.executionPayload = legacyPayload->executionPayload;
    legacyRequest.parentBeaconBlockRoot = legacyPayload->parentBeaconBlockRoot;
    legacyRequest.executionRequests = std::vector<bytes>{};
    NewPayloadRequest newRequest;
    newRequest.executionPayload = newPayload->executionPayload;
    newRequest.parentBeaconBlockRoot = newPayload->parentBeaconBlockRoot;
    newRequest.executionRequests = std::vector<bytes>{};

    checkStatusParity(task::syncWait(pair.legacy.newPayload(legacyRequest, 4)),
        task::syncWait(pair.fresh.newPayload(newRequest, 4)));
}

BOOST_AUTO_TEST_CASE(generic_new_payload_validation_errors_match)
{
    ServicePair pair;
    auto forkchoiceState = makeForkchoiceState();
    setForkchoiceBlockNumbers(pair.legacyStorage, forkchoiceState, c_initialBlockNumber,
        c_initialBlockNumber, c_initialBlockNumber);
    setForkchoiceBlockNumbers(pair.newStorage, forkchoiceState, c_initialBlockNumber,
        c_initialBlockNumber, c_initialBlockNumber);

    auto payloadAttributes = makePayloadAttributesV3();
    auto legacyBuild =
        task::syncWait(pair.legacy.updateForkchoice(forkchoiceState, &payloadAttributes, 3));
    auto newBuild =
        task::syncWait(pair.fresh.updateForkchoice(forkchoiceState, &payloadAttributes, 3));
    checkForkchoiceParity(legacyBuild, newBuild);

    auto legacyPayload = task::syncWait(pair.legacy.getPayload(*legacyBuild.payloadId, 3));
    auto newPayload = task::syncWait(pair.fresh.getPayload(*newBuild.payloadId, 3));

    NewPayloadRequest missingBeacon;
    missingBeacon.executionPayload = legacyPayload->executionPayload;
    checkStatusParity(task::syncWait(pair.legacy.newPayload(missingBeacon, 3)),
        task::syncWait(pair.fresh.newPayload(
            NewPayloadRequest{.executionPayload = newPayload->executionPayload}, 3)));

    auto blobAttributes = makePayloadAttributesV3();
    blobAttributes.transactions = std::vector<std::string>{"0x03aabb"};
    auto legacyBlobFcu =
        task::syncWait(pair.legacy.updateForkchoice(forkchoiceState, &blobAttributes, 3));
    auto newBlobFcu =
        task::syncWait(pair.fresh.updateForkchoice(forkchoiceState, &blobAttributes, 3));
    checkForkchoiceParity(legacyBlobFcu, newBlobFcu);

    NewPayloadRequest blobRequest;
    blobRequest.executionPayload.transactions.push_back(
        {.raw = bytes{0x03, 0xaa, 0xbb}, .decoded = nullptr});
    checkStatusParity(task::syncWait(pair.legacy.newPayload(blobRequest, 1)),
        task::syncWait(pair.fresh.newPayload(blobRequest, 1)));

    auto request = makeNewPayloadRequestV3(legacyPayload->executionPayload);
    request.expectedBlobVersionedHashes = {
        h256("3333333333333333333333333333333333333333333333333333333333333333")};
    auto newBlobRequest = makeNewPayloadRequestV3(newPayload->executionPayload);
    newBlobRequest.expectedBlobVersionedHashes = request.expectedBlobVersionedHashes;
    checkStatusParity(task::syncWait(pair.legacy.newPayload(request, 3)),
        task::syncWait(pair.fresh.newPayload(newBlobRequest, 3)));
}

BOOST_AUTO_TEST_SUITE_END()
