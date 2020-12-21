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
#include <libp2p/P2PSession.h>
#include <libprotocol/CommonProtocolType.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <test/unittests/libblockverifier/FakeBlockVerifier.h>
#include <test/unittests/libsync/FakeBlockSync.h>
#include <test/unittests/libtxpool/FakeBlockChain.h>
#include <boost/test/unit_test.hpp>
#include <cstdint>
#include <functional>
#include <memory>
#include <thread>

using namespace std;
using namespace bcos;
using namespace bcos::test;
using namespace bcos::consensus;
using namespace bcos::protocol;
using namespace bcos::p2p;
using namespace bcos::network;

namespace bcos
{
namespace test
{
class FakeBaseSession : public SessionFace
{
public:
    FakeBaseSession()
    {
        m_endpoint = NodeIPEndpoint(boost::asio::ip::make_address("127.0.0.1"), 30303);
    }
    NodeIPEndpoint nodeIPEndpoint() const override { return m_endpoint; }
    void start() override {}
    void disconnect(bcos::network::DisconnectReason) override {}

    void asyncSendMessage(Message::Ptr, Options = Options(), CallbackFunc = CallbackFunc()) override
    {}
    void setMessageHandler(
        std::function<void(NetworkException, std::shared_ptr<SessionFace>, Message::Ptr)>) override
    {}
    bool actived() const override { return true; }
    std::shared_ptr<SocketFace> socket() override { return nullptr; }

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

    bool actived() override { return m_run; }

    void start() override { m_run = true; }

    void stop(bcos::network::DisconnectReason) override { m_run = false; }

    NodeID nodeID() override { return m_id; }

    SessionFace::Ptr session() override { return m_session; }

    bool m_run = false;
    NodeID m_id;
    SessionFace::Ptr m_session;
};

RaftMsgPacket transDataToPacket(RaftMsg& _msg, RaftPacketType _packetType,
    raft::NodeIndex _nodeIdx = 0, Public _nodeId = Public(0))
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
    packet.nodeIdx = _nodeIdx;
    packet.nodeId = _nodeId;
    return packet;
}

class RaftEngineTestFixture : public TestOutputHelperFixture
{
public:
    RaftEngineTestFixture(uint64_t _blockNum = 5, size_t _txSize = 5, size_t _sealerNum = 5)
      : txpoolCreator(make_shared<TxPoolFixture>(_blockNum, _txSize)), fakeBlock(5)
    {
        auto fakeService = txpoolCreator->m_topicService;
        auto fakeTxPool = txpoolCreator->m_txPool;
        auto fakeBlockChain = txpoolCreator->m_blockChain;
        auto fakeSync = make_shared<FakeBlockSync>();
        auto fakeBlockVerifier = std::make_shared<FakeBlockverifier>();
        auto fakeKeyPair = KeyPair::create();

        // add node itself to sealer list
        fakeSealerList.push_back(fakeKeyPair.pub());

        for (size_t i = 0; i < _sealerNum; i++)
        {
            KeyPair keyPair = KeyPair::create();
            fakeSealerList.push_back(keyPair.pub());
        }
        std::sort(fakeSealerList.begin(), fakeSealerList.end());

        raftEngine = make_shared<FakeRaftEngine>(fakeService, fakeTxPool, fakeBlockChain, fakeSync,
            fakeBlockVerifier, fakeKeyPair, 1000, 2000, ProtocolID::Raft, fakeSealerList);
        raftEngine->start();
        auto state = raftEngine->getState();
        BOOST_CHECK(state == RaftRole::EN_STATE_FOLLOWER);

        raftEngine->reportBlock(*fakeBlockChain->getBlockByNumber(4));
        BOOST_CHECK(raftEngine->getHighestBlock().number() == 4);
    }

    h512s fakeSealerList;
    shared_ptr<FakeRaftEngine> raftEngine;
    std::shared_ptr<TxPoolFixture> txpoolCreator;
    FakeBlock fakeBlock;
};

BOOST_FIXTURE_TEST_SUITE(RaftEngineTest, RaftEngineTestFixture)

BOOST_AUTO_TEST_CASE(testGetIndexBySealer)
{
    for (ssize_t i = 0; i < static_cast<ssize_t>(fakeSealerList.size()); ++i)
    {
        auto sealer = fakeSealerList[i];
        auto index = raftEngine->getIndexBySealer(sealer);
        BOOST_CHECK(index == i);
    }
}

BOOST_AUTO_TEST_CASE(testGetNodeIdByIndex)
{
    h512 nodeId;
    for (ssize_t i = 0; i < static_cast<ssize_t>(fakeSealerList.size()); ++i)
    {
        auto exist = raftEngine->getNodeIdByIndex(nodeId, i);
        BOOST_CHECK(exist == true);
        BOOST_CHECK(nodeId == fakeSealerList[i]);
    }
    BOOST_CHECK(raftEngine->getNodeIdByIndex(nodeId, fakeSealerList.size()) == false);
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

BOOST_AUTO_TEST_CASE(testGenerateMsg)
{
    raftEngine->setUncommitedBlock(*fakeBlock.m_block);
    raftEngine->setUncommitedNumber(10);
    raftEngine->setConsensusBlockNumber(10);

    auto hbMsg = raftEngine->generateHeartbeat();
    BOOST_CHECK(hbMsg->protocolID() == raftEngine->protocolId());

    RaftMsgPacket packet;
    auto data = *(hbMsg->buffer());
    packet.decode(ref(data));

    RaftHeartBeat hb;
    hb.populate(RLP(ref(packet.data))[0]);
    BOOST_CHECK(hb.leader == raftEngine->nodeIdx());
    BOOST_CHECK(hb.uncommitedBlockNumber == 10);

    auto vrMsg = raftEngine->generateVoteReq();
    BOOST_CHECK(vrMsg->protocolID() == raftEngine->protocolId());

    data = *(vrMsg->buffer());
    packet.decode(ref(data));

    RaftVoteReq vr;
    vr.populate(RLP(ref(packet.data))[0]);
    BOOST_CHECK(vr.candidate == raftEngine->nodeIdx());
    BOOST_CHECK(vr.lastBlockNumber == raftEngine->getBlockChain()->number() + 1);
}

BOOST_AUTO_TEST_CASE(testHandleHeartbeatMsg)
{
    RaftHeartBeat hb;
    hb.term = 0;

    raftEngine->setTerm(10);
    raftEngine->setLastLeaderTerm(9);

    auto stepDown = raftEngine->handleHeartbeat(u256(0), h512(0), hb);
    BOOST_CHECK(stepDown == false);

    hb.term = 10;
    stepDown = raftEngine->handleHeartbeat(u256(0), h512(0), hb);
    BOOST_CHECK(stepDown == true);

    hb.term = 11;
    stepDown = raftEngine->handleHeartbeat(u256(0), h512(0), hb);
    BOOST_CHECK(stepDown == true);
}

BOOST_AUTO_TEST_CASE(testHandleVoteResponse)
{
    VoteState voteState;
    RaftVoteResp resp;

    resp.idx = 0;
    resp.term = 0;
    resp.height = raftEngine->getHighestBlock().number();
    resp.blockHash = raftEngine->getHighestBlock().hash();
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
    req.height = raftEngine->getHighestBlock().number();
    req.blockHash = raftEngine->getHighestBlock().hash();
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

    /*
    req.lastBlockNumber = 11;
    result = raftEngine->handleVoteRequest(u256(0), h512(0), req);
    BOOST_CHECK(result == true);
    */
}

BOOST_AUTO_TEST_CASE(testRunAsLeader)
{
    bool flag = false;
    std::unordered_map<bcos::h512, unsigned> memberHeartbeatLog;

    BOOST_CHECK_NO_THROW(flag = raftEngine->runAsLeaderImp(memberHeartbeatLog));
    BOOST_CHECK(flag == false);
    auto state = raftEngine->getState();
    BOOST_CHECK(state == RaftRole::EN_STATE_FOLLOWER);

    auto resetLeader = [this, &memberHeartbeatLog]() {
        raftEngine->heartbeatTimeout = false;
        raftEngine->setTerm(10);
        raftEngine->setLastLeaderTerm(10);
        raftEngine->setState(RaftRole::EN_STATE_LEADER);
        raftEngine->setUncommitedBlock(Block());
        memberHeartbeatLog.clear();
    };

    resetLeader();

    raftEngine->heartbeatTimeout = true;
    raftEngine->setState(RaftRole::EN_STATE_LEADER);
    BOOST_CHECK_NO_THROW(flag = raftEngine->runAsLeaderImp(memberHeartbeatLog));
    BOOST_CHECK(flag == false);
    state = raftEngine->getState();
    BOOST_CHECK(state == RaftRole::EN_STATE_CANDIDATE);

    resetLeader();

    RaftHeartBeat hb;
    hb.term = 0;
    raftEngine->msgQueue().push(transDataToPacket(hb, RaftPacketType::RaftHeartBeatPacket));
    BOOST_CHECK_NO_THROW(flag = raftEngine->runAsLeaderImp(memberHeartbeatLog));
    BOOST_CHECK(flag == true);
    state = raftEngine->getState();
    BOOST_CHECK(state == RaftRole::EN_STATE_LEADER);
    BOOST_CHECK(raftEngine->getTerm() == 10);
    BOOST_CHECK(raftEngine->getLastLeaderTerm() == 10);

    resetLeader();

    hb.term = 11;
    raftEngine->msgQueue().push(transDataToPacket(hb, RaftPacketType::RaftHeartBeatPacket));
    BOOST_CHECK_NO_THROW(flag = raftEngine->runAsLeaderImp(memberHeartbeatLog));
    BOOST_CHECK(flag == false);
    state = raftEngine->getState();
    BOOST_CHECK(state == RaftRole::EN_STATE_FOLLOWER);

    resetLeader();

    RaftHeartBeatResp resp;
    resp.term = 11;
    raftEngine->msgQueue().push(transDataToPacket(resp, RaftPacketType::RaftHeartBeatRespPacket));
    BOOST_CHECK_NO_THROW(flag = raftEngine->runAsLeaderImp(memberHeartbeatLog));
    BOOST_CHECK(flag == true);
    state = raftEngine->getState();
    BOOST_CHECK(state == RaftRole::EN_STATE_LEADER);

    resetLeader();

    resp.term = 10;
    unsigned repeatTime = 2;
    unsigned msgNum = fakeSealerList.size() * repeatTime;
    for (unsigned i = 0; i < msgNum; ++i)
    {
        raftEngine->msgQueue().push(
            transDataToPacket(hb, RaftPacketType::RaftHeartBeatRespPacket, i));
        BOOST_CHECK_NO_THROW(flag = raftEngine->runAsLeaderImp(memberHeartbeatLog));
        BOOST_CHECK(flag == true);
        state = raftEngine->getState();
        BOOST_CHECK(state == RaftRole::EN_STATE_LEADER);
    }

    for (auto i = memberHeartbeatLog.begin(); i != memberHeartbeatLog.end(); ++i)
    {
        BOOST_CHECK(i->second == repeatTime);
    }

    RaftVoteReq req;
    req.lastBlockNumber = raftEngine->getBlockChain()->number();

    resetLeader();

    req.term = 10;
    req.lastLeaderTerm = 10;
    raftEngine->msgQueue().push(transDataToPacket(req, RaftPacketType::RaftVoteReqPacket));
    BOOST_CHECK_NO_THROW(flag = raftEngine->runAsLeaderImp(memberHeartbeatLog));
    BOOST_CHECK(flag == true);
    state = raftEngine->getState();
    BOOST_CHECK(state == RaftRole::EN_STATE_LEADER);

    resetLeader();
    req.term = 11;
    req.lastLeaderTerm = 11;

    for (auto i = 0; i < 2; ++i)
    {
        raftEngine->msgQueue().push(transDataToPacket(req, RaftPacketType::RaftVoteReqPacket));
        BOOST_CHECK_NO_THROW(flag = raftEngine->runAsLeaderImp(memberHeartbeatLog));
    }

    BOOST_CHECK(flag == false);
    state = raftEngine->getState();
    BOOST_CHECK(state == RaftRole::EN_STATE_FOLLOWER);
}

BOOST_AUTO_TEST_CASE(testRunAsFollower)
{
    bool flag = false;

    raftEngine->setState(RaftRole::EN_STATE_LEADER);
    BOOST_CHECK_NO_THROW(flag = raftEngine->runAsFollowerImp());
    auto state = raftEngine->getState();
    BOOST_CHECK(flag == false);
    BOOST_CHECK(state == RaftRole::EN_STATE_LEADER);

    auto resetFollower = [this]() {
        raftEngine->electTimeout = false;
        raftEngine->setTerm(10);
        raftEngine->setLastLeaderTerm(10);
        raftEngine->setState(RaftRole::EN_STATE_FOLLOWER);
    };

    resetFollower();

    raftEngine->electTimeout = true;
    BOOST_CHECK_NO_THROW(flag = raftEngine->runAsFollowerImp());
    state = raftEngine->getState();
    BOOST_CHECK(flag == false);
    BOOST_CHECK(state == RaftRole::EN_STATE_CANDIDATE);

    resetFollower();

    RaftVoteReq req;
    req.lastBlockNumber = raftEngine->getBlockChain()->number();
    req.term = 10;
    req.lastLeaderTerm = 10;
    raftEngine->msgQueue().push(transDataToPacket(req, RaftPacketType::RaftVoteReqPacket));
    BOOST_CHECK_NO_THROW(flag = raftEngine->runAsFollowerImp());
    BOOST_CHECK(flag == true);

    resetFollower();
    req.lastBlockNumber = raftEngine->getBlockChain()->number();
    req.term = 11;
    req.lastLeaderTerm = 11;
    raftEngine->msgQueue().push(transDataToPacket(req, RaftPacketType::RaftVoteReqPacket));
    BOOST_CHECK_NO_THROW(flag = raftEngine->runAsFollowerImp());
    BOOST_CHECK(flag == true);
    raftEngine->msgQueue().push(transDataToPacket(req, RaftPacketType::RaftVoteReqPacket));
    BOOST_CHECK_NO_THROW(flag = raftEngine->runAsFollowerImp());
    BOOST_CHECK(flag == false);

    resetFollower();

    RaftHeartBeatResp resp;
    raftEngine->msgQueue().push(transDataToPacket(resp, RaftPacketType::RaftHeartBeatRespPacket));
    BOOST_CHECK_NO_THROW(flag = raftEngine->runAsFollowerImp());
    BOOST_CHECK(flag == true);

    resetFollower();

    RaftHeartBeat hb;
    hb.term = 10;
    raftEngine->msgQueue().push(transDataToPacket(hb, RaftPacketType::RaftHeartBeatPacket));
    BOOST_CHECK_NO_THROW(flag = raftEngine->runAsFollowerImp());
    BOOST_CHECK(flag == true);
}

BOOST_AUTO_TEST_CASE(testRunAsCandidate)
{
    auto flag = false;
    VoteState voteState;

    auto resetCandidate = [this, &voteState]() {
        raftEngine->electTimeout = false;
        raftEngine->setTerm(10);
        raftEngine->setLastLeaderTerm(10);
        raftEngine->setState(RaftRole::EN_STATE_CANDIDATE);
        voteState.vote = 1;  // vote self
        raftEngine->setVote(raftEngine->getIdx());
        raftEngine->setFirstVote(raftEngine->getIdx());
    };

    resetCandidate();

    raftEngine->electTimeout = true;
    BOOST_CHECK_NO_THROW(flag = raftEngine->runAsCandidateImp(voteState));
    BOOST_CHECK(flag == false);
    auto state = raftEngine->getState();
    BOOST_CHECK(state == RaftRole::EN_STATE_FOLLOWER);

    resetCandidate();

    voteState.vote = fakeSealerList.size();
    raftEngine->electTimeout = true;
    BOOST_CHECK_NO_THROW(flag = raftEngine->runAsCandidateImp(voteState));
    BOOST_CHECK(flag == false);
    state = raftEngine->getState();
    BOOST_CHECK(state == RaftRole::EN_STATE_CANDIDATE);

    resetCandidate();

    RaftVoteReq req;
    req.lastBlockNumber = raftEngine->getBlockChain()->number();
    req.term = 10;
    req.lastLeaderTerm = 10;
    raftEngine->msgQueue().push(transDataToPacket(req, RaftPacketType::RaftVoteReqPacket));
    BOOST_CHECK_NO_THROW(flag = raftEngine->runAsCandidateImp(voteState));
    BOOST_CHECK(flag == true);
    state = raftEngine->getState();
    BOOST_CHECK(state == RaftRole::EN_STATE_CANDIDATE);

    resetCandidate();

    req.term = 11;
    raftEngine->msgQueue().push(transDataToPacket(req, RaftPacketType::RaftVoteReqPacket));
    BOOST_CHECK_NO_THROW(flag = raftEngine->runAsCandidateImp(voteState));
    BOOST_CHECK(flag == false);
    state = raftEngine->getState();
    BOOST_CHECK(state == RaftRole::EN_STATE_FOLLOWER);

    resetCandidate();

    RaftVoteResp resp;
    resp.idx = 0;
    resp.term = 10;
    resp.height = raftEngine->getHighestBlock().number();
    resp.blockHash = raftEngine->getHighestBlock().hash();
    resp.voteFlag = VOTE_RESP_REJECT;
    resp.lastLeaderTerm = 10;

    voteState.vote = 0;
    raftEngine->msgQueue().push(transDataToPacket(resp, RaftPacketType::RaftVoteRespPacket));
    BOOST_CHECK_NO_THROW(flag = raftEngine->runAsCandidateImp(voteState));
    BOOST_CHECK(flag == true);
    state = raftEngine->getState();
    BOOST_CHECK(state == RaftRole::EN_STATE_CANDIDATE);

    resetCandidate();

    resp.term = 11;
    resp.voteFlag = VOTE_RESP_LEADER_REJECT;
    raftEngine->msgQueue().push(transDataToPacket(resp, RaftPacketType::RaftVoteRespPacket));
    BOOST_CHECK_NO_THROW(flag = raftEngine->runAsCandidateImp(voteState));
    BOOST_CHECK(flag == false);
    state = raftEngine->getState();
    BOOST_CHECK(state == RaftRole::EN_STATE_FOLLOWER);

    resetCandidate();

    resp.term = 10;
    resp.voteFlag = VOTE_RESP_GRANTED;
    voteState.vote = fakeSealerList.size();
    raftEngine->msgQueue().push(transDataToPacket(resp, RaftPacketType::RaftVoteRespPacket));
    BOOST_CHECK_NO_THROW(flag = raftEngine->runAsCandidateImp(voteState));
    BOOST_CHECK(flag == false);
    state = raftEngine->getState();
    BOOST_CHECK(state == RaftRole::EN_STATE_LEADER);

    resetCandidate();

    RaftHeartBeat hb;
    hb.term = 9;
    raftEngine->msgQueue().push(transDataToPacket(hb, RaftPacketType::RaftHeartBeatPacket));
    BOOST_CHECK_NO_THROW(flag = raftEngine->runAsCandidateImp(voteState));
    BOOST_CHECK(flag == true);
    state = raftEngine->getState();
    BOOST_CHECK(state == RaftRole::EN_STATE_CANDIDATE);

    resetCandidate();
    hb.term = 11;
    raftEngine->msgQueue().push(transDataToPacket(hb, RaftPacketType::RaftHeartBeatPacket));
    BOOST_CHECK_NO_THROW(flag = raftEngine->runAsCandidateImp(voteState));
    BOOST_CHECK(flag == false);
    state = raftEngine->getState();
    BOOST_CHECK(state == RaftRole::EN_STATE_FOLLOWER);

    resetCandidate();

    RaftHeartBeatResp hbResp;
    raftEngine->msgQueue().push(transDataToPacket(hbResp, RaftPacketType::RaftHeartBeatRespPacket));
    BOOST_CHECK_NO_THROW(flag = raftEngine->runAsCandidateImp(voteState));
    BOOST_CHECK(flag == true);
    state = raftEngine->getState();
    BOOST_CHECK(state == RaftRole::EN_STATE_CANDIDATE);
}

BOOST_AUTO_TEST_CASE(testTryCommitUncommitedBlock)
{
    raftEngine->setUncommitedBlock(Block());
    RaftHeartBeatResp resp;
    BOOST_CHECK_NO_THROW(raftEngine->tryCommitUncommitedBlock(resp));
}

BOOST_AUTO_TEST_CASE(testShouldSeal)
{
    raftEngine->setState(RaftRole::EN_STATE_FOLLOWER);
    BOOST_CHECK(raftEngine->shouldSeal() == false);
}

BOOST_AUTO_TEST_CASE(testCommitBlock)
{
    raftEngine->setState(RaftRole::EN_STATE_LEADER);
    auto t = std::thread(std::bind(&FakeRaftEngine::fakeLeader, raftEngine.get()));
    auto flag = false;
    auto block = *fakeBlock.m_block;
    auto number = raftEngine->getBlockChain()->number();
    block.header().setNumber(number + 1);
    auto parent = raftEngine->getBlockChain()->getBlockByNumber(number);
    block.header().setParentHash(parent->header().hash());
    block.header().setSealerList(fakeSealerList);
    BOOST_CHECK_NO_THROW(flag = raftEngine->commit(block));
    BOOST_CHECK(flag == true);
    t.join();
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos
