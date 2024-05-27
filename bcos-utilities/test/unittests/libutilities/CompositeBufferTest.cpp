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
 * @brief Unit tests for the CompositeBuffer
 * @file Base64.cpp
 */
#include "bcos-utilities/CompositeBuffer.h"
#include "bcos-utilities/DataConvertUtility.h"
#include "bcos-utilities/testutils/TestPromptFixture.h"
#include <boost/test/tools/old/interface.hpp>
#include <boost/test/unit_test.hpp>
#include <utility>

using namespace bcos;
namespace bcos
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(CompositeBufferTest, TestPromptFixture)

BOOST_AUTO_TEST_CASE(compositeBufferTest)
{
    {
        auto compositeBuffer = CompositeBufferFactory::build();
        BOOST_CHECK_EQUAL(compositeBuffer->length(), 0);
        BOOST_CHECK_EQUAL(compositeBuffer->payloadSize(), 0);
        BOOST_CHECK_EQUAL(compositeBuffer->constToSingleBuffer().size(), 0);
        BOOST_CHECK_EQUAL(compositeBuffer->buffers().size(), 0);

        std::string s0 = "Hello";
        compositeBuffer->append(asBytes(s0));

        BOOST_CHECK_EQUAL(compositeBuffer->length(), 1);
        BOOST_CHECK_EQUAL(compositeBuffer->payloadSize(), s0.size());
        // BOOST_CHECK_EQUAL(compositeBuffer->toSingleBuffer().size(), s0.size());
        BOOST_CHECK_EQUAL(compositeBuffer->constToSingleBuffer().size(), s0.size());
        BOOST_CHECK_EQUAL(compositeBuffer->buffers().size(), 1);
        auto singleBuffer0 = compositeBuffer->constToSingleBuffer();
        BOOST_CHECK_EQUAL(std::string(singleBuffer0.begin(), singleBuffer0.end()), s0);


        std::string s1 = "FISCO BCOS 3.0.";
        compositeBuffer->append(asBytes(s1));

        BOOST_CHECK_EQUAL(compositeBuffer->length(), 2);
        BOOST_CHECK_EQUAL(compositeBuffer->payloadSize(), (s0 + s1).size());
        // BOOST_CHECK_EQUAL(compositeBuffer->toSingleBuffer().size(), (s0 + s1).size());
        BOOST_CHECK_EQUAL(compositeBuffer->constToSingleBuffer().size(), (s0 + s1).size());
        BOOST_CHECK_EQUAL(compositeBuffer->buffers().size(), 2);
        auto singleBuffer1 = compositeBuffer->constToSingleBuffer();
        BOOST_CHECK_EQUAL(std::string(singleBuffer1.begin(), singleBuffer1.end()), s0 + s1);

        std::string s2(100000, '1');

        compositeBuffer->append(asBytes(s2));

        BOOST_CHECK_EQUAL(compositeBuffer->length(), 3);
        BOOST_CHECK_EQUAL(compositeBuffer->payloadSize(), (s0 + s1 + s2).size());
        // BOOST_CHECK_EQUAL(compositeBuffer->toSingleBuffer().size(), (s0 + s1 + s2).size());
        BOOST_CHECK_EQUAL(compositeBuffer->constToSingleBuffer().size(), (s0 + s1 + s2).size());
        BOOST_CHECK_EQUAL(compositeBuffer->buffers().size(), 3);
        auto singleBuffer2 = compositeBuffer->constToSingleBuffer();
        BOOST_CHECK_EQUAL(std::string(singleBuffer2.begin(), singleBuffer2.end()), s0 + s1 + s2);

        std::string s3(100000, '2');

        compositeBuffer->append(asBytes(s3));

        BOOST_CHECK_EQUAL(compositeBuffer->length(), 4);
        BOOST_CHECK_EQUAL(compositeBuffer->payloadSize(), (s0 + s1 + s2 + s3).size());
        // BOOST_CHECK_EQUAL(compositeBuffer->toSingleBuffer().size(), (s0 + s1 + s2 + s3).size());
        BOOST_CHECK_EQUAL(
            compositeBuffer->constToSingleBuffer().size(), (s0 + s1 + s2 + s3).size());
        BOOST_CHECK_EQUAL(compositeBuffer->buffers().size(), 4);
        auto singleBuffer3 = compositeBuffer->constToSingleBuffer();
        BOOST_CHECK_EQUAL(
            std::string(singleBuffer3.begin(), singleBuffer3.end()), s0 + s1 + s2 + s3);

        auto newCompositeBuffer = std::move(*compositeBuffer);
        BOOST_CHECK_EQUAL(compositeBuffer->length(), 0);
        BOOST_CHECK_EQUAL(compositeBuffer->buffers().size(), 0);
        BOOST_CHECK_EQUAL(compositeBuffer->payloadSize(), 0);

        BOOST_CHECK_EQUAL(newCompositeBuffer.length(), 4);
        BOOST_CHECK_EQUAL(newCompositeBuffer.payloadSize(), (s0 + s1 + s2 + s3).size());
        // BOOST_CHECK_EQUAL(compositeBuffer->toSingleBuffer().size(), (s0 + s1 + s2 + s3).size());
        BOOST_CHECK_EQUAL(
            newCompositeBuffer.constToSingleBuffer().size(), (s0 + s1 + s2 + s3).size());
        BOOST_CHECK_EQUAL(newCompositeBuffer.buffers().size(), 4);
        auto singleBuffer4 = newCompositeBuffer.constToSingleBuffer();
        BOOST_CHECK_EQUAL(
            std::string(singleBuffer4.begin(), singleBuffer4.end()), s0 + s1 + s2 + s3);
    }

    {
        std::string s0 = "Hello";
        auto compositeBuffer = CompositeBufferFactory::build(asBytes(s0));

        BOOST_CHECK_EQUAL(compositeBuffer->length(), 1);
        BOOST_CHECK_EQUAL(compositeBuffer->payloadSize(), s0.size());
        BOOST_CHECK_EQUAL(compositeBuffer->constToSingleBuffer().size(), s0.size());
        BOOST_CHECK_EQUAL(compositeBuffer->buffers().size(), 1);

        auto singleBuffer0 = compositeBuffer->constToSingleBuffer();
        BOOST_CHECK_EQUAL(std::string(singleBuffer0.begin(), singleBuffer0.end()), s0);

        std::string s1 = "FISCO BCOS 3.0.";
        compositeBuffer->append(asBytes(s1), true);

        BOOST_CHECK_EQUAL(compositeBuffer->length(), 2);
        BOOST_CHECK_EQUAL(compositeBuffer->payloadSize(), (s0 + s1).size());
        // BOOST_CHECK_EQUAL(compositeBuffer->toSingleBuffer().size(), (s0 + s1).size());
        BOOST_CHECK_EQUAL(compositeBuffer->constToSingleBuffer().size(), (s0 + s1).size());
        BOOST_CHECK_EQUAL(compositeBuffer->buffers().size(), 2);
        auto singleBuffer1 = compositeBuffer->constToSingleBuffer();
        BOOST_CHECK_EQUAL(std::string(singleBuffer1.begin(), singleBuffer1.end()), s1 + s0);

        std::string s2(100000, '1');

        compositeBuffer->append(asBytes(s2), true);

        BOOST_CHECK_EQUAL(compositeBuffer->length(), 3);
        BOOST_CHECK_EQUAL(compositeBuffer->payloadSize(), (s0 + s1 + s2).size());
        // BOOST_CHECK_EQUAL(compositeBuffer->toSingleBuffer().size(), (s0 + s1 + s2).size());
        BOOST_CHECK_EQUAL(compositeBuffer->constToSingleBuffer().size(), (s0 + s1 + s2).size());
        BOOST_CHECK_EQUAL(compositeBuffer->buffers().size(), 3);
        auto singleBuffer2 = compositeBuffer->constToSingleBuffer();
        BOOST_CHECK_EQUAL(std::string(singleBuffer2.begin(), singleBuffer2.end()), s2 + s1 + s0);

        std::string s3(100000, '2');

        compositeBuffer->append(asBytes(s3));

        BOOST_CHECK_EQUAL(compositeBuffer->length(), 4);
        BOOST_CHECK_EQUAL(compositeBuffer->payloadSize(), (s0 + s1 + s2 + s3).size());
        // BOOST_CHECK_EQUAL(compositeBuffer->toSingleBuffer().size(), (s0 + s1 + s2 + s3).size());
        BOOST_CHECK_EQUAL(
            compositeBuffer->constToSingleBuffer().size(), (s0 + s1 + s2 + s3).size());
        BOOST_CHECK_EQUAL(compositeBuffer->buffers().size(), 4);
        auto singleBuffer3 = compositeBuffer->constToSingleBuffer();
        BOOST_CHECK_EQUAL(
            std::string(singleBuffer3.begin(), singleBuffer3.end()), s2 + s1 + s0 + s3);

        auto newCompositeBuffer = std::move(*compositeBuffer);
        BOOST_CHECK_EQUAL(compositeBuffer->length(), 0);
        BOOST_CHECK_EQUAL(compositeBuffer->buffers().size(), 0);
        BOOST_CHECK_EQUAL(compositeBuffer->payloadSize(), 0);

        BOOST_CHECK_EQUAL(newCompositeBuffer.length(), 4);
        BOOST_CHECK_EQUAL(newCompositeBuffer.payloadSize(), (s0 + s1 + s2 + s3).size());
        // BOOST_CHECK_EQUAL(compositeBuffer->toSingleBuffer().size(), (s0 + s1 + s2 + s3).size());
        BOOST_CHECK_EQUAL(
            newCompositeBuffer.constToSingleBuffer().size(), (s0 + s1 + s2 + s3).size());
        BOOST_CHECK_EQUAL(newCompositeBuffer.buffers().size(), 4);
        auto singleBuffer4 = newCompositeBuffer.constToSingleBuffer();
        BOOST_CHECK_EQUAL(
            std::string(singleBuffer4.begin(), singleBuffer4.end()), s2 + s1 + s0 + s3);
    }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos
