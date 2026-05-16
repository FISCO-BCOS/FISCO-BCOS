/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 */

#include "engine/bcos-engine/EngineService.h"

#include <algorithm>
#include <bcos-concepts/ByteBuffer.h>
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/storage/Entry.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-task/Wait.h>
#include <boost/lexical_cast.hpp>
#include <boost/test/unit_test.hpp>
#include <unordered_map>

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

struct FakeMemPool
{};

struct FakeGlobalStateStorage
{
    using Value = storage::Entry;

    std::unordered_map<std::string, std::unordered_map<std::string, Value>> data;

    void setBlockNumber(const h256& blockHash, bcos::protocol::BlockNumber blockNumber)
    {
        storage::Entry entry;
        entry.setField(0, boost::lexical_cast<std::string>(blockNumber));
        data[std::string(ledger::SYS_HASH_2_NUMBER)]
            [std::string(bcos::concepts::bytebuffer::toView(blockHash))] = std::move(entry);
    }

    task::Task<std::optional<Value>> readOne(executor_v1::StateKeyView key)
    {
        auto [table, field] = key.get();
        if (auto tableIt = data.find(std::string(table)); tableIt != data.end())
        {
            if (auto fieldIt = tableIt->second.find(std::string(field)); fieldIt != tableIt->second.end())
            {
                co_return std::make_optional(fieldIt->second);
            }
        }
        co_return std::nullopt;
    }

    task::Task<std::optional<Value>> readOne(executor_v1::StateKey key)
    {
        co_return co_await readOne(executor_v1::StateKeyView{key});
    }

    template <class Keys>
    task::Task<std::vector<std::optional<Value>>> readSome(Keys keys)
    {
        std::vector<std::optional<Value>> results;
        if constexpr (::ranges::sized_range<Keys>)
        {
            results.reserve(::ranges::size(keys));
        }
        else
        {
            results.reserve(::ranges::distance(keys));
        }

        for (auto&& key : keys)
        {
            results.emplace_back(co_await readOne(executor_v1::StateKeyView{key}));
        }
        co_return results;
    }

    FakeGlobalStateStorage fork() { return *this; }
};

using TestEngineService = EngineService<FakeMemPool, FakeGlobalStateStorage>;
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
    payloadAttributes.suggestedFeeRecipient =
        Address("1234567890abcdef1234567890abcdef12345678");
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
    TestEngineService engineService;

    auto capabilities = task::syncWait(
        engineService.exchangeCapabilities({"engine_forkchoiceUpdatedV1", "unknown_method"}));

    BOOST_CHECK_EQUAL(capabilities.size(), 10);
    BOOST_CHECK(
        std::find(capabilities.begin(), capabilities.end(), "engine_getPayloadV3") !=
        capabilities.end());
}

BOOST_AUTO_TEST_CASE(custom_dependency_types_can_be_injected)
{
    FakeMemPool memPool;
    FakeGlobalStateStorage globalStateStorage;
    EngineService<FakeMemPool, FakeGlobalStateStorage> engineService(
        memPool, globalStateStorage);

    BOOST_CHECK(engineService.memPool() == &memPool);
    BOOST_CHECK(engineService.globalStateStorage() == &globalStateStorage);
}

BOOST_AUTO_TEST_CASE(forkchoice_with_payload_attributes_builds_retrievable_payload)
{
    FakeMemPool memPool;
    FakeGlobalStateStorage globalStateStorage;
    auto forkchoiceState = makeForkchoiceState();
    globalStateStorage.setBlockNumber(forkchoiceState.headBlockHash, c_initialBlockNumber);

    TestEngineService engineService(memPool, globalStateStorage);

    auto result = task::syncWait(
        engineService.updateForkchoice(forkchoiceState, makePayloadAttributesV2(), 2));

    BOOST_CHECK_EQUAL(static_cast<int>(result.payloadStatus.status),
        static_cast<int>(PayloadValidationStatus::Valid));
    BOOST_REQUIRE(result.payloadId.has_value());

    auto payload = task::syncWait(engineService.getPayload(*result.payloadId, 2));
    BOOST_CHECK_EQUAL(payload.executionPayload.parentHash, forkchoiceState.headBlockHash);
    BOOST_CHECK_EQUAL(payload.executionPayload.blockNumber, 0);
    BOOST_CHECK_EQUAL(payload.executionPayload.timestamp, c_timestamp);
    BOOST_CHECK(payload.executionPayload.withdrawals.has_value());
    BOOST_CHECK(!payload.executionPayload.blobGasUsed.has_value());
}

