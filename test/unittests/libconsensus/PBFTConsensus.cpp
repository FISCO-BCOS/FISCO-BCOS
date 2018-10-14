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
 * @brief: unit test for libconsensus/pbft/PBFTConsensus.*
 * @file: PBFTConsensus.cpp
 * @author: yujiechen
 * @date: 2018-10-09
 */
#include "PBFTConsensus.h"
#include "Common.h"
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/filesystem.hpp>
#include <boost/test/unit_test.hpp>
namespace dev
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(pbftConsensusTest, TestOutputHelperFixture)

/// test constructor (invalid ProtocolID will cause exception)
/// test initPBFTEnv (normal case)
BOOST_AUTO_TEST_CASE(testInitPBFTEnvNormalCase)
{
    FakeConsensus<FakePBFTConsensus> fake_pbft(1, ProtocolID::PBFT);
    BOOST_CHECK(fake_pbft.consensus()->keyPair().pub() != h512());
    BOOST_CHECK(fake_pbft.consensus()->broadCastCache());
    BOOST_CHECK(fake_pbft.consensus()->reqCache());
    /// init pbft env
    fake_pbft.consensus()->initPBFTEnv(
        fake_pbft.consensus()->timeManager().m_intervalBlockTime * 3);
    /// check level db has already been openend
    BOOST_CHECK(fake_pbft.consensus()->backupDB());
    BOOST_CHECK(fake_pbft.consensus()->consensusBlockNumber() == 0);
    BOOST_CHECK(fake_pbft.consensus()->toView() == u256(0));
    BOOST_CHECK(fake_pbft.consensus()->view() == u256(0));
    /// check resetConfig
    BOOST_CHECK(fake_pbft.consensus()->accountType() != NodeAccountType::MinerAccount);
    fake_pbft.m_minerList.push_back(fake_pbft.consensus()->keyPair().pub());
    fake_pbft.consensus()->setMinerList(fake_pbft.m_minerList);
    fake_pbft.consensus()->resetConfig();
    BOOST_CHECK(fake_pbft.consensus()->accountType() == NodeAccountType::MinerAccount);
    BOOST_CHECK(fake_pbft.consensus()->nodeIdx() == u256(fake_pbft.m_minerList.size() - 1));
    BOOST_CHECK(fake_pbft.consensus()->minerList().size() == fake_pbft.m_minerList.size());
    BOOST_CHECK(fake_pbft.consensus()->nodeNum() == u256(fake_pbft.m_minerList.size()));
    BOOST_CHECK(fake_pbft.consensus()->fValue() ==
                u256((fake_pbft.consensus()->nodeNum() - u256(1)) / u256(3)));

    /// check reloadMsg: empty committedPrepareCache
    checkPBFTMsg(fake_pbft.consensus()->reqCache()->committedPrepareCache());
    /// check m_timeManager
    BOOST_CHECK(fake_pbft.consensus()->timeManager().m_lastExecFinishTime > 0);
    BOOST_CHECK(fake_pbft.consensus()->timeManager().m_lastExecFinishTime <= utcTime());
    BOOST_CHECK((fake_pbft.consensus()->timeManager().m_lastExecFinishTime) ==
                (fake_pbft.consensus()->timeManager().m_lastExecBlockFiniTime));
    BOOST_CHECK((fake_pbft.consensus()->timeManager().m_lastExecFinishTime) ==
                (fake_pbft.consensus()->timeManager().m_lastConsensusTime));
    BOOST_CHECK(fake_pbft.consensus()->timeManager().m_viewTimeout ==
                fake_pbft.consensus()->timeManager().m_intervalBlockTime * 3);
    BOOST_CHECK(fake_pbft.consensus()->timeManager().m_changeCycle == 0);
    BOOST_CHECK(fake_pbft.consensus()->timeManager().m_lastGarbageCollection <=
                std::chrono::system_clock::now());
}
/// test exception case of initPBFT
BOOST_AUTO_TEST_CASE(testInitPBFTExceptionCase)
{
    FakeConsensus<FakePBFTConsensus> fake_pbft(1, ProtocolID::PBFT);
    boost::filesystem::create_directories(boost::filesystem::path("./invalid"));
    fake_pbft.consensus()->setBaseDir("./invalid");
    BOOST_CHECK_THROW(fake_pbft.consensus()->initPBFTEnv(1000), NotEnoughAvailableSpace);
}


/// test onRecvPBFTMessage
BOOST_AUTO_TEST_CASE(testOnRecvPBFTMessage)
{
    KeyPair key_pair;
    /// fake prepare_req
    PrepareReq prepare_req = FakePrepareReq(key_pair);
    /// fake FakePBFTConsensus
    FakeConsensus<FakePBFTConsensus> fake_pbft(1, ProtocolID::PBFT);
    NodeIPEndpoint endpoint;
    /// fake session
    std::shared_ptr<Session> session = FakeSession(key_pair.pub());
    ///------ test invalid case(recv message from own-node)
    /// check onreceive prepare request
    CheckOnRecvPBFTMessage(fake_pbft.consensus(), session, prepare_req, PrepareReqPacket, false);
    /// check onreceive sign request
    SignReq sign_req(prepare_req, key_pair, prepare_req.idx + u256(100));
    CheckOnRecvPBFTMessage(fake_pbft.consensus(), session, sign_req, SignReqPacket, false);
    /// check onReceive commit request
    CommitReq commit_req(prepare_req, key_pair, prepare_req.idx + u256(10));
    CheckOnRecvPBFTMessage(fake_pbft.consensus(), session, commit_req, CommitReqPacket, false);
    /// test viewchange case
    ViewChangeReq viewChange_req(
        key_pair, prepare_req.height, prepare_req.view, prepare_req.idx, prepare_req.block_hash);
    CheckOnRecvPBFTMessage(
        fake_pbft.consensus(), session, viewChange_req, ViewChangeReqPacket, false);

    KeyPair key_pair2 = KeyPair::create();
    std::shared_ptr<Session> session2 = FakeSession(fake_pbft.m_minerList[0]);
    /// test invalid case: this node is not miner
    CheckOnRecvPBFTMessage(fake_pbft.consensus(), session2, prepare_req, PrepareReqPacket, false);
    ///----- test valid case
    /// test recv packet from other nodes
    FakePBFTMiner(fake_pbft);  // set this node to be miner
    CheckOnRecvPBFTMessage(fake_pbft.consensus(), session2, prepare_req, PrepareReqPacket, true);
    CheckOnRecvPBFTMessage(fake_pbft.consensus(), session2, sign_req, SignReqPacket, true);
    CheckOnRecvPBFTMessage(fake_pbft.consensus(), session2, commit_req, CommitReqPacket, true);
    CheckOnRecvPBFTMessage(
        fake_pbft.consensus(), session2, viewChange_req, ViewChangeReqPacket, true);
}
/// test broadcastMsg
BOOST_AUTO_TEST_CASE(testBroadcastMsg)
{
    FakeConsensus<FakePBFTConsensus> fake_pbft(1, ProtocolID::PBFT);
    KeyPair peer_keyPair = KeyPair::create();
    /// append session info1
    appendSessionInfo(fake_pbft, peer_keyPair.pub());
    /// append session info2
    KeyPair peer2_keyPair = KeyPair::create();
    appendSessionInfo(fake_pbft, peer2_keyPair.pub());
    KeyPair key_pair;
    /// fake prepare_req
    PrepareReq prepare_req = FakePrepareReq(key_pair);
    bytes data;
    prepare_req.encode(data);
    /// case1: all peer is not the miner, stop broadcasting
    fake_pbft.consensus()->broadcastMsg(PrepareReqPacket, prepare_req.sig.hex(), ref(data));
    BOOST_CHECK(fake_pbft.consensus()->broadcastFilter(
                    peer_keyPair.pub(), PrepareReqPacket, prepare_req.sig.hex()) == false);
    BOOST_CHECK(fake_pbft.consensus()->broadcastFilter(
                    peer2_keyPair.pub(), PrepareReqPacket, prepare_req.sig.hex()) == false);
    compareAsyncSendTime(fake_pbft, peer_keyPair.pub(), 0);
    compareAsyncSendTime(fake_pbft, peer2_keyPair.pub(), 0);

    /// case2: only one peer is the miner, broadcast to one node
    fake_pbft.m_minerList.push_back(peer_keyPair.pub());
    FakePBFTMiner(fake_pbft);
    fake_pbft.consensus()->broadcastMsg(PrepareReqPacket, prepare_req.sig.hex(), ref(data));
    BOOST_CHECK(fake_pbft.consensus()->broadcastFilter(
                    peer_keyPair.pub(), PrepareReqPacket, prepare_req.sig.hex()) == true);
    BOOST_CHECK(fake_pbft.consensus()->broadcastFilter(
                    peer2_keyPair.pub(), PrepareReqPacket, prepare_req.sig.hex()) == false);
    compareAsyncSendTime(fake_pbft, peer_keyPair.pub(), 1);
    compareAsyncSendTime(fake_pbft, peer2_keyPair.pub(), 0);

    /// case3: two nodes are all miners , but one peer is in the cache, broadcast to one node
    fake_pbft.m_minerList.push_back(peer2_keyPair.pub());
    FakePBFTMiner(fake_pbft);
    fake_pbft.consensus()->broadcastMsg(PrepareReqPacket, prepare_req.sig.hex(), ref(data));
    BOOST_CHECK(fake_pbft.consensus()->broadcastFilter(
                    peer_keyPair.pub(), PrepareReqPacket, prepare_req.sig.hex()) == true);
    BOOST_CHECK(fake_pbft.consensus()->broadcastFilter(
                    peer2_keyPair.pub(), PrepareReqPacket, prepare_req.sig.hex()) == true);
    compareAsyncSendTime(fake_pbft, peer_keyPair.pub(), 1);
    compareAsyncSendTime(fake_pbft, peer2_keyPair.pub(), 1);

    /// case3: the miner-peer in the filter
    KeyPair peer3_keyPair = KeyPair::create();
    appendSessionInfo(fake_pbft, peer3_keyPair.pub());
    fake_pbft.m_minerList.push_back(peer3_keyPair.pub());
    FakePBFTMiner(fake_pbft);
    /// fake the filter with node id of peer3
    std::unordered_set<h512> filter;
    filter.insert(peer3_keyPair.pub());
    fake_pbft.consensus()->broadcastMsg(PrepareReqPacket, prepare_req.sig.hex(), ref(data), filter);
    BOOST_CHECK(fake_pbft.consensus()->broadcastFilter(
                    peer3_keyPair.pub(), PrepareReqPacket, prepare_req.sig.hex()) == true);
    compareAsyncSendTime(fake_pbft, peer3_keyPair.pub(), 0);
}

/// test broadcastSignReq
BOOST_AUTO_TEST_CASE(testBroadcastSignReq)
{
    FakeConsensus<FakePBFTConsensus> fake_pbft(1, ProtocolID::PBFT);
    KeyPair key_pair;
    KeyPair peer_keyPair = KeyPair::create();
    /// fake prepare_req
    PrepareReq prepare_req = FakePrepareReq(key_pair);
    /// obtain sig of SignReq
    SignReq tmp_req(prepare_req, fake_pbft.consensus()->keyPair(), prepare_req.idx);
    std::string signReq_key = tmp_req.sig.hex();
    /// append session info
    appendSessionInfo(fake_pbft, peer_keyPair.pub());
    /// case1: the peer node is not miner
    fake_pbft.consensus()->broadcastSignReq(prepare_req);
    BOOST_CHECK(fake_pbft.consensus()->broadcastFilter(
                    peer_keyPair.pub(), SignReqPacket, signReq_key) == false);
    compareAsyncSendTime(fake_pbft, peer_keyPair.pub(), 0);
    /// check cache
    BOOST_CHECK(fake_pbft.consensus()->reqCache()->isExistSign(tmp_req));
    /// case2: the the peer node is a miner
    fake_pbft.m_minerList.push_back(peer_keyPair.pub());
    FakePBFTMiner(fake_pbft);
    fake_pbft.consensus()->broadcastSignReq(prepare_req);
    BOOST_CHECK(fake_pbft.consensus()->broadcastFilter(
                    peer_keyPair.pub(), SignReqPacket, signReq_key) == true);
    compareAsyncSendTime(fake_pbft, peer_keyPair.pub(), 1);
    BOOST_CHECK(fake_pbft.consensus()->reqCache()->isExistSign(tmp_req));
}

/// test checkAndSave && reportBlock
BOOST_AUTO_TEST_CASE(testCheckAndSave)
{
    /// fake collected commitReq and signReq
    BlockHeader highest;
    size_t invalid_height = 2;
    size_t invalid_hash = 3;
    size_t valid = 4;
    FakeConsensus<FakePBFTConsensus> fake_pbft(1, ProtocolID::PBFT);
    fake_pbft.consensus()->setHighest(highest);
    PrepareReq prepare_req;
    FakeValidNodeNum(fake_pbft, valid);
    FakeSignAndCommitCache(fake_pbft, prepare_req, highest, invalid_height, invalid_hash, valid);
    /// exception case: invalid view
    fake_pbft.consensus()->setView(prepare_req.view + u256(1));
    CheckBlockChain(fake_pbft, false);
    /// exception case2: invalid height
    fake_pbft.consensus()->setView(prepare_req.view);
    fake_pbft.consensus()->mutableHighest().setNumber(prepare_req.height);
    CheckBlockChain(fake_pbft, false);
    /// normal case: test checkAndSave(import success)
    fake_pbft.consensus()->mutableHighest().setNumber(prepare_req.height - 1);
    CheckBlockChain(fake_pbft, true);
    /// check reportBlock
    BOOST_CHECK(fake_pbft.consensus()->mutableHighest().number() == prepare_req.height);
    checkReportBlock(fake_pbft, highest, false);
    /// set to miner
    FakePBFTMiner(fake_pbft);
    fake_pbft.consensus()->resetConfig();
    checkResetConfig(fake_pbft, true);
}
/// test checkAndCommit
BOOST_AUTO_TEST_CASE(testCheckAndCommit) {}
/// test handlePrepareMsg
BOOST_AUTO_TEST_CASE(testHandlePrepareMsg) {}
BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace dev
