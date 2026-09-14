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
 * @file TxPoolFaultRpcTest.cpp
 * @author: kyonGuo
 * @date 2026/9/10
 */

#include "../common/RPCFixture.h"
#include "../common/ThrowingTxPool.h"
#include "../common/Web3TxSamples.h"
#include <bcos-protocol/TransactionStatus.h>
#include <bcos-rpc/jsonrpc/Common.h>
#include <bcos-rpc/web3jsonrpc/endpoints/EthEndpoint.h>
#include <bcos-rpc/web3jsonrpc/utils/Common.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/Error.h>
#include <boost/test/unit_test.hpp>
#include <future>
#include <memory>
#include <string>
#include <variant>

using namespace bcos;
using namespace bcos::rpc;

namespace bcos::test
{
// submitTransaction answers a refusal and a fault with the same C++ type and the same int field.
// An in-process pool only ever puts a TransactionStatus there, but a MAX/TARS deployment's
// TxPoolServiceClient throws BCOS_ERROR(-1, "No value!") when the callback left no value, and
// toBcosError(tarsRet)'s "TARS error!" when the call itself failed. Casting one of those to a
// TransactionStatus reaches no row of the admission table and lands on its fallback, which would
// answer a node fault as -32000 "Unknown" -- a refusal's code, a name that is not the fault's,
// and the real message gone. These pin the split: only a code this node can name is answered
// from the table, the rest reach Web3JsonRpcImpl's catch-all as they did before the table.
class TxPoolFaultFixture : public RPCFixture
{
public:
    // The Error sendRawTransaction lets out, or the JsonRpcException it turns it into.
    std::variant<std::monostate, Error, JsonRpcException> answer(int32_t code, std::string message)
    {
        auto pool = std::make_shared<ThrowingTxPool>(code, std::move(message));
        auto service = std::make_shared<rpc::NodeService>(
            m_ledger, scheduler, pool, nullptr, nullptr, m_blockFactory, nullptr);
        EthEndpoint endpoint(service, nullptr, /*syncTransaction=*/true);
        Json::Value params(Json::arrayValue);
        params.append(std::string(c_unprotectedRawTx));
        Json::Value response;
        try
        {
            task::syncWait(endpoint.sendRawTransaction(params, response));
        }
        catch (JsonRpcException const& e)
        {
            return e;
        }
        catch (bcos::Error const& e)
        {
            return e;
        }
        return std::monostate{};
    }

    // The same submission through the whole Web3 face, down to the bytes the client receives.
    // answer() stops one hop short of this: it sees the exception leave sendRawTransaction, not
    // what Web3JsonRpcImpl's catch-all makes of it.
    Json::Value response(int32_t code, std::string message)
    {
        auto pool = std::make_shared<ThrowingTxPool>(code, std::move(message));
        auto service = std::make_shared<rpc::NodeService>(
            m_ledger, scheduler, pool, nullptr, nullptr, m_blockFactory, nullptr);
        auto localRpc = factory->buildLocalRpc(groupInfo, service);
        auto web3 = localRpc->web3JsonRpc();
        BOOST_REQUIRE(web3 != nullptr);
        auto const payload =
            std::string(
                R"({"jsonrpc":"2.0","id":1,"method":"eth_sendRawTransaction","params":[")") +
            std::string(c_unprotectedRawTx) + R"("]})";
        std::promise<bcos::bytes> promise;
        web3->onRPCRequest(payload, [&promise](bcos::bytes resp, boost::beast::http::status) {
            promise.set_value(std::move(resp));
        });
        auto const bytes = promise.get_future().get();
        Json::Value parsed;
        Json::Reader reader;
        std::string_view json((char*)bytes.data(), bytes.size());
        reader.parse(json.begin(), json.end(), parsed);
        return parsed;
    }
};

BOOST_FIXTURE_TEST_SUITE(TxPoolFaultRpcTest, TxPoolFaultFixture)

// TxPoolServiceClient.cpp's await_resume, when the tars callback left the variant empty.
BOOST_AUTO_TEST_CASE(poolFaultKeepsItsCodeAndMessage)
{
    auto const answered = answer(-1, "No value!");
    BOOST_REQUIRE(std::holds_alternative<Error>(answered));
    auto const& error = std::get<Error>(answered);
    BOOST_CHECK_EQUAL(error.errorCode(), -1);
    BOOST_CHECK_EQUAL(error.errorMessage(), "No value!");
}

// ErrorConverter.h's toBcosError(tars::Int32): a TARS transport code, negative and not a status.
BOOST_AUTO_TEST_CASE(tarsTransportFaultKeepsItsCodeAndMessage)
{
    auto const answered = answer(-7, "TARS error!");
    BOOST_REQUIRE(std::holds_alternative<Error>(answered));
    auto const& error = std::get<Error>(answered);
    BOOST_CHECK_EQUAL(error.errorCode(), -7);
    BOOST_CHECK_EQUAL(error.errorMessage(), "TARS error!");
}

// A positive value the enum does not declare is no more a verdict than a negative one.
BOOST_AUTO_TEST_CASE(undeclaredStatusValueIsNotAVerdict)
{
    auto const answered = answer(99999, "not a status");
    BOOST_REQUIRE(std::holds_alternative<Error>(answered));
    BOOST_CHECK_EQUAL(std::get<Error>(answered).errorCode(), 99999);
    BOOST_CHECK_EQUAL(std::get<Error>(answered).errorMessage(), "not a status");
}

// What the client is left holding: Web3JsonRpcImpl's catch-all, -32603 and the message the fault
// came with. This is the answer base 1f371d2f5 gave, before the txpool branch had a catch at all.
BOOST_AUTO_TEST_CASE(poolFaultReachesTheClientAsAnInternalError)
{
    auto const answered = response(-1, "No value!");
    BOOST_REQUIRE(answered.isMember("error"));
    BOOST_CHECK_EQUAL(answered["error"]["code"].asInt(), InternalError);
    BOOST_CHECK_EQUAL(answered["error"]["message"].asString(), "No value!");
    BOOST_TEST(!answered.isMember("result"));
}

// The guard is not a way out of the table: a real refusal is still answered from it.
BOOST_AUTO_TEST_CASE(refusalIsStillAnsweredFromTheTable)
{
    constexpr auto full = protocol::TransactionStatus::TxPoolIsFull;
    auto const answered = answer(static_cast<int32_t>(full), protocol::toString(full));
    BOOST_REQUIRE(std::holds_alternative<JsonRpcException>(answered));
    auto const& refusal = std::get<JsonRpcException>(answered);
    BOOST_CHECK_EQUAL(refusal.code(), Web3DefaultError);
    BOOST_CHECK_EQUAL(refusal.msg(), "txpool is full");
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
