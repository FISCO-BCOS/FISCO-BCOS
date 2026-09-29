/**
 *  Copyright (C) 2021 FISCO BCOS.
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
 * @brief eth_maxPriorityFeePerGas must track tx_gas_price so that the standard EIP-1559
 *        client formula (2 * baseFeePerGas + maxPriorityFeePerGas, with baseFeePerGas = 0)
 *        yields a maxFeePerGas that TxValidator admits.
 * @file Web3MaxPriorityFeeTest.cpp
 */
#include "../common/RPCFixture.h"
#include <bcos-rpc/web3jsonrpc/Web3JsonRpcImpl.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/test/unit_test.hpp>
#include <future>
#include <string_view>

namespace bcos::test
{
class Web3MaxPriorityFeeFixture : public RPCFixture
{
public:
    Web3MaxPriorityFeeFixture()
    {
        rpc = factory->buildLocalRpc(groupInfo, nodeService);
        web3JsonRpc = rpc->web3JsonRpc();
        BOOST_REQUIRE(web3JsonRpc != nullptr);
    }

    Json::Value call(std::string_view method, std::string_view params = "[]")
    {
        std::string request = std::string(R"({"jsonrpc":"2.0","id":1,"method":")") +
                              std::string(method) + R"(","params":)" + std::string(params) + "}";
        std::promise<bcos::bytes> promise;
        web3JsonRpc->onRPCRequest(
            request, [&promise](bcos::bytes resp) { promise.set_value(std::move(resp)); });
        auto jsonBytes = promise.get_future().get();
        Json::Value value;
        Json::Reader reader;
        std::string_view json((char*)jsonBytes.data(), jsonBytes.size());
        BOOST_REQUIRE(reader.parse(json.begin(), json.end(), value));
        BOOST_REQUIRE(value.isMember("result"));
        return value["result"];
    }

    uint64_t quantity(std::string_view method)
    {
        return bcos::fromQuantity(call(method).asString());
    }

    Rpc::Ptr rpc;
    Web3JsonRpcImpl::Ptr web3JsonRpc;
};

BOOST_FIXTURE_TEST_SUITE(Web3MaxPriorityFeeTest, Web3MaxPriorityFeeFixture)

BOOST_AUTO_TEST_CASE(tracksSystemGasPrice)
{
    // No tx_gas_price configured: both hints fall back to 0.
    BOOST_CHECK_EQUAL(quantity("eth_gasPrice"), 0U);
    BOOST_CHECK_EQUAL(quantity("eth_maxPriorityFeePerGas"), 0U);

    // POTOS testnet shape: tx_gas_price = 21000 wei (0x5208), baseFeePerGas reported as 0.
    m_ledger->setSystemConfig(ledger::SYSTEM_KEY_TX_GAS_PRICE, "0x5208");
    BOOST_CHECK_EQUAL(quantity("eth_gasPrice"), 21000U);
    BOOST_CHECK_EQUAL(quantity("eth_maxPriorityFeePerGas"), 21000U);

    // The fix relies on blocks reporting baseFeePerGas = 0; read it rather than assume it, so
    // this test fires if BlockResponse ever starts emitting a real base fee.
    auto const block = call("eth_getBlockByNumber", R"(["latest", false])");
    BOOST_REQUIRE(block.isMember("baseFeePerGas"));
    auto const baseFee = bcos::fromQuantity(block["baseFeePerGas"].asString());
    BOOST_CHECK_EQUAL(baseFee, 0U);

    // Standard client formula (web3j / ethers / viem) must land on the admission floor.
    BOOST_CHECK_GE(2 * baseFee + quantity("eth_maxPriorityFeePerGas"), 21000U);
}

BOOST_AUTO_TEST_CASE(followsConfigChange)
{
    m_ledger->setSystemConfig(ledger::SYSTEM_KEY_TX_GAS_PRICE, "0x99e670");
    BOOST_CHECK_EQUAL(quantity("eth_maxPriorityFeePerGas"), 10086000U);
    m_ledger->setSystemConfig(ledger::SYSTEM_KEY_TX_GAS_PRICE, "0x0");
    BOOST_CHECK_EQUAL(quantity("eth_maxPriorityFeePerGas"), 0U);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
