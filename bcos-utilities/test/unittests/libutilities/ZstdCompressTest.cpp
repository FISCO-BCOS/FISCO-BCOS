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
 *
 * @brief Unit tests for the ZstdCompress
 * @file ZstdCompressTest.cpp
 */
#include "bcos-utilities/ZstdCompress.h"
#include "bcos-utilities/testutils/TestPromptFixture.h"
#include <boost/test/unit_test.hpp>
#include <iostream>
#include <string>

using namespace bcos;
namespace bcos
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(ZstdCompressTests, TestPromptFixture)
BOOST_AUTO_TEST_CASE(testZstdCompress)
{
    auto payload = std::make_shared<bytes>(10000, 'a');
    auto wrongCompressedData = std::make_shared<bytes>(10000, 'b');
    std::shared_ptr<bytes> compressData = std::make_shared<bytes>();
    std::shared_ptr<bytes> uncompressData = std::make_shared<bytes>();

    // compress uncompress success
    bool retCompress = ZstdCompress::compress(ref(*payload), *compressData, 1);
    BOOST_CHECK(retCompress);
    ssize_t retUncompress = ZstdCompress::uncompress(ref(*compressData), *uncompressData);
    BOOST_CHECK(retUncompress);
    BOOST_CHECK_EQUAL(std::string(uncompressData->begin(), uncompressData->end()),
        std::string(payload->begin(), payload->end()));

    // uncompress fail
    bool retUncompressFail = ZstdCompress::uncompress(ref(*wrongCompressedData), *uncompressData);
    BOOST_CHECK(!retUncompressFail);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos
