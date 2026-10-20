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
 * @file Web3EthCallRevertErrorTest.cpp
 * @brief Regression tests for issue #5709: a failed eth_call / eth_estimateGas answers the
 *        Ethereum JSON-RPC error shape — code 3 + "execution reverted[: <reason>]" for a REVERT,
 *        -32000 + a description for other execution faults — instead of the internal
 *        TransactionStatus value with an empty message that viem / ethers.js cannot recognise.
 */

#include "../common/RPCFixture.h"
#include <bcos-protocol/TransactionStatus.h>
#include <bcos-rpc/web3jsonrpc/Web3JsonRpcImpl.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptImpl.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/test/unit_test.hpp>
#include <future>
#include <string>
#include <string_view>

using namespace bcos;
using namespace bcos::rpc;

namespace bcos::test
{

/// Answers every call with a receipt carrying the configured status / output / message.
class StatusScheduler : public FakeScheduler
{
public:
    using FakeScheduler::FakeScheduler;

    protocol::TransactionStatus m_status{protocol::TransactionStatus::None};
    bytes m_output;
    std::string m_message;

    void call(protocol::Transaction::Ptr,
        std::function<void(Error::Ptr, protocol::TransactionReceipt::Ptr)> callback) noexcept
        override
    {
        auto receipt = std::make_shared<bcostars::protocol::TransactionReceiptImpl>();
        receipt->inner().data.status = static_cast<int32_t>(m_status);
        receipt->inner().data.output.assign(m_output.begin(), m_output.end());
        receipt->setMessage(m_message);
        callback({}, std::move(receipt));
    }
};

class Web3EthCallRevertErrorFixture : public RPCFixture
{
public:
    Web3EthCallRevertErrorFixture()
    {
        m_scheduler = std::make_shared<StatusScheduler>(m_ledger, m_blockFactory);
        auto service = std::make_shared<rpc::NodeService>(
            m_ledger, m_scheduler, txPool, nullptr, nullptr, m_blockFactory, nullptr);
        rpc = factory->buildLocalRpc(groupInfo, service);
        web3 = rpc->web3JsonRpc();
        BOOST_REQUIRE(web3 != nullptr);
    }

    Json::Value request(std::string_view method)
    {
        auto payload =
            std::string(R"({"jsonrpc":"2.0","id":1,"method":")") + std::string(method) +
            R"(","params":[{"to":"0x1234567890123456789012345678901234567890","data":"0x"},"latest"]})";
        std::promise<bcos::bytes> promise;
        web3->onRPCRequest(payload, [&promise](bcos::bytes resp, boost::beast::http::status) {
            promise.set_value(std::move(resp));
        });
        auto jsonBytes = promise.get_future().get();
        Json::Value value;
        Json::Reader reader;
        std::string_view json((char*)jsonBytes.data(), jsonBytes.size());
        BOOST_REQUIRE(reader.parse(json.begin(), json.end(), value));
        return value;
    }

    /// ABI encoding of `Error(string reason)`: selector ‖ offset 0x20 ‖ length ‖ padded bytes.
    static bytes abiEncodeErrorString(std::string_view reason)
    {
        bytes out = fromHex("08c379a0");
        auto appendWord = [&out](uint64_t value) {
            bytes word(32, 0);
            for (size_t i = 0; i < sizeof(value); ++i)
            {
                word[31 - i] = static_cast<uint8_t>(value >> (8 * i));
            }
            out.insert(out.end(), word.begin(), word.end());
        };
        appendWord(32);
        appendWord(reason.size());
        out.insert(out.end(), reason.begin(), reason.end());
        out.resize(out.size() + (32 - reason.size() % 32) % 32, 0);
        return out;
    }

    std::shared_ptr<StatusScheduler> m_scheduler;
    Rpc::Ptr rpc;
    Web3JsonRpcImpl::Ptr web3;
};

BOOST_FIXTURE_TEST_SUITE(Web3EthCallRevertErrorTest, Web3EthCallRevertErrorFixture)

// require("boom") → code 3, the decoded reason in the message, the raw payload in data.
BOOST_AUTO_TEST_CASE(revertWithReasonIsCode3WithDecodedMessage)
{
    m_scheduler->m_status = protocol::TransactionStatus::RevertInstruction;
    m_scheduler->m_output = abiEncodeErrorString("boom");

    auto resp = request("eth_call");
    BOOST_REQUIRE(resp.isMember("error"));
    BOOST_CHECK_EQUAL(resp["error"]["code"].asInt(), 3);
    BOOST_CHECK_EQUAL(resp["error"]["message"].asString(), "execution reverted: boom");
    BOOST_CHECK_EQUAL(
        resp["error"]["data"].asString(), toHexStringWithPrefix(m_scheduler->m_output));
}

