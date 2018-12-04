/*
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2018 fisco-dev contributors.
 */

/**
 * @brief: unit test for libconsensus/raft/RaftEngine.cpp
 * @file: RaftEngine.cpp
 * @author: catli
 * @date: 2018-11-30
 */
#include "FakeRaftEngine.h"
#include <libdevcrypto/Common.h>
#include <libethcore/Protocol.h>
#include <libp2p/P2PSession.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <test/unittests/libblockverifier/FakeBlockVerifier.h>
#include <test/unittests/libsync/FakeBlockSync.h>
#include <test/unittests/libtxpool/FakeBlockChain.h>
#include <boost/test/unit_test.hpp>
#include <memory>

using namespace std;
using namespace dev;
using namespace dev::test;
using namespace dev::consensus;
using namespace dev::eth;
using namespace dev::p2p;

namespace dev
{
namespace test
{
class FakeBaseSession : public SessionFace
{
public:
    FakeBaseSession()
    {
        m_endpoint = NodeIPEndpoint(bi::address::from_string("127.0.0.1"), 30303, 30303);
    }
    NodeIPEndpoint nodeIPEndpoint() const override { return m_endpoint; }
    void start() override {}
    void disconnect(DisconnectReason _reason) override {}

    bool isConnected() const override { return true; }

    void asyncSendMessage(Message::Ptr message, Options options = Options(),
        CallbackFunc callback = CallbackFunc()) override
    {}
    void setMessageHandler(
        std::function<void(NetworkException, std::shared_ptr<SessionFace>, Message::Ptr)>
            messageHandler) override
    {}
    bool actived() const override { return true; }

private:
    NodeIPEndpoint m_endpoint;
};

class FakeSession : public P2PSession
{
public:
    FakeSession(NodeID _id = NodeID()) : P2PSession(), m_id(_id)
    {
        m_session = std::make_shared<FakeBaseSession>();
    };
    virtual ~FakeSession(){};

    virtual bool actived() override { return m_run; }

    virtual void start() override { m_run = true; }

    virtual void stop(DisconnectReason reason) { m_run = false; }

    virtual NodeID nodeID() override { return m_id; }

    SessionFace::Ptr session() override { return m_session; }

    bool m_run = false;
    NodeID m_id;
    SessionFace::Ptr m_session;
};

class RaftEngineTestFixture : public TestOutputHelperFixture
{
public:
    RaftEngineTestFixture(uint64_t _blockNum = 5, size_t _txSize = 5, size_t _minerNum = 5)
      : txpoolCreator(make_shared<TxPoolFixture>(_blockNum, _txSize))
    {
        auto fakeService = txpoolCreator->m_topicService;
        auto fakeTxPool = txpoolCreator->m_txPool;
        auto fakeBlockChain = txpoolCreator->m_blockChain;
        auto fakeSync = make_shared<FakeBlockSync>();
        auto fakeBlockVerifier = std::make_shared<FakeBlockverifier>();
        auto fakeKeyPair = KeyPair::create();

        // add node itself to miner list
        fakeMinerList.push_back(fakeKeyPair.pub());

        for (size_t i = 0; i < _minerNum; i++)
        {
            KeyPair keyPair = KeyPair::create();
            fakeMinerList.push_back(keyPair.pub());
        }

        raftEngine = make_shared<FakeRaftEngine>(fakeService, fakeTxPool, fakeBlockChain, fakeSync,
            fakeBlockVerifier, fakeKeyPair, 1000, 2000, ProtocolID::Raft, fakeMinerList);
        raftEngine->start();
    }

    h512s fakeMinerList;
    shared_ptr<FakeRaftEngine> raftEngine;
    std::shared_ptr<TxPoolFixture> txpoolCreator;
};

BOOST_FIXTURE_TEST_SUITE(RaftEngineTest, RaftEngineTestFixture)

BOOST_AUTO_TEST_CASE(testGetIndexByMiner)
{
    for (ssize_t i = 0; i < static_cast<ssize_t>(fakeMinerList.size()); ++i)
    {
        auto miner = fakeMinerList[i];
        auto index = raftEngine->getIndexByMiner(miner);
        BOOST_CHECK(index == i);
    }
}

BOOST_AUTO_TEST_CASE(testGetNodeIdByIndex)
{
    h512 nodeId;
    for (ssize_t i = 0; i < static_cast<ssize_t>(fakeMinerList.size()); ++i)
    {
        auto exist = raftEngine->getNodeIdByIndex(nodeId, i);
        BOOST_CHECK(exist == true);
        BOOST_CHECK(nodeId == fakeMinerList[i]);
    }
    BOOST_CHECK(raftEngine->getNodeIdByIndex(nodeId, fakeMinerList.size()) == false);
}

BOOST_AUTO_TEST_CASE(testMsgEncodeAndDecode)
{
    auto fakeSession = std::make_shared<FakeSession>(fakeMinerList[fakeMinerList.size() - 1]);

    auto voteReqMsg = raftEngine->generateVoteReq();
    raftEngine->onRecvRaftMessage(NetworkException(0, ""), fakeSession, voteReqMsg);
    std::pair<bool, RaftMsgPacket> ret = raftEngine->msgQueue().tryPop(5);
    BOOST_CHECK(ret.first == true);
    BOOST_CHECK(ret.second.packetType == RaftPacketType::RaftVoteReqPacket);

    RaftVoteReq req;
    BOOST_CHECK_NO_THROW(req.populate(RLP(ref(ret.second.data).cropped(1))));
    BOOST_CHECK(req.idx == raftEngine->getNodeIdx());
    BOOST_CHECK(req.term == raftEngine->getTerm());
    BOOST_CHECK(req.height == raftEngine->getHighestBlockHeader().number());
    BOOST_CHECK(req.blockHash == raftEngine->getHighestBlockHeader().hash());
    BOOST_CHECK(req.candidate == raftEngine->getNodeIdx());
    BOOST_CHECK(req.lastLeaderTerm == raftEngine->getLastLeaderTerm());

    auto heartbeatMsg = raftEngine->generateHeartbeat();
    raftEngine->onRecvRaftMessage(NetworkException(0, ""), fakeSession, heartbeatMsg);
    ret = raftEngine->msgQueue().tryPop(5);
    BOOST_CHECK(ret.first == true);
    BOOST_CHECK(ret.second.packetType == RaftPacketType::RaftHeartBeatPacket);

    RaftHeartBeat hb;
    BOOST_CHECK_NO_THROW(hb.populate(RLP(ref(ret.second.data).cropped(1))));
    BOOST_CHECK(hb.idx == raftEngine->getNodeIdx());
    BOOST_CHECK(hb.term == raftEngine->getTerm());
    BOOST_CHECK(hb.height == raftEngine->getHighestBlockHeader().number());
    BOOST_CHECK(hb.blockHash == raftEngine->getHighestBlockHeader().hash());
    BOOST_CHECK(hb.leader == raftEngine->getNodeIdx());
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace dev