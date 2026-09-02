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
 * @brief test for WsMessage
 * @file WsMessageTest.cpp
 * @author: octopus
 * @date 2021-07-12
 */

#include <bcos-boostssl/websocket/WsMessage.h>

#include <boost/test/unit_test.hpp>

using namespace bcos;

using namespace bcos::boostssl;
using namespace bcos::boostssl::ws;

BOOST_AUTO_TEST_SUITE(WsMessageTest)

BOOST_AUTO_TEST_CASE(test_WsMessage)
{
    auto msg = std::make_shared<WsMessage>();
    auto buffer = std::make_shared<bytes>();
    auto r = msg->encode(*buffer);
    auto seq = msg->seq();

    BOOST_CHECK(r);
    BOOST_CHECK_EQUAL(buffer->size(), WsMessage::MESSAGE_MIN_LENGTH);

    {
        auto decodeMsg = std::make_shared<WsMessage>();
        auto size = decodeMsg->decode(bytesConstRef(buffer->data(), buffer->size()));
        BOOST_CHECK(size > 0);
        BOOST_CHECK_EQUAL(decodeMsg->payload().size(), 0);
        auto decodeSeq = msg->seq();
        BOOST_CHECK_EQUAL(seq, decodeSeq);
    }
}


BOOST_AUTO_TEST_CASE(test_buildMessage)
{
    {
        int16_t status = 111;
        uint16_t type = 222;
        std::string data = "HelloWorld.";
        auto msg = std::make_shared<WsMessage>();
        msg->setStatus(status);
        msg->setPacketType(type);
        msg->setPayload(bytes(data.begin(), data.end()));

        auto buffer = std::make_shared<bytes>();
        auto r = msg->encode(*buffer);
        auto seq = msg->seq();

        BOOST_CHECK(r);
        BOOST_CHECK_EQUAL(buffer->size(), WsMessage::MESSAGE_MIN_LENGTH + data.length());

        auto decodeMsg = std::make_shared<WsMessage>();
        auto size = decodeMsg->decode(bytesConstRef(buffer->data(), buffer->size()));
        BOOST_CHECK(size > 0);
        BOOST_CHECK_EQUAL(decodeMsg->status(), status);
        BOOST_CHECK_EQUAL(decodeMsg->packetType(), type);
        BOOST_CHECK_EQUAL(decodeMsg->payload().size(), data.size());
        auto decodeSeq = msg->seq();
        BOOST_CHECK_EQUAL(seq, decodeSeq);
        BOOST_CHECK_EQUAL(
            data, std::string(decodeMsg->payload().begin(), decodeMsg->payload().end()));
    }

    {
        int16_t status = 222;
        uint16_t type = 111;
        std::string data = "HelloWorld.";
        auto payload = bytes(data.begin(), data.end());
        auto msg = std::make_shared<WsMessage>();
        msg->setPacketType(type);
        msg->setPayload(std::move(payload));
        msg->setStatus(status);
        msg->setPacketType(type);

        auto buffer = std::make_shared<bytes>();
        auto r = msg->encode(*buffer);
        auto seq = msg->seq();

        BOOST_CHECK(r);
        BOOST_CHECK_EQUAL(buffer->size(), WsMessage::MESSAGE_MIN_LENGTH + data.length());

        auto decodeMsg = std::make_shared<WsMessage>();
        auto size = decodeMsg->decode(bytesConstRef(buffer->data(), buffer->size()));
        BOOST_CHECK(size > 0);
        BOOST_CHECK_EQUAL(decodeMsg->status(), status);
        BOOST_CHECK_EQUAL(decodeMsg->packetType(), type);
        BOOST_CHECK_EQUAL(decodeMsg->payload().size(), data.size());
        auto decodeSeq = msg->seq();
        BOOST_CHECK_EQUAL(seq, decodeSeq);
        BOOST_CHECK_EQUAL(
            data, std::string(decodeMsg->payload().begin(), decodeMsg->payload().end()));
    }
    auto msg = std::make_shared<WsMessage>();
    std::string invalidMessage =
        "GET / HTTP/1.1\r\nHost: 127.0.0.1:20200\r\nUpgrade: websocket\r\nConnection: "
        "upgrade\r\nSec-WebSocket-Key: lkBb9dFFu4tuMNJyXAWIfQ==\r\nSec-WebSocket-Version: "
        "13\r\n\r\n";
    auto invalidMsgBytes = bcos::bytes(invalidMessage.begin(), invalidMessage.end());
    BOOST_CHECK_THROW(msg->decode(ref(invalidMsgBytes)), std::out_of_range);
}

BOOST_AUTO_TEST_CASE(test_newSeq)
{
    auto seq = newSeq();
    BOOST_CHECK_EQUAL(seq.size(), 32);
    for (char c : seq)
    {
        BOOST_CHECK(std::isxdigit(static_cast<unsigned char>(c)));
        BOOST_CHECK(!std::isupper(static_cast<unsigned char>(c)));
    }
    BOOST_CHECK_NE(seq, newSeq());
}

// Golden-byte tests: lock the exact wire format (big-endian field order,
// ext-after-seq) so a symmetric codec change cannot silently pass the suite
// while breaking interop with older nodes / the Java SDK / the console.
// The expected bytes below were verified byte-identical to the pre-PR encoder:
// treat them as the historical interop contract, do not regenerate from encode.
BOOST_AUTO_TEST_CASE(test_encode_golden_header)
{
    WsMessage msg;
    // setVersion is intentionally a no-op, so version stays 0 on the wire
    msg.setVersion(1);
    msg.setPacketType(0x1234);
    msg.setStatus(0x005A);
    msg.setSeq("abcd");
    msg.setExt(0x00FF);
    msg.setPayload(bcos::bytes{0x01, 0x02, 0x03, 0x04});

    bcos::bytes buffer;
    BOOST_CHECK(msg.encode(buffer));

    // version(2) type(2) status(2) seqLen(2) seq(4) ext(2) payload(4)
    const bcos::bytes expected = {
        0x00, 0x00,  // version = 0 (setVersion is a no-op)
        0x12, 0x34,  // packetType = 0x1234
        0x00, 0x5A,  // status = 0x5A
        0x00, 0x04,  // seqLength = 4
        0x61, 0x62, 0x63, 0x64,  // seq = "abcd"
        0x00, 0xFF,  // ext = 0xFF
        0x01, 0x02, 0x03, 0x04   // payload
    };
    BOOST_CHECK(buffer == expected);

    // decode of the golden bytes must restore every field
    WsMessage decoded;
    auto consumed = decoded.decode(bytesConstRef(expected.data(), expected.size()));
    BOOST_CHECK_EQUAL(consumed, static_cast<int64_t>(expected.size()));
    BOOST_CHECK_EQUAL(decoded.version(), 0);
    BOOST_CHECK_EQUAL(decoded.packetType(), 0x1234);
    BOOST_CHECK_EQUAL(decoded.status(), 0x005A);
    BOOST_CHECK_EQUAL(decoded.seq(), "abcd");
    BOOST_CHECK_EQUAL(decoded.ext(), 0x00FF);
    const bcos::bytes decodedPayload(
        decoded.payload().begin(), decoded.payload().end());
    const bcos::bytes expectedPayload{0x01, 0x02, 0x03, 0x04};
    BOOST_CHECK(decodedPayload == expectedPayload);
}

BOOST_AUTO_TEST_CASE(test_encode_golden_raw)
{
    // raw mode carries the payload verbatim with no header
    WsMessage msg(true);
    msg.setPayload(bcos::bytes{0xDE, 0xAD, 0xBE, 0xEF});

    bcos::bytes buffer;
    BOOST_CHECK(msg.encode(buffer));

    const bcos::bytes expected = {0xDE, 0xAD, 0xBE, 0xEF};
    BOOST_CHECK(buffer == expected);

    WsMessage decoded(true);
    auto consumed = decoded.decode(bytesConstRef(expected.data(), expected.size()));
    BOOST_CHECK_EQUAL(consumed, 4);
    const bcos::bytes decodedPayload(decoded.payload().begin(), decoded.payload().end());
    BOOST_CHECK(decodedPayload == expected);
}

// move-only semantics: payload/seq must survive move construction/assignment
// (the bug class the lightnode dangling-payload fix demonstrated)
BOOST_AUTO_TEST_CASE(test_move_roundtrip)
{
    WsMessage src;
    src.setSeq("seq-1234567890");
    src.setPacketType(7);
    src.setPayload(bcos::bytes{1, 2, 3, 4, 5});

    const bcos::bytes payload{1, 2, 3, 4, 5};
    WsMessage dst(std::move(src));
    BOOST_CHECK_EQUAL(dst.seq(), "seq-1234567890");
    BOOST_CHECK_EQUAL(dst.packetType(), 7);
    const bcos::bytes dstPayload(dst.payload().begin(), dst.payload().end());
    BOOST_CHECK(dstPayload == payload);

    WsMessage assignTarget;
    assignTarget = std::move(dst);
    BOOST_CHECK_EQUAL(assignTarget.seq(), "seq-1234567890");
    BOOST_CHECK_EQUAL(assignTarget.packetType(), 7);
    const bcos::bytes assignPayload(assignTarget.payload().begin(), assignTarget.payload().end());
    BOOST_CHECK(assignPayload == payload);

    // the raw() flag must survive move construction/assignment
    WsMessage rawSrc(true);
    rawSrc.setPayload(bcos::bytes{9, 9});
    WsMessage rawDst(std::move(rawSrc));
    BOOST_CHECK(rawDst.raw());
    const bcos::bytes rawPayload(rawDst.payload().begin(), rawDst.payload().end());
    const bcos::bytes rawExpected{9, 9};
    BOOST_CHECK(rawPayload == rawExpected);
}
BOOST_AUTO_TEST_SUITE_END()