// A bare REVERT (no data) still says "execution reverted"; data is the empty payload.
BOOST_AUTO_TEST_CASE(revertWithoutDataIsCode3WithPlainMessage)
{
    m_scheduler->m_status = protocol::TransactionStatus::RevertInstruction;

    auto resp = request("eth_call");
    BOOST_REQUIRE(resp.isMember("error"));
    BOOST_CHECK_EQUAL(resp["error"]["code"].asInt(), 3);
    BOOST_CHECK_EQUAL(resp["error"]["message"].asString(), "execution reverted");
    BOOST_CHECK_EQUAL(resp["error"]["data"].asString(), "0x");
}

// A custom error (unknown selector) is not decoded: clients decode `data` themselves.
BOOST_AUTO_TEST_CASE(revertWithCustomErrorKeepsRawData)
{
    m_scheduler->m_status = protocol::TransactionStatus::RevertInstruction;
    m_scheduler->m_output =
        fromHex("deadbeef0000000000000000000000000000000000000000000000000000000000000001");

    auto resp = request("eth_call");
    BOOST_REQUIRE(resp.isMember("error"));
    BOOST_CHECK_EQUAL(resp["error"]["code"].asInt(), 3);
    BOOST_CHECK_EQUAL(resp["error"]["message"].asString(), "execution reverted");
    BOOST_CHECK_EQUAL(
        resp["error"]["data"].asString(), toHexStringWithPrefix(m_scheduler->m_output));
}

// Solidity Panic(uint256) 0x11 → geth's text for the panic code.
BOOST_AUTO_TEST_CASE(panicIsCode3WithPanicDescription)
{
    m_scheduler->m_status = protocol::TransactionStatus::RevertInstruction;
    m_scheduler->m_output =
        fromHex("4e487b710000000000000000000000000000000000000000000000000000000000000011");

    auto resp = request("eth_call");
    BOOST_REQUIRE(resp.isMember("error"));
    BOOST_CHECK_EQUAL(resp["error"]["code"].asInt(), 3);
    BOOST_CHECK_EQUAL(resp["error"]["message"].asString(),
        "execution reverted: arithmetic underflow or overflow");
}

// Non-revert execution faults are -32000 with a description, never the internal status value.
BOOST_AUTO_TEST_CASE(outOfGasIsMinus32000)
{
    m_scheduler->m_status = protocol::TransactionStatus::OutOfGas;

    auto resp = request("eth_call");
    BOOST_REQUIRE(resp.isMember("error"));
    BOOST_CHECK_EQUAL(resp["error"]["code"].asInt(), -32000);
    BOOST_CHECK_EQUAL(resp["error"]["message"].asString(), "out of gas");
}

// FISCO's "call to an address without code" carries Error("Call address error.") in the
// output (issue #5709 case 2). Like geth, a non-revert fault gets no reason appended — the
// status name is the message, the payload stays in data for clients that want it.
BOOST_AUTO_TEST_CASE(callAddressErrorIsMinus32000WithStatusName)
{
    m_scheduler->m_status = protocol::TransactionStatus::CallAddressError;
    m_scheduler->m_output = abiEncodeErrorString("Call address error.");

    auto resp = request("eth_call");
    BOOST_REQUIRE(resp.isMember("error"));
    BOOST_CHECK_EQUAL(resp["error"]["code"].asInt(), -32000);
    BOOST_CHECK_EQUAL(resp["error"]["message"].asString(), "CallAddressError");
    BOOST_CHECK_EQUAL(
        resp["error"]["data"].asString(), toHexStringWithPrefix(m_scheduler->m_output));
}

// A receipt message (precompiled errors carry text there) is used when nothing decodes.
BOOST_AUTO_TEST_CASE(receiptMessageIsAppendedWhenDataDoesNotDecode)
{
    m_scheduler->m_status = protocol::TransactionStatus::PrecompiledError;
    m_scheduler->m_message = "unknown function";

    auto resp = request("eth_call");
    BOOST_REQUIRE(resp.isMember("error"));
    BOOST_CHECK_EQUAL(resp["error"]["code"].asInt(), -32000);
    BOOST_CHECK_EQUAL(resp["error"]["message"].asString(), "PrecompiledError: unknown function");
}

// eth_estimateGas shares the call path and therefore the error shape.
BOOST_AUTO_TEST_CASE(estimateGasReportsTheSameRevertShape)
{
    m_scheduler->m_status = protocol::TransactionStatus::RevertInstruction;
    m_scheduler->m_output = abiEncodeErrorString("nope");

    auto resp = request("eth_estimateGas");
    BOOST_REQUIRE(resp.isMember("error"));
    BOOST_CHECK_EQUAL(resp["error"]["code"].asInt(), 3);
    BOOST_CHECK_EQUAL(resp["error"]["message"].asString(), "execution reverted: nope");
}

// Control: a successful call is unchanged.
BOOST_AUTO_TEST_CASE(successfulCallReturnsOutputAsResult)
{
    m_scheduler->m_output =
        fromHex("00000000000000000000000000000000000000000000000000000000000000ff");

    auto resp = request("eth_call");
    BOOST_REQUIRE(resp.isMember("result"));
    BOOST_CHECK_EQUAL(resp["result"].asString(), toHexStringWithPrefix(m_scheduler->m_output));
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
