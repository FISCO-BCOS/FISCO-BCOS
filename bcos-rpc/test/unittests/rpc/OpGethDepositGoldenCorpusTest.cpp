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
 * @file OpGethDepositGoldenCorpusTest.cpp
 * @brief Load every pinned op-geth deposit-receipt golden and pin the contracts the
 *        corpus exists to anchor (reachability + key parity invariants).
 */

#include <boost/test/unit_test.hpp>

#include <json/json.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace
{
std::filesystem::path goldenDir()
{
    return std::filesystem::path(BCOS_RPC_TEST_SOURCE_DIR) /
           "unittests/rpc/golden/op-geth-deposit-receipt";
}

Json::Value loadJson(const std::filesystem::path& path)
{
    std::ifstream in(path);
    BOOST_REQUIRE_MESSAGE(in.good(), "cannot open golden " << path);
    Json::Reader reader;
    Json::Value value;
    BOOST_REQUIRE_MESSAGE(reader.parse(in, value), "cannot parse golden " << path);
    return value;
}

std::vector<std::string> stems()
{
    std::vector<std::string> out;
    for (auto const& entry : std::filesystem::directory_iterator(goldenDir()))
    {
        if (entry.path().extension() == ".json" && entry.path().stem().string().ends_with(".tx"))
        {
            out.push_back(entry.path().stem().string().substr(
                0, entry.path().stem().string().size() - 3));
        }
    }
    BOOST_REQUIRE(!out.empty());
    return out;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(OpGethDepositGoldenCorpusTest)

// The corpus was pinned (op-geth e8800cff) to anchor the deposit rendering parity,
// including the always-present `mint` on the tx side. A test that never loads the
// files pins nothing: this case makes every fixture reachable and asserts the very
// invariants the RPC parity work leaned on.
BOOST_AUTO_TEST_CASE(EveryGoldenParsesAndPinsTheParityContracts)
{
    for (auto const& stem : stems())
    {
        const auto tx = loadJson(goldenDir() / (stem + ".tx.json"));
        const auto receipt = loadJson(goldenDir() / (stem + ".receipt.json"));

        // op-geth always emits mint on deposit txs (nil decodes to "0x0"): the FISCO
        // render mirrors that (DepositTransaction.cpp) — the golden is the anchor.
        BOOST_CHECK_MESSAGE(tx.isMember("mint"), stem << ": tx golden lacks mint");
        BOOST_CHECK_MESSAGE(tx["type"].asString() == "0x7e", stem << ": not a deposit tx");

        // Receipt side: status present, transactionHash matches the tx hash, and the
        // Ecotone-era goldens carry depositReceiptVersion/depositNonce (which is why
        // no l1FeeScalar appears — that field is Bedrock-era only upstream).
        BOOST_CHECK(receipt.isMember("status"));
        BOOST_CHECK_EQUAL(receipt["transactionHash"].asString(), tx["hash"].asString());
        BOOST_CHECK(receipt.isMember("depositNonce"));
        BOOST_CHECK(receipt.isMember("depositReceiptVersion"));
        BOOST_CHECK_MESSAGE(!receipt.isMember("l1FeeScalar"),
            stem << ": Ecotone-era golden unexpectedly carries l1FeeScalar");
    }
}

BOOST_AUTO_TEST_SUITE_END()
