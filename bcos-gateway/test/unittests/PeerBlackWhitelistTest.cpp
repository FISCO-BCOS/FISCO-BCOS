/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

#include <bcos-gateway/libnetwork/PeerBlacklist.h>
#include <bcos-gateway/libnetwork/PeerWhitelist.h>
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
    PeerWhitelist wl(std::set<std::string>{kNodeA, kNodeB}, /*enable=*/true);
    BOOST_CHECK(wl.enable());
    BOOST_CHECK_EQUAL(wl.size(), 2U);
    BOOST_CHECK(wl.has(kNodeA));
    BOOST_CHECK(wl.has(kNodeB));
    BOOST_CHECK(!wl.has(kNodeC));
    BOOST_CHECK(!wl.dump().empty());
}

BOOST_AUTO_TEST_CASE(whitelistDisabledMatchesAll)
{
    // A disabled whitelist "has" every peer (hasValueWhenDisable() == true).
    PeerWhitelist wl(std::set<std::string>{kNodeA}, /*enable=*/false);
    BOOST_CHECK(!wl.enable());
    BOOST_CHECK(wl.hasValueWhenDisable());
    BOOST_CHECK(wl.has(kNodeC));  // disabled → everyone passes
}

BOOST_AUTO_TEST_CASE(whitelistUpdateAndSetEnable)
{
    PeerWhitelist wl(std::set<std::string>{kNodeA}, true);
    wl.update(std::set<std::string>{kNodeB, kNodeC}, true);
    BOOST_CHECK(!wl.has(kNodeA));
    BOOST_CHECK(wl.has(kNodeB));
    BOOST_CHECK_EQUAL(wl.size(), 2U);

    wl.setEnable(false);
    BOOST_CHECK(!wl.enable());
}

BOOST_AUTO_TEST_CASE(blacklistMembership)
{
    PeerBlacklist bl(std::set<std::string>{kNodeA}, /*enable=*/true);
    BOOST_CHECK(bl.enable());
    BOOST_CHECK(bl.has(kNodeA));
    BOOST_CHECK(!bl.has(kNodeB));
    // A disabled blacklist blocks no one (hasValueWhenDisable() == false).
    BOOST_CHECK(!bl.hasValueWhenDisable());
}

BOOST_AUTO_TEST_CASE(blacklistDisabledMatchesNone)
{
    PeerBlacklist bl(std::set<std::string>{kNodeA}, /*enable=*/false);
    BOOST_CHECK(!bl.has(kNodeA));  // disabled → nobody is blacklisted
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
