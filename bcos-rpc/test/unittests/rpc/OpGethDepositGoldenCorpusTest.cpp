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

#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-framework/protocol/TransactionReceipt.h>
#include <bcos-rpc/web3jsonrpc/model/DepositTransaction.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h>
#include <bcos-utilities/DataConvertUtility.h>
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
        // deposit nonce is always present. depositReceiptVersion is era-gated (Canyon+):
        // pre-Canyon goldens legitimately carry nonce ONLY (the presence-of-version rule
        // encodeReceiptLeaf implements) — assert both shapes positively.
        BOOST_CHECK(receipt.isMember("status"));
        BOOST_CHECK_EQUAL(receipt["transactionHash"].asString(), tx["hash"].asString());
        BOOST_CHECK(receipt.isMember("depositNonce"));
        if (stem.find("pre-canyon") != std::string::npos)
        {
            BOOST_CHECK_MESSAGE(!receipt.isMember("depositReceiptVersion"),
                stem << ": pre-Canyon golden unexpectedly carries depositReceiptVersion");
        }
        else
        {
            BOOST_CHECK(receipt.isMember("depositReceiptVersion"));
        }
        BOOST_CHECK_MESSAGE(!receipt.isMember("l1FeeScalar"),
            stem << ": Ecotone-era golden unexpectedly carries l1FeeScalar");
    }
}

namespace
{
bcos::protocol::TransactionReceipt::Ptr receiptWithDepositMeta(
    std::optional<uint64_t> nonce, std::optional<uint64_t> version)
{
    auto crypto = std::make_shared<bcos::crypto::CryptoSuite>(
        std::make_shared<bcos::crypto::Keccak256>(),
        std::make_shared<bcos::crypto::Secp256k1Crypto>(), nullptr);
    auto factory = std::make_shared<bcostars::protocol::TransactionReceiptFactoryImpl>(crypto);
    auto receipt = factory->createReceipt(bcos::u256(21000),
        "0x0000000000000000000000000000000000000000", {}, 1, bcos::bytesConstRef{}, 1);
    bcos::protocol::OpStackReceiptMeta meta;
    meta.deposit_nonce = nonce;
    meta.deposit_receipt_version = version;
    receipt->setOpStackMeta(meta);
    return receipt;
}

std::string lowerHex(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}
}  // namespace

// The deposit-driven renderer over a fixture's own fields, compared VALUE-level against
// the golden — the presence-only shape above cannot see a wrong value (the canyon-era
// fixtures carry nonce 0xbb9 / depositReceiptVersion the renderer could not emit before
// the receipt-driven fill).
BOOST_AUTO_TEST_CASE(RenderersReproduceTheGoldenValues)
{
    for (auto const& stem : stems())
    {
        const auto goldenTx = loadJson(goldenDir() / (stem + ".tx.json"));
        const auto goldenReceipt = loadJson(goldenDir() / (stem + ".receipt.json"));

        bcos::rpc::DepositTransaction deposit;
        deposit.sourceHash = bcos::h256(goldenTx["sourceHash"].asString());
        deposit.from = bcos::Address(goldenTx["from"].asString());
        deposit.to = goldenTx["to"].isNull() ?
                         std::nullopt :
                         std::optional<bcos::Address>(bcos::Address(goldenTx["to"].asString()));
        deposit.mint = bcos::u256(goldenTx["mint"].asString());
        deposit.value = bcos::u256(goldenTx["value"].asString());
        deposit.gas = bcos::u256(goldenTx["gas"].asString()).convert_to<uint64_t>();
        deposit.isSystemTx = goldenTx.get("isSystemTx", false).asBool();
        deposit.input = bcos::fromHex(goldenTx["input"].asString());

        Json::Value result(Json::objectValue);
        bcos::rpc::combineDepositTxResponse(result, deposit);

        std::optional<uint64_t> nonce, version;
        if (goldenReceipt.isMember("depositNonce"))
        {
            nonce = bcos::u256(goldenReceipt["depositNonce"].asString()).convert_to<uint64_t>();
        }
        if (goldenReceipt.isMember("depositReceiptVersion"))
        {
            version =
                bcos::u256(goldenReceipt["depositReceiptVersion"].asString()).convert_to<uint64_t>();
        }
        auto receipt = receiptWithDepositMeta(nonce, version);
        bcos::rpc::fillDepositReceiptFields(result, *receipt);

        for (auto const* key : {"type", "sourceHash", "gas", "value", "input", "mint",
                 "gasPrice", "v", "r", "s", "nonce"})
        {
            BOOST_REQUIRE_MESSAGE(result.isMember(key), stem << ": renderer lacks " << key);
            BOOST_CHECK_MESSAGE(result[key].asString() == goldenTx[key].asString(),
                stem << ": " << key << " rendered " << result[key].asString() << " != golden "
                     << goldenTx[key].asString());
        }
        BOOST_CHECK_MESSAGE(lowerHex(result["from"].asString()) == lowerHex(goldenTx["from"].asString()),
            stem << ": from mismatch");
        if (goldenTx["to"].isNull())
        {
            BOOST_CHECK_MESSAGE(result["to"].isNull(), stem << ": to must be null");
        }
        else
        {
            BOOST_CHECK_MESSAGE(
                lowerHex(result["to"].asString()) == lowerHex(goldenTx["to"].asString()),
                stem << ": to mismatch");
        }
        if (goldenTx.isMember("isSystemTx"))
        {
            BOOST_CHECK_EQUAL(result["isSystemTx"].asBool(), goldenTx["isSystemTx"].asBool());
        }
        // depositReceiptVersion rides the receipt: present iff the receipt golden carries it.
        if (goldenTx.isMember("depositReceiptVersion"))
        {
            BOOST_CHECK_MESSAGE(
                result.isMember("depositReceiptVersion") &&
                    result["depositReceiptVersion"].asString() ==
                        goldenTx["depositReceiptVersion"].asString(),
                stem << ": depositReceiptVersion mismatch");
        }
        else
        {
            BOOST_CHECK_MESSAGE(!result.isMember("depositReceiptVersion"),
                stem << ": depositReceiptVersion must not be emitted without the receipt meta");
        }
    }
}

BOOST_AUTO_TEST_SUITE_END()
