/**
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
 */
/**
 * @brief : unitest for SCALE utils
 * @author: catli
 * @date: 2021-09-26
 */

#include "../src/dag/Abi.h"
#include "../src/dag/ScaleUtils.h"
#include "bcos-utilities/Common.h"
#include "bcos-utilities/DataConvertUtility.h"
#include <boost/test/unit_test.hpp>
#include <utility>
#include <vector>

using namespace std;
using namespace bcos;
using namespace bcos::executor;

namespace bcos
{
namespace test
{
BOOST_AUTO_TEST_SUITE(TestScaleUtils)

BOOST_AUTO_TEST_CASE(DecodeCompactInteger)
{
    auto testCases =
        vector<pair<size_t, bytes>>{{0, {0}}, {1, {4}}, {63, {252}}, {64, {1, 1}}, {255, {253, 3}},
            {511, {253, 7}}, {16383, {253, 255}}, {16384, {2, 0, 1, 0}}, {65535, {254, 255, 3, 0}},
            {1073741823ul, {254, 255, 255, 255}}, {1073741824, {3, 0, 0, 0, 64}}};

    for (auto& testCase : testCases)
    {
        auto value = testCase.first;
        auto& encodedBytes = testCase.second;
        auto result = decodeCompactInteger(encodedBytes, 0);
        BOOST_CHECK(result.has_value());
        BOOST_CHECK_EQUAL(result.value(), value);
    }

    auto badCase = bytes{255, 255, 255, 255};
    auto result = decodeCompactInteger(badCase, 0);
    BOOST_CHECK(!result.has_value());
}

BOOST_AUTO_TEST_CASE(CalculateEncodingLength)
{
    // Encoding of string "Alice"
    auto encodedBytes = fromHexString("14616c696365");
    auto paramAbi = ParameterAbi("string");

    auto result = scaleEncodingLength(paramAbi, *encodedBytes, 0);
    BOOST_CHECK(result.has_value());
    BOOST_CHECK_EQUAL(result.value(), 6);

    // Encoding of uint32 number 20210926
    encodedBytes = fromHexString("ee643401");
    paramAbi.type = "uint32";
    result = scaleEncodingLength(paramAbi, *encodedBytes, 0);
    BOOST_CHECK(result.has_value());
    BOOST_CHECK_EQUAL(result.value(), 4);

    // Encoding of int128 number 20180710
    encodedBytes = fromHexString("e6ee3301000000000000000000000000");
    paramAbi.type = "int128";
    result = scaleEncodingLength(paramAbi, *encodedBytes, 0);
    BOOST_CHECK(result.has_value());
    BOOST_CHECK_EQUAL(result.value(), 16);

    // Encoding of vector of string ["Alice", "Bob"]
    encodedBytes = fromHexString("0814416c6963650c426f62");
    paramAbi.type = "string[]";
    result = scaleEncodingLength(paramAbi, *encodedBytes, 0);
    BOOST_CHECK(result.has_value());
    BOOST_CHECK_EQUAL(result.value(), 11);

    // Encoding of static arrary of string ["Alice", "Bob"]
    encodedBytes = fromHexString("14416c6963650c426f62");
    paramAbi.type = "string[2]";
    result = scaleEncodingLength(paramAbi, *encodedBytes, 0);
    BOOST_CHECK(result.has_value());
    BOOST_CHECK_EQUAL(result.value(), 10);

    // Encoding of tuple<string, bytes32, bytes, bool>("Alice", [0, 0, ...], [0, 0, 0], false)
    encodedBytes = fromHexString(
        "14416c69636500000000000000000000000000000000000000000000000000000000000000000c00000000");
    paramAbi.type = "tuple";
    paramAbi.components.push_back(ParameterAbi("string"));
    paramAbi.components.push_back(ParameterAbi("bytes32"));
    paramAbi.components.push_back(ParameterAbi("bytes"));
    paramAbi.components.push_back(ParameterAbi("bool"));
    result = scaleEncodingLength(paramAbi, *encodedBytes, 0);
    BOOST_CHECK(result.has_value());
    BOOST_CHECK_EQUAL(result.value(), 43);

    // Encoding of tuple<string, tuple<uint32[], string>>("Alice", ([0, 1], "Dwell not negative
    // signs"))
    encodedBytes = fromHexString(
        "14416c696365080000000001000000604477656c6c206e6f74206e65676174697665207369676e73");
    paramAbi.type = "tuple";
    paramAbi.components.clear();
    paramAbi.components.push_back(ParameterAbi("string"));
    paramAbi.components.push_back(ParameterAbi("tuple", vector<ParameterAbi>{
                                                            ParameterAbi("uint32[]"),
                                                            ParameterAbi("string"),
                                                        }));
    result = scaleEncodingLength(paramAbi, *encodedBytes, 0);
    BOOST_CHECK(result.has_value());
    BOOST_CHECK_EQUAL(result.value(), 40);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos
