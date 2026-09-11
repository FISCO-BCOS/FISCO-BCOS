/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
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
 * @brief eth_estimateGas must find the gas a call needs, not echo the receipt's gasUsed
 * (issue #5587).
 */

#include "../common/RPCFixture.h"
#include <bcos-protocol/TransactionStatus.h>
#include <bcos-rpc/web3jsonrpc/Web3JsonRpcImpl.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::rpc;

namespace bcos::test
{
// Models a precompiled target: the receipt always books the intrinsic 21000 no matter the
// budget, but execution only succeeds when tx.gasLimit() covers what the internal sub-calls
// actually burn. Records every gas cap probed so the search itself can be asserted.
class GasThresholdScheduler : public FakeScheduler
{
public:
    GasThresholdScheduler(FakeLedger::Ptr ledger, BlockFactory::Ptr blockFactory,
        uint64_t requiredGas, u256 reportedGasUsed)
      : FakeScheduler(std::move(ledger), blockFactory),
        m_receiptFactory(blockFactory->receiptFactory()),
        m_requiredGas(requiredGas),
        m_reportedGasUsed(std::move(reportedGasUsed))
    {}

    void call(protocol::Transaction::Ptr tx,
        std::function<void(Error::Ptr, protocol::TransactionReceipt::Ptr)> callback) noexcept
        override
    {
        auto const gasLimit = tx->gasLimit();
        m_probes.push_back(gasLimit);
        // gasLimit 0 means "no cap": executor v1 then budgets the chain's tx_gas_limit.
        auto const ok = gasLimit == 0 || gasLimit >= static_cast<int64_t>(m_requiredGas);
        auto receipt = m_receiptFactory->createReceipt2(m_reportedGasUsed, "", {},
            ok ? static_cast<int32_t>(protocol::TransactionStatus::None) :
                 static_cast<int32_t>(protocol::TransactionStatus::OutOfGas),
            ref(m_output), 0);
        if (!ok)
        {
            receipt->setMessage(m_failMessage);
        }
        callback({}, std::move(receipt));
    }

    std::vector<int64_t> m_probes;
    bytes m_output{0xde, 0xad, 0xbe, 0xef};
    std::string m_failMessage = "Call precompiled out of gas.";

private:
    protocol::TransactionReceiptFactory::Ptr m_receiptFactory;
    uint64_t m_requiredGas;
    u256 m_reportedGasUsed;
};

class EstimateGasSearchFixture : public RPCFixture
{
public:
    Web3JsonRpcImpl::Ptr buildWeb3(std::shared_ptr<GasThresholdScheduler> const& threshold)
    {
        auto service = std::make_shared<rpc::NodeService>(
            m_ledger, threshold, txPool, nullptr, nullptr, m_blockFactory);
        auto localRpc = factory->buildLocalRpc(groupInfo, service);
        m_rpcs.push_back(localRpc);
        return localRpc->web3JsonRpc();
    }

    static Json::Value request(Web3JsonRpcImpl::Ptr const& web3, std::string_view body)
    {
        std::promise<bytes> promise;
        web3->onRPCRequest(body, [&promise](bytes resp) { promise.set_value(std::move(resp)); });
        auto jsonBytes = promise.get_future().get();
        Json::Value value;
        Json::Reader reader;
        reader.parse((char*)jsonBytes.data(), (char*)jsonBytes.data() + jsonBytes.size(), value);
        return value;
    }

    static std::string estimateRequest(std::string_view gasField = "")
    {
        return std::string(
                   R"({"jsonrpc":"2.0","id":1,"method":"eth_estimateGas","params":[{"from":"0x2a09be8823b80f337170650802d1a0f8a99fe2d8","to":"0x0000000000000000000000000000000000001011","data":"0x12345678")") +
               std::string(gasField) + R"(},"latest"]})";
    }

    std::vector<Rpc::Ptr> m_rpcs;
};

BOOST_FIXTURE_TEST_SUITE(Web3EstimateGasSearchTest, EstimateGasSearchFixture)

// The #5587 shape: receipt says 21000, execution needs 150000. Before the fix the answer was
// 21000 and every auto-gas client then failed OutOfGas.
BOOST_AUTO_TEST_CASE(precompiledTargetFindsRealRequirement)
{
    constexpr uint64_t required = 150000;
    constexpr uint64_t txGasLimit = 3000000;
    m_ledger->setSystemConfig(ledger::SYSTEM_KEY_TX_GAS_LIMIT, std::to_string(txGasLimit));
    auto threshold =
        std::make_shared<GasThresholdScheduler>(m_ledger, m_blockFactory, required, u256(21000));
    auto web3 = buildWeb3(threshold);

    auto response = request(web3, estimateRequest());
    BOOST_REQUIRE(!response.isMember("error"));
    BOOST_CHECK_EQUAL(response["result"].asString(), toQuantity(u256(required)));

    // First probe at the chain cap, second at the receipt's gasUsed, then bisection; the
    // whole search stays within log2(cap) + 2 executions.
    BOOST_REQUIRE_GE(threshold->m_probes.size(), 3U);
    BOOST_CHECK_EQUAL(threshold->m_probes[0], static_cast<int64_t>(txGasLimit));
    BOOST_CHECK_EQUAL(threshold->m_probes[1], 21000);
    BOOST_CHECK_LE(threshold->m_probes.size(), 2U + 22U);
}

// Ordinary contract: gasUsed is already enough, so the answer is gasUsed after exactly two
// executions — the pre-existing fast path must not get slower.
BOOST_AUTO_TEST_CASE(gasUsedSufficientReturnsGasUsedInTwoRuns)
{
    m_ledger->setSystemConfig(ledger::SYSTEM_KEY_TX_GAS_LIMIT, "3000000");
    auto threshold =
        std::make_shared<GasThresholdScheduler>(m_ledger, m_blockFactory, 50000, u256(60000));
    auto web3 = buildWeb3(threshold);

    auto response = request(web3, estimateRequest());
    BOOST_REQUIRE(!response.isMember("error"));
    BOOST_CHECK_EQUAL(response["result"].asString(), toQuantity(u256(60000)));
    BOOST_CHECK_EQUAL(threshold->m_probes.size(), 2U);
}

// A gas field in the request bounds the search instead of the chain's tx_gas_limit, and a
// call that fails even at that bound reports the failure rather than a number.
BOOST_AUTO_TEST_CASE(requestGasCapsSearchAndFailureAtCapIsReported)
{
    m_ledger->setSystemConfig(ledger::SYSTEM_KEY_TX_GAS_LIMIT, "3000000");
    auto threshold =
        std::make_shared<GasThresholdScheduler>(m_ledger, m_blockFactory, 150000, u256(21000));
    auto web3 = buildWeb3(threshold);

    auto response = request(web3, estimateRequest(R"(,"gas":"0x30d40")"));  // 200000
    BOOST_REQUIRE(!response.isMember("error"));
    BOOST_CHECK_EQUAL(response["result"].asString(), toQuantity(u256(150000)));
    BOOST_CHECK_EQUAL(threshold->m_probes[0], 200000);

    threshold->m_probes.clear();
    response = request(web3, estimateRequest(R"(,"gas":"0x186a0")"));  // 100000 < required
    BOOST_REQUIRE(response.isMember("error"));
    BOOST_CHECK_EQUAL(response["error"]["code"].asInt(),
        static_cast<int32_t>(protocol::TransactionStatus::OutOfGas));
    BOOST_CHECK_EQUAL(threshold->m_probes.size(), 1U);
}

// No tx_gas_limit in the ledger and no gas in the request: cap is 0, the executor budgets its
// own limit, and the answer is the first run's gasUsed after a single execution — the
// pre-existing behavior.
BOOST_AUTO_TEST_CASE(noCapDegradesToSingleRunGasUsed)
{
    auto threshold =
        std::make_shared<GasThresholdScheduler>(m_ledger, m_blockFactory, 150000, u256(21000));
    auto web3 = buildWeb3(threshold);

    auto response = request(web3, estimateRequest());
    BOOST_REQUIRE(!response.isMember("error"));
    BOOST_CHECK_EQUAL(response["result"].asString(), toQuantity(u256(21000)));
    BOOST_REQUIRE_EQUAL(threshold->m_probes.size(), 1U);
    BOOST_CHECK_EQUAL(threshold->m_probes[0], 0);
}

// eth_call shares executeCall/buildCallResponse with estimateGas; pin the wire shape of both
// outcomes so the refactor cannot drift: success = {"result": output}, failure =
// {"error": {"code": status, "message": ..., "data": output}} with no "result".
BOOST_AUTO_TEST_CASE(ethCallResponseShapeIsPinned)
{
    auto threshold =
        std::make_shared<GasThresholdScheduler>(m_ledger, m_blockFactory, 150000, u256(21000));
    auto web3 = buildWeb3(threshold);
    auto callRequest = [](std::string_view gas) {
        return std::string(
                   R"({"jsonrpc":"2.0","id":7,"method":"eth_call","params":[{"to":"0x0000000000000000000000000000000000001011","data":"0x12345678","gas":")") +
               std::string(gas) + R"("},"latest"]})";
    };

    auto ok = request(web3, callRequest("0x30d40"));  // 200000 >= required
    BOOST_CHECK_EQUAL(ok["jsonrpc"].asString(), "2.0");
    BOOST_CHECK_EQUAL(ok["id"].asInt(), 7);
    BOOST_CHECK_EQUAL(ok["result"].asString(), "0xdeadbeef");
    BOOST_CHECK(!ok.isMember("error"));

    auto failed = request(web3, callRequest("0x186a0"));  // 100000 < required
    BOOST_CHECK_EQUAL(failed["jsonrpc"].asString(), "2.0");
    BOOST_CHECK(!failed.isMember("result"));
    BOOST_REQUIRE(failed.isMember("error"));
    BOOST_CHECK_EQUAL(failed["error"]["code"].asInt(),
        static_cast<int32_t>(protocol::TransactionStatus::OutOfGas));
    BOOST_CHECK_EQUAL(failed["error"]["message"].asString(), threshold->m_failMessage);
    BOOST_CHECK_EQUAL(failed["error"]["data"].asString(), "0xdeadbeef");
    BOOST_CHECK_EQUAL(threshold->m_probes.size(), 2U);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
