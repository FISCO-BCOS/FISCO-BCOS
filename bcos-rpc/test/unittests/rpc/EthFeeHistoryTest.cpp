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
 * @brief eth_feeHistory: pins the request bounds (blockCount / rewardPercentiles), the
 *        empty-priorities null reward entries, and the trailing predicted-baseFee behavior
 *        (PBFT headers stay 0x0; an OP header carries the calcOpBaseFee prediction).
 * @file EthFeeHistoryTest.cpp
 */

#include "../common/RPCFixture.h"
#include <bcos-rpc/web3jsonrpc/utils/util.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/test/unit_test.hpp>
#include <future>
#include <string>
#include <vector>

namespace bcos::test
{
class EthFeeHistoryFixture : public RPCFixture
{
public:
    EthFeeHistoryFixture()
    {
        rpc = factory->buildLocalRpc(groupInfo, nodeService);
        web3JsonRpc = rpc->web3JsonRpc();
        BOOST_TEST(web3JsonRpc != nullptr);
    }

    Json::Value request(Json::Value const& params)
    {
        Json::Value req;
        req["jsonrpc"] = "2.0";
        req["id"] = 1;
        req["method"] = "eth_feeHistory";
        req["params"] = params;
        return requestRaw(printJson(req));
    }

    Json::Value requestRaw(std::string const& body)
    {
        Json::Value value;
        Json::Reader reader;
        std::promise<bcos::bytes> promise;
        web3JsonRpc->onRPCRequest(body, [&promise](bcos::bytes resp, boost::beast::http::status) {
            promise.set_value(std::move(resp));
        });
        auto jsonBytes = promise.get_future().get();
        std::string_view json((char*)jsonBytes.data(), (char*)jsonBytes.data() + jsonBytes.size());
        reader.parse(json.begin(), json.end(), value);
        return value;
    }

    Json::Value params(
        std::string const& blockCount, Json::Value const& percentiles = Json::nullValue)
    {
        Json::Value p(Json::arrayValue);
        p.append(blockCount);
        p.append("latest");
        if (!percentiles.isNull())
        {
            p.append(percentiles);
        }
        return p;
    }

