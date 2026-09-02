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
 * @file MessagesTest.cpp
 * @brief devp2p base-protocol Hello/Disconnect/Ping codec tests.
 * @date 2026/9/2
 */
#include <bcos-devp2p/rlpx/Messages.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/test/unit_test.hpp>
#include <string>
#include <vector>

using namespace bcos;
using namespace bcos::devp2p::rlpx;

namespace
{
HelloMessage makeHello()
{
    HelloMessage msg;
    msg.version = 5;
    msg.clientId = "FISCO-BCOS";
    msg.capabilities = {{"eth", 68}, {"les", 2}};
    msg.listenPort = 30303;
    msg.id.assign(64, 0x11);
    return msg;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(MessagesTest)

// Hello encode/decode round trip preserves every field.
BOOST_AUTO_TEST_CASE(helloRoundTrip)
{
    auto msg = makeHello();
    // Capability version 255 must round-trip (upper bound of the uint8 range).
    msg.capabilities = {{"eth", 68}, {"snap", 1}, {"edge", 255}};

    auto wire = encodeHello(msg);
    HelloMessage decoded = decodeHello(bytesConstRef(wire.data(), wire.size()));

    BOOST_CHECK_EQUAL(decoded.version, msg.version);
    BOOST_CHECK(decoded.clientId == msg.clientId);
    BOOST_CHECK_EQUAL(decoded.capabilities.size(), msg.capabilities.size());
    for (size_t i = 0; i < msg.capabilities.size(); ++i)
    {
        BOOST_CHECK(decoded.capabilities[i].name == msg.capabilities[i].name);
        BOOST_CHECK_EQUAL(decoded.capabilities[i].version, msg.capabilities[i].version);
    }
    BOOST_CHECK_EQUAL(decoded.listenPort, msg.listenPort);
    BOOST_CHECK(decoded.id == msg.id);
}

// Golden Hello bytes, independently hand-RLP-encoded:
//   [version=5, "FISCO-BCOS", [], listenPort=30303, id = 64 x 0x11]
// pins the exact wire format (empty capability list).
BOOST_AUTO_TEST_CASE(helloGoldenWireFormat)
{
    HelloMessage msg;
    msg.version = 5;
    msg.clientId = "FISCO-BCOS";
    msg.capabilities = {};
    msg.listenPort = 30303;
    msg.id.assign(64, 0x11);

    auto wire = encodeHello(msg);
    BOOST_CHECK_EQUAL(toHex(wire),
        "f852058a464953434f2d42434f53c082765fb840111111111111111111111111"
        "1111111111111111111111111111111111111111111111111111111111111111"
        "1111111111111111111111111111111111111111");

    // And the golden decodes back to the same message.
    HelloMessage decoded = decodeHello(bytesConstRef(wire.data(), wire.size()));
    BOOST_CHECK_EQUAL(decoded.version, 5);
    BOOST_CHECK(decoded.clientId == "FISCO-BCOS");
    BOOST_CHECK(decoded.capabilities.empty());
    BOOST_CHECK_EQUAL(decoded.listenPort, 30303);
    BOOST_CHECK(decoded.id == msg.id);
}

// A capability version > 0xff (256 here) must be rejected, not silently wrapped.
BOOST_AUTO_TEST_CASE(helloRejectsOversizedCapVersion)
{
    // [5, "FISCO-BCOS", [["eth", 256]], 30303, 64 x 0x11]
    auto wire = fromHex(
        "f85a058a464953434f2d42434f53c8c78365746882010082765fb84011111111"
        "1111111111111111111111111111111111111111111111111111111111111111"
        "11111111111111111111111111111111111111111111111111111111");
    BOOST_CHECK_THROW(decodeHello(bytesConstRef(wire.data(), wire.size())), std::runtime_error);
}

// Disconnect is always emitted in the geth list form [reason] and both the list
// form and the legacy bare-integer form are accepted on decode.
BOOST_AUTO_TEST_CASE(disconnectCodecs)
{
    // [0x00] (DisconnectRequested)
    DisconnectMessage msg;
    msg.reason = DisconnectReason::DisconnectRequested;
    auto wire = encodeDisconnect(msg);
    BOOST_CHECK(wire == (bytes{0xc1, 0x80}));

    // [0x02] (ProtocolBreach)
    msg.reason = DisconnectReason::ProtocolBreach;
    wire = encodeDisconnect(msg);
    BOOST_CHECK(wire == (bytes{0xc1, 0x02}));

    // Decode the list form.
    auto listForm = fromHex("c180");
    auto decoded = decodeDisconnect(bytesConstRef(listForm.data(), listForm.size()));
    BOOST_CHECK(decoded.reason == DisconnectReason::DisconnectRequested);

    // Decode the legacy bare-integer form (as sent by some clients).
    auto bareForm = fromHex("02");
    decoded = decodeDisconnect(bytesConstRef(bareForm.data(), bareForm.size()));
    BOOST_CHECK(decoded.reason == DisconnectReason::ProtocolBreach);

    // 0x10 is the spec's subprotocol-specific reason.
    auto subreason = fromHex("10");
    decoded = decodeDisconnect(bytesConstRef(subreason.data(), subreason.size()));
    BOOST_CHECK(decoded.reason == DisconnectReason::SubprotocolReason);
}

// Ping/Pong payloads are the empty list.
BOOST_AUTO_TEST_CASE(pingPongEmptyList)
{
    BOOST_CHECK(encodePing() == (bytes{0xc0}));
    BOOST_CHECK(encodePong() == (bytes{0xc0}));
}

BOOST_AUTO_TEST_SUITE_END()
