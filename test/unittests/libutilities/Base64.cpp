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
 * @brief Unit tests for the Base64
 * @file Base64.cpp
 * @author: chaychen
 * @date 2018
 */
#include <libutilities/Base64.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>
#include <iostream>
#include <string>

using namespace bcos;
namespace bcos
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(Base64, TestOutputHelperFixture)
BOOST_AUTO_TEST_CASE(testBase64DecodeBytes)
{
    const std::string encodeStr = "MTIzNEFCY2Q=";
    auto decodeStr = base64DecodeBytes(encodeStr);
    std::string oriStr;
    for (size_t i = 0; i < decodeStr->size(); i++)
    {
        oriStr += char((*decodeStr)[i]);
    }
    BOOST_CHECK(oriStr == "1234ABcd");

    std::string originString = "1ab21d8355cfa17f8e61194831e81a8f22bec8c728fefb747ed035eb5082aa2b";
    bytes originBytes = *fromHexString(originString);
    std::string base64Str = base64Encode(bytesConstRef(originBytes.data(), originBytes.size()));
    std::cout << "base64Encode for " << *toHexString(originBytes) << " is: " << base64Str
              << std::endl;
    // decode the base64Str
    auto decodedBytes = base64DecodeBytes(base64Str);
    std::cout << "decodedBytes is: " << *toHexString(*decodedBytes) << std::endl;
    BOOST_CHECK(*toHexString(*decodedBytes) == originString);

    auto encodedStr = base64Encode(originString);
    std::string decodedString = base64Decode(encodedStr);
    std::cout << "encodedStr for " << originString << " is " << encodedStr << std::endl;
    std::cout << "decodedString for " << originString << " is " << decodedString << std::endl;
    BOOST_CHECK(decodedString == originString);
}

BOOST_AUTO_TEST_CASE(testBase64Encode)
{
    const std::string decodeStr = "1234ABcd";
    BOOST_CHECK(base64Encode(decodeStr) == "MTIzNEFCY2Q=");
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos
