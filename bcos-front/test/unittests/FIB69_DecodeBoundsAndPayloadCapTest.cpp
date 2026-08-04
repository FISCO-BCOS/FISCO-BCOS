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
 * @brief FIB-69: FrontMessage::decode must enforce bounds before reading uuid/ext
 *        and cap payload at MAX_PAYLOAD_LENGTH.
 * @file FIB69_DecodeBoundsAndPayloadCapTest.cpp
 */

#include <bcos-front/FrontMessage.h>
#include <boost/asio/detail/socket_ops.hpp>
#include <boost/test/unit_test.hpp>

namespace bcos::test {
namespace {

/// Build a minimal valid FrontMessage wire buffer.
/// moduleID(2) + uuidLength(1) + uuid(actualUuidSize bytes) + ext(2) + [no payload]
inline bcos::bytes encodeHeaderFib69(
    uint16_t moduleID, uint8_t uuidLength, uint16_t ext, std::size_t actualUuidSize)
{
    bcos::bytes buf;
    auto netModuleID = boost::asio::detail::socket_ops::host_to_network_short(moduleID);
    auto netExt = boost::asio::detail::socket_ops::host_to_network_short(ext);
    buf.insert(buf.end(), (bcos::byte*)&netModuleID, (bcos::byte*)&netModuleID + 2);
    buf.push_back(static_cast<bcos::byte>(uuidLength));
    buf.insert(buf.end(), actualUuidSize, 'U');
    buf.insert(buf.end(), (bcos::byte*)&netExt, (bcos::byte*)&netExt + 2);
    return buf;
}

}  // namespace

BOOST_AUTO_TEST_SUITE(FIB69DecodeBoundsAndPayloadCapTest)

/// Scenario 1: buffer shorter than HEADER_MIN_LENGTH must be rejected.
BOOST_AUTO_TEST_CASE(short_buffer_below_header_min_rejected)
{
    auto msg = std::make_shared<bcos::front::FrontMessage>();
    bcos::bytes tooShort(3, 0);
    auto result = msg->decode(bcos::bytesConstRef(tooShort.data(), tooShort.size()));
    BOOST_CHECK_LT(result, 0);
}

/// Scenario 2: uuidLength field claims more bytes than remain in the buffer — reject before OOB.
BOOST_AUTO_TEST_CASE(uuid_length_exceeds_buffer_rejected)
{
    auto msg = std::make_shared<bcos::front::FrontMessage>();
    // moduleID(2)=0x0001, uuidLength=200, but only 5 bytes total: uuid region overflows.
    bcos::bytes buf = {0x00, 0x01, 200, 0x00, 0x00};
    auto result = msg->decode(bcos::bytesConstRef(buf.data(), buf.size()));
    BOOST_CHECK_LT(result, 0);
}

/// Scenario 3: uuidLength is valid but buffer is 1 byte too short to hold the ext field — reject.
BOOST_AUTO_TEST_CASE(ext_field_oob_rejected)
{
    // uuidLength=10 → need 2+1+10+2 = 15 bytes; we supply 14.
    bcos::bytes buf(14, 'U');
    buf[0] = 0x00;
    buf[1] = 0x01;
    buf[2] = 10;  // uuidLength = 10
    auto msg = std::make_shared<bcos::front::FrontMessage>();
    auto result = msg->decode(bcos::bytesConstRef(buf.data(), buf.size()));
    BOOST_CHECK_LT(result, 0);
}

/// Scenario 4: payload exceeds MAX_PAYLOAD_LENGTH — must be rejected.
BOOST_AUTO_TEST_CASE(payload_above_cap_rejected)
{
    auto header = encodeHeaderFib69(1, 0, 0, 0);
    bcos::bytes oversize = header;
    oversize.resize(header.size() + bcos::front::MAX_PAYLOAD_LENGTH + 1, 'P');
    auto msg = std::make_shared<bcos::front::FrontMessage>();
    auto result = msg->decode(bcos::bytesConstRef(oversize.data(), oversize.size()));
    BOOST_CHECK_LT(result, 0);
}

/// Scenario 5: payload exactly at cap boundary — must be accepted and payload size preserved.
BOOST_AUTO_TEST_CASE(payload_at_cap_boundary_accepted)
{
    auto header = encodeHeaderFib69(1, 0, 0, 0);
    bcos::bytes near = header;
    near.resize(header.size() + bcos::front::MAX_PAYLOAD_LENGTH, 'P');
    auto msg = std::make_shared<bcos::front::FrontMessage>();
    auto result = msg->decode(bcos::bytesConstRef(near.data(), near.size()));
    BOOST_CHECK_GE(result, 0);
    BOOST_CHECK_EQUAL(msg->payload().size(), bcos::front::MAX_PAYLOAD_LENGTH);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test