BOOST_AUTO_TEST_CASE(forkchoice_v3_tracks_safe_and_finalized_block_numbers)
{
    FakeMemPool memPool;
    FakeGlobalStateStorage globalStateStorage;
    TestEngineService engineService(memPool, globalStateStorage);

    auto initialForkchoice = makeForkchoiceState();
    globalStateStorage.setBlockNumber(
        initialForkchoice.headBlockHash, c_trackedInitialBlockNumber);
    auto initialResult =
        task::syncWait(engineService.updateForkchoice(initialForkchoice, makePayloadAttributesV3(), 3));
    BOOST_REQUIRE(initialResult.payloadId.has_value());
    auto builtPayload = task::syncWait(engineService.getPayload(*initialResult.payloadId, 3));
    globalStateStorage.setBlockNumber(
        builtPayload.executionPayload.blockHash, c_trackedNextBlockNumber);

    auto request = makeNewPayloadRequestV3(builtPayload.executionPayload);
    auto newPayloadStatus = task::syncWait(engineService.newPayload(request, 3));
    BOOST_CHECK_EQUAL(static_cast<int>(newPayloadStatus.status),
        static_cast<int>(PayloadValidationStatus::Valid));

    ForkchoiceState trackedForkchoice{builtPayload.executionPayload.blockHash,
        builtPayload.executionPayload.blockHash, builtPayload.executionPayload.blockHash};
    auto trackedResult = task::syncWait(engineService.updateForkchoice(trackedForkchoice, std::nullopt, 3));
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
    FakeMemPool memPool;
    FakeGlobalStateStorage globalStateStorage;
    auto forkchoiceState = makeForkchoiceState();
    globalStateStorage.setBlockNumber(forkchoiceState.headBlockHash, c_validationBlockNumber);
    TestEngineService engineService(memPool, globalStateStorage);

    auto result = task::syncWait(
        engineService.updateForkchoice(forkchoiceState, makePayloadAttributesV3(), 3));
    BOOST_REQUIRE(result.payloadId.has_value());

    auto payload = task::syncWait(engineService.getPayload(*result.payloadId, 3));
    NewPayloadRequest request;
    request.executionPayload = payload.executionPayload;

    auto status = task::syncWait(engineService.newPayload(request, 3));

    BOOST_CHECK_EQUAL(static_cast<int>(status.status),
        static_cast<int>(PayloadValidationStatus::Invalid));
    BOOST_REQUIRE(status.validationError.has_value());
    BOOST_CHECK_NE(status.validationError->find("parentBeaconBlockRoot"), std::string::npos);
}

BOOST_AUTO_TEST_CASE(forkchoice_rejects_non_sequential_head_block_number)
{
    FakeMemPool memPool;
    FakeGlobalStateStorage globalStateStorage;
    TestEngineService engineService(memPool, globalStateStorage);

    auto initialForkchoice = makeForkchoiceState();
    globalStateStorage.setBlockNumber(
        initialForkchoice.headBlockHash, c_reorgStartBlockNumber);
    auto initialResult =
        task::syncWait(engineService.updateForkchoice(initialForkchoice, std::nullopt, 3));
    BOOST_CHECK_EQUAL(static_cast<int>(initialResult.payloadStatus.status),
        static_cast<int>(PayloadValidationStatus::Valid));

    ForkchoiceState reorgForkchoice{
        h256("dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd"),
        h256("dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd"),
        h256("dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd")};
    globalStateStorage.setBlockNumber(
        reorgForkchoice.headBlockHash, c_reorgTargetBlockNumber);

    BOOST_CHECK_THROW(
        task::syncWait(engineService.updateForkchoice(reorgForkchoice, std::nullopt, 3)),
        bcos::engine::InvalidForkchoiceState);
}

