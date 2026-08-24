/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

#include <bcos-boostssl/websocket/Common.h>
#include <bcos-boostssl/websocket/WsMessage.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::boostssl::ws;

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(RawWsMessageTest)

BOOST_AUTO_TEST_CASE(rawModeIsFixedAtConstruction)
{
    WsMessage msg(true);
    BOOST_CHECK(msg.raw());
    BOOST_CHECK_EQUAL(msg.packetType(), WS_RAW_MESSAGE_TYPE);
}

BOOST_AUTO_TEST_CASE(accessorsRoundTrip)
{
    WsMessage msg(true);
    // setPayload takes bcos::bytes by value and payload() returns a non-owning bytesConstRef
    msg.setPayload(bcos::bytes{0x01, 0x02, 0x03});
    msg.setSeq("seq-1");
    msg.setPacketType(7);

    BOOST_CHECK_EQUAL(msg.payload().size(), 3U);
    BOOST_CHECK_EQUAL(msg.seq(), "seq-1");
    // setPacketType is a no-op for raw messages: packetType() stays fixed at
    // WS_RAW_MESSAGE_TYPE regardless of the value passed above.
    BOOST_CHECK_EQUAL(msg.packetType(), WS_RAW_MESSAGE_TYPE);

    // setVersion/setExt are no-ops; the getters are hardcoded to 0.
    msg.setVersion(2);
    msg.setExt(1);
    BOOST_CHECK_EQUAL(msg.version(), 0);
    BOOST_CHECK_EQUAL(msg.ext(), 0);
}

BOOST_AUTO_TEST_CASE(respPacketFlag)
{
    WsMessage msg(true);
    // setRespPacket() is a no-op and isRespPacket() is hardcoded false: raw
    // messages carry no response flag, so the call must never report true.
    msg.setRespPacket();
    BOOST_CHECK(!msg.isRespPacket());
}

BOOST_AUTO_TEST_CASE(encodeAppendsPayloadDecodeRestoresIt)
{
    WsMessage msg(true);
    bcos::bytes data{0xDE, 0xAD, 0xBE, 0xEF};
    msg.setPayload(data);

    bcos::bytes buffer;
    BOOST_REQUIRE(msg.encode(buffer));
    BOOST_CHECK(buffer == data);  // raw message encodes payload verbatim
    BOOST_CHECK_EQUAL(msg.payload().size(), buffer.size());

    WsMessage decoded(true);
    auto consumed = decoded.decode(bcos::ref(buffer));
    BOOST_CHECK_EQUAL(consumed, static_cast<int64_t>(buffer.size()));
    BOOST_CHECK(decoded.payload().toBytes() == data);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
