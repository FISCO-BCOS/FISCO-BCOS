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
 * @file HexPrefixTest.cpp
 * @brief Unit tests for Hex-Prefix encoding (Yellow Paper Appendix C vectors)
 */

#include "bcos-ledger/mpt/HexPrefix.h"
#include "bcos-ledger/mpt/Errors.h"
#include <boost/test/unit_test.hpp>
#include <vector>

namespace bcos::ledger::mpt::test
{

BOOST_AUTO_TEST_SUITE(HexPrefixSuite)

// Yellow Paper Appendix C standard vector: even count, no terminator
// nibbles={0,1,2,3,4,5}, term=false -> {0x00, 0x01, 0x23, 0x45}
BOOST_AUTO_TEST_CASE(EncodeEvenNoTerminator)
{
    std::vector<uint8_t> const nibbles{0, 1, 2, 3, 4, 5};
    bcos::bytes const expected{0x00, 0x01, 0x23, 0x45};
    auto const result = hexPrefixEncode(nibbles, false);
    BOOST_CHECK_EQUAL_COLLECTIONS(result.begin(), result.end(), expected.begin(), expected.end());
}

// Yellow Paper Appendix C standard vector: odd count, no terminator
// nibbles={1,2,3,4,5}, term=false -> {0x11, 0x23, 0x45}
BOOST_AUTO_TEST_CASE(EncodeOddNoTerminator)
{
    std::vector<uint8_t> const nibbles{1, 2, 3, 4, 5};
    bcos::bytes const expected{0x11, 0x23, 0x45};
    auto const result = hexPrefixEncode(nibbles, false);
    BOOST_CHECK_EQUAL_COLLECTIONS(result.begin(), result.end(), expected.begin(), expected.end());
}

// Yellow Paper Appendix C standard vector: even count, with terminator
// nibbles={0,1,2,3,4,5}, term=true -> {0x20, 0x01, 0x23, 0x45}
BOOST_AUTO_TEST_CASE(EncodeEvenWithTerminator)
{
    std::vector<uint8_t> const nibbles{0, 1, 2, 3, 4, 5};
    bcos::bytes const expected{0x20, 0x01, 0x23, 0x45};
    auto const result = hexPrefixEncode(nibbles, true);
    BOOST_CHECK_EQUAL_COLLECTIONS(result.begin(), result.end(), expected.begin(), expected.end());
}

// Yellow Paper Appendix C standard vector: odd count, with terminator
// nibbles={1,2,3,4,5}, term=true -> {0x31, 0x23, 0x45}
BOOST_AUTO_TEST_CASE(EncodeOddWithTerminator)
{
    std::vector<uint8_t> const nibbles{1, 2, 3, 4, 5};
    bcos::bytes const expected{0x31, 0x23, 0x45};
    auto const result = hexPrefixEncode(nibbles, true);
    BOOST_CHECK_EQUAL_COLLECTIONS(result.begin(), result.end(), expected.begin(), expected.end());
}

// Round-trip for both terminator values
BOOST_AUTO_TEST_CASE(EncodeDecodeRoundTrip)
{
    std::vector<uint8_t> const nibbles{0x0a, 0x0b, 0x0c, 0x0d, 0x0e};

    for (bool term : {false, true})
    {
        auto const encoded = hexPrefixEncode(nibbles, term);
        auto const [decoded, decodedTerm] = hexPrefixDecode(encoded);
        BOOST_CHECK_EQUAL(decodedTerm, term);
        BOOST_CHECK_EQUAL_COLLECTIONS(
            decoded.begin(), decoded.end(), nibbles.begin(), nibbles.end());
    }
}

BOOST_AUTO_TEST_CASE(DecodeEmptyThrows)
{
    bcos::bytes const empty{};
    BOOST_CHECK_THROW(hexPrefixDecode(empty), MPTDecodeError);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::ledger::mpt::test
