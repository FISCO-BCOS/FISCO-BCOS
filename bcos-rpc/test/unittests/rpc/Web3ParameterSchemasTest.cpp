// Copyright (C) 2026 FISCO BCOS.
// SPDX-License-Identifier: Apache-2.0

#include <bcos-rpc/jsonrpc/Common.h>
#include <bcos-rpc/validator/JsonValidator.h>
#include <bcos-rpc/validator/Web3RpcSchema.h>
#include <bcos-rpc/web3jsonrpc/endpoints/EndpointsMapping.h>
#include <boost/test/unit_test.hpp>
#include <sstream>

using namespace bcos::rpc;

namespace
{
Json::Value parseJson(std::string_view source)
{
    Json::Value root;
    Json::CharReaderBuilder reader;
    std::istringstream input(std::string{source});
    BOOST_REQUIRE(Json::parseFromStream(reader, input, &root, nullptr));
    return root;
}

void accepts(std::string_view method, std::string_view params)
{
    BOOST_TEST_CONTEXT(method << " " << params)
    {
        BOOST_CHECK_NO_THROW(JsonValidator::requireParams(method, parseJson(params)));
    }
}

void rejects(std::string_view method, std::string_view params)
{
    BOOST_TEST_CONTEXT(method << " " << params)
    {
        BOOST_CHECK_EXCEPTION(JsonValidator::requireParams(method, parseJson(params)),
            JsonRpcException,
            [](JsonRpcException const& error) { return error.code() == InvalidParams; });
    }
}
}  // namespace

BOOST_AUTO_TEST_SUITE(Web3ParameterSchemas)

BOOST_AUTO_TEST_CASE(allEmbeddedMethodsAreExposedAndEnforceTheirArity)
{
    auto const specification = parseJson(schema::web3RpcSchema);
    BOOST_REQUIRE(specification["methods"].isArray());
    BOOST_REQUIRE_EQUAL(specification["methods"].size(), 62);
    EndpointsMapping const endpoints(true, true, true);
    Json::Value oversized(Json::arrayValue);
    oversized.resize(128);
    for (auto const& descriptor : specification["methods"])
    {
        auto const method = descriptor["name"].asString();
        BOOST_TEST_CONTEXT(method)
        {
            BOOST_CHECK(method == "eth_subscribe" || method == "eth_unsubscribe" ||
                        endpoints.findHandler(method).has_value());
            BOOST_CHECK_EXCEPTION(JsonValidator::requireParams(method, oversized), JsonRpcException,
                [](JsonRpcException const& error) { return error.code() == InvalidParams; });
        }
    }

    // This registered method has no OpenRPC method descriptor at the pinned revision.
    BOOST_REQUIRE(endpoints.findHandler("web3_sha3").has_value());
    BOOST_CHECK_NO_THROW(JsonValidator::requireParams("web3_sha3", oversized));
}

BOOST_AUTO_TEST_CASE(pilotTuplesAndStrictTypes)
{
    accepts("eth_getBalance", R"(["0x0000000000000000000000000000000000000000"])");
    rejects("eth_getBalance", R"(["0x0000000000000000000000000000000000000000",null])");
    rejects("eth_getBalance", R"(["0x0000000000000000000000000000000000000000",""])");
    rejects("eth_getBalance", R"(["0x0000000000000000000000000000000000000000","latest",false])");
    rejects("eth_getBalance", R"(["0x1234","latest"])");
    rejects("eth_getBalance", "[]");
    accepts("eth_getStorageAt", R"(["0x0000000000000000000000000000000000000000","0x"])");
    rejects(
        "eth_getStorageAt", R"(["0x0000000000000000000000000000000000000000","0xasdf","latest"])");
    rejects("eth_getStorageAt", R"(["0x0000000000000000000000000000000000000000",0])");
    rejects("eth_getStorageAt", R"(["0x0000000000000000000000000000000000000000","0x0",{}])");
    rejects("eth_getStorageAt", R"(["0x0000000000000000000000000000000000000000","0xA\u0000"])");
    rejects("eth_getStorageAt", R"(["0x0000000000000000000000000000000000000000","０"])");
    accepts("eth_getBlockByNumber", R"(["0x0",false])");
    accepts("eth_getBlockByNumber", R"(["0x7fffffffffffffff",true])");
    rejects("eth_getBlockByNumber", R"(["latest","false"])");
    rejects("eth_getBlockByNumber", R"(["latest",0])");
    rejects("eth_getBlockByNumber", R"(["latest",null])");
    rejects("eth_getBlockByNumber", R"(["latest\u0000",false])");
    rejects("eth_getBlockByNumber", R"(["0x0\n",false])");
    rejects("eth_getBlockByNumber", R"(["latest"])");
    rejects("eth_getBlockByNumber", R"([42,false])");
    rejects("eth_getBlockByNumber", R"([{"blockNumber":"0x0"},false])");
    rejects("eth_getBlockByNumber", R"([null,false])");
    rejects("eth_getBlockByNumber", R"(["0x",false])");
    rejects("eth_getBlockByNumber", R"(["0x-0",false])");
    rejects("eth_getBlockByNumber", R"(["0x+1",false])");
    rejects("eth_getBlockByNumber", R"(["0X1",false])");
    rejects("eth_getBlockByNumber", R"(["LATEST",false])");
}

BOOST_AUTO_TEST_CASE(eip1898ObjectShapes)
{
    accepts("eth_getBalance",
        R"(["0x7dcd17433742f4c0ca53122ab541d0ba67fc27df",{"blockHash":"0x6146b2fa7dd4d923ed43e2f25c8a2895369d15463ba974f83937f9a6fa58adbf","requireCanonical":true}])");
    accepts("eth_getBalance",
        R"(["0x7dcd17433742f4c0ca53122ab541d0ba67fc27df",{"blockNumber":"0x0"}])");
    rejects("eth_getBalance",
        R"(["0x7dcd17433742f4c0ca53122ab541d0ba67fc27df",{"blockNumber":"0x0","requireCanonical":true}])");
    rejects("eth_getBalance",
        R"(["0x7dcd17433742f4c0ca53122ab541d0ba67fc27df",{"blockHash":"0x6146b2fa7dd4d923ed43e2f25c8a2895369d15463ba974f83937f9a6fa58adbf","requireCanonical":"true"}])");
}

BOOST_AUTO_TEST_CASE(compoundArgumentShapes)
{
    accepts("eth_call", R"([{"to":null,"accessList":[]}])");
    rejects("eth_call", R"([{"accessList":[{"storageKeys":[]}]}])");
    rejects("eth_call",
        R"([{"accessList":[{"address":"0x0000000000000000000000000000000000000000","storageKeys":[false]}]}])");
    rejects("eth_call", R"([{"unknownProperty":true}])");

    accepts("eth_getLogs", R"([{"fromBlock":"0x0","toBlock":"latest","topics":[null,[]]}])");
    rejects("eth_getLogs", R"([{"fromBlock":"0x0","toBlock":"latest","topics":[false]}])");
    rejects("eth_getLogs", R"([{"blockHash":"0x0"}])");

    accepts("engine_exchangeCapabilities", R"([["engine_newPayloadV4"]])");
    rejects("engine_exchangeCapabilities", R"([[false]])");
    rejects("engine_forkchoiceUpdatedV1", R"([{}])");

    accepts("eth_subscribe", R"(["newHeads"])");
    accepts("eth_subscribe", R"(["newPendingTransactions",true])");
    rejects("eth_subscribe", R"([false])");
    rejects("eth_unsubscribe", R"([false])");
}

BOOST_AUTO_TEST_CASE(legacyEngineNullPayloadAttributesAndCallDataAlias)
{
    std::string const zeroHash = "0x" + std::string(64, '0');
    Json::Value state(Json::objectValue);
    state["headBlockHash"] = zeroHash;
    state["safeBlockHash"] = zeroHash;
    state["finalizedBlockHash"] = zeroHash;
    Json::Value params(Json::arrayValue);
    params.append(state);
    params.append(Json::nullValue);
    for (auto const* method : {"engine_forkchoiceUpdatedV1", "engine_forkchoiceUpdatedV2",
             "engine_forkchoiceUpdatedV3", "engine_forkchoiceUpdatedV4"})
    {
        BOOST_TEST_CONTEXT(method)
        {
            BOOST_CHECK_NO_THROW(JsonValidator::requireParams(method, params));
        }
    }

    for (auto const* method : {"eth_call", "eth_estimateGas"})
    {
        accepts(method, R"([{"data":"0xaabb"}])");
        rejects(method, R"([{"data":false}])");
    }
}

BOOST_AUTO_TEST_CASE(errorReportsTheFailingParameterPath)
{
    auto const params = parseJson(
        R"([{"accessList":[{"address":"0x0000000000000000000000000000000000000000","storageKeys":[false]}]}])");
    BOOST_CHECK_EXCEPTION(JsonValidator::requireParams("eth_call", params), JsonRpcException,
        [](JsonRpcException const& error) {
            return error.code() == InvalidParams &&
                   error.msg() == "Invalid params at /params/0/accessList/0/storageKeys/0";
        });
}

BOOST_AUTO_TEST_CASE(discriminatesSubscriptionKindsAndPreservesIssuedIds)
{
    accepts("eth_subscribe", R"(["logs",{}])");
    accepts("eth_subscribe", R"(["transactionReceipts",{"transactionHashes":[]}])");
    rejects("eth_subscribe", R"(["transactionReceipts",{"address":null}])");
    rejects("eth_subscribe", R"(["logs",true])");
    rejects("eth_subscribe", R"(["newPendingTransactions",{}])");
    rejects("eth_subscribe", R"(["newHeads",{}])");
    accepts("eth_unsubscribe", R"(["0x00112233445566778899aabbccddeeff"])");
    rejects("eth_unsubscribe", R"(["0x01"])");
    accepts("debug_getRawHeader",
        R"(["0x00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff"])");
    rejects("debug_getRawHeader", R"(["0x01"])");
}

BOOST_AUTO_TEST_CASE(discriminatesEngineV2ObjectShapes)
{
    std::string const hash = "0x" + std::string(64, '0');
    std::string const address = "0x" + std::string(40, '0');
    Json::Value payload(Json::objectValue);
    for (auto const* key : {"parentHash", "stateRoot", "receiptsRoot", "prevRandao", "blockHash"})
        payload[key] = hash;
    for (auto const* key : {"blockNumber", "gasLimit", "gasUsed", "timestamp", "baseFeePerGas"})
        payload[key] = "0x0";
    payload["feeRecipient"] = address;
    payload["logsBloom"] = "0x" + std::string(512, '0');
    payload["extraData"] = "0x";
    payload["transactions"] = Json::arrayValue;
    Json::Value params(Json::arrayValue);
    params.append(payload);
    BOOST_CHECK_NO_THROW(JsonValidator::requireParams("engine_newPayloadV2", params));
    params[0U]["withdrawals"] = Json::arrayValue;
    BOOST_CHECK_NO_THROW(JsonValidator::requireParams("engine_newPayloadV2", params));
    params[0U]["withdrawals"] = false;
    BOOST_CHECK_EXCEPTION(JsonValidator::requireParams("engine_newPayloadV2", params),
        JsonRpcException,
        [](JsonRpcException const& error) { return error.code() == InvalidParams; });

    Json::Value state(Json::objectValue);
    state["headBlockHash"] = hash;
    state["safeBlockHash"] = hash;
    state["finalizedBlockHash"] = hash;
    Json::Value attrs(Json::objectValue);
    attrs["timestamp"] = "0x0";
    attrs["prevRandao"] = hash;
    attrs["suggestedFeeRecipient"] = address;
    params[0U] = state;
    params.append(attrs);
    BOOST_CHECK_NO_THROW(JsonValidator::requireParams("engine_forkchoiceUpdatedV2", params));
    params[1U]["withdrawals"] = Json::arrayValue;
    BOOST_CHECK_NO_THROW(JsonValidator::requireParams("engine_forkchoiceUpdatedV2", params));
    params[1U]["withdrawals"] = false;
    BOOST_CHECK_EXCEPTION(JsonValidator::requireParams("engine_forkchoiceUpdatedV2", params),
        JsonRpcException,
        [](JsonRpcException const& error) { return error.code() == InvalidParams; });
}

BOOST_AUTO_TEST_SUITE_END()
