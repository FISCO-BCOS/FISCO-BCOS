/*
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
 * @brief eth_estimateGas: shared simulation budget for upward + binary search.
 */

#include "../common/RPCFixture.h"
#include <bcos-rpc/web3jsonrpc/Web3JsonRpcImpl.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptImpl.h>
#include <boost/test/unit_test.hpp>
#include <atomic>
#include <future>
#include <string>

using namespace bcos;
using namespace bcos::rpc;

namespace bcos::test
{
class EthEstimateGasFixture : public RPCFixture
{
public:
    EthEstimateGasFixture()
    {
        // Replace the default FakeScheduler2 with one we can instrument.
        instrumented = std::make_shared<FakeScheduler2>(m_ledger, m_blockFactory);
        auto svc = std::make_shared<rpc::NodeService>(
            m_ledger, instrumented, txPool, nullptr, nullptr, m_blockFactory, nullptr);
        rpc = factory->buildLocalRpc(groupInfo, svc);
        rpc->groupManager()->updateGroupInfo(groupInfo);
        web3JsonRpc = rpc->web3JsonRpc();
        BOOST_REQUIRE(web3JsonRpc);
    }

    Json::Value call(std::string const& body)
    {
        std::promise<bcos::bytes> promise;
        web3JsonRpc->onRPCRequest(body, [&promise](bcos::bytes resp, boost::beast::http::status) {
            promise.set_value(std::move(resp));
        });
        auto jsonBytes = promise.get_future().get();
        Json::Value value;
        Json::Reader reader;
        std::string_view json((char*)jsonBytes.data(), jsonBytes.size());
        reader.parse(json.begin(), json.end(), value);
        return value;
    }

    std::shared_ptr<FakeScheduler2> instrumented;
    Rpc::Ptr rpc;
    Web3JsonRpcImpl::Ptr web3JsonRpc;
};

BOOST_FIXTURE_TEST_SUITE(EthEstimateGasBudgetTest, EthEstimateGasFixture)

BOOST_AUTO_TEST_CASE(UpwardAndBinarySearchShareSimulationBudget)
{
    // First simulation succeeds with a small gasUsed; every re-simulation fails so the
    // estimator enters the upward + binary search. Without a shared budget a uint64-scale
    // ceiling would drive ~64 doubling probes; the shared 16-sim budget must bound it.
    std::atomic<size_t> simulations{0};
    instrumented->callHandler = [&](protocol::Transaction::Ptr, FakeScheduler2::CallCallback cb) {
        auto receipt = std::make_shared<bcostars::protocol::TransactionReceiptImpl>();
        auto n = ++simulations;
        if (n == 1)
        {
            receipt->inner().data.gasUsed = "21000";
            receipt->inner().data.status = 0;
        }
        else
        {
            receipt->inner().data.gasUsed = "21000";
            receipt->inner().data.status = 1;  // execution reverted
        }
        cb({}, receipt);
    };

    auto resp = call(
        R"({"jsonrpc":"2.0","id":1,"method":"eth_estimateGas","params":[{"to":"0x0000000000000000000000000000000000000001","gas":"0xffffffffffffffff"},"latest"]})");
    // First run + at most 16 search simulations.
    BOOST_CHECK_LE(simulations.load(), 17U);
    BOOST_CHECK(resp.isMember("result") || resp.isMember("error"));
}

BOOST_AUTO_TEST_CASE(MalformedGasIsRejected)
{
    auto resp = call(
        R"({"jsonrpc":"2.0","id":1,"method":"eth_estimateGas","params":[{"to":"0x0000000000000000000000000000000000000001","gas":"0xzz"},"latest"]})");
    BOOST_REQUIRE(resp.isMember("error"));
    BOOST_CHECK_EQUAL(resp["error"]["code"].asInt(), -32602);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
