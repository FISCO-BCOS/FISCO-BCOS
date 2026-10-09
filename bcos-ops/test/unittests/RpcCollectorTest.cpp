/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

#include "fixtures/FakeRpc.h"
#include <bcos-ops/collect/RpcCollector.h>
#include <boost/test/unit_test.hpp>

namespace bcos::ops::test
{
BOOST_AUTO_TEST_SUITE(RpcCollectorTest)

BOOST_AUTO_TEST_CASE(mapsEveryField)
{
    FakeRpc rpc;
    auto status = collectFromRpc(rpc.call(), "group0", "rpc", LocalFallbacks{15000, 3000});
    BOOST_CHECK_EQUAL(status.source, "rpc");
    BOOST_REQUIRE(status.nodeId);
    BOOST_CHECK_EQUAL(*status.nodeId, "3a1f00");
    BOOST_CHECK(status.isConsensusNode && *status.isConsensusNode);
    BOOST_CHECK_EQUAL(*status.view, 0);
    BOOST_CHECK_EQUAL(*status.leaderIndex, 2);
    BOOST_CHECK_EQUAL(*status.changeCycle, 0);
    BOOST_CHECK(status.inTimeout && !*status.inTimeout);
    BOOST_CHECK_EQUAL(*status.consensusNodesNum, 4);
    BOOST_CHECK_EQUAL(*status.connectedGroupNodes, 4);
    BOOST_CHECK_EQUAL(*status.minRequiredQuorum, 3);
    BOOST_CHECK_EQUAL(*status.version, "3.18.0");
    BOOST_CHECK_EQUAL(*status.chainId, "chain0");
    BOOST_CHECK_EQUAL(*status.groupId, "group0");
    BOOST_CHECK(status.smCrypto && !*status.smCrypto);
    BOOST_CHECK(status.authCheck && !*status.authCheck);
    BOOST_CHECK_EQUAL(*status.blockNumber, 128);
    BOOST_CHECK_EQUAL(*status.latestHash, "0x7b00");
    BOOST_CHECK_EQUAL(*status.latestTimestamp, 1760000000000);
    BOOST_CHECK(status.isSyncing && !*status.isSyncing);
    BOOST_CHECK_EQUAL(*status.knownHighestNumber, 128);
    BOOST_CHECK_EQUAL(*status.lag, 0);
    BOOST_CHECK_EQUAL(*status.peerCount, 3);
    BOOST_CHECK_EQUAL(*status.pendingTxSize, 0);
    BOOST_CHECK_EQUAL(*status.consensusTimeoutMs, 3000);
    BOOST_CHECK_EQUAL(*status.txpoolLimit, 15000);
    BOOST_CHECK(status.reasons.empty());
    // 3.18+ nodes report the period themselves; it wins over the node-dir fallback
    rpc.setConsensus("consensusTimeout", 5000);
    auto fresh = collectFromRpc(rpc.call(), "group0", "rpc", LocalFallbacks{15000, 3000});
    BOOST_CHECK_EQUAL(*fresh.consensusTimeoutMs, 5000);
    // remote mode without either: null + reason
    FakeRpc old;
    auto remote = collectFromRpc(old.call(), "group0", "rpc");
    BOOST_CHECK(!remote.consensusTimeoutMs);
    BOOST_CHECK(remote.reasons.contains("consensusTimeoutMs"));
    BOOST_CHECK(status.collectedAt > 0);
}

BOOST_AUTO_TEST_CASE(stringWrappedResultsAreFlattened)
{
    FakeRpc rpc;
    rpc.setConsensus("timeout", true);
    rpc.setConsensus("view", 3);
    rpc.setSync("isSyncing", true);
    rpc.setSync("knownHighestNumber", 150);
    auto status = collectFromRpc(rpc.call(), "group0", "rpc");
    BOOST_CHECK(*status.inTimeout);
    BOOST_CHECK_EQUAL(*status.view, 3);
    BOOST_CHECK(*status.isSyncing);
    BOOST_CHECK_EQUAL(*status.lag, 22);  // 150 - 128 (local height from getBlockNumber)
}

BOOST_AUTO_TEST_CASE(failedRpcLeavesNullWithReason)
{
    FakeRpc rpc;
    rpc.remove("getSyncStatus");
    rpc.remove("getPendingTxSize");
    auto status = collectFromRpc(rpc.call(), "group0", "rpc");
    BOOST_CHECK(!status.isSyncing);
    BOOST_CHECK(!status.lag);
    BOOST_CHECK(!status.pendingTxSize);
    BOOST_CHECK(status.reasons.at("lag").find("getSyncStatus") != std::string::npos);
    BOOST_CHECK(status.reasons.at("pendingTxSize").find("getPendingTxSize") != std::string::npos);
    BOOST_CHECK(status.reasons.contains("txpoolLimit"));  // no node dir in remote mode
    // the other fields are still filled
    BOOST_CHECK_EQUAL(*status.blockNumber, 128);
    auto json = status.toJson();
    BOOST_CHECK(json["sync"]["lag"].isNull());
    BOOST_CHECK_EQUAL(json["chain"]["blockNumber"].asInt64(), 128);
    BOOST_CHECK_EQUAL(json["source"].asString(), "rpc");
}

BOOST_AUTO_TEST_CASE(groupInfoIniConfigIsParsed)
{
    FakeRpc rpc;
    Json::Value groupInfo;
    groupInfo["chainID"] = "chainX";
    Json::Value node;
    node["nodeID"] = "3a1f00";
    node["iniConfig"] =
        R"({"binaryInfo":{"version":"3.17.1"},"isAuthCheck":true,"smCryptoType":true})";
    groupInfo["nodeList"] = Json::Value(Json::arrayValue);
    groupInfo["nodeList"].append(node);
    rpc.set("getGroupInfo", groupInfo);
    auto status = collectFromRpc(rpc.call(), "group0", "rpc");
    BOOST_CHECK_EQUAL(*status.version, "3.17.1");
    BOOST_CHECK(*status.authCheck);
    BOOST_CHECK(*status.smCrypto);
    BOOST_CHECK_EQUAL(*status.chainId, "chainX");
}

BOOST_AUTO_TEST_CASE(discoverGroupTakesFirst)
{
    FakeRpc rpc;
    BOOST_CHECK_EQUAL(discoverGroup(rpc.call()), "group0");
    Json::Value empty;
    empty["groupList"] = Json::Value(Json::arrayValue);
    rpc.set("getGroupList", empty);
    BOOST_CHECK_THROW(discoverGroup(rpc.call()), OpsError);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::ops::test
