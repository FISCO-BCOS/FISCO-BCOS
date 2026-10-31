/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

#include "bcos-gateway/GatewayConfig.h"
#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/test/unit_test.hpp>
#include <sstream>

using namespace bcos;
using namespace bcos::gateway;

namespace bcos::test
{
namespace
{
boost::property_tree::ptree fromIni(std::string const& ini)
{
    boost::property_tree::ptree pt;
    std::stringstream ss(ini);
    boost::property_tree::read_ini(ss, pt);
    return pt;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(GatewayConfigSettersTest)

BOOST_AUTO_TEST_CASE(settersAndGetters)
{
    auto config = std::make_shared<GatewayConfig>();

    config->setCertPath("/cert");
    config->setNodePath("/node");
    config->setNodeFileName("nodes.json");
    config->setConfigFile("config.ini");
    config->setUUID("uuid-1");
    config->setEnableRIPProtocol(false);
    config->setEnableCompress(false);
    config->setAllowMaxMsgSize(1024U * 1024U);
    config->setSessionRecvBufferSize(64U * 1024U);
    config->setMaxReadDataSize(32U * 1024U);
    config->setMaxSendDataSize(48U * 1024U);
    config->setMaxSendMsgCount(20U);

    BOOST_CHECK_EQUAL(config->uuid(), "uuid-1");
    BOOST_CHECK(!config->enableRIPProtocol());
    BOOST_CHECK(!config->enableCompress());
    BOOST_CHECK_EQUAL(config->allowMaxMsgSize(), 1024U * 1024U);
    BOOST_CHECK_EQUAL(config->sessionRecvBufferSize(), 64U * 1024U);
    BOOST_CHECK_EQUAL(config->maxReadDataSize(), 32U * 1024U);
    BOOST_CHECK_EQUAL(config->maxSendDataSize(), 48U * 1024U);
    BOOST_CHECK_EQUAL(config->maxMsgCountSendOneTime(), 20U);

    // The setter shares initP2PConfig's clamp: a 0 byte budget would stall every outbound write
    // (the batch loop never pops), so it can never be stored through any path.
    config->setMaxSendDataSize(0U);
    BOOST_CHECK_EQUAL(config->maxSendDataSize(), 1U);

    // read-only getters should not throw on a freshly constructed config
    BOOST_CHECK_NO_THROW(config->listenIP());
    BOOST_CHECK_NO_THROW(config->listenPort());
    // threadPoolSize() went away with p2p.thread_count (superseded by the node-wide
    // thread_pool.io_thread_count).
    BOOST_CHECK_NO_THROW(config->smSSL());
    BOOST_CHECK_NO_THROW(config->sslClientMode());
    BOOST_CHECK_NO_THROW(config->sslServerMode());
    BOOST_CHECK_NO_THROW(config->certConfig());
    BOOST_CHECK_NO_THROW(config->smCertConfig());
    BOOST_CHECK_NO_THROW(config->rateLimiterConfig());
    // redisConfig() went away with the distributed-ratelimit redis backend.
    BOOST_CHECK_NO_THROW(config->connectedNodes());
    BOOST_CHECK_NO_THROW(config->enableBlacklist());
    BOOST_CHECK_NO_THROW(config->peerBlacklist());
    BOOST_CHECK_NO_THROW(config->enableWhitelist());
    BOOST_CHECK_NO_THROW(config->peerWhitelist());
    BOOST_CHECK_NO_THROW(config->readonly());
    BOOST_CHECK_NO_THROW(config->enableSSLVerify());
    BOOST_CHECK_NO_THROW(config->hashImpl());
}

BOOST_AUTO_TEST_CASE(peerBlacklistConfig)
{
    auto config = std::make_shared<GatewayConfig>();
    std::string validNodeID(512, '1');  // 256-byte h2048 hex, non-zero → valid

    // no section → blacklist disabled
    config->initPeerBlacklistConfig(fromIni("[p2p]\nlisten_ip=0.0.0.0\n"));
    BOOST_CHECK(!config->enableBlacklist());

    // valid nodeID → blacklist enabled
    config->initPeerBlacklistConfig(
        fromIni("[certificate_blacklist]\ncrl.0=" + validNodeID + "\n"));
    BOOST_CHECK(config->enableBlacklist());
    BOOST_CHECK_EQUAL(config->peerBlacklist().size(), 1U);

    // invalid nodeID → entry skipped
    config->initPeerBlacklistConfig(fromIni("[certificate_blacklist]\ncrl.0=tooshort\n"));
    BOOST_CHECK(!config->enableBlacklist());
}

BOOST_AUTO_TEST_CASE(peerWhitelistConfig)
{
    auto config = std::make_shared<GatewayConfig>();
    std::string validNodeID(512, '2');

    config->initPeerWhitelistConfig(fromIni("[p2p]\nlisten_ip=0.0.0.0\n"));
    BOOST_CHECK(!config->enableWhitelist());

    config->initPeerWhitelistConfig(
        fromIni("[certificate_whitelist]\ncal.0=" + validNodeID + "\n"));
    BOOST_CHECK(config->enableWhitelist());
    BOOST_CHECK_EQUAL(config->peerWhitelist().size(), 1U);

    config->initPeerWhitelistConfig(fromIni("[certificate_whitelist]\ncal.0=bad\n"));
    BOOST_CHECK(!config->enableWhitelist());
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
