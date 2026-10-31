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
 * @file FIB168_EtcdValueCapTest.cpp
 * @brief FIB-168: WatcherConfig::updateLeaderInfo must cap watched etcd value
 *        size before decode and avoid logging full payloads on failure.
 *        See audit/findings/FIB-168.md.
 * @date 2026-05-08
 */
#include "WatcherConfig.h"
#include <boost/test/unit_test.hpp>
#include <string>

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(FIB168EtcdValueCapTest)

using bcos::election::WatcherConfig;

// FIB-168 anchor #1: any value strictly larger than c_maxEtcdValueSize must be
// rejected by isOversizeEtcdValue() so updateLeaderInfo() can early-return
// before invoking the Tars decoder. Values at or under the cap pass.
BOOST_AUTO_TEST_CASE(rejectOversizePayload)
{
    BOOST_CHECK_EQUAL(WatcherConfig::c_maxEtcdValueSize, std::size_t{64 * 1024});
    BOOST_CHECK(!WatcherConfig::isOversizeEtcdValue(0));
    BOOST_CHECK(!WatcherConfig::isOversizeEtcdValue(1));
    BOOST_CHECK(!WatcherConfig::isOversizeEtcdValue(WatcherConfig::c_maxEtcdValueSize));
    BOOST_CHECK(WatcherConfig::isOversizeEtcdValue(WatcherConfig::c_maxEtcdValueSize + 1));
    BOOST_CHECK(WatcherConfig::isOversizeEtcdValue(2 * WatcherConfig::c_maxEtcdValueSize));
    // 128KB attacker payload — exactly the scenario in CertiK FIB-168.
    BOOST_CHECK(WatcherConfig::isOversizeEtcdValue(128 * 1024));
}

// FIB-168 anchor #2: failure-path log must NOT echo the full payload — only a
// bounded hex prefix. truncateValueForLog() must (a) never exceed
// c_logValuePrefixBytes input bytes (= 2 * that many hex chars), and (b) be a
// length-preserving prefix view (deterministic, no allocation surprises).
BOOST_AUTO_TEST_CASE(truncateValueForLogCapsOutput)
{
    // Empty input → empty output (no leading-byte assumption).
    BOOST_CHECK(WatcherConfig::truncateValueForLog("").empty());

    // Short payload (under prefix cap) is hex-encoded in full.
    auto shortHex = WatcherConfig::truncateValueForLog(std::string_view("\x00\x01\xff", 3));
    BOOST_CHECK_EQUAL(shortHex, "0001ff");

    // Oversize attacker payload — output must be exactly 2 * c_logValuePrefixBytes
    // characters and identical regardless of how much padding follows.
    std::string oversize(128 * 1024, 'X');  // 128KB payload, all 0x58 bytes
    auto truncated = WatcherConfig::truncateValueForLog(oversize);
    BOOST_CHECK_EQUAL(truncated.size(), WatcherConfig::c_logValuePrefixBytes * 2);
    // 'X' = 0x58 — every byte should encode to "58".
    for (std::size_t i = 0; i < truncated.size(); i += 2)
    {
        BOOST_CHECK_EQUAL(truncated[i], '5');
        BOOST_CHECK_EQUAL(truncated[i + 1], '8');
    }
    // Same prefix for any payload sharing the first c_logValuePrefixBytes bytes —
    // i.e. no observable difference between a 64KiB+1 'X' run and a 1MiB 'X' run.
    std::string evenBigger(1024 * 1024, 'X');
    BOOST_CHECK_EQUAL(WatcherConfig::truncateValueForLog(evenBigger), truncated);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
