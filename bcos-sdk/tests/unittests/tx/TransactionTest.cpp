/*
 *  Copyright (C) 2021 FISCO BCOS.
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
 * @file TransactionTest.cpp
 * @author: yujiechen
 * @date 2022-05-31
 */

#include <bcos-crypto/hash/SM3.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-crypto/signature/sm2/SM2Crypto.h>
#include <bcos-tars-protocol/protocol/TransactionImpl.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>

using namespace bcostars;
using namespace bcos;
using namespace bcos::crypto;

namespace bcos::test
{
BOOST_FIXTURE_TEST_SUITE(TransactionTest, TestPromptFixture)

BOOST_AUTO_TEST_CASE(test_transaction)
{
    TransactionData txData;
    std::string to("Target");
    bcos::bytes input(bcos::asBytes("Arguments"));
    bcos::u256 nonce(800);

    txData.version = 0;
    txData.to = to;
    vector<tars::Char> txInput(input.begin(), input.end());
    txData.input = std::move(txInput);
    txData.nonce = boost::lexical_cast<std::string>(nonce);
    txData.blockLimit = 100;
    txData.chainID = "testChain";
    txData.groupID = "testGroup";

    auto cryptoSuite =
        std::make_shared<bcos::crypto::CryptoSuite>(std::make_shared<bcos::crypto::SM3>(),
            std::make_shared<bcos::crypto::SM2Crypto>(), nullptr);

    // auto hash = txData.hash(cryptoSuite->hashImpl());
    // BOOST_CHECK_EQUAL(
    //     hash.hex(), "3577ef0338695b03c6f19d8b7c1aa1f443973214dde94879a44188490529ea70");

    // // set version to 10
    // txData.version = 10;
    // hash = txData.hash(cryptoSuite->hashImpl());
    // BOOST_CHECK_EQUAL(
    //     hash.hex(), "435da41370f4711de4259094c8362a7332cf752ec359d057bee97453ca9e5072");
}
BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test