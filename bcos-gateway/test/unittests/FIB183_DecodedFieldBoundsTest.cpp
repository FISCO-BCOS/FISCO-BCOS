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
 * @brief Regression tests for FIB-183: bounds-check under-validated P2P decoded fields
 * @file FIB183_DecodedFieldBoundsTest.cpp
 * @date 2026-06-15
 */

#include "bcos-gateway/Gateway.h"
#include "bcos-gateway/libp2p/P2PMessage.h"
#include "bcos-gateway/libp2p/P2PMessageV2.h"
#include "bcos-gateway/libp2p/ServiceV2.h"
#include "bcos-gateway/libp2p/router/RouterTableImpl.h"
#include "bcos-utilities/testutils/TestPromptFixture.h"

#include <boost/test/unit_test.hpp>
using namespace bcos;
using namespace bcos::gateway;
using namespace bcos::test;

BOOST_FIXTURE_TEST_SUITE(FIB183_DecodedFieldBoundsTest, TestPromptFixture)

// Exposes the protected network-entry handler so we can drive it directly.
// The default-constructed Gateway leaves m_gatewayNodeManager null; the FIB-183
// guard must return before any member is dereferenced.
class FakeGatewayFIB183 : public Gateway
{
public:
    FakeGatewayFIB183() : Gateway() {}
    ~FakeGatewayFIB183() override {}
    void start() override {}
    void stop() override {}

    void callOnReceiveP2PMessage(
        NetworkException const& _e, P2PSession::Ptr _session, std::shared_ptr<P2PMessage> _msg)
    {
        onReceiveP2PMessage(_e, _session, _msg);
    }
};

// Exposes the protected router-seq handler.
class FakeServiceV2FIB183 : public ServiceV2
{
public:
    // ServiceV2 borrows an external io_context now; the test owns it and passes it in.
    FakeServiceV2FIB183(
        P2PInfo const& _info, RouterTableFactory::Ptr _factory, boost::asio::io_context& _ioContext)
      : ServiceV2(_info, std::move(_factory), _ioContext)
    {}
    void callOnReceiveRouterSeq(
        NetworkException _error, std::shared_ptr<P2PSession> _session, P2PMessage::Ptr _message)
    {
        onReceiveRouterSeq(std::move(_error), std::move(_session), std::move(_message));
    }
};

// Fix A: a P2PMessage whose decoded options carry zero dstNodeIDs must not be
// indexed (dstNodeIDs[0] on an empty vector is OOB). The guard drops it before
// touching m_gatewayNodeManager (which is null here), so no crash occurs.
BOOST_AUTO_TEST_CASE(EmptyDstNodeIDsIsDropped)
{
    // Build a real P2PMessage with a non-zero moduleID (so the front-message
    // moduleID-decode branch is skipped) and an empty dstNodeIDs list.
    auto factory = std::make_shared<P2PMessageFactory>();
    auto msg = std::static_pointer_cast<P2PMessage>(factory->buildMessage());
    msg->setPacketType(GatewayMessageType::PeerToPeerMessage);

    P2PMessageOptions options;
    options.setGroupID("group0");
    options.setModuleID(42);
    std::string srcNodeID = "srcNode";
    options.setSrcNodeID(bytes(srcNodeID.begin(), srcNodeID.end()));
    // Intentionally leave dstNodeIDs empty.
    msg->setOptions(options);
    BOOST_CHECK(msg->options().dstNodeIDs().empty());

    auto gateway = std::make_shared<FakeGatewayFIB183>();
    // Must not crash / must not dereference the null GatewayNodeManager.
    BOOST_CHECK_NO_THROW(gateway->callOnReceiveP2PMessage(NetworkException(0, ""), nullptr, msg));
}

// Sanity check: confirm that a message decoded straight off the wire can legitimately
// have zero dstNodeIDs, i.e. the OOB precondition is reachable from untrusted input.
BOOST_AUTO_TEST_CASE(DecodedOptionsCanHaveEmptyDstNodeIDs)
{
    P2PMessageOptions options;
    options.setGroupID("group0");
    options.setModuleID(7);
    std::string srcNodeID = "srcNode";
    options.setSrcNodeID(bytes(srcNodeID.begin(), srcNodeID.end()));
    // no dstNodeIDs pushed

    bytes buffer;
    BOOST_CHECK(options.encode(buffer));

    P2PMessageOptions decoded;
    auto ret = decoded.decode(bytesConstRef(buffer.data(), buffer.size()));
    BOOST_CHECK(ret > 0);
    BOOST_CHECK(decoded.dstNodeIDs().empty());
}

// Fix B: onReceiveRouterSeq reads a 4-byte sequence from payload. A 0..3 byte payload
// must early-return before dereferencing past the payload buffer (and before touching
// the session, so a null session here is safe).
BOOST_AUTO_TEST_CASE(ShortRouterSeqPayloadIsDropped)
{
    P2PInfo selfInfo;
    selfInfo.rawP2pID = "selfRawP2pID";
    selfInfo.p2pID = "selfP2pID";
    auto routerTableFactory = std::make_shared<RouterTableFactoryImpl>();
    boost::asio::io_context ioContext;
    auto service = std::make_shared<FakeServiceV2FIB183>(selfInfo, routerTableFactory, ioContext);
    auto factory = std::make_shared<P2PMessageFactoryV2>();

    for (size_t len = 0; len < sizeof(uint32_t); ++len)
    {
        auto msg = std::static_pointer_cast<P2PMessage>(factory->buildMessage());
        msg->setPacketType(GatewayMessageType::RouterTableSyncSeq);
        if (len > 0)
        {
            msg->setPayload(bytes(len, 0xAB));
        }
        BOOST_CHECK_EQUAL(msg->payload().size(), len);
        // Session is nullptr on purpose: the guard returns before it is used.
        BOOST_CHECK_NO_THROW(
            service->callOnReceiveRouterSeq(NetworkException(0, ""), nullptr, msg));
    }

    service->stop();
}

BOOST_AUTO_TEST_SUITE_END()
