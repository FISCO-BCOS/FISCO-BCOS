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
 * @file EthWithdrawalTest.cpp
 * @brief Golden-vector + round-trip tests for EthWithdrawal
 * @date 2026/8/18
 */

#include "bcos-rlp-protocol/EthWithdrawal.h"
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::protocol;

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(EthWithdrawalTest)

// Golden vector: rlp([index=1, validatorIndex=2, 0x33*20, amount=4])
static const std::string kGoldenHex = "d8010294333333333333333333333333333333333333333304";

static EthWithdrawalData makeWithdrawal()
{
    EthWithdrawalData w;
    w.index = 1;
    w.validatorIndex = 2;
    w.address =
        Address(std::string_view("0x3333333333333333333333333333333333333333"), Address::FromHex);
    w.amount = 4;
    return w;
}

BOOST_AUTO_TEST_CASE(goldenEncode)
{
    EthWithdrawal w(makeWithdrawal());
    bytes out;
    w.rlpEncode(out);
    BOOST_CHECK_EQUAL(toHex(out), kGoldenHex);
}

BOOST_AUTO_TEST_CASE(goldenDecode)
{
    EthWithdrawal w;
    auto rawkGolden = fromHex(kGoldenHex);
    auto err = w.rlpDecode(ref(rawkGolden));
    BOOST_CHECK(!err);
    BOOST_CHECK(w.data() == makeWithdrawal());
}

BOOST_AUTO_TEST_CASE(roundTrip)
{
    auto wd = makeWithdrawal();
    EthWithdrawal w(wd);
    bytes out;
    w.rlpEncode(out);
    EthWithdrawal decoded;
    BOOST_CHECK(!decoded.rlpDecode(ref(out)));
    BOOST_CHECK(decoded.data() == wd);
}

// Trailing bytes after the top-level RLP item must be rejected (round-6 F3 guard).
BOOST_AUTO_TEST_CASE(rlpDecodeRejectsTrailingBytes)
{
    auto wd = makeWithdrawal();
    EthWithdrawal w(wd);
    bytes out;
    w.rlpEncode(out);
    out.push_back(0xff);
    EthWithdrawal decoded;
    BOOST_REQUIRE(decoded.rlpDecode(ref(out)) != nullptr);
}

// std::vector<EthWithdrawalData> through the generic list codec (used by EthBlock).
BOOST_AUTO_TEST_CASE(vectorRoundTrip)
{
    std::vector<EthWithdrawalData> ws = {makeWithdrawal(), makeWithdrawal()};
    bytes out;
    codec::rlp::encode(out, ws);
    std::vector<EthWithdrawalData> decoded;
    auto mutableData = out;
    bytesRef in(mutableData.data(), mutableData.size());
    BOOST_CHECK(!codec::rlp::decode(in, decoded));
    BOOST_CHECK(decoded == ws);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
