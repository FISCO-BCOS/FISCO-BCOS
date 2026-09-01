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
#include "bcos-ledger/test/unittests/ExceptionCheck.h"
#include <boost/test/unit_test.hpp>

namespace bcos::ledger::mpt::test
{
using bcos::test::errinfoContains;

BOOST_AUTO_TEST_SUITE(NibbleSuite)

BOOST_AUTO_TEST_CASE(BytesToNibblesEvenLength)
{
    bcos::bytes const input{0xab, 0xcd};
    bcos::bytes const expected{0x0a, 0x0b, 0x0c, 0x0d};
    auto const result = bytesToNibbles(bcos::ref(input));
    BOOST_CHECK_EQUAL_COLLECTIONS(result.begin(), result.end(), expected.begin(), expected.end());
}

BOOST_AUTO_TEST_CASE(NibblesToBytesRoundTripEvenOnly)
{
    bcos::bytes const input{0x01, 0x02, 0x03, 0x04};
    bcos::bytes const expected{0x12, 0x34};
    auto const result = nibblesToBytes(bcos::ref(input));
    BOOST_CHECK_EQUAL_COLLECTIONS(result.begin(), result.end(), expected.begin(), expected.end());
}

BOOST_AUTO_TEST_CASE(CommonPrefixBasic)
{
    bcos::bytes const lhs{0x01, 0x02, 0x0a, 0x0b};
    bcos::bytes const rhs{0x01, 0x02, 0x0c, 0x0d};
    BOOST_CHECK_EQUAL(commonPrefixLen(bcos::ref(lhs), bcos::ref(rhs)), 2U);
}

BOOST_AUTO_TEST_CASE(NibblesToBytesOddThrows)
{
    bcos::bytes const odd{0x01, 0x02, 0x03};
    BOOST_CHECK_EXCEPTION(nibblesToBytes(bcos::ref(odd)), MPTInvariantViolation,
        [](auto const& e) { return errinfoContains(e, "requires even nibble count"); });
}

BOOST_AUTO_TEST_CASE(NibblesToBytesOutOfRangeThrows)
{
    // 0x10 has bits above the low nibble — violates the strict 4-bit invariant.
    bcos::bytes const bad{0x01, 0x10};
    BOOST_CHECK_EXCEPTION(nibblesToBytes(bcos::ref(bad)), MPTInvariantViolation,
        [](auto const& e) { return errinfoContains(e, "nibble value out of range"); });
}

BOOST_AUTO_TEST_CASE(BytesToNibblesRoundTrip)
{
    bcos::bytes const input{0x12, 0x34, 0x56, 0x78};
    auto const nibbles = bytesToNibbles(bcos::ref(input));
    auto const roundTrip = nibblesToBytes(bcos::ref(nibbles));
    BOOST_CHECK_EQUAL_COLLECTIONS(roundTrip.begin(), roundTrip.end(), input.begin(), input.end());
}

BOOST_AUTO_TEST_CASE(CommonPrefixEmpty)
{
    bcos::bytes const lhs{};
    bcos::bytes const rhs{0x01};
    BOOST_CHECK_EQUAL(commonPrefixLen(bcos::ref(lhs), bcos::ref(rhs)), 0U);
}

BOOST_AUTO_TEST_CASE(CommonPrefixIdentical)
{
    bcos::bytes const lhs{0x01, 0x02, 0x03};
    BOOST_CHECK_EQUAL(commonPrefixLen(bcos::ref(lhs), bcos::ref(lhs)), 3U);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::ledger::mpt::test
