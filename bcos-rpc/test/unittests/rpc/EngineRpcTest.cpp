/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @file EngineRpcTest.cpp
 * @brief Unit tests for OPEngineEndpoints (openginerpc)
 */

#include "../common/RPCFixture.h"
#include <bcos-framework/engine/AnyEngineService.h>
#include <bcos-rpc/openginerpc/Common.h>
#include <bcos-rpc/openginerpc/endpoints/OPEngineEndpoints.h>
#include <bcos-codec/wrapper/CodecWrapper.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <json/json.h>
#include <memory>

using namespace bcos;
using namespace bcos::rpc;
using namespace bcos::crypto;

namespace bcos::test
{
/// A minimal mock EngineService that captures call parameters and returns
/// pre-configured results.  Uses a shared State so that the same state is
/// visible through AnyEngineService's proxy copy.
class MockOpEngineService
{
public:
    struct State
    {
        std::vector<std::string> capturedRemoteCaps;
        engine::ForkchoiceUpdatedResult forkchoiceUpdatedResult{
            .payloadStatus =
                engine::PayloadStatus{
                    .status = engine::PayloadValidationStatus::Valid,
                    .latestValidHash = std::nullopt,
                    .validationError = std::nullopt,
                },
            .payloadId = std::nullopt,
        };
        std::optional<engine::ForkchoiceState> capturedForkchoiceState;
        std::optional<int> capturedForkchoiceVersion;
        engine::GetPayloadResult getPayloadResult;
        std::optional<engine::PayloadID> capturedPayloadId;
        std::optional<std::uint32_t> capturedGetPayloadVersion;
        std::optional<engine::NewPayloadRequest> capturedNewPayloadRequest;
        std::optional<std::uint32_t> capturedNewPayloadVersion;
    };
    std::shared_ptr<State> m_state = std::make_shared<State>();

    task::Task<std::vector<std::string>> exchangeCapabilities(
        std::vector<std::string> remoteCapabilities)
    {
        m_state->capturedRemoteCaps = remoteCapabilities;
        co_return remoteCapabilities;
    }

    task::Task<engine::ForkchoiceUpdatedResult> updateForkchoice(
        const engine::ForkchoiceState& forkchoiceState,
        const engine::PayloadAttributes* /*payloadAttributes*/, std::uint32_t version)
    {
        m_state->capturedForkchoiceState = forkchoiceState;
        m_state->capturedForkchoiceVersion = static_cast<int>(version);
        co_return m_state->forkchoiceUpdatedResult;
    }

    task::Task<engine::GetPayloadResult> getPayload(
        const engine::PayloadID& payloadId, std::uint32_t version)
    {
        m_state->capturedPayloadId = payloadId;
        m_state->capturedGetPayloadVersion = version;
        co_return m_state->getPayloadResult;
    }

    task::Task<engine::PayloadStatus> newPayload(
        const engine::NewPayloadRequest& request, std::uint32_t version)
    {
        m_state->capturedNewPayloadRequest = request;
        m_state->capturedNewPayloadVersion = version;
        co_return m_state->forkchoiceUpdatedResult.payloadStatus;
    }

    std::optional<bcos::protocol::BlockNumber> getSafeBlockNumber() const
    {
        return std::nullopt;
    }
    std::optional<bcos::protocol::BlockNumber> getFinalizedBlockNumber() const
    {
        return std::nullopt;
    }
};

class EngineRpcTestFixture : public RPCFixture
{
public:
    EngineRpcTestFixture()
    {
        // Set a mock engine service so endpoints have something to call
        nodeService->engineService() =
            std::make_shared<bcos::engine::AnyEngineService>(mockService);

        // Build the OPEngineEndpoints from the same NodeService.
        // The OPE ndpoints have a default engineService extracted from NodeService.
        endpoints = std::make_unique<OPEngineEndpoints>(nodeService);
    }

    std::unique_ptr<OPEngineEndpoints> endpoints;
    MockOpEngineService mockService;
};

// ================================================================
// exchangeCapabilities
// ================================================================
BOOST_FIXTURE_TEST_SUITE(EngineRpcTest, EngineRpcTestFixture)

BOOST_AUTO_TEST_CASE(exchangeCapabilities)
{
    Json::Value params(Json::arrayValue);
    params.append("engine_newPayloadV2");
    params.append("engine_newPayloadV3");

    Json::Value response;
    task::wait([&](OPEngineEndpoints* ep, Json::Value p, Json::Value& r) -> task::Task<void> {
        co_await ep->exchangeCapabilities(p, r);
    }(endpoints.get(), params, response));

    BOOST_CHECK(response.isArray());
    BOOST_CHECK_EQUAL(response.size(), 2);
    BOOST_CHECK_EQUAL(response[0u].asString(), "engine_newPayloadV2");
    BOOST_CHECK_EQUAL(response[1u].asString(), "engine_newPayloadV3");
}

// ================================================================
// forkchoiceUpdated
// ================================================================
BOOST_AUTO_TEST_CASE(forkchoiceUpdatedV1)
{
    Json::Value params(Json::arrayValue);
    Json::Value fc;
    fc["headBlockHash"] = "0x1111111111111111111111111111111111111111111111111111111111111111";
    fc["safeBlockHash"] = "0x2222222222222222222222222222222222222222222222222222222222222222";
    fc["finalizedBlockHash"] = "0x3333333333333333333333333333333333333333333333333333333333333333";
    params.append(fc);

    Json::Value response;
    task::wait([&](OPEngineEndpoints* ep, Json::Value p, Json::Value& r) -> task::Task<void> {
        co_await ep->forkchoiceUpdatedV1(p, r);
    }(endpoints.get(), params, response));

    BOOST_CHECK(response.isMember("payloadStatus"));
    BOOST_CHECK_EQUAL(response["payloadStatus"]["status"].asString(), "VALID");
    BOOST_CHECK(response.isMember("payloadId"));
    BOOST_CHECK(response["payloadId"].isNull());

    // Verify mock captured the state
    BOOST_REQUIRE(mockService.m_state->capturedForkchoiceState.has_value());
    BOOST_CHECK_EQUAL(
        mockService.m_state->capturedForkchoiceState->headBlockHash.hex(),
        "1111111111111111111111111111111111111111111111111111111111111111");
    BOOST_REQUIRE(mockService.m_state->capturedForkchoiceVersion.has_value());
    BOOST_CHECK_EQUAL(*mockService.m_state->capturedForkchoiceVersion, 1);
}

BOOST_AUTO_TEST_CASE(forkchoiceUpdatedV2)
{
    Json::Value params(Json::arrayValue);
    Json::Value fc;
    fc["headBlockHash"] = "0x1111111111111111111111111111111111111111111111111111111111111111";
    fc["safeBlockHash"] = "0x2222222222222222222222222222222222222222222222222222222222222222";
    fc["finalizedBlockHash"] = "0x3333333333333333333333333333333333333333333333333333333333333333";
    params.append(fc);
    Json::Value attrs;
    attrs["timestamp"] = "0x1";
    attrs["prevRandao"] = "0x4444444444444444444444444444444444444444444444444444444444444444";
    attrs["suggestedFeeRecipient"] = "0x5555555555555555555555555555555555555555";
    params.append(attrs);

    Json::Value response;
    task::wait([&](OPEngineEndpoints* ep, Json::Value p, Json::Value& r) -> task::Task<void> {
        co_await ep->forkchoiceUpdatedV2(p, r);
    }(endpoints.get(), params, response));

    BOOST_CHECK(response.isMember("payloadStatus"));
    BOOST_CHECK_EQUAL(response["payloadStatus"]["status"].asString(), "VALID");

    BOOST_REQUIRE(mockService.m_state->capturedForkchoiceVersion.has_value());
    BOOST_CHECK_EQUAL(*mockService.m_state->capturedForkchoiceVersion, 2);
}

BOOST_AUTO_TEST_CASE(forkchoiceUpdatedV3)
{
    Json::Value params(Json::arrayValue);
    Json::Value fc;
    fc["headBlockHash"] = "0x1111111111111111111111111111111111111111111111111111111111111111";
    fc["safeBlockHash"] = "0x2222222222222222222222222222222222222222222222222222222222222222";
    fc["finalizedBlockHash"] = "0x3333333333333333333333333333333333333333333333333333333333333333";
    params.append(fc);
    Json::Value attrs;
    attrs["timestamp"] = "0x1";
    attrs["prevRandao"] = "0x4444444444444444444444444444444444444444444444444444444444444444";
    attrs["suggestedFeeRecipient"] = "0x5555555555555555555555555555555555555555";
    attrs["parentBeaconBlockRoot"] = "0x6666666666666666666666666666666666666666666666666666666666666666";
    params.append(attrs);

    Json::Value response;
    task::wait([&](OPEngineEndpoints* ep, Json::Value p, Json::Value& r) -> task::Task<void> {
        co_await ep->forkchoiceUpdatedV3(p, r);
    }(endpoints.get(), params, response));

    BOOST_CHECK(response.isMember("payloadStatus"));
    BOOST_CHECK_EQUAL(response["payloadStatus"]["status"].asString(), "VALID");

    BOOST_REQUIRE(mockService.m_state->capturedForkchoiceVersion.has_value());
    BOOST_CHECK_EQUAL(*mockService.m_state->capturedForkchoiceVersion, 3);
}

BOOST_AUTO_TEST_CASE(forkchoiceUpdatedV4)
{
    Json::Value params(Json::arrayValue);
    Json::Value fc;
    fc["headBlockHash"] = "0x1111111111111111111111111111111111111111111111111111111111111111";
    fc["safeBlockHash"] = "0x2222222222222222222222222222222222222222222222222222222222222222";
    fc["finalizedBlockHash"] = "0x3333333333333333333333333333333333333333333333333333333333333333";
    params.append(fc);

    Json::Value response;
    BOOST_CHECK_THROW(
        task::wait([&](OPEngineEndpoints* ep, Json::Value p, Json::Value& r) -> task::Task<void> {
            co_await ep->forkchoiceUpdatedV4(p, r);
        }(endpoints.get(), params, response)),
        JsonRpcException);
}

// ================================================================
// getPayload
// ================================================================
BOOST_AUTO_TEST_CASE(getPayloadV1)
{
    // Prepare a minimal payload in the mock
    mockService.m_state->getPayloadResult.executionPayload.parentHash =
        h256("1111111111111111111111111111111111111111111111111111111111111111");
    mockService.m_state->getPayloadResult.executionPayload.blockHash =
        h256("2222222222222222222222222222222222222222222222222222222222222222");

    Json::Value params(Json::arrayValue);
    params.append("0x0000000021f32cc1");

    Json::Value response;
    task::wait([&](OPEngineEndpoints* ep, Json::Value p, Json::Value& r) -> task::Task<void> {
        co_await ep->getPayloadV1(p, r);
    }(endpoints.get(), params, response));

    BOOST_CHECK(response.isMember("parentHash"));
    BOOST_CHECK_EQUAL(response["parentHash"].asString(),
        "0x1111111111111111111111111111111111111111111111111111111111111111");

    BOOST_REQUIRE(mockService.m_state->capturedPayloadId.has_value());
    BOOST_CHECK_EQUAL(*mockService.m_state->capturedPayloadId, "0x0000000021f32cc1");
    BOOST_REQUIRE(mockService.m_state->capturedGetPayloadVersion.has_value());
    BOOST_CHECK_EQUAL(*mockService.m_state->capturedGetPayloadVersion, 1);
}

BOOST_AUTO_TEST_CASE(getPayloadV3)
{
    mockService.m_state->getPayloadResult.executionPayload.parentHash =
        h256("1111111111111111111111111111111111111111111111111111111111111111");
    mockService.m_state->getPayloadResult.executionPayload.blockHash =
        h256("2222222222222222222222222222222222222222222222222222222222222222");
    mockService.m_state->getPayloadResult.blockValue = u256(100);

    Json::Value params(Json::arrayValue);
    params.append("0x0000000021f32cc1");

    Json::Value response;
    task::wait([&](OPEngineEndpoints* ep, Json::Value p, Json::Value& r) -> task::Task<void> {
        co_await ep->getPayloadV3(p, r);
    }(endpoints.get(), params, response));

    BOOST_CHECK(response.isMember("executionPayload"));
    BOOST_CHECK(response.isMember("blockValue"));
    BOOST_CHECK(response.isMember("blobsBundle"));
    BOOST_CHECK(response.isMember("shouldOverrideBuilder"));

    BOOST_REQUIRE(mockService.m_state->capturedGetPayloadVersion.has_value());
    BOOST_CHECK_EQUAL(*mockService.m_state->capturedGetPayloadVersion, 3);
}

BOOST_AUTO_TEST_CASE(getPayloadV4)
{
    Json::Value params(Json::arrayValue);
    params.append("0x0000000021f32cc1");

    Json::Value response;
    BOOST_CHECK_THROW(
        task::wait([&](OPEngineEndpoints* ep, Json::Value p, Json::Value& r) -> task::Task<void> {
            co_await ep->getPayloadV4(p, r);
        }(endpoints.get(), params, response)),
        JsonRpcException);
}

// ================================================================
// newPayload
// ================================================================
BOOST_AUTO_TEST_CASE(newPayloadV1)
{
    Json::Value params(Json::arrayValue);
    Json::Value ep;
    ep["parentHash"] = "0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    ep["feeRecipient"] = "0xbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
    ep["stateRoot"] = "0xcccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc";
    ep["receiptsRoot"] = "0xdddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd";
    ep["logsBloom"] = "0x" + std::string(512, '0');
    ep["prevRandao"] = "0xeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee";
    ep["blockNumber"] = "0x1";
    ep["gasLimit"] = "0x5208";
    ep["gasUsed"] = "0x0";
    ep["timestamp"] = "0x1";
    ep["extraData"] = "0x1234";
    ep["baseFeePerGas"] = "0x1";
    ep["blockHash"] = "0xffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff";
    ep["transactions"] = Json::Value(Json::arrayValue);
    params.append(ep);

    Json::Value response;
    task::wait([&](OPEngineEndpoints* epObj, Json::Value p, Json::Value& r) -> task::Task<void> {
        co_await epObj->newPayloadV1(p, r);
    }(endpoints.get(), params, response));

    BOOST_CHECK(response.isMember("status"));
    BOOST_CHECK_EQUAL(response["status"].asString(), "VALID");
    BOOST_REQUIRE(mockService.m_state->capturedNewPayloadVersion.has_value());
    BOOST_CHECK_EQUAL(*mockService.m_state->capturedNewPayloadVersion, 1);
}

BOOST_AUTO_TEST_CASE(newPayloadV2)
{
    // Build a transaction
    auto tx = m_blockFactory->transactionFactory()->createTransaction(
        0, "0xabcdabcdabcdabcdabcdabcdabcdabcdabcdabcd", bytes{0x12, 0x34}, "nonce-1", 100,
        chainId, groupId, static_cast<int64_t>(utcTime()));
    bytes encodedTx;
    tx->encode(encodedTx);
    auto encodedTxHex = toHexStringWithPrefix(encodedTx);

    auto const largeQuantity = std::string("0x100000000000000000");

    Json::Value params(Json::arrayValue);
    Json::Value ep;
    ep["parentHash"] = "0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    ep["feeRecipient"] = "0xbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
    ep["stateRoot"] = "0xcccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc";
    ep["receiptsRoot"] = "0xdddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd";
    ep["logsBloom"] = "0x" + std::string(512, '0');
    ep["prevRandao"] = "0xeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee";
    ep["blockNumber"] = "0x1";
    ep["gasLimit"] = "0x5208";
    ep["gasUsed"] = "0x5208";
    ep["timestamp"] = "0x1";
    ep["extraData"] = "0x1234";
    ep["baseFeePerGas"] = "0x1";
    ep["blockHash"] = "0xffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff";
    ep["transactions"] = Json::Value(Json::arrayValue);
    ep["transactions"].append(encodedTxHex);

    // Withdrawals
    Json::Value w;
    w["index"] = "0x1";
    w["validatorIndex"] = "0x2";
    w["address"] = "0x7777777777777777777777777777777777777777";
    w["amount"] = largeQuantity;
    ep["withdrawals"].append(w);

    // Blob gas for V2 parsing — spec says V2 may have blob fields
    ep["blobGasUsed"] = largeQuantity;
    ep["excessBlobGas"] = largeQuantity;

    params.append(ep);

    Json::Value response;
    task::wait([&](OPEngineEndpoints* epObj, Json::Value p, Json::Value& r) -> task::Task<void> {
        co_await epObj->newPayloadV2(p, r);
    }(endpoints.get(), params, response));

    BOOST_CHECK(response.isMember("status"));
    BOOST_CHECK_EQUAL(response["status"].asString(), "VALID");

    BOOST_REQUIRE(mockService.m_state->capturedNewPayloadRequest.has_value());
    auto& capturedReq = *mockService.m_state->capturedNewPayloadRequest;
    BOOST_CHECK_EQUAL(capturedReq.executionPayload.transactions.size(), 1);
    BOOST_REQUIRE(capturedReq.executionPayload.withdrawals.has_value());
    BOOST_CHECK_EQUAL(capturedReq.executionPayload.withdrawals->front().amount,
        fromBigQuantity(largeQuantity));
    BOOST_REQUIRE(capturedReq.executionPayload.blobGasUsed.has_value());
    BOOST_CHECK_EQUAL(
        *capturedReq.executionPayload.blobGasUsed, fromBigQuantity(largeQuantity));
    BOOST_REQUIRE(capturedReq.executionPayload.excessBlobGas.has_value());
    BOOST_CHECK_EQUAL(
        *capturedReq.executionPayload.excessBlobGas, fromBigQuantity(largeQuantity));
    BOOST_REQUIRE(mockService.m_state->capturedNewPayloadVersion.has_value());
    BOOST_CHECK_EQUAL(*mockService.m_state->capturedNewPayloadVersion, 2);
}

BOOST_AUTO_TEST_CASE(newPayloadV3)
{
    auto tx = m_blockFactory->transactionFactory()->createTransaction(
        0, "0xabcdabcdabcdabcdabcdabcdabcdabcdabcdabcd", bytes{0x12, 0x34}, "nonce-1", 100,
        chainId, groupId, static_cast<int64_t>(utcTime()));
    bytes encodedTx;
    tx->encode(encodedTx);
    auto encodedTxHex = toHexStringWithPrefix(encodedTx);

    Json::Value params(Json::arrayValue);
    Json::Value ep;
    ep["parentHash"] = "0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    ep["feeRecipient"] = "0xbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
    ep["stateRoot"] = "0xcccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc";
    ep["receiptsRoot"] = "0xdddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd";
    ep["logsBloom"] = "0x" + std::string(512, '0');
    ep["prevRandao"] = "0xeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee";
    ep["blockNumber"] = "0x1";
    ep["gasLimit"] = "0x5208";
    ep["gasUsed"] = "0x0";
    ep["timestamp"] = "0x1";
    ep["extraData"] = "0x1234";
    ep["baseFeePerGas"] = "0x1";
    ep["blockHash"] = "0xffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff";
    ep["transactions"] = Json::Value(Json::arrayValue);
    ep["transactions"].append(encodedTxHex);
    ep["withdrawals"] = Json::Value(Json::arrayValue);
    ep["blobGasUsed"] = "0x0";
    ep["excessBlobGas"] = "0x0";
    params.append(ep);

    // expectedBlobVersionedHashes
    Json::Value blobHashes(Json::arrayValue);
    blobHashes.append("0x000657f37554c781402a22917dee2f75def7ab966d7b770905398eba3c444014");
    params.append(blobHashes);

    // parentBeaconBlockRoot
    params.append("0x169630f535b4a41330164c6e5c92b1224c0c407f582d407d0ac3d206cd32fd52");

    Json::Value response;
    task::wait([&](OPEngineEndpoints* epObj, Json::Value p, Json::Value& r) -> task::Task<void> {
        co_await epObj->newPayloadV3(p, r);
    }(endpoints.get(), params, response));

    BOOST_CHECK(response.isMember("status"));
    BOOST_CHECK_EQUAL(response["status"].asString(), "VALID");
    BOOST_REQUIRE(mockService.m_state->capturedNewPayloadVersion.has_value());
    BOOST_CHECK_EQUAL(*mockService.m_state->capturedNewPayloadVersion, 3);
}

BOOST_AUTO_TEST_CASE(newPayloadV4)
{
    Json::Value params(Json::arrayValue);
    Json::Value ep;
    ep["parentHash"] = "0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    ep["feeRecipient"] = "0xbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
    ep["stateRoot"] = "0xcccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc";
    ep["receiptsRoot"] = "0xdddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd";
    ep["logsBloom"] = "0x" + std::string(512, '0');
    ep["prevRandao"] = "0xeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee";
    ep["blockNumber"] = "0x1";
    ep["gasLimit"] = "0x5208";
    ep["gasUsed"] = "0x0";
    ep["timestamp"] = "0x1";
    ep["extraData"] = "0x1234";
    ep["baseFeePerGas"] = "0x1";
    ep["blockHash"] = "0xffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff";
    ep["transactions"] = Json::Value(Json::arrayValue);
    ep["withdrawals"] = Json::Value(Json::arrayValue);
    ep["blobGasUsed"] = "0x0";
    ep["excessBlobGas"] = "0x0";
    params.append(ep);
    params.append(Json::Value(Json::arrayValue));
    params.append("0x0000000000000000000000000000000000000000000000000000000000000000");
    params.append(Json::Value(Json::arrayValue));

    Json::Value response;
    BOOST_CHECK_THROW(
        task::wait([&](OPEngineEndpoints* epObj, Json::Value p, Json::Value& r) -> task::Task<void> {
            co_await epObj->newPayloadV4(p, r);
        }(endpoints.get(), params, response)),
        JsonRpcException);
}

// ================================================================
// Error cases
// ================================================================

BOOST_AUTO_TEST_CASE(engineNotAvailable)
{
    // Create endpoints without setting engine service (default from NodeService is null)
    auto nullNodeService = std::make_shared<rpc::NodeService>(
        m_ledger, scheduler, txPool, nullptr, nullptr, m_blockFactory, nullptr);
    OPEngineEndpoints nullEndpoints(nullNodeService);

    Json::Value params(Json::arrayValue);
    params.append("test");
    Json::Value response;

    BOOST_CHECK_THROW(
        task::wait([&](OPEngineEndpoints* ep, Json::Value p, Json::Value& r) -> task::Task<void> {
            co_await ep->exchangeCapabilities(p, r);
        }(nullEndpoints.get(), params, response)),
        JsonRpcException);
}

BOOST_AUTO_TEST_CASE(getPayloadInvalidParams)
{
    Json::Value params(Json::arrayValue);
    params.append(42);  // not a string

    Json::Value response;
    BOOST_CHECK_THROW(
        task::wait([&](OPEngineEndpoints* ep, Json::Value p, Json::Value& r) -> task::Task<void> {
            co_await ep->getPayloadV1(p, r);
        }(endpoints.get(), params, response)),
        JsonRpcException);
}

BOOST_AUTO_TEST_CASE(exchangeCapabilitiesNonString)
{
    Json::Value params(Json::arrayValue);
    params.append(123);  // number, not a string

    Json::Value response;
    BOOST_CHECK_THROW(
        task::wait([&](OPEngineEndpoints* ep, Json::Value p, Json::Value& r) -> task::Task<void> {
            co_await ep->exchangeCapabilities(p, r);
        }(endpoints.get(), params, response)),
        JsonRpcException);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
