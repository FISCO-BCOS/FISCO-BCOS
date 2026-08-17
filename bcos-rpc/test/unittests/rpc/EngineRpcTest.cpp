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
 * @brief Unit tests for Engine API through Endpoints/EngineEndpoint
 */

#include "../common/RPCFixture.h"
#include <bcos-codec/wrapper/CodecWrapper.h>
#include <bcos-framework/engine/AnyEngineService.h>
#include <bcos-rpc/web3jsonrpc/endpoints/Endpoints.h>
#include <bcos-rpc/web3jsonrpc/utils/Common.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <json/json.h>
#include <memory>

using namespace bcos;
using namespace bcos::rpc;
using namespace bcos::crypto;

namespace bcos::test
{
class MockOpEngineService
{
public:
    struct State
    {
        std::vector<std::string> capturedRemoteCaps;
        engine::ForkchoiceUpdatedResult forkchoiceUpdatedResult{
            .payloadStatus =
                engine::PayloadStatus{
                    .latestValidHash = std::nullopt,
                    .validationError = std::nullopt,
                    .status = engine::PayloadValidationStatus::Valid,
                },
            .payloadId = std::nullopt,
        };
        std::optional<engine::ForkchoiceState> capturedForkchoiceState;
        std::optional<int> capturedForkchoiceVersion;
        engine::GetPayloadResult getPayloadResult = std::make_unique<engine::GetPayloadData>();
        std::optional<engine::PayloadID> capturedPayloadId;
        std::optional<std::uint32_t> capturedGetPayloadVersion;
        std::optional<engine::NewPayloadRequest> capturedNewPayloadRequest;
        std::optional<std::uint32_t> capturedNewPayloadVersion;
        bool throwUnknownPayload = false;
    };
    std::shared_ptr<State> m_state = std::make_shared<State>();

    task::Task<std::vector<std::string>> exchangeCapabilities(
        std::vector<std::string> remoteCapabilities)
    {
        m_state->capturedRemoteCaps = remoteCapabilities;
        // Return a copy to avoid potential proxy coroutine return issues
        co_return std::vector<std::string>(remoteCapabilities);
    }

    task::Task<engine::ForkchoiceUpdatedResult> updateForkchoice(
        const engine::ForkchoiceState& forkchoiceState, const engine::PayloadAttributes*,
        std::uint32_t version)
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
        if (m_state->throwUnknownPayload)
        {
            BOOST_THROW_EXCEPTION(engine::UnknownPayload{});
        }
        co_return std::make_unique<engine::GetPayloadData>(*m_state->getPayloadResult);
    }

    task::Task<engine::PayloadStatus> newPayload(
        const engine::NewPayloadRequest& request, std::uint32_t version)
    {
        m_state->capturedNewPayloadRequest = request;
        m_state->capturedNewPayloadVersion = version;
        co_return m_state->forkchoiceUpdatedResult.payloadStatus;
    }

    std::optional<bcos::protocol::BlockNumber> getSafeBlockNumber() const { return std::nullopt; }
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
        nodeService->engineService() =
            std::make_shared<bcos::engine::AnyEngineService>(mockService);
        endpoints = std::make_unique<Endpoints>(nodeService, nullptr, false);
    }

    std::unique_ptr<Endpoints> endpoints;
    MockOpEngineService mockService;
};

// Helper macro: call an EngineEndpoint method through Endpoints
#define CALL_ENGINE(method, params, response)                                          \
    task::wait([&](Endpoints* ep, Json::Value p, Json::Value& r) -> task::Task<void> { \
        co_await ep->method(p, r);                                                     \
    }(endpoints.get(), params, response))

BOOST_FIXTURE_TEST_SUITE(EngineRpcTest, EngineRpcTestFixture)

BOOST_AUTO_TEST_CASE(exchangeCapabilities)
{
    Json::Value params(Json::arrayValue);
    params.append("engine_newPayloadV2");
    params.append("engine_newPayloadV3");

    Json::Value response;
    CALL_ENGINE(exchangeCapabilities, params, response);

    BOOST_CHECK(response.isMember("jsonrpc"));
    BOOST_CHECK(response.isMember("result"));
}

// Karst-only surface: every pre-Karst method version answers -38005 without reaching
// the engine service (op-geth answers engine.UnsupportedFork for wrong-fork versions).
BOOST_AUTO_TEST_CASE(preKarstVersionsRejected)
{
    for (auto method : {&Endpoints::forkchoiceUpdatedV1, &Endpoints::forkchoiceUpdatedV2,
             &Endpoints::getPayloadV1, &Endpoints::getPayloadV2, &Endpoints::getPayloadV3,
             &Endpoints::newPayloadV1, &Endpoints::newPayloadV2, &Endpoints::newPayloadV3})
    {
        Json::Value params(Json::arrayValue);
        Json::Value response;
        task::wait([&](Endpoints* ep, Json::Value p, Json::Value& r) -> task::Task<void> {
            co_await (ep->*method)(p, r);
        }(endpoints.get(), params, response));

        BOOST_CHECK(response.isMember("error"));
        BOOST_CHECK_EQUAL(response["error"]["code"].asInt(), EngineError::UnsupportedFork);
    }
    // None of the rejected calls may have reached the engine service.
    BOOST_CHECK(!mockService.m_state->capturedForkchoiceVersion.has_value());
    BOOST_CHECK(!mockService.m_state->capturedGetPayloadVersion.has_value());
    BOOST_CHECK(!mockService.m_state->capturedNewPayloadVersion.has_value());
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
    attrs["parentBeaconBlockRoot"] =
        "0x6666666666666666666666666666666666666666666666666666666666666666";
    params.append(attrs);

    Json::Value response;
    CALL_ENGINE(forkchoiceUpdatedV3, params, response);

    BOOST_CHECK(response["result"].isMember("payloadStatus"));
    BOOST_CHECK_EQUAL(response["result"]["payloadStatus"]["status"].asString(), "VALID");

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
    CALL_ENGINE(forkchoiceUpdatedV4, params, response);

    BOOST_CHECK(response.isMember("error"));
    BOOST_CHECK_EQUAL(response["error"]["code"].asInt(), EngineError::UnsupportedFork);
}

BOOST_AUTO_TEST_CASE(getPayloadV4)
{
    Json::Value params(Json::arrayValue);
    params.append("0x0000000021f32cc1");

    Json::Value response;
    CALL_ENGINE(getPayloadV4, params, response);

    BOOST_CHECK(response.isMember("error"));
    BOOST_CHECK_EQUAL(response["error"]["code"].asInt(), EngineError::UnsupportedFork);
}

BOOST_AUTO_TEST_CASE(getPayloadV5)
{
    mockService.m_state->getPayloadResult->executionPayload.parentHash =
        h256("1111111111111111111111111111111111111111111111111111111111111111");
    mockService.m_state->getPayloadResult->executionPayload.blockHash =
        h256("2222222222222222222222222222222222222222222222222222222222222222");
    mockService.m_state->getPayloadResult->executionPayload.withdrawals =
        std::vector<engine::WithdrawalV1>{};
    mockService.m_state->getPayloadResult->executionPayload.withdrawalsRoot = h256{};
    mockService.m_state->getPayloadResult->blockValue = u256(100);
    mockService.m_state->getPayloadResult->executionRequests = std::vector<bytes>{};

    Json::Value params(Json::arrayValue);
    params.append("0x0000000021f32cc1");

    Json::Value response;
    CALL_ENGINE(getPayloadV5, params, response);

    // op-node's expected V5 response shape: executionPayload (V4 form, with
    // withdrawalsRoot) + blockValue + blobsBundle (three empty arrays) +
    // shouldOverrideBuilder + executionRequests=[].
    auto const& result = response["result"];
    BOOST_CHECK(result.isMember("executionPayload"));
    BOOST_CHECK(result["executionPayload"].isMember("withdrawalsRoot"));
    BOOST_CHECK(result.isMember("blockValue"));
    BOOST_REQUIRE(result.isMember("blobsBundle"));
    BOOST_CHECK(result["blobsBundle"]["commitments"].isArray());
    BOOST_CHECK_EQUAL(result["blobsBundle"]["commitments"].size(), 0);
    BOOST_CHECK_EQUAL(result["blobsBundle"]["proofs"].size(), 0);
    BOOST_CHECK_EQUAL(result["blobsBundle"]["blobs"].size(), 0);
    BOOST_REQUIRE(result.isMember("shouldOverrideBuilder"));
    BOOST_CHECK_EQUAL(result["shouldOverrideBuilder"].asBool(), false);
    BOOST_REQUIRE(result.isMember("executionRequests"));
    BOOST_CHECK(result["executionRequests"].isArray());
    BOOST_CHECK_EQUAL(result["executionRequests"].size(), 0);
    BOOST_REQUIRE(mockService.m_state->capturedGetPayloadVersion.has_value());
    BOOST_CHECK_EQUAL(*mockService.m_state->capturedGetPayloadVersion, 5);
}

BOOST_AUTO_TEST_CASE(getPayloadV5UnknownPayload)
{
    mockService.m_state->throwUnknownPayload = true;

    Json::Value params(Json::arrayValue);
    params.append("0x00000000deadbeef");
    Json::Value response;

    // The service's typed UnknownPayload surfaces as Engine error -38001.
    BOOST_CHECK_EXCEPTION(CALL_ENGINE(getPayloadV5, params, response), JsonRpcException,
        [](JsonRpcException const& e) { return e.code() == EngineError::UnknownPayload; });
}

BOOST_AUTO_TEST_CASE(getPayloadV5MissingParams)
{
    Json::Value params(Json::arrayValue);
    Json::Value response;

    BOOST_CHECK_EXCEPTION(CALL_ENGINE(getPayloadV5, params, response), JsonRpcException,
        [](JsonRpcException const& e) { return e.code() == InvalidParams; });
}

namespace
{
/// Base V4 (Isthmus-shape) executionPayload JSON accepted by engine_newPayloadV4.
Json::Value makeV4ExecutionPayloadJson()
{
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
    ep["withdrawalsRoot"] = "0x9999999999999999999999999999999999999999999999999999999999999999";
    return ep;
}

constexpr auto c_beaconRootHex =
    "0x169630f535b4a41330164c6e5c92b1224c0c407f582d407d0ac3d206cd32fd52";
}  // namespace

BOOST_AUTO_TEST_CASE(newPayloadV4)
{
    auto tx = m_blockFactory->transactionFactory()->createTransaction(0,
        "0xabcdabcdabcdabcdabcdabcdabcdabcdabcdabcd", bytes{0x12, 0x34}, "nonce-1", 100, chainId,
        groupId, static_cast<int64_t>(utcTime()));
    bytes encodedTx;
    tx->encode(encodedTx);
    auto encodedTxHex = toHexStringWithPrefix(encodedTx);

    auto ep = makeV4ExecutionPayloadJson();
    ep["transactions"].append(encodedTxHex);

    Json::Value params(Json::arrayValue);
    params.append(ep);
    params.append(Json::Value(Json::arrayValue));  // expectedBlobVersionedHashes (empty)
    params.append(c_beaconRootHex);                // parentBeaconBlockRoot
    params.append(Json::Value(Json::arrayValue));  // executionRequests (empty)

    Json::Value response;
    CALL_ENGINE(newPayloadV4, params, response);

    BOOST_CHECK(response["result"].isMember("status"));
    BOOST_CHECK_EQUAL(response["result"]["status"].asString(), "VALID");
    BOOST_REQUIRE(mockService.m_state->capturedNewPayloadVersion.has_value());
    BOOST_CHECK_EQUAL(*mockService.m_state->capturedNewPayloadVersion, 4);

    BOOST_REQUIRE(mockService.m_state->capturedNewPayloadRequest.has_value());
    auto& capturedReq = *mockService.m_state->capturedNewPayloadRequest;
    // Raw-bytes carrier: newPayload preserves the wire bytes verbatim (no decoding).
    BOOST_REQUIRE_EQUAL(capturedReq.executionPayload.transactions.size(), 1);
    BOOST_CHECK_EQUAL(
        toHexStringWithPrefix(capturedReq.executionPayload.transactions.front().raw), encodedTxHex);
    // V4-only fields all made it through the parse.
    BOOST_REQUIRE(capturedReq.executionPayload.withdrawalsRoot.has_value());
    BOOST_CHECK_EQUAL(capturedReq.executionPayload.withdrawalsRoot->hexPrefixed(),
        "0x9999999999999999999999999999999999999999999999999999999999999999");
    BOOST_REQUIRE(capturedReq.parentBeaconBlockRoot.has_value());
    BOOST_CHECK_EQUAL(capturedReq.parentBeaconBlockRoot->hexPrefixed(), c_beaconRootHex);
    BOOST_CHECK(capturedReq.expectedBlobVersionedHashes.empty());
    BOOST_REQUIRE(capturedReq.executionRequests.has_value());
    BOOST_CHECK(capturedReq.executionRequests->empty());
}

BOOST_AUTO_TEST_CASE(newPayloadV4MissingParams)
{
    // All four params are required — dropping the executionRequests tail (or the whole
    // params array) is a shape error, not a payload-validity error.
    auto expectInvalidParams = [&](Json::Value params) {
        Json::Value response;
        BOOST_CHECK_EXCEPTION(CALL_ENGINE(newPayloadV4, params, response), JsonRpcException,
            [](JsonRpcException const& e) { return e.code() == InvalidParams; });
    };

    Json::Value threeParams(Json::arrayValue);
    threeParams.append(makeV4ExecutionPayloadJson());
    threeParams.append(Json::Value(Json::arrayValue));
    threeParams.append(c_beaconRootHex);
    expectInvalidParams(threeParams);

    // Missing withdrawalsRoot inside the payload is likewise InvalidParams at V4.
    auto epWithoutRoot = makeV4ExecutionPayloadJson();
    epWithoutRoot.removeMember("withdrawalsRoot");
    Json::Value noRootParams(Json::arrayValue);
    noRootParams.append(epWithoutRoot);
    noRootParams.append(Json::Value(Json::arrayValue));
    noRootParams.append(c_beaconRootHex);
    noRootParams.append(Json::Value(Json::arrayValue));
    expectInvalidParams(noRootParams);

    // Null parentBeaconBlockRoot is a shape error too.
    Json::Value nullBeaconParams(Json::arrayValue);
    nullBeaconParams.append(makeV4ExecutionPayloadJson());
    nullBeaconParams.append(Json::Value(Json::arrayValue));
    nullBeaconParams.append(Json::Value(Json::nullValue));
    nullBeaconParams.append(Json::Value(Json::arrayValue));
    expectInvalidParams(nullBeaconParams);

    // Malformed (non-hex) withdrawalsRoot maps to InvalidParams, not InternalError:
    // parseH256 wraps fromHex's BadHexCharacter.
    auto epBadRoot = makeV4ExecutionPayloadJson();
    epBadRoot["withdrawalsRoot"] =
        "0xzz99999999999999999999999999999999999999999999999999999999999999";
    Json::Value badRootParams(Json::arrayValue);
    badRootParams.append(epBadRoot);
    badRootParams.append(Json::Value(Json::arrayValue));
    badRootParams.append(c_beaconRootHex);
    badRootParams.append(Json::Value(Json::arrayValue));
    expectInvalidParams(badRootParams);
}

// Only the last case here is new behavior; the four before it were already rejected by the
// pre-existing tail gate and are kept as regression guards for the gate's move to the top
// of parseNewPayloadRequest.
BOOST_AUTO_TEST_CASE(newPayloadV4RejectsMalformedParamShapes)
{
    auto expectInvalidParams = [&](Json::Value params) {
        Json::Value response;
        BOOST_CHECK_EXCEPTION(CALL_ENGINE(newPayloadV4, params, response), JsonRpcException,
            [](JsonRpcException const& e) { return e.code() == InvalidParams; });
    };
    auto makeParams = [](Json::Value blobHashes, Json::Value beaconRoot,
                          Json::Value executionRequests) {
        Json::Value params(Json::arrayValue);
        params.append(makeV4ExecutionPayloadJson());
        params.append(std::move(blobHashes));
        params.append(std::move(beaconRoot));
        params.append(std::move(executionRequests));
        return params;
    };
    auto const emptyArray = Json::Value(Json::arrayValue);

    // expectedBlobVersionedHashes must be an array, not a string.
    expectInvalidParams(makeParams("notarray", c_beaconRootHex, emptyArray));
    // executionRequests must be an array, not a string.
    expectInvalidParams(makeParams(emptyArray, c_beaconRootHex, "notarray"));
    // executionRequests entries must be hex strings, not numbers...
    Json::Value numericRequests(Json::arrayValue);
    numericRequests.append(123);
    expectInvalidParams(makeParams(emptyArray, c_beaconRootHex, numericRequests));
    // ...and not malformed hex.
    Json::Value badHexRequests(Json::arrayValue);
    badHexRequests.append("0xzz");
    expectInvalidParams(makeParams(emptyArray, c_beaconRootHex, badHexRequests));
    // A numeric parentBeaconBlockRoot is a shape error, and must be reported as one. The
    // message matters here, not just the code: the V3 parse block consumes params[2] with
    // parseH256, which turns 123 into the string "123" and fails on its length — so
    // without the up-front shape gate this still answers InvalidParams, but blames the
    // h256 length instead of naming the four-parameter shape.
    {
        Json::Value response;
        BOOST_CHECK_EXCEPTION(
            CALL_ENGINE(newPayloadV4, makeParams(emptyArray, 123, emptyArray), response),
            JsonRpcException, [](JsonRpcException const& e) {
                return e.code() == InvalidParams &&
                       e.msg().find("engine_newPayloadV4 expects") != std::string::npos;
            });
    }

    // None of the rejected calls may have reached the engine service.
    BOOST_CHECK(!mockService.m_state->capturedNewPayloadVersion.has_value());
}

BOOST_AUTO_TEST_CASE(newPayloadV4RequiresV2AndV3PayloadFields)
{
    auto expectInvalidParams = [&](Json::Value const& executionPayload) {
        Json::Value params(Json::arrayValue);
        params.append(executionPayload);
        params.append(Json::Value(Json::arrayValue));
        params.append(c_beaconRootHex);
        params.append(Json::Value(Json::arrayValue));
        Json::Value response;
        BOOST_CHECK_EXCEPTION(CALL_ENGINE(newPayloadV4, params, response), JsonRpcException,
            [](JsonRpcException const& e) { return e.code() == InvalidParams; });
    };

    // op-geth NewPayloadV4 answers -32602 for a nil withdrawals / blobGasUsed /
    // excessBlobGas, not an INVALID payload status (eth/catalyst/api.go:745-750).
    for (auto const* field : {"withdrawals", "blobGasUsed", "excessBlobGas"})
    {
        auto ep = makeV4ExecutionPayloadJson();
        ep.removeMember(field);
        expectInvalidParams(ep);
        // Explicit JSON null is rejected the same way as an absent member.
        auto nullEp = makeV4ExecutionPayloadJson();
        nullEp[field] = Json::Value(Json::nullValue);
        expectInvalidParams(nullEp);
    }
    BOOST_CHECK(!mockService.m_state->capturedNewPayloadVersion.has_value());
}

BOOST_AUTO_TEST_CASE(newPayloadV4RejectsWrongTypedPayloadFields)
{
    auto expectInvalidParams = [&](Json::Value const& executionPayload) {
        Json::Value params(Json::arrayValue);
        params.append(executionPayload);
        params.append(Json::Value(Json::arrayValue));
        params.append(c_beaconRootHex);
        params.append(Json::Value(Json::arrayValue));
        Json::Value response;
        BOOST_CHECK_EXCEPTION(CALL_ENGINE(newPayloadV4, params, response), JsonRpcException,
            [](JsonRpcException const& e) { return e.code() == InvalidParams; });
    };

    // Presence alone is not enough. jsoncpp iterates a non-array as an EMPTY range, so a
    // string withdrawals would otherwise be silently accepted as a valid empty list —
    // fail-open on exactly the field Isthmus cares about.
    auto stringWithdrawals = makeV4ExecutionPayloadJson();
    stringWithdrawals["withdrawals"] = "garbage";
    expectInvalidParams(stringWithdrawals);

    // And asString() on an array/object throws Json::LogicError, which would escape the
    // parse as -32603 InternalError instead of naming the offending field.
    for (auto const* field : {"blobGasUsed", "excessBlobGas", "withdrawalsRoot"})
    {
        auto arrayValued = makeV4ExecutionPayloadJson();
        arrayValued[field] = Json::Value(Json::arrayValue);
        expectInvalidParams(arrayValued);
    }

    // executionPayload itself must be an object; jsoncpp's isMember() asserts otherwise.
    Json::Value scalarPayloadParams(Json::arrayValue);
    scalarPayloadParams.append("not-an-object");
    scalarPayloadParams.append(Json::Value(Json::arrayValue));
    scalarPayloadParams.append(c_beaconRootHex);
    scalarPayloadParams.append(Json::Value(Json::arrayValue));
    Json::Value response;
    BOOST_CHECK_EXCEPTION(CALL_ENGINE(newPayloadV4, scalarPayloadParams, response),
        JsonRpcException, [](JsonRpcException const& e) { return e.code() == InvalidParams; });

    BOOST_CHECK(!mockService.m_state->capturedNewPayloadVersion.has_value());
}

BOOST_AUTO_TEST_CASE(newPayloadAndGetPayloadRoundTrip)
{
    auto tx = m_blockFactory->transactionFactory()->createTransaction(0,
        "0xabcdabcdabcdabcdabcdabcdabcdabcdabcdabcd", bytes{0x12, 0x34}, "nonce-1", 100, chainId,
        groupId, static_cast<int64_t>(utcTime()));
    bytes encodedTx;
    tx->encode(encodedTx);
    auto encodedTxHex = toHexStringWithPrefix(encodedTx);

    auto const largeQuantity = std::string("0x100000000000000000");
    auto const expectedLargeValue = fromBigQuantity(largeQuantity);

    // --- engine_newPayloadV4 ---
    auto ep = makeV4ExecutionPayloadJson();
    ep["gasUsed"] = "0x5208";
    ep["transactions"].append(encodedTxHex);
    Json::Value w;
    w["index"] = "0x1";
    w["validatorIndex"] = "0x2";
    w["address"] = "0x7777777777777777777777777777777777777777";
    w["amount"] = largeQuantity;
    ep["withdrawals"].append(w);

    Json::Value params(Json::arrayValue);
    params.append(ep);
    params.append(Json::Value(Json::arrayValue));
    params.append(c_beaconRootHex);
    params.append(Json::Value(Json::arrayValue));

    Json::Value newPayloadResponse;
    CALL_ENGINE(newPayloadV4, params, newPayloadResponse);

    BOOST_CHECK(newPayloadResponse["result"].isMember("status"));
    BOOST_CHECK_EQUAL(newPayloadResponse["result"]["status"].asString(), "VALID");
    BOOST_REQUIRE(mockService.m_state->capturedNewPayloadRequest.has_value());
    BOOST_REQUIRE(mockService.m_state->capturedNewPayloadVersion.has_value());
    BOOST_CHECK_EQUAL(*mockService.m_state->capturedNewPayloadVersion, 4);
    auto const& capturedPayload = mockService.m_state->capturedNewPayloadRequest->executionPayload;
    BOOST_CHECK_EQUAL(capturedPayload.transactions.size(), 1);
    BOOST_REQUIRE(capturedPayload.withdrawals.has_value());
    BOOST_CHECK_EQUAL(capturedPayload.withdrawals->front().amount, expectedLargeValue);

    // Raw-bytes carrier: newPayload preserves the wire bytes verbatim (no decoding).
    BOOST_CHECK_EQUAL(
        toHexStringWithPrefix(capturedPayload.transactions.front().raw), encodedTxHex);

    // --- engine_getPayloadV5 ---
    mockService.m_state->getPayloadResult->executionPayload = capturedPayload;
    mockService.m_state->getPayloadResult->blockValue = expectedLargeValue;
    mockService.m_state->getPayloadResult->executionRequests = std::vector<bytes>{};

    Json::Value getPayloadParams(Json::arrayValue);
    getPayloadParams.append("payload-id-1");

    Json::Value getPayloadResponse;
    CALL_ENGINE(getPayloadV5, getPayloadParams, getPayloadResponse);

    BOOST_REQUIRE(mockService.m_state->capturedPayloadId.has_value());
    BOOST_CHECK_EQUAL(*mockService.m_state->capturedPayloadId, "payload-id-1");
    BOOST_REQUIRE(mockService.m_state->capturedGetPayloadVersion.has_value());
    BOOST_CHECK_EQUAL(*mockService.m_state->capturedGetPayloadVersion, 5);
    auto const& result = getPayloadResponse["result"];
    BOOST_CHECK(result.isMember("executionPayload"));
    BOOST_CHECK_EQUAL(result["executionPayload"]["transactions"].size(), 1);
    // The transaction bytes that entered via newPayloadV4 come back byte-identical.
    BOOST_CHECK_EQUAL(result["executionPayload"]["transactions"][0u].asString(), encodedTxHex);
    BOOST_CHECK_EQUAL(
        result["executionPayload"]["withdrawals"][0u]["amount"].asString(), largeQuantity);
    BOOST_CHECK_EQUAL(result["executionPayload"]["withdrawalsRoot"].asString(),
        "0x9999999999999999999999999999999999999999999999999999999999999999");
    BOOST_CHECK_EQUAL(result["blockValue"].asString(), largeQuantity);
    BOOST_CHECK_EQUAL(result["executionRequests"].size(), 0);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