    Rpc::Ptr rpc;
    Web3JsonRpcImpl::Ptr web3JsonRpc;
};

BOOST_FIXTURE_TEST_SUITE(EthFeeHistoryTest, EthFeeHistoryFixture)

// blockCount is a QTY bound to [1, 1024]: 0 and 1025 must be rejected up front, before any
// ledger read.
BOOST_AUTO_TEST_CASE(BlockCountBoundsAreEnforced)
{
    auto zero = request(params("0x0"));
    BOOST_REQUIRE(zero.isMember("error"));
    BOOST_CHECK_EQUAL(zero["error"]["code"].asInt(), -32602);
    BOOST_CHECK(zero["error"]["message"].asString().find("blockCount must be between 1 and 1024") !=
                std::string::npos);

    auto tooMany = request(params("0x401"));
    BOOST_REQUIRE(tooMany.isMember("error"));
    BOOST_CHECK_EQUAL(tooMany["error"]["code"].asInt(), -32602);
    BOOST_CHECK(tooMany["error"]["message"].asString().find(
                    "blockCount must be between 1 and 1024") != std::string::npos);
}

// rewardPercentiles entries outside [0, 100] (and lists longer than 100) are rejected:
// a negative value would otherwise wrap through the later size_t cast.
BOOST_AUTO_TEST_CASE(PercentileBoundsAreEnforced)
{
    Json::Value negative(Json::arrayValue);
    negative.append(-0.1);
    auto neg = request(params("0x1", negative));
    BOOST_REQUIRE(neg.isMember("error"));
    BOOST_CHECK_EQUAL(neg["error"]["code"].asInt(), -32602);
    BOOST_CHECK(neg["error"]["message"].asString().find(
                    "rewardPercentiles entries must be in [0, 100]") != std::string::npos);

    Json::Value over(Json::arrayValue);
    over.append(100.1);
    auto tooHigh = request(params("0x1", over));
    BOOST_REQUIRE(tooHigh.isMember("error"));
    BOOST_CHECK_EQUAL(tooHigh["error"]["code"].asInt(), -32602);

    Json::Value longList(Json::arrayValue);
    for (int i = 0; i <= 100; ++i)
    {
        longList.append(50.0);
    }
    auto tooLong = request(params("0x1", longList));
    BOOST_REQUIRE(tooLong.isMember("error"));
    BOOST_CHECK_EQUAL(tooLong["error"]["code"].asInt(), -32602);
    BOOST_CHECK(
        tooLong["error"]["message"].asString().find("at most 100 entries") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(PercentilesMustBeStrictlyIncreasing)
{
    for (auto const& values : {std::vector<double>{10.0, 10.0}, std::vector<double>{80.0, 20.0}})
    {
        Json::Value percentiles(Json::arrayValue);
        for (auto const value : values)
        {
            percentiles.append(value);
        }
        auto response = request(params("0x1", percentiles));
        BOOST_REQUIRE(response.isMember("error"));
        BOOST_CHECK_EQUAL(response["error"]["code"].asInt(), -32602);
        BOOST_CHECK(response["error"]["message"].asString().find("strictly increasing") !=
                    std::string::npos);
    }
}

// PBFT headers never write the tars baseFee field: every baseFeePerGas entry — including
// the trailing prediction, which must stay 0x0 instead of calcOpBaseFee's 1-for-zero — is
// 0x0. reward is absent when no percentiles were requested.
BOOST_AUTO_TEST_CASE(PbftHeadersYieldZeroBaseFeesAndZeroTrailing)
{
    auto resp = request(params("0x1"));
    BOOST_REQUIRE(resp.isMember("result"));
    auto const& result = resp["result"];
    BOOST_TEST(result["oldestBlock"].asString() == "0x13");  // latest = 19
    BOOST_REQUIRE(result["baseFeePerGas"].isArray());
    // 1 resolved block + 1 trailing prediction
    BOOST_REQUIRE_EQUAL(result["baseFeePerGas"].size(), 2U);
    BOOST_TEST(result["baseFeePerGas"][0U].asString() == "0x0");
    BOOST_TEST(result["baseFeePerGas"][1U].asString() == "0x0");
    BOOST_REQUIRE(result["gasUsedRatio"].isArray());
    BOOST_REQUIRE_EQUAL(result["gasUsedRatio"].size(), 1U);
    BOOST_TEST(!result.isMember("reward"));
}

// The fake chain's receipts carry no effectiveGasPrice, so every priority set is empty —
// geth semantics: a percentile of an empty reward set is null, never a fabricated 0x0.
// This also pins the multi-block shape (oldestBlock back-fill, one reward row per block).
BOOST_AUTO_TEST_CASE(EmptyPrioritiesEmitNullRewards)
{
    Json::Value percentiles(Json::arrayValue);
    percentiles.append(50.0);
    auto resp = request(params("0x3", percentiles));
    BOOST_REQUIRE(resp.isMember("result"));
    auto const& result = resp["result"];
    BOOST_TEST(result["oldestBlock"].asString() == "0x11");  // 19 - 3 + 1
    BOOST_REQUIRE(result["baseFeePerGas"].isArray());
    BOOST_REQUIRE_EQUAL(result["baseFeePerGas"].size(), 4U);
    BOOST_REQUIRE(result["reward"].isArray());
    BOOST_REQUIRE_EQUAL(result["reward"].size(), 3U);
    for (Json::Value::ArrayIndex i = 0; i < 3; ++i)
    {
        BOOST_REQUIRE(result["reward"][i].isArray());
        BOOST_REQUIRE_EQUAL(result["reward"][i].size(), 1U);
        BOOST_CHECK(result["reward"][i][0U].isNull());
    }
}

BOOST_AUTO_TEST_CASE(RewardPercentilesAreWeightedByGasUsed)
{
    auto const block = m_ledger->ledgerData().back();
    auto const header = block->blockHeader();
    header->setBaseFee(bcos::u256(100));
    header->setGasLimit(bcos::u256(100'000));
    header->setGasUsed(bcos::u256(100'000));

    auto const receiptFactory = m_blockFactory->receiptFactory();
    auto makeReceipt = [&](bcos::u256 gasUsed, std::string effectiveGasPrice) {
        std::vector<bcos::protocol::LogEntry> logs;
        auto receipt = receiptFactory->createReceipt(
            gasUsed, "", logs, /*status=*/0, bcos::bytesConstRef{}, header->number());
        receipt->setEffectiveGasPrice(std::move(effectiveGasPrice));
        return receipt;
    };
    block->clearReceipts();
    block->appendReceipt(makeReceipt(bcos::u256(90'000), "0x65"));  // priority fee 1
    block->appendReceipt(makeReceipt(bcos::u256(10'000), "0xc8"));  // priority fee 100

    Json::Value percentiles(Json::arrayValue);
    percentiles.append(50.0);
    auto response = request(params("0x1", percentiles));
    BOOST_REQUIRE(response.isMember("result"));
    auto const& rewards = response["result"]["reward"];
    BOOST_REQUIRE_EQUAL(rewards.size(), 1U);
    BOOST_REQUIRE_EQUAL(rewards[0U].size(), 1U);
    BOOST_TEST(rewards[0U][0U].asString() == "0x1");
}

// An OP header (baseFee present) drives the trailing entry through calcOpBaseFee; a Holocene
// 9-byte extraData keeps the fork sniff away from Jovian. Golden values mirror CalcOpBaseFeeTest:
// gasLimit 30M / elasticity 2 -> target 15M; gasUsed 20M -> deltaFee 41,666,666 over baseFee 1e9.
BOOST_AUTO_TEST_CASE(OpHeaderTrailingPredictsNextBaseFee)
{
    auto const header = m_ledger->ledgerData().back()->blockHeader();
    header->setGasLimit(bcos::u256(30'000'000));
    header->setGasUsed(bcos::u256(20'000'000));
    header->setBaseFee(bcos::u256(1'000'000'000));
    // Holocene: version(0) || denominator 8 || elasticity 2 (9 bytes, not Jovian).
    header->setExtraData(bcos::bytes{0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x02});

    auto resp = request(params("0x1"));
    BOOST_REQUIRE(resp.isMember("result"));
    auto const& result = resp["result"];
    BOOST_REQUIRE_EQUAL(result["baseFeePerGas"].size(), 2U);
    BOOST_TEST(result["baseFeePerGas"][0U].asString() == "0x3b9aca00");  // 1e9
    BOOST_TEST(result["baseFeePerGas"][1U].asString() == "0x3e16926a");  // 1,041,666,666
}

// newestBlock is required and must be a non-empty QUANTITY/TAG string. Missing, non-string,
// or empty values must not fall through toView() → latest.
BOOST_AUTO_TEST_CASE(NewestBlockMustBeStringTag)
{
    Json::Value missing(Json::arrayValue);
    missing.append("0x1");
    auto missingResp = request(missing);
    BOOST_REQUIRE(missingResp.isMember("error"));
    BOOST_CHECK_EQUAL(missingResp["error"]["code"].asInt(), -32602);

    Json::Value numeric(Json::arrayValue);
    numeric.append("0x1");
    numeric.append(1);
    auto numericResp = request(numeric);
    BOOST_REQUIRE(numericResp.isMember("error"));
    BOOST_CHECK_EQUAL(numericResp["error"]["code"].asInt(), -32602);

    Json::Value empty(Json::arrayValue);
    empty.append("0x1");
    empty.append("");
    auto emptyResp = request(empty);
    BOOST_REQUIRE(emptyResp.isMember("error"));
    BOOST_CHECK_EQUAL(emptyResp["error"]["code"].asInt(), -32602);
}

// Jovian extraData (>=17 bytes) raises the trailing next-baseFee to minBaseFee when the
// Holocene exact-target result would sit under the floor.
BOOST_AUTO_TEST_CASE(JovianHeaderTrailingAppliesMinBaseFeeFloor)
{
    auto const header = m_ledger->ledgerData().back()->blockHeader();
    header->setGasLimit(bcos::u256(30'000'000));
    header->setGasUsed(bcos::u256(15'000'000));  // exact target → Holocene delta 0
    header->setBaseFee(bcos::u256(100));
    // version(0) || denominator 8 || elasticity 2 || minBaseFee 1000 (u64 BE)
    header->setExtraData(bcos::bytes{0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x02, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0xe8});

    auto resp = request(params("0x1"));
    BOOST_REQUIRE(resp.isMember("result"));
    auto const& result = resp["result"];
    BOOST_REQUIRE_EQUAL(result["baseFeePerGas"].size(), 2U);
    BOOST_TEST(result["baseFeePerGas"][0U].asString() == "0x64");   // parent 100
    BOOST_TEST(result["baseFeePerGas"][1U].asString() == "0x3e8");  // floor 1000
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
