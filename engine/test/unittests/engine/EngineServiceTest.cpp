/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 */

#include "engine/bcos-engine/EngineService.h"

#include <algorithm>
#include <bcos-task/Wait.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::engine;

namespace
{
constexpr std::uint64_t c_timestamp = 123456;

struct FakeMemPool
{};

struct FakeGlobalStateStorage
{};

using TestEngineService = EngineService<FakeMemPool, NoGlobalStateStorage>;

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
    TestEngineService engineService;

    auto forkchoiceState = makeForkchoiceState();
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
    TestEngineService engineService;

    auto initialForkchoice = makeForkchoiceState();
    auto initialResult =
        task::syncWait(engineService.updateForkchoice(initialForkchoice, makePayloadAttributesV3(), 3));
    BOOST_REQUIRE(initialResult.payloadId.has_value());
    auto builtPayload = task::syncWait(engineService.getPayload(*initialResult.payloadId, 3));

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
    BOOST_CHECK_EQUAL(*safeBlockNumber, builtPayload.executionPayload.blockNumber);
    BOOST_CHECK_EQUAL(*finalizedBlockNumber, builtPayload.executionPayload.blockNumber);
}

BOOST_AUTO_TEST_CASE(new_payload_rejects_missing_required_v3_fields)
{
    TestEngineService engineService;

    auto result = task::syncWait(
        engineService.updateForkchoice(makeForkchoiceState(), makePayloadAttributesV3(), 3));
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

BOOST_AUTO_TEST_SUITE_END()