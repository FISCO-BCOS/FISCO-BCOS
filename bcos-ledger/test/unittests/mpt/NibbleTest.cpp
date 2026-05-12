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
 * @file NibbleTest.cpp
 * @brief Unit tests for Nibble helpers (spec §5.5)
 */

#include "bcos-ledger/mpt/Nibble.h"
#include "bcos-ledger/mpt/Errors.h"
#include <boost/test/unit_test.hpp>
#include <vector>

namespace bcos::ledger::mpt::test
{

BOOST_AUTO_TEST_SUITE(NibbleSuite)

BOOST_AUTO_TEST_CASE(BytesToNibblesEvenLength)
{
    bcos::bytes const input{0xab, 0xcd};
    std::vector<uint8_t> const expected{0x0a, 0x0b, 0x0c, 0x0d};
    auto const result = bytesToNibbles(input);
    BOOST_CHECK_EQUAL_COLLECTIONS(result.begin(), result.end(), expected.begin(), expected.end());
}

BOOST_AUTO_TEST_CASE(NibblesToBytesRoundTripEvenOnly)
{
    std::vector<uint8_t> const input{0x01, 0x02, 0x03, 0x04};
    bcos::bytes const expected{0x12, 0x34};
    auto const result = nibblesToBytes(input);
    BOOST_CHECK_EQUAL_COLLECTIONS(result.begin(), result.end(), expected.begin(), expected.end());
}

BOOST_AUTO_TEST_CASE(CommonPrefixBasic)
{
    std::vector<uint8_t> const a{0x01, 0x02, 0x0a, 0x0b};
    std::vector<uint8_t> const b{0x01, 0x02, 0x0c, 0x0d};
    BOOST_CHECK_EQUAL(commonPrefixLen(a, b), 2u);
}

BOOST_AUTO_TEST_CASE(NibblesToBytesOddThrows)
{
    std::vector<uint8_t> const odd{0x01, 0x02, 0x03};
    BOOST_CHECK_THROW(nibblesToBytes(odd), MPTInvariantViolation);
}

BOOST_AUTO_TEST_CASE(BytesToNibblesRoundTrip)
{
    bcos::bytes const input{0x12, 0x34, 0x56, 0x78};
    auto const nibbles = bytesToNibbles(input);
    auto const roundTrip = nibblesToBytes(nibbles);
    BOOST_CHECK_EQUAL_COLLECTIONS(roundTrip.begin(), roundTrip.end(), input.begin(), input.end());
}

BOOST_AUTO_TEST_CASE(CommonPrefixEmpty)
{
    std::vector<uint8_t> const a{};
    std::vector<uint8_t> const b{0x01};
    BOOST_CHECK_EQUAL(commonPrefixLen(a, b), 0u);
}

BOOST_AUTO_TEST_CASE(CommonPrefixIdentical)
{
    std::vector<uint8_t> const a{0x01, 0x02, 0x03};
    BOOST_CHECK_EQUAL(commonPrefixLen(a, a), 3u);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::ledger::mpt::test
