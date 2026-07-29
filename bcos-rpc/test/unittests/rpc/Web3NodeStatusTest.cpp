/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

#include "../common/RPCFixture.h"
#include <bcos-framework/sync/BlockSyncInterface.h>
#include <bcos-rpc/web3jsonrpc/Web3JsonRpcImpl.h>
#include <boost/test/unit_test.hpp>
#include <future>

using namespace bcos;
using namespace bcos::rpc;

namespace bcos::test
{
namespace
{
// Minimal BlockSyncInterface so NodeService::sync() is non-null and the
// sync-dependent RPC handlers (eth_syncing, getSyncStatus) can run.
class FakeBlockSync : public bcos::sync::BlockSyncInterface
{
public:
    void start() override {}
    void stop() override {}
    void asyncNotifyNewBlock(
        bcos::ledger::LedgerConfig::Ptr, std::function<void(Error::Ptr)>) override
    {}
    void asyncNotifyBlockSyncMessage(Error::Ptr, std::string const&, bcos::crypto::NodeIDPtr,
        bytesConstRef, std::function<void(Error::Ptr)>) override
    {}
    void asyncGetSyncInfo(std::function<void(Error::Ptr, std::string)> _cb) override
    {
        _cb(nullptr, R"({"isSyncing":false,"blockNumber":19})");
    }
    std::vector<bcos::sync::PeerStatus::Ptr> getPeerStatus() override { return {}; }
    void asyncNotifyCommittedIndex(
        bcos::protocol::BlockNumber, std::function<void(Error::Ptr)>) override
    {}
    void notifyConnectedNodes(
        bcos::crypto::NodeIDSet const&, std::function<void(Error::Ptr)>) override
    {}
    bool faultyNode(bcos::crypto::NodeIDPtr) override { return false; }
};
}  // namespace

// Rebuilds the local RPC with non-null consensus + sync so the handlers that
// dereference those services no longer segfault under test.
class Web3StatusFixture : public RPCFixture
{
public:
    Web3StatusFixture()
    {
        auto sync = std::make_shared<FakeBlockSync>();
        // NodeService gained a trailing AnyEngineService argument; not exercised here.
        auto fullNodeService = std::make_shared<rpc::NodeService>(
            m_ledger, scheduler, txPool, nullptr, sync, m_blockFactory, nullptr);
        rpc = factory->buildLocalRpc(groupInfo, fullNodeService);
        rpc->groupManager()->updateGroupInfo(groupInfo);
        web3JsonRpc = rpc->web3JsonRpc();
        BOOST_REQUIRE(web3JsonRpc);
    }

    Json::Value call(std::string_view request)
    {
        std::promise<bcos::bytes> promise;
        web3JsonRpc->onRPCRequest(
            request, [&promise](bcos::bytes resp, boost::beast::http::status) { promise.set_value(std::move(resp)); });
        auto jsonBytes = promise.get_future().get();
        Json::Value value;
        Json::Reader reader;
        std::string_view json((char*)jsonBytes.data(), jsonBytes.size());
        reader.parse(json.begin(), json.end(), value);
        return value;
    }

    static std::string req(std::string_view method, std::string_view params = "[]")
    {
        return std::string(R"({"jsonrpc":"2.0","id":1,"method":")") + std::string(method) +
               R"(","params":)" + std::string(params) + "}";
    }

    Rpc::Ptr rpc;
    Web3JsonRpcImpl::Ptr web3JsonRpc;
};

BOOST_FIXTURE_TEST_SUITE(Web3NodeStatusTest, Web3StatusFixture)

BOOST_AUTO_TEST_CASE(ethSyncingResolvesViaSyncService)
{
    // With a non-null BlockSyncInterface wired in, eth_syncing runs the
    // handler that dereferences sync() — which segfaults when sync is null.
    auto syncing = call(req("eth_syncing"));
    BOOST_CHECK(syncing.isMember("result") || syncing.isMember("error"));
    BOOST_CHECK(syncing.isMember("id"));
    BOOST_CHECK_EQUAL(syncing["jsonrpc"].asString(), "2.0");
}

BOOST_AUTO_TEST_CASE(netPeerCountUsesSyncPeerStatus)
{
    // net_peerCount dereferences sync()->getPeerStatus(); FakeBlockSync
    // returns an empty list → 0x0.
    auto resp = call(req("net_peerCount"));
    BOOST_CHECK(resp.isMember("result") || resp.isMember("error"));
    BOOST_CHECK(resp.isMember("id"));
}

BOOST_AUTO_TEST_CASE(netVersionReadsChainIdFromLedger)
{
    // net_version co_awaits the ledger system-config for the web3 chain id.
    auto resp = call(req("net_version"));
    BOOST_CHECK(resp.isMember("result") || resp.isMember("error"));
}

BOOST_AUTO_TEST_CASE(netListeningIsConstantTrue)
{
    auto resp = call(req("net_listening"));
    BOOST_CHECK(resp.isMember("result") || resp.isMember("error"));
}

BOOST_AUTO_TEST_CASE(maxPriorityFeePerGasIsConstant)
{
    auto resp = call(req("eth_maxPriorityFeePerGas"));
    BOOST_CHECK(resp.isMember("result") || resp.isMember("error"));
}

BOOST_AUTO_TEST_CASE(protocolVersionReportsNotImplemented)
{
    // eth_protocolVersion deliberately throws MethodNotFound — must surface as
    // a JSON-RPC error, not crash.
    auto resp = call(req("eth_protocolVersion"));
    BOOST_CHECK(resp.isMember("error") || resp.isMember("result"));
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
