/**
 *  Copyright (C) 2020 FISCO BCOS.
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
 * @brief Unit tests for the FixedBytes
 * @file FixedBytes.cpp
 * @author: chaychen
 * @date 2018
 */

#include <libutilities/DataConvertUtility.h>
#include <libutilities/FixedBytes.h>
#include <libutilities/JsonDataConvertUtility.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>
#include <string>

using namespace bcos;
namespace bcos
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(FixedBytes, TestOutputHelperFixture)
BOOST_AUTO_TEST_CASE(testLeft160)
{
    const std::string str = "0x067150c07dab4facb7160e075548007e067150c07dab4facb7160e075548007e";
    h256 h = jsToFixed<32>(str);
    BOOST_CHECK("0x067150c07dab4facb7160e075548007e067150c0" == toJonString(left160(h)));
}

BOOST_AUTO_TEST_CASE(testRight160)
{
    const std::string str = "0x067150c07dab4facb7160e075548007e067150c07dab4facb7160e075548007e";
    h256 h = jsToFixed<32>(str);
    BOOST_CHECK("0x5548007e067150c07dab4facb7160e075548007e" == toJonString(right160(h)));
    // test u256
    h256 h256Data(
        *fromHexString("12b5155eda010a5b7ae26a4a268e466a4b8d31547ad875fce9ab298c639a1b2f"));
    // trans h256Data to u256
    u256 value(h256Data);
    // trans value to h256 again
    h256 convertedH256Data = value;
    std::cout << "### value: " << value << ", h256Data:" << *toHexString(h256Data)
              << "convertedH256Data" << *toHexString(convertedH256Data) << std::endl;
    BOOST_CHECK(convertedH256Data == h256Data);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos
