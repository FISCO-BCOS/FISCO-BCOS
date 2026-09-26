/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

#include <bcos-gateway/libnetwork/Host.h>
#include <bcos-gateway/libnetwork/PeerBlackWhitelist.h>
#include <boost/test/unit_test.hpp>
#include <set>
#include <string>

using namespace bcos::gateway;

namespace bcos::test
{
namespace
{
const std::string kNodeA(128, 'a');  // 64-byte hex node id
const std::string kNodeB(128, 'b');
const std::string kNodeC(128, 'c');
}  // namespace

BOOST_AUTO_TEST_SUITE(PeerBlackWhitelistTest)

BOOST_AUTO_TEST_CASE(whitelistMembershipAndSize)
{
    PeerBlackWhitelist wl(
        PeerBlackWhitelist::Type::Whitelist, std::set<std::string>{kNodeA, kNodeB}, /*enable=*/true);
    BOOST_CHECK(wl.enable());
    BOOST_CHECK_EQUAL(wl.size(), 2U);
    BOOST_CHECK(wl.has(kNodeA));
    BOOST_CHECK(wl.has(kNodeB));
    BOOST_CHECK(!wl.has(kNodeC));
    BOOST_CHECK(!wl.dump().empty());
}

BOOST_AUTO_TEST_CASE(whitelistDisabledMatchesAll)
{
    // A disabled whitelist "has" every peer.
    PeerBlackWhitelist wl(
        PeerBlackWhitelist::Type::Whitelist, std::set<std::string>{kNodeA}, /*enable=*/false);
    BOOST_CHECK(!wl.enable());
    BOOST_CHECK(wl.has(kNodeC));  // disabled → everyone passes
}

BOOST_AUTO_TEST_CASE(whitelistUpdateAndSetEnable)
{
    PeerBlackWhitelist wl(PeerBlackWhitelist::Type::Whitelist, std::set<std::string>{kNodeA}, true);
    wl.update(std::set<std::string>{kNodeB, kNodeC}, true);
    BOOST_CHECK(!wl.has(kNodeA));
    BOOST_CHECK(wl.has(kNodeB));
    BOOST_CHECK_EQUAL(wl.size(), 2U);

    wl.setEnable(false);
    BOOST_CHECK(!wl.enable());
}

BOOST_AUTO_TEST_CASE(blacklistMembership)
{
    PeerBlackWhitelist bl(
        PeerBlackWhitelist::Type::Blacklist, std::set<std::string>{kNodeA}, /*enable=*/true);
    BOOST_CHECK(bl.enable());
    BOOST_CHECK(bl.has(kNodeA));
    BOOST_CHECK(!bl.has(kNodeB));
}

BOOST_AUTO_TEST_CASE(blacklistDisabledMatchesNone)
{
    // A disabled blacklist blocks no one.
    PeerBlackWhitelist bl(
        PeerBlackWhitelist::Type::Blacklist, std::set<std::string>{kNodeA}, /*enable=*/false);
    BOOST_CHECK(!bl.has(kNodeA));  // disabled → nobody is blacklisted
}

BOOST_AUTO_TEST_CASE(hostDefaultListsMatchRemovedNullptrGuards)
{
    // A freshly built Host's default (disabled, empty) lists must behave like the nullptr guards
    // that used to short-circuit the verify callback: blacklist blocks no one, whitelist passes
    // everyone — a swapped Type default here would only surface as rejected live handshakes.
    Host host(nullptr, nullptr, nullptr);
    BOOST_CHECK(!host.peerBlacklist().has(kNodeA));
    BOOST_CHECK(host.peerWhitelist().has(kNodeA));
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