BOOST_AUTO_TEST_CASE(forkchoice_rejects_safe_block_number_above_head)
{
    FakeMemPool memPool;
    FakeGlobalStateStorage globalStateStorage;
    TestEngineService engineService(memPool, globalStateStorage);

    ForkchoiceState forkchoiceState{
        h256("eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee"),
        h256("ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"),
        h256("0000000000000000000000000000000000000000000000000000000000000011")};
    globalStateStorage.setBlockNumber(forkchoiceState.headBlockHash, c_headOrderingBlockNumber);
    globalStateStorage.setBlockNumber(forkchoiceState.safeBlockHash, c_safeOrderingBlockNumber);
    globalStateStorage.setBlockNumber(forkchoiceState.finalizedBlockHash, c_headOrderingBlockNumber);

    BOOST_CHECK_THROW(
        task::syncWait(engineService.updateForkchoice(forkchoiceState, std::nullopt, 3)),
        bcos::engine::InvalidForkchoiceState);
}

BOOST_AUTO_TEST_CASE(forkchoice_rejects_finalized_block_number_above_safe)
{
    FakeMemPool memPool;
    FakeGlobalStateStorage globalStateStorage;
    TestEngineService engineService(memPool, globalStateStorage);

    ForkchoiceState forkchoiceState{
        h256("1212121212121212121212121212121212121212121212121212121212121212"),
        h256("1313131313131313131313131313131313131313131313131313131313131313"),
        h256("1414141414141414141414141414141414141414141414141414141414141414")};
    globalStateStorage.setBlockNumber(forkchoiceState.headBlockHash, c_finalizedOrderingBlockNumber);
    globalStateStorage.setBlockNumber(forkchoiceState.safeBlockHash, c_headOrderingBlockNumber);
    globalStateStorage.setBlockNumber(
        forkchoiceState.finalizedBlockHash, c_safeOrderingBlockNumber);

    BOOST_CHECK_THROW(
        task::syncWait(engineService.updateForkchoice(forkchoiceState, std::nullopt, 3)),
        bcos::engine::InvalidForkchoiceState);
}

BOOST_AUTO_TEST_CASE(forkchoice_ignores_stale_update_after_newer_head_wins)
{
    FakeMemPool memPool;
    FakeGlobalStateStorage globalStateStorage;
    TestEngineService engineService(memPool, globalStateStorage);

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

    globalStateStorage.setBlockNumber(
        firstForkchoice.headBlockHash, c_staleInitialBlockNumber);
    globalStateStorage.setBlockNumber(
        secondForkchoice.headBlockHash, c_staleNextBlockNumber);
    globalStateStorage.setBlockNumber(
        thirdForkchoice.headBlockHash, c_staleThirdBlockNumber);

    auto firstResult =
        task::syncWait(engineService.updateForkchoice(firstForkchoice, std::nullopt, 3));
    BOOST_CHECK_EQUAL(static_cast<int>(firstResult.payloadStatus.status),
        static_cast<int>(PayloadValidationStatus::Valid));

    auto secondResult =
        task::syncWait(engineService.updateForkchoice(secondForkchoice, std::nullopt, 3));
    BOOST_CHECK_EQUAL(static_cast<int>(secondResult.payloadStatus.status),
        static_cast<int>(PayloadValidationStatus::Valid));

    auto staleResult =
        task::syncWait(engineService.updateForkchoice(firstForkchoice, std::nullopt, 3));
    BOOST_CHECK_EQUAL(static_cast<int>(staleResult.payloadStatus.status),
        static_cast<int>(PayloadValidationStatus::Valid));

    auto thirdResult =
        task::syncWait(engineService.updateForkchoice(thirdForkchoice, std::nullopt, 3));
    BOOST_CHECK_EQUAL(static_cast<int>(thirdResult.payloadStatus.status),
        static_cast<int>(PayloadValidationStatus::Valid));
}

BOOST_AUTO_TEST_SUITE_END()