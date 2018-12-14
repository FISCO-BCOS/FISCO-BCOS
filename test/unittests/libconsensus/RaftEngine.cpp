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

RaftMsgPacket transDataToPacket(RaftMsg& _msg, RaftPacketType _packetType)
{
    RLPStream ts;
    _msg.streamRLPFields(ts);
    RaftMsgPacket packet;

    RLPStream listRLP;
    listRLP.appendList(1).append(ref(ts.out()));
    bytes packetData;
    listRLP.swapOut(packetData);
    packet.data = packetData;
    packet.packetType = _packetType;
    return packet;
}

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
        auto state = raftEngine->getState();
        BOOST_CHECK(state == RaftRole::EN_STATE_FOLLOWER);
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

BOOST_AUTO_TEST_CASE(testVoteState)
{
    VoteState voteState;
    BOOST_CHECK(voteState.totalVoteCount() == 0);

    voteState.vote = 1;
    voteState.unVote = 2;
    voteState.lastTermErr = 3;
    voteState.firstVote = 4;
    voteState.discardedVote = 5;
    BOOST_CHECK(voteState.totalVoteCount() == 15);
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

BOOST_AUTO_TEST_CASE(testHandleHeartbeatMsg)
{
    RaftHeartBeat hb;
    hb.idx = 0;
    hb.term = 0;
    hb.height = 0;
    hb.blockHash = raftEngine->getHighestBlockHeader().hash();
    hb.leader = raftEngine->getNodeIdx();
    raftEngine->setTerm(10);
    raftEngine->setLastLeaderTerm(12);

    auto stepDown = raftEngine->handleHeartbeat(u256(0), h512(0), hb);
    BOOST_CHECK(stepDown == false);

    hb.term = 11;
    raftEngine->setState(RaftRole::EN_STATE_CANDIDATE);
    stepDown = raftEngine->handleHeartbeat(u256(0), h512(0), hb);
    BOOST_CHECK(stepDown == true);

    hb.term = 13;
    raftEngine->setState(RaftRole::EN_STATE_CANDIDATE);
    stepDown = raftEngine->handleHeartbeat(u256(0), h512(0), hb);
    BOOST_CHECK(stepDown == true);
}

BOOST_AUTO_TEST_CASE(testHandleVoteResponse)
{
    VoteState voteState;
    RaftVoteResp resp;

    resp.idx = 0;
    resp.term = 0;
    resp.height = raftEngine->getHighestBlockHeader().number();
    resp.blockHash = raftEngine->getHighestBlockHeader().hash();
    resp.voteFlag = VOTE_RESP_REJECT;
    resp.lastLeaderTerm = 0;

    raftEngine->setTerm(10);
    HandleVoteResult result = raftEngine->handleVoteResponse(u256(0), h512(0), resp, voteState);
    BOOST_CHECK(result == HandleVoteResult::NONE);

    resp.term = 11;
    voteState.unVote = 0;
    result = raftEngine->handleVoteResponse(u256(0), h512(0), resp, voteState);
    BOOST_CHECK(result == HandleVoteResult::NONE);
    voteState.unVote = 5;
    result = raftEngine->handleVoteResponse(u256(0), h512(0), resp, voteState);
    BOOST_CHECK(result == HandleVoteResult::TO_FOLLOWER);

    resp.voteFlag = VOTE_RESP_LEADER_REJECT;
    result = raftEngine->handleVoteResponse(u256(0), h512(0), resp, voteState);
    BOOST_CHECK(result == HandleVoteResult::TO_FOLLOWER);

    resp.voteFlag = VOTE_RESP_LASTTERM_ERROR;
    voteState.lastTermErr = 0;
    result = raftEngine->handleVoteResponse(u256(0), h512(0), resp, voteState);
    BOOST_CHECK(result == HandleVoteResult::NONE);
    voteState.lastTermErr = 5;
    result = raftEngine->handleVoteResponse(u256(0), h512(0), resp, voteState);
    BOOST_CHECK(result == HandleVoteResult::TO_FOLLOWER);

    resp.voteFlag = VOTE_RESP_FIRST_VOTE;
    voteState.firstVote = 0;
    result = raftEngine->handleVoteResponse(u256(0), h512(0), resp, voteState);
    BOOST_CHECK(result == HandleVoteResult::NONE);
    voteState.firstVote = 5;
    result = raftEngine->handleVoteResponse(u256(0), h512(0), resp, voteState);
    BOOST_CHECK(result == HandleVoteResult::TO_FOLLOWER);
    BOOST_CHECK(raftEngine->getTerm() == 10);

    resp.voteFlag = VOTE_RESP_DISCARD;
    result = raftEngine->handleVoteResponse(u256(0), h512(0), resp, voteState);
    BOOST_CHECK(result == HandleVoteResult::NONE);

    resp.voteFlag = VOTE_RESP_GRANTED;
    voteState.vote = 0;
    result = raftEngine->handleVoteResponse(u256(0), h512(0), resp, voteState);
    BOOST_CHECK(result == NONE);
    voteState.vote = 5;
    result = raftEngine->handleVoteResponse(u256(0), h512(0), resp, voteState);
    BOOST_CHECK(result == TO_LEADER);

    resp.voteFlag = (VoteRespFlag)0xff;
    result = raftEngine->handleVoteResponse(u256(0), h512(0), resp, voteState);
    BOOST_CHECK(result == NONE);
}

BOOST_AUTO_TEST_CASE(testHandleVoteRequest)
{
    RaftVoteReq req;
    req.idx = 0;
    req.term = 0;
    req.height = raftEngine->getHighestBlockHeader().number();
    req.blockHash = raftEngine->getHighestBlockHeader().hash();
    req.candidate = 0;
    req.lastLeaderTerm = 0;

    raftEngine->setTerm(10);
    raftEngine->setLastLeaderTerm(10);
    raftEngine->setState(RaftRole::EN_STATE_LEADER);
    auto result = raftEngine->handleVoteRequest(u256(0), h512(0), req);
    BOOST_CHECK(result == false);

    raftEngine->setState(RaftRole::EN_STATE_CANDIDATE);
    result = raftEngine->handleVoteRequest(u256(0), h512(0), req);
    BOOST_CHECK(result == false);
    req.term = raftEngine->getTerm();
    result = raftEngine->handleVoteRequest(u256(0), h512(0), req);
    BOOST_CHECK(result == false);

    req.term = 11;
    result = raftEngine->handleVoteRequest(u256(0), h512(0), req);
    BOOST_CHECK(result == false);

    req.lastLeaderTerm = 11;
    result = raftEngine->handleVoteRequest(u256(0), h512(0), req);
    BOOST_CHECK(result == false);

    result = raftEngine->handleVoteRequest(u256(0), h512(0), req);
    BOOST_CHECK(result == true);
}

BOOST_AUTO_TEST_CASE(testRunAsLeader)
{
    bool flag = false;
    std::unordered_map<dev::h512, unsigned> memberHeartbeatLog;

    BOOST_CHECK_NO_THROW(flag = raftEngine->runAsLeaderImp(memberHeartbeatLog));
    BOOST_CHECK(flag == false);
    auto state = raftEngine->getState();
    BOOST_CHECK(state == RaftRole::EN_STATE_FOLLOWER);

    raftEngine->heartbeatTimeout = true;
    raftEngine->setState(RaftRole::EN_STATE_LEADER);
    BOOST_CHECK_NO_THROW(flag = raftEngine->runAsLeaderImp(memberHeartbeatLog));
    BOOST_CHECK(flag == false);
    state = raftEngine->getState();
    BOOST_CHECK(state == RaftRole::EN_STATE_CANDIDATE);

    raftEngine->setTerm(10);
    raftEngine->setLastLeaderTerm(10);
    raftEngine->setState(RaftRole::EN_STATE_LEADER);
    RaftHeartBeat hb;
    hb.idx = 0;
    hb.term = 0;
    hb.height = raftEngine->getHighestBlockHeader().number();
    hb.blockHash = raftEngine->getHighestBlockHeader().hash();
    hb.leader = 0;
    raftEngine->heartbeatTimeout = false;
    raftEngine->msgQueue().push(transDataToPacket(hb, RaftPacketType::RaftHeartBeatPacket));
    BOOST_CHECK_NO_THROW(flag = raftEngine->runAsLeaderImp(memberHeartbeatLog));
    BOOST_CHECK(flag == true);
    state = raftEngine->getState();
    BOOST_CHECK(state == RaftRole::EN_STATE_LEADER);

    hb.term = 11;
    raftEngine->msgQueue().push(transDataToPacket(hb, RaftPacketType::RaftHeartBeatPacket));
    BOOST_CHECK_NO_THROW(flag = raftEngine->runAsLeaderImp(memberHeartbeatLog));
    BOOST_CHECK(flag == false);
    state = raftEngine->getState();
    BOOST_CHECK(state == RaftRole::EN_STATE_FOLLOWER);

    raftEngine->setTerm(10);
    raftEngine->setLastLeaderTerm(10);
    raftEngine->setState(RaftRole::EN_STATE_LEADER);
    RaftHeartBeatResp resp;
    hb.term = 10;
    raftEngine->msgQueue().push(transDataToPacket(hb, RaftPacketType::RaftHeartBeatPacket));
    BOOST_CHECK_NO_THROW(flag = raftEngine->runAsLeaderImp(memberHeartbeatLog));
    BOOST_CHECK(flag == true);
    state = raftEngine->getState();
    BOOST_CHECK(state == RaftRole::EN_STATE_LEADER);

    hb.term = 10;
    raftEngine->msgQueue().push(transDataToPacket(hb, RaftPacketType::RaftHeartBeatPacket));
    BOOST_CHECK_NO_THROW(flag = raftEngine->runAsLeaderImp(memberHeartbeatLog));
    BOOST_CHECK(flag == true);
    state = raftEngine->getState();
    BOOST_CHECK(state == RaftRole::EN_STATE_LEADER);

    raftEngine->msgQueue().push(transDataToPacket(hb, RaftPacketType::RaftHeartBeatPacket));
    BOOST_CHECK_NO_THROW(flag = raftEngine->runAsLeaderImp(memberHeartbeatLog));
    BOOST_CHECK(flag == true);
    state = raftEngine->getState();
    BOOST_CHECK(state == RaftRole::EN_STATE_LEADER);

    RaftVoteReq req;
    req.idx = 0;
    req.term = 0;
    req.height = raftEngine->getHighestBlockHeader().number();
    req.blockHash = raftEngine->getHighestBlockHeader().hash();
    req.candidate = 0;
    req.lastLeaderTerm = 0;
    raftEngine->msgQueue().push(transDataToPacket(req, RaftPacketType::RaftVoteReqPacket));
    BOOST_CHECK_NO_THROW(flag = raftEngine->runAsLeaderImp(memberHeartbeatLog));
    BOOST_CHECK(flag == true);
    state = raftEngine->getState();
    BOOST_CHECK(state == RaftRole::EN_STATE_LEADER);

    req.term = 11;
    req.lastLeaderTerm = 11;
    raftEngine->msgQueue().push(transDataToPacket(req, RaftPacketType::RaftVoteReqPacket));
    BOOST_CHECK_NO_THROW(flag = raftEngine->runAsLeaderImp(memberHeartbeatLog));
    BOOST_CHECK(flag == true);
    state = raftEngine->getState();
    BOOST_CHECK(state == RaftRole::EN_STATE_LEADER);

    raftEngine->msgQueue().push(transDataToPacket(req, RaftPacketType::RaftVoteReqPacket));
    BOOST_CHECK_NO_THROW(flag = raftEngine->runAsLeaderImp(memberHeartbeatLog));
    BOOST_CHECK(flag == false);
    state = raftEngine->getState();
    BOOST_CHECK(state == RaftRole::EN_STATE_FOLLOWER);
}

BOOST_AUTO_TEST_CASE(testRunAsFollower)
{
    raftEngine->setState(RaftRole::EN_STATE_LEADER);
    BOOST_CHECK_NO_THROW(raftEngine->runAsFollower());
    auto state = raftEngine->getState();
    BOOST_CHECK(state == RaftRole::EN_STATE_LEADER);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace dev