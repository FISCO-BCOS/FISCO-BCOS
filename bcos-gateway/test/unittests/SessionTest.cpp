/**
 *  Copyright (C) 2023 FISCO BCOS.
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
 * @brief test for gateway
 * @file SessionTest.cpp
 * @author: octopus
 * @date 2023-02-23
 */

#include <bcos-gateway/libnetwork/Session.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/test/tools/old/interface.hpp>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace gateway;
using namespace bcos::test;

BOOST_FIXTURE_TEST_SUITE(SessionTest, TestPromptFixture)


BOOST_AUTO_TEST_CASE(SessionRecvBufferTest)
{
    {
        // 0/r/w                               1024
        // |___________|__________|____________|
        //
        std::size_t recvBufferSize = 1024;
        SessionRecvBuffer recvBuffer(recvBufferSize);

        BOOST_CHECK_EQUAL(recvBuffer.recvBufferSize(), recvBufferSize);

        BOOST_CHECK_EQUAL(recvBuffer.readPos(), 0);
        BOOST_CHECK_EQUAL(recvBuffer.writePos(), 0);
        BOOST_CHECK_EQUAL(recvBuffer.dataSize(), 0);
        BOOST_CHECK_EQUAL(recvBuffer.asReadBuffer().size(), 0);
        BOOST_CHECK_EQUAL(recvBuffer.asWriteBuffer().size(), recvBufferSize);

        // move header operation
        recvBuffer.moveToHeader();
        BOOST_CHECK_EQUAL(recvBuffer.recvBufferSize(), recvBufferSize);

        BOOST_CHECK_EQUAL(recvBuffer.readPos(), 0);
        BOOST_CHECK_EQUAL(recvBuffer.writePos(), 0);
        BOOST_CHECK_EQUAL(recvBuffer.dataSize(), 0);
        BOOST_CHECK_EQUAL(recvBuffer.asReadBuffer().size(), 0);
        BOOST_CHECK_EQUAL(recvBuffer.asWriteBuffer().size(), recvBufferSize);

        // 0/r                   w            1024
        // |_____________________|____________|
        //
        // write 1000Byte to buffer
        int writeDataSize1 = 1000;
        auto result = recvBuffer.onWrite(writeDataSize1);
        // success
        BOOST_CHECK_EQUAL(result, true);
        BOOST_CHECK_EQUAL(recvBuffer.readPos(), 0);
        BOOST_CHECK_EQUAL(recvBuffer.writePos(), writeDataSize1);
        BOOST_CHECK_EQUAL(recvBuffer.dataSize(), writeDataSize1);
        BOOST_CHECK_EQUAL(recvBuffer.asReadBuffer().size(), writeDataSize1);
        BOOST_CHECK_EQUAL(recvBuffer.asWriteBuffer().size(), recvBufferSize - writeDataSize1);

        // write 1000B to buffer again
        int writeDataSize2 = 1000;
        result = recvBuffer.onWrite(writeDataSize2);
        // failure
        BOOST_CHECK_EQUAL(result, false);
        BOOST_CHECK_EQUAL(recvBuffer.readPos(), 0);
        BOOST_CHECK_EQUAL(recvBuffer.writePos(), writeDataSize1);
        BOOST_CHECK_EQUAL(recvBuffer.dataSize(), writeDataSize1);
        BOOST_CHECK_EQUAL(recvBuffer.asReadBuffer().size(), writeDataSize1);
        BOOST_CHECK_EQUAL(recvBuffer.asWriteBuffer().size(), recvBufferSize - writeDataSize1);

        // 0/r                               w/1024
        // |_________________________________|
        //
        // write 24B to buffer again
        int writeDataSize3 = 24;
        result = recvBuffer.onWrite(writeDataSize3);
        // ok
        BOOST_CHECK_EQUAL(result, true);
        BOOST_CHECK_EQUAL(recvBuffer.readPos(), 0);
        BOOST_CHECK_EQUAL(recvBuffer.writePos(), writeDataSize1 + writeDataSize3);
        BOOST_CHECK_EQUAL(recvBuffer.dataSize(), writeDataSize1 + writeDataSize3);
        BOOST_CHECK_EQUAL(recvBuffer.asReadBuffer().size(), writeDataSize1 + writeDataSize3);
        BOOST_CHECK_EQUAL(
            recvBuffer.asWriteBuffer().size(), recvBufferSize - writeDataSize1 - writeDataSize3);


        // 0/r    w/1024                      3072
        // |______|___________________________|
        //
        recvBufferSize *= 3;
        //  resize the buffer
        recvBuffer.resizeBuffer(recvBufferSize);
        BOOST_CHECK_EQUAL(recvBuffer.readPos(), 0);
        BOOST_CHECK_EQUAL(recvBuffer.writePos(), writeDataSize1 + writeDataSize3);
        BOOST_CHECK_EQUAL(recvBuffer.dataSize(), writeDataSize1 + writeDataSize3);
        BOOST_CHECK_EQUAL(recvBuffer.asReadBuffer().size(), writeDataSize1 + writeDataSize3);
        BOOST_CHECK_EQUAL(
            recvBuffer.asWriteBuffer().size(), recvBufferSize - writeDataSize1 - writeDataSize3);


        // 0                    r w            3072
        // |____________________|_|____________|
        //
        // read 999B from buffer
        int readDataSize1 = 999;
        result = recvBuffer.onRead(readDataSize1);
        // success
        BOOST_CHECK_EQUAL(result, true);
        BOOST_CHECK_EQUAL(recvBuffer.readPos(), readDataSize1);
        BOOST_CHECK_EQUAL(recvBuffer.writePos(), writeDataSize1 + writeDataSize3);
        BOOST_CHECK_EQUAL(recvBuffer.dataSize(), writeDataSize1 + writeDataSize3 - readDataSize1);
        BOOST_CHECK_EQUAL(
            recvBuffer.asReadBuffer().size(), writeDataSize1 + writeDataSize3 - readDataSize1);
        BOOST_CHECK_EQUAL(
            recvBuffer.asWriteBuffer().size(), recvBufferSize - writeDataSize1 - writeDataSize3);

        recvBuffer.moveToHeader();
        // move data to header
        // r w                             3072
        // |_|______________________________|

        BOOST_CHECK_EQUAL(recvBuffer.readPos(), 0);
        BOOST_CHECK_EQUAL(recvBuffer.writePos(), writeDataSize1 + writeDataSize3 - readDataSize1);
        BOOST_CHECK_EQUAL(recvBuffer.dataSize(), writeDataSize1 + writeDataSize3 - readDataSize1);
        BOOST_CHECK_EQUAL(
            recvBuffer.asReadBuffer().size(), writeDataSize1 + writeDataSize3 - readDataSize1);
        BOOST_CHECK_EQUAL(recvBuffer.asWriteBuffer().size(),
            recvBufferSize - (writeDataSize1 + writeDataSize3 - readDataSize1));

        // read 3072B from buffer
        int readDataSize2 = 3072;
        result = recvBuffer.onRead(readDataSize2);
        // failure
        BOOST_CHECK_EQUAL(result, false);
        BOOST_CHECK_EQUAL(recvBuffer.readPos(), 0);
        BOOST_CHECK_EQUAL(recvBuffer.writePos(), writeDataSize1 + writeDataSize3 - readDataSize1);
        BOOST_CHECK_EQUAL(recvBuffer.dataSize(), writeDataSize1 + writeDataSize3 - readDataSize1);
        BOOST_CHECK_EQUAL(
            recvBuffer.asReadBuffer().size(), writeDataSize1 + writeDataSize3 - readDataSize1);
        BOOST_CHECK_EQUAL(recvBuffer.asWriteBuffer().size(),
            recvBufferSize - (writeDataSize1 + writeDataSize3 - readDataSize1));
    }
}

BOOST_AUTO_TEST_SUITE_END()
