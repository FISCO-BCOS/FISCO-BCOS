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
 * @brief FIB-69 follow-up: front::MAX_PAYLOAD_LENGTH must stay aligned with the gateway
 *        message cap. With the caps misaligned (front 8 MB < gateway 32 MB), messages in
 *        (8 MB, 32 MB] passed P2PMessage::decode() but were silently dropped by
 *        FrontMessage::decode(), so consensus/sync messages of large blocks never reached
 *        their module.
 * @file FIB69_PayloadCapAlignmentTest.cpp
 */

#include <bcos-front/FrontMessage.h>
#include <boost/asio/detail/socket_ops.hpp>
#include <boost/test/unit_test.hpp>

namespace bcos::test
{
namespace
{

/// Build a minimal valid FrontMessage wire buffer:
/// moduleID(2) + uuidLength(1)=0 + ext(2), payload appended by the caller.
inline bcos::bytes encodeEmptyHeader(uint16_t moduleID)
{
    bcos::bytes buf;
    auto netModuleID = boost::asio::detail::socket_ops::host_to_network_short(moduleID);
    uint16_t netExt = 0;
    buf.insert(buf.end(), (bcos::byte*)&netModuleID, (bcos::byte*)&netModuleID + 2);
    buf.push_back(0);  // uuidLength = 0
    buf.insert(buf.end(), (bcos::byte*)&netExt, (bcos::byte*)&netExt + 2);
    return buf;
}

}  // namespace

// Anchor the alignment: bcos-front must not include bcos-gateway headers, so the gateway's
// MAX_MESSAGE_LENGTH (bcos-gateway/bcos-gateway/Common.h) is pinned here by value. The
// mirror assertion pinning the gateway constant lives in
// bcos-gateway/test/unittests/FIB69_MessageLengthAlignmentTest.cpp; together they make a
// unilateral change of either side go red.
static_assert(bcos::front::MAX_PAYLOAD_LENGTH == 32UL * 1024 * 1024,
    "front::MAX_PAYLOAD_LENGTH must equal gateway MAX_MESSAGE_LENGTH (32 MB); see "
    "bcos-gateway/bcos-gateway/Common.h");

BOOST_AUTO_TEST_SUITE(FIB69PayloadCapAlignmentTest)

/// Regression for the dead zone: a payload of 8 MB + 1 (beyond the former front cap, within
/// the gateway cap) must decode successfully and be preserved intact.
BOOST_AUTO_TEST_CASE(payload_in_former_dead_zone_accepted)
{
    constexpr std::size_t formerCap = 8UL * 1024 * 1024;
    auto header = encodeEmptyHeader(1);
    bcos::bytes buf = header;
    buf.resize(header.size() + formerCap + 1, 'P');

    auto msg = std::make_shared<bcos::front::FrontMessage>();
    auto result = msg->decode(bcos::bytesConstRef(buf.data(), buf.size()));
    BOOST_CHECK_EQUAL(result, bcos::front::MessageDecodeStatus::MESSAGE_COMPLETE);
    BOOST_CHECK_EQUAL(msg->payload().size(), formerCap + 1);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test
