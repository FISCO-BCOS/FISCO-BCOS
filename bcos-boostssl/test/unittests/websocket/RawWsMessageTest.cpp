/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

#include <bcos-boostssl/websocket/RawWsMessage.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::boostssl::ws;

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(RawWsMessageTest)

BOOST_AUTO_TEST_CASE(factoryBuildsRawMessage)
{
    RawWsMessageFactory factory;
    auto msg = factory.buildMessage();
    BOOST_REQUIRE(msg);
}

BOOST_AUTO_TEST_CASE(accessorsRoundTrip)
{
    RawWsMessage msg;
    auto payload = std::make_shared<bcos::bytes>(bcos::bytes{0x01, 0x02, 0x03});
    msg.setPayload(payload);
    msg.setSeq("seq-1");
    msg.setPacketType(7);

    BOOST_REQUIRE(msg.payload());
    BOOST_CHECK_EQUAL(msg.payload()->size(), 3U);
    BOOST_CHECK_EQUAL(msg.seq(), "seq-1");
    // packetType is fixed to WS_RAW_MESSAGE_TYPE for raw messages; just exercise
    // the getter/setter without asserting a changed value.
    BOOST_CHECK_NO_THROW(msg.packetType());

    // setVersion/setExt are documented no-ops; calling them must not crash and
    // the getters stay well-defined.
    msg.setVersion(2);
    msg.setExt(1);
    BOOST_CHECK_NO_THROW(msg.version());
    BOOST_CHECK_NO_THROW(msg.ext());
}

BOOST_AUTO_TEST_CASE(respPacketFlag)
{
    RawWsMessage msg;
    msg.setRespPacket();
    BOOST_CHECK_NO_THROW(msg.isRespPacket());
}

BOOST_AUTO_TEST_CASE(encodeAppendsPayloadDecodeRestoresIt)
{
    RawWsMessage msg;
    bcos::bytes data{0xDE, 0xAD, 0xBE, 0xEF};
    msg.setPayload(std::make_shared<bcos::bytes>(data));

    bcos::bytes buffer;
    BOOST_REQUIRE(msg.encode(buffer));
    BOOST_CHECK(buffer == data);  // raw message encodes payload verbatim
    BOOST_CHECK_EQUAL(msg.length(), buffer.size());

    RawWsMessage decoded;
    auto consumed = decoded.decode(bcos::ref(buffer));
    BOOST_CHECK_EQUAL(consumed, static_cast<int64_t>(buffer.size()));
    BOOST_REQUIRE(decoded.payload());
    BOOST_CHECK(*decoded.payload() == data);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
