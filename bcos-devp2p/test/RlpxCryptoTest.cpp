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
 * @file RlpxCryptoTest.cpp
 * @brief Tests for RLPx Keccak-256 streaming and snapshot behavior.
 */
#include <bcos-devp2p/rlpx/Crypto.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/test/unit_test.hpp>
#include <string>

using namespace bcos;
using namespace bcos::devp2p::rlpx;

BOOST_AUTO_TEST_SUITE(RlpxCryptoTest)

BOOST_AUTO_TEST_CASE(keccakStreamingSnapshot)
{
    Sha3Hasher hasher;
    std::string abc = "abc";
    hasher.update(bytesConstRef(abc));
    BOOST_CHECK_EQUAL(
        toHex(hasher.hash()), "4e03657aea45a94fc7d47ba826c8d667c0d1e6e33a64a036ec44f58fa12d6c45");

    std::string def = "def";
    hasher.update(bytesConstRef(def));
    auto continued = hasher.hash();

    Sha3Hasher reference;
    std::string abcdef = "abcdef";
    reference.update(bytesConstRef(abcdef));
    BOOST_CHECK(continued == reference.hash());
    BOOST_CHECK(keccak256(bytesConstRef(abc), bytesConstRef(def)) == continued);
}

BOOST_AUTO_TEST_CASE(keccakBlockBoundaries)
{
    std::string input(300, 'x');
    auto const* data = reinterpret_cast<byte const*>(input.data());
    Sha3Hasher streaming;
    streaming.update(bytesConstRef(data, 136));
    streaming.update(bytesConstRef(data + 136, 1));
    streaming.update(bytesConstRef(data + 137, input.size() - 137));
    BOOST_CHECK(streaming.hash() == keccak256(bytesConstRef(input)));
}

BOOST_AUTO_TEST_CASE(keccakPaddingBoundaries)
{
    Sha3Hasher empty;
    BOOST_CHECK_EQUAL(
        toHex(empty.hash()), "c5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470");

    std::string input(135, 'x');
    BOOST_CHECK_EQUAL(toHex(keccak256(bytesConstRef(input))),
        "16570bdb055e663ea1cb57ac6f09194f4bc7b7070847971fc0b86710366dc34f");
}

BOOST_AUTO_TEST_SUITE_END()
