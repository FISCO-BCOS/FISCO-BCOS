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
 * @brief: UT for SnappyCompress
 *
 * @file CompressTest.cpp
 * @author: yujiechen
 * @date 2019-03-21
 */

#include <libutilities/DataConvertUtility.h>
#include <libutilities/SnappyCompress.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>
#include <iostream>

using namespace bcos;
using namespace bcos::compress;
namespace bcos
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(CompressTest, TestOutputHelperFixture)

class CompressTestFixture
{
public:
    void checkCompressAlgorithm()
    {
        std::string inputData = "abc21324";
        bytes compressedData;
        bytes inputDataBytes(inputData.begin(), inputData.end());

        /// compress test
        size_t compressLen = SnappyCompress::compress(ref(inputDataBytes), compressedData);
        BOOST_CHECK((compressLen) == compressedData.size());

        /// uncompress test
        bytes uncompressData;
        size_t uncompressLen = SnappyCompress::uncompress(ref(compressedData), uncompressData);
        BOOST_CHECK(uncompressData.size() == uncompressLen);
        BOOST_CHECK(*toHexString(uncompressData) == *toHexString(inputDataBytes));
        BOOST_CHECK(asString(uncompressData) == inputData);
    }
};

BOOST_AUTO_TEST_CASE(testCompressAlgorithm)
{
    CompressTestFixture compressFixture;
    compressFixture.checkCompressAlgorithm();
}
BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos
