/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */
// @file NodeConfigPeerServingTest.cpp
// @brief [ethereum] peer serving keys (serve_peers / listen_port / max_peers): defaults,
//        boundaries and the rejection messages.

#include "ExceptionCheck.h"
#include <bcos-crypto/signature/key/KeyFactoryImpl.h>
#include <bcos-tool/NodeConfig.h>
#include <boost/test/unit_test.hpp>
#include <string>

using namespace bcos;
using namespace bcos::tool;

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(NodeConfigPeerServingTest)

BOOST_AUTO_TEST_CASE(defaultsAndBounds)
{
    auto keyFactory = std::make_shared<bcos::crypto::KeyFactoryImpl>();
    {
        NodeConfig cfg(keyFactory);
        BOOST_REQUIRE_NO_THROW(cfg.loadConfigFromString("[ethereum]\n"));
        BOOST_CHECK(cfg.ethereumServePeers());
        BOOST_CHECK_EQUAL(cfg.ethereumListenPort(), 30303);
        BOOST_CHECK_EQUAL(cfg.ethereumMaxPeers(), 50u);
    }
    {
        NodeConfig cfg(keyFactory);
        BOOST_REQUIRE_NO_THROW(cfg.loadConfigFromString(
            "[ethereum]\nserve_peers=false\nlisten_port=65535\nmax_peers=1000\n"));
        BOOST_CHECK(!cfg.ethereumServePeers());
        BOOST_CHECK_EQUAL(cfg.ethereumListenPort(), 65535);
        BOOST_CHECK_EQUAL(cfg.ethereumMaxPeers(), 1000u);
    }
    {
        NodeConfig cfg(keyFactory);
        BOOST_REQUIRE_NO_THROW(
            cfg.loadConfigFromString("[ethereum]\nlisten_port=1\nmax_peers=1\n"));
        BOOST_CHECK_EQUAL(cfg.ethereumListenPort(), 1);
        BOOST_CHECK_EQUAL(cfg.ethereumMaxPeers(), 1u);
    }
    for (std::string const port : {"0", "65536"})
    {
        NodeConfig cfg(keyFactory);
        BOOST_CHECK_EXCEPTION(cfg.loadConfigFromString("[ethereum]\nlisten_port=" + port + "\n"),
            InvalidConfig,
            [](auto const& e) { return errinfoContains(e, "listen_port must be in [1, 65535]"); });
    }
    for (std::string const peers : {"0", "1001"})
    {
        NodeConfig cfg(keyFactory);
        BOOST_CHECK_EXCEPTION(cfg.loadConfigFromString("[ethereum]\nmax_peers=" + peers + "\n"),
            InvalidConfig,
            [](auto const& e) { return errinfoContains(e, "max_peers must be in [1, 1000]"); });
    }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
