/*
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
 * @file HandshakeResponseTest.cpp
 * @author: octopus
 * @date 2021-10-26
 */

#include <bcos-cpp-sdk/multigroup/JsonGroupInfoCodec.h>
#include <bcos-cpp-sdk/ws/Common.h>
#include <bcos-cpp-sdk/ws/HandshakeResponse.h>
#include <bcos-framework/rpc/HandshakeRequest.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/test/tools/old/interface.hpp>
#include <boost/test/unit_test.hpp>
#include <future>

using namespace bcos;
using namespace bcos::cppsdk;
using namespace bcos::cppsdk::service;

using namespace bcos;
using namespace bcos::rpc;
using namespace bcos::test;

BOOST_FIXTURE_TEST_SUITE(HandshakeResponseTest, TestPromptFixture)

BOOST_AUTO_TEST_CASE(test_HandshakeResponse)
{
    auto groupInfoCodec = std::make_shared<bcos::group::JsonGroupInfoCodec>();
    {
        int protocolVersion = 111;
        auto response = std::make_shared<bcos::cppsdk::service::HandshakeResponse>(groupInfoCodec);
        response->setProtocolVersion(protocolVersion);
        BOOST_CHECK_EQUAL(response->protocolVersion(), protocolVersion);
    }

    {
        int protocolVersion = 111;
        auto response = std::make_shared<bcos::cppsdk::service::HandshakeResponse>(groupInfoCodec);
        response->setProtocolVersion(protocolVersion);
        std::string encodedData;
        response->encode(encodedData);

        auto response1 = std::make_shared<bcos::cppsdk::service::HandshakeResponse>(groupInfoCodec);
        auto r = response1->decode(encodedData);

        BOOST_CHECK_EQUAL(r, true);
        BOOST_CHECK_EQUAL(response1->protocolVersion(), protocolVersion);
    }

    {
        auto response = std::make_shared<bcos::cppsdk::service::HandshakeResponse>(groupInfoCodec);
        BOOST_CHECK_EQUAL(response->decode("1adf"), false);
    }
}
BOOST_AUTO_TEST_CASE(test_HandshakeRequest)
{
    auto protocolInfo = std::make_shared<bcos::protocol::ProtocolInfo>(
        bcos::protocol::ProtocolModuleID::RpcService, 1, 10000);
    HandshakeRequest request(protocolInfo);
    auto encodedData = request.encode();

    // decode
    HandshakeRequest decodedRequest;
    decodedRequest.decode(*encodedData);
    BOOST_CHECK_EQUAL(
        request.protocol().protocolModuleID(), decodedRequest.protocol().protocolModuleID());
    BOOST_CHECK_EQUAL(request.protocol().minVersion(), decodedRequest.protocol().minVersion());
    BOOST_CHECK_EQUAL(request.protocol().maxVersion(), decodedRequest.protocol().maxVersion());

    // decode exception
    HandshakeRequest exceptionRequest;
    std::string invalidData = "invalidTest";
    BOOST_CHECK_EQUAL(
        exceptionRequest.decode(bcos::bytes(invalidData.begin(), invalidData.end())), false);
}
BOOST_AUTO_TEST_SUITE_END()