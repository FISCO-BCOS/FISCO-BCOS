// Copyright (C) 2026 FISCO BCOS.
// SPDX-License-Identifier: Apache-2.0

#include <bcos-rpc/jsonrpc/Common.h>
#include <bcos-rpc/validator/JsonValidator.h>
#include <bcos-rpc/web3jsonrpc/endpoints/EthMethods.h>
#include <boost/test/unit_test.hpp>
#include <sstream>

using namespace bcos::rpc;

namespace
{
Json::Value parseParams(std::string_view source)
{
    Json::Value root;
    Json::CharReaderBuilder reader;
    std::istringstream input(std::string{source});
    BOOST_REQUIRE(Json::parseFromStream(reader, input, &root, nullptr));
    return root;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(Web3ParameterSchemas)

BOOST_AUTO_TEST_CASE(pilotTuplesAndStrictTypes)
{
    auto const balance = EthMethod::eth_getBalance;
    auto const storage = EthMethod::eth_getStorageAt;
    auto const block = EthMethod::eth_getBlockByNumber;
    auto check = [](EthMethod method, std::string_view params, std::string_view path = "") {
        auto const value = parseParams(params);
        if (path.empty())
        {
            BOOST_CHECK_NO_THROW(JsonValidator::requireParams(method, value));
            return;
        }
        BOOST_CHECK_EXCEPTION(JsonValidator::requireParams(method, value), JsonRpcException,
            [path](JsonRpcException const& error) {
                return error.code() == InvalidParams &&
                       error.msg() == "Invalid params at " + std::string(path);
            });
    };

    check(balance, R"(["0XaAaAaAaAaAaAaAaAaAaAaAaAaAaAaAaAaAaAaAaA"])");
    check(balance, R"(["0x0000000000000000000000000000000000000000",null])", "/params/1");
    check(balance, R"(["0x0000000000000000000000000000000000000000",""])", "/params/1");
    check(balance, R"(["0x0000000000000000000000000000000000000000","latest",false])", "/params");
    check(balance, R"(["0x1234","latest"])", "/params/0");
    check(balance, R"([])", "/params");
    check(storage, R"(["0000000000000000000000000000000000000000","0x"])");
    check(storage, R"(["0000000000000000000000000000000000000000",0])", "/params/1");
    check(storage, R"(["0000000000000000000000000000000000000000","0x0",{}])", "/params/2");
    check(block, R"(["0x0",false])");
    check(block, R"(["0x7fffffffffffffff",true])");
    check(block, R"(["0x8000000000000000",true])", "/params/0");
    check(block, R"(["0x0000000000000000000000000000000000000000000000000000000000000000",false])");
    check(block, R"(["latest","false"])", "/params/1");
    check(block, R"(["latest",0])", "/params/1");
    check(block, R"(["latest",null])", "/params/1");
    check(block, R"(["latest\u0000",false])", "/params/0");
    check(block, R"(["0x0\n",false])", "/params/0");
    check(block, R"(["latest"])", "/params");
    check(block, R"([42,false])", "/params/0");
    check(block, R"([{"blockNumber":"0x0"},false])", "/params/0");
    check(block, R"([null,false])", "/params/0");
    check(block, R"([42])", "/params");
    check(block, R"(null)", "/params");
    check(block, R"(["0x",false])", "/params/0");
    check(block, R"(["0x-0",false])", "/params/0");
    check(block, R"(["0x+1",false])", "/params/0");
    check(block, R"(["0X1",false])", "/params/0");
    check(block, R"(["0x000000000000000000000000000000000000007fffffffffffffff",false])");
    check(block, R"(["0x000000000000000000000000000000000000008000000000000000",false])",
        "/params/0");
    check(block, R"(["0x8000000000000000000000000000000000000000000000000000000000000000",false])");
    check(block, R"(["LATEST",false])", "/params/0");
    check(storage, R"(["0000000000000000000000000000000000000000",""])");
    check(storage, R"(["0000000000000000000000000000000000000000","0xA\u0000"])", "/params/1");
    check(storage, R"(["0000000000000000000000000000000000000000","０"])", "/params/1");

    Json::Value oversized(Json::arrayValue);
    oversized.append("0000000000000000000000000000000000000000");
    oversized.append(std::string(128 * 1024, '0'));
    BOOST_CHECK_EXCEPTION(JsonValidator::requireParams(storage, oversized), JsonRpcException,
        [](JsonRpcException const& error) {
            return error.code() == InvalidParams && error.msg() == "Invalid params at /params/1";
        });
}

BOOST_AUTO_TEST_SUITE_END()
