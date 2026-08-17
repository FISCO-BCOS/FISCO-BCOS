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
#include <bcos-framework/consensus/ConsensusConfigInterface.h>
#include <bcos-framework/consensus/ConsensusInterface.h>
#include <bcos-framework/sync/BlockSyncInterface.h>
#include <bcos-rpc/jsonrpc/JsonRpcImpl_2_0.h>
#include <bcos-rpc/web3jsonrpc/Web3JsonRpcImpl.h>
#include <boost/test/unit_test.hpp>
#include <future>

using namespace bcos;
using namespace bcos::rpc;

namespace bcos::test
{
namespace
{
class StubConsensusConfig : public bcos::consensus::ConsensusConfigInterface
{
public:
    explicit StubConsensusConfig(bcos::crypto::PublicPtr nodeId) : m_nodeId(std::move(nodeId)) {}
    bcos::crypto::PublicPtr nodeID() const override { return m_nodeId; }
    bcos::consensus::IndexType nodeIndex() const override { return 0; }
    bcos::consensus::ConsensusNodeList consensusNodeList() const override { return {}; }
    bcos::crypto::NodeIDs consensusNodeIDList(bool) const override { return {}; }
    bool isConsensusNode() const override { return true; }
    uint64_t consensusTimeout() const override { return 3000; }
    uint64_t minRequiredQuorum() const override { return 1; }
    void setConsensusNodeList(bcos::consensus::ConsensusNodeList) override {}
    void setConsensusTimeout(uint64_t) override {}
    void setCommittedProposal(bcos::consensus::ProposalInterface::Ptr) override {}
    bcos::consensus::ProposalInterface::ConstPtr committedProposal() override { return nullptr; }
    bcos::ledger::Features features() const override { return {}; }
    void setFeatures(bcos::ledger::Features) override {}

private:
    bcos::crypto::PublicPtr m_nodeId;
};

class StubConsensus : public bcos::consensus::ConsensusInterface
{
public:
    explicit StubConsensus(bcos::crypto::PublicPtr nodeId)
      : m_config(std::make_shared<StubConsensusConfig>(std::move(nodeId)))
    {}
    void start() override {}
    void stop() override {}
    void asyncSubmitProposal(bool, const protocol::Block&, bcos::protocol::BlockNumber,
        bcos::crypto::HashType const&, std::function<void(Error::Ptr)>) override
    {}
    void asyncGetPBFTView(std::function<void(Error::Ptr, bcos::consensus::ViewType)> cb) override
    {
        cb(nullptr, 0);
    }
    void asyncCheckBlock(bcos::protocol::Block::Ptr, std::function<void(Error::Ptr, bool)>) override
    {}
    void asyncNotifyNewBlock(
        bcos::ledger::LedgerConfig::Ptr, std::function<void(Error::Ptr)>) override
    {}
    void asyncNotifyConsensusMessage(bcos::Error::Ptr, std::string const&, bcos::crypto::NodeIDPtr,
        bytesConstRef, std::function<void(Error::Ptr)>) override
    {}
    void notifyHighestSyncingNumber(bcos::protocol::BlockNumber) override {}
    void asyncGetConsensusStatus(std::function<void(Error::Ptr, std::string)> cb) override
    {
        cb(nullptr, "{}");
    }
    void notifyConnectedNodes(
        bcos::crypto::NodeIDSet const&, std::function<void(Error::Ptr)>) override
    {}
    bcos::consensus::ConsensusConfigInterface::ConstPtr consensusConfig() const override
    {
        return m_config;
    }

private:
    std::shared_ptr<StubConsensusConfig> m_config;
};

class StubSync : public bcos::sync::BlockSyncInterface
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
    void asyncGetSyncInfo(std::function<void(Error::Ptr, std::string)> cb) override
    {
        cb(nullptr, "{}");
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

class Web3ConsensusFixture : public RPCFixture
{
public:
    Web3ConsensusFixture()
    {
        auto keyPair = cryptoSuite->signatureImpl()->generateKeyPair();
        auto consensus = std::make_shared<StubConsensus>(keyPair->publicKey());
        auto sync = std::make_shared<StubSync>();
        // NodeService gained a trailing AnyEngineService argument; not exercised here.
        auto svc = std::make_shared<rpc::NodeService>(
            m_ledger, scheduler, txPool, consensus, sync, m_blockFactory, nullptr);
        rpc = factory->buildLocalRpc(groupInfo, svc);
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

    Rpc::Ptr rpc;
    Web3JsonRpcImpl::Ptr web3JsonRpc;
};

BOOST_FIXTURE_TEST_SUITE(Web3ConsensusTest, Web3ConsensusFixture)

BOOST_AUTO_TEST_CASE(coinbaseDerivesAddressFromConsensusNodeId)
{
    // eth_coinbase dereferences consensus()->consensusConfig()->nodeID();
    // with the stub it returns the keccak-derived address rather than crashing.
    auto resp = call(R"({"jsonrpc":"2.0","id":1,"method":"eth_coinbase","params":[]})");
    BOOST_REQUIRE(resp.isMember("result"));
    auto addr = resp["result"].asString();
    BOOST_CHECK_EQUAL(addr.substr(0, 2), "0x");
    BOOST_CHECK_EQUAL(addr.size(), 42U);  // 0x + 20-byte address
}

BOOST_AUTO_TEST_CASE(jsonRpcConsensusAndSyncStatusHandlers)
{
    auto* impl = rpc->jsonRpcImpl().get();
    bool pbftCalled = false;
    bool consensusCalled = false;
    bool syncCalled = false;

    // The stub consensus/sync call their callbacks synchronously, so the
    // RespFunc fires inline. These handlers previously segfaulted on null
    // consensus()/sync().
    impl->getPbftView(groupId, "", [&](bcos::Error::Ptr, Json::Value&) { pbftCalled = true; });
    impl->getConsensusStatus(
        groupId, "", [&](bcos::Error::Ptr, Json::Value&) { consensusCalled = true; });
    impl->getSyncStatus(groupId, "", [&](bcos::Error::Ptr, Json::Value&) { syncCalled = true; });

    BOOST_CHECK(pbftCalled);
    BOOST_CHECK(consensusCalled);
    BOOST_CHECK(syncCalled);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
