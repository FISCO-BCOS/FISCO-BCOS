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

/// test broadcastSignReq and broadcastCommitReq
BOOST_AUTO_TEST_CASE(testBroadcastSignAndCommitReq)
{
    FakeConsensus<FakePBFTConsensus> fake_pbft(1, ProtocolID::PBFT);
    /// check broadcastSignReq
    SignReq sign_req;
    checkBroadcastSpecifiedMsg(fake_pbft, sign_req, SignReqPacket);
    BOOST_CHECK(fake_pbft.consensus()->reqCache()->isExistSign(sign_req));
    /// check broadcastCommitReq
    CommitReq commit_req;
    checkBroadcastSpecifiedMsg(fake_pbft, commit_req, CommitReqPacket);
    BOOST_CHECK(fake_pbft.consensus()->reqCache()->isExistCommit(commit_req));
}

/// test checkAndSave && reportBlock
BOOST_AUTO_TEST_CASE(testCheckAndSave)
{
    /// fake collected commitReq and signReq
    BlockHeader highest;
    size_t invalid_height = 2;
    size_t invalid_hash = 0;
    size_t valid = 4;
    FakeConsensus<FakePBFTConsensus> fake_pbft(1, ProtocolID::PBFT);
    fake_pbft.consensus()->initPBFTEnv(
        3 * fake_pbft.consensus()->timeManager().m_intervalBlockTime);
    PrepareReq prepare_req;
    FakeValidNodeNum(fake_pbft, u256(valid));
    FakeSignAndCommitCache(fake_pbft, prepare_req, highest, invalid_height, invalid_hash, valid, 2);
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
BOOST_AUTO_TEST_CASE(testCheckAndCommit)
{
    /// fake collected commitReq and signReq
    BlockHeader highest;
    size_t invalid_height = 2;
    size_t invalid_hash = 0;
    size_t valid = 4;
    FakeConsensus<FakePBFTConsensus> fake_pbft(1, ProtocolID::PBFT);
    fake_pbft.consensus()->initPBFTEnv(
        3 * fake_pbft.consensus()->timeManager().m_intervalBlockTime);
    int64_t block_number = obtainBlockNumber(fake_pbft);
    PrepareReq prepare_req;

    KeyPair peer_keyPair = KeyPair::create();
    fake_pbft.m_minerList.push_back(peer_keyPair.pub());
    fake_pbft.consensus()->setMinerList(fake_pbft.m_minerList);

    FakeValidNodeNum(fake_pbft, u256(valid));
    FakeSignAndCommitCache(fake_pbft, prepare_req, highest, invalid_height, invalid_hash,
        fake_pbft.consensus()->minValidNodes().convert_to<size_t>(), 0);

    /// case1: invalid view, return directly(import failed)
    fake_pbft.consensus()->setView(prepare_req.view + u256(1));
    fake_pbft.consensus()->checkAndCommit();
    /// check submit failed
    CheckBlockChain(fake_pbft, block_number);
    /// check update commitedPrepareCache failed

    BOOST_CHECK(fake_pbft.consensus()->reqCache()->rawPrepareCache() !=
                fake_pbft.consensus()->reqCache()->committedPrepareCache());

    /// check backupMsg failed
    bytes data = bytes();
    checkBackupMsg(fake_pbft, FakePBFTConsensus::backupKeyCommitted(), data);

    //// check no broadcast
    BOOST_CHECK(fake_pbft.consensus()->broadcastFilter(
                    peer_keyPair.pub(), CommitReqPacket, prepare_req.sig.hex()) == false);

    /// case2: valid view
    fake_pbft.consensus()->setView(prepare_req.view);
    fake_pbft.consensus()->checkAndCommit();
    /// check backupMsg succ
    BOOST_CHECK(fake_pbft.consensus()->reqCache()->rawPrepareCache() ==
                fake_pbft.consensus()->reqCache()->committedPrepareCache());
    fake_pbft.consensus()->reqCache()->committedPrepareCache().encode(data);
    checkBackupMsg(fake_pbft, FakePBFTConsensus::backupKeyCommitted(), data);
    /// submit failed for collected commitReq is not enough
    CheckBlockChain(fake_pbft, block_number);

    /// case3: enough commitReq && SignReq
    fake_pbft.consensus()->reqCache()->clearAll();
    FakeSignAndCommitCache(fake_pbft, prepare_req, highest, invalid_height, invalid_hash,
        fake_pbft.consensus()->minValidNodes().convert_to<size_t>(), 2);
    fake_pbft.consensus()->checkAndCommit();
    /// check backupMsg succ
    fake_pbft.consensus()->reqCache()->committedPrepareCache().encode(data);
    checkBackupMsg(fake_pbft, FakePBFTConsensus::backupKeyCommitted(), data);
    /// check submit block scc
    CheckBlockChain(fake_pbft, block_number + 1);
    BOOST_CHECK(fake_pbft.consensus()->reqCache()->rawPrepareCache() ==
                fake_pbft.consensus()->reqCache()->committedPrepareCache());
    /// check reportBlock
    checkReportBlock(fake_pbft, highest, false);
    BOOST_CHECK(fake_pbft.consensus()->timeManager().m_lastSignTime <= utcTime());
}
/// test isValidPrepare
BOOST_AUTO_TEST_CASE(testIsValidPrepare)
{
    FakeConsensus<FakePBFTConsensus> fake_pbft(1, ProtocolID::PBFT);
    PrepareReq req;
    TestIsValidPrepare(fake_pbft, req, true);
    TestIsValidPrepare(fake_pbft, req, false);
}
/// test handlePrepareReq
BOOST_AUTO_TEST_CASE(testHandlePrepareReq)
{
    FakeConsensus<FakePBFTConsensus> fake_pbft(1, ProtocolID::PBFT);
    fake_pbft.consensus()->initPBFTEnv(
        3 * (fake_pbft.consensus()->timeManager().m_intervalBlockTime));
    PrepareReq req;
    TestIsValidPrepare(fake_pbft, req, true);
    for (size_t i = 0; i < fake_pbft.m_minerList.size(); i++)
    {
        appendSessionInfo(fake_pbft, fake_pbft.m_minerList[i]);
    }

    /// case1: without omit empty block, re-generate prepareReq and broadcast
    fake_pbft.consensus()->reqCache()->clearAll();
    fake_pbft.consensus()->setOmitEmpty(false);
    fake_pbft.consensus()->handlePrepareMsg(req, false);
    BOOST_CHECK(fake_pbft.consensus()->reqCache()->prepareCache().block_hash == req.block_hash);
    /// check broadcastSign
    for (size_t i = 0; i < fake_pbft.m_minerList.size(); i++)
    {
        compareAsyncSendTime(fake_pbft, fake_pbft.m_minerList[i], 1);
    }
    /// check checkAndCommit(without enough sign and commit requests)
    BOOST_CHECK(fake_pbft.consensus()->reqCache()->rawPrepareCache() !=
                fake_pbft.consensus()->reqCache()->committedPrepareCache());

    /// case2: with enough sign and commit: submit block
    fake_pbft.consensus()->reqCache()->clearAll();
    BlockHeader highest;
    FakeSignAndCommitCache(fake_pbft, req, highest, 0, 0,
        fake_pbft.consensus()->minValidNodes().convert_to<size_t>() - 1, 2, false, false);
    int64_t block_number = obtainBlockNumber(fake_pbft);
    fake_pbft.consensus()->handlePrepareMsg(req, false);

    BOOST_CHECK(fake_pbft.consensus()->reqCache()->prepareCache().block_hash == h256());
    BOOST_CHECK(fake_pbft.consensus()->reqCache()->rawPrepareCache() ==
                fake_pbft.consensus()->reqCache()->committedPrepareCache());
    bytes data;
    fake_pbft.consensus()->reqCache()->committedPrepareCache().encode(data);
    checkBackupMsg(fake_pbft, FakePBFTConsensus::backupKeyCommitted(), data);
    /// submit failed for collected commitReq is not enough
    CheckBlockChain(fake_pbft, block_number + 1);
}

BOOST_AUTO_TEST_CASE(testIsValidSignReq)
{
    FakeConsensus<FakePBFTConsensus> fake_pbft(1, ProtocolID::PBFT);
    PBFTMsgPacket pbftMsg;
    SignReq signReq;
    PrepareReq prepareReq;
    KeyPair peer_keyPair = KeyPair::create();
    TestIsValidSignReq(fake_pbft, pbftMsg, signReq, prepareReq, peer_keyPair, false);
}

/// test handleSignMsg
BOOST_AUTO_TEST_CASE(testHandleSignMsg)
{
    FakeConsensus<FakePBFTConsensus> fake_pbft(1, ProtocolID::PBFT);

    fake_pbft.consensus()->initPBFTEnv(
        fake_pbft.consensus()->timeManager().m_intervalBlockTime * 3);

    PBFTMsgPacket pbftMsg;
    SignReq signReq;
    PrepareReq prepareReq;
    KeyPair peer_keyPair = KeyPair::create();
    TestIsValidSignReq(fake_pbft, pbftMsg, signReq, prepareReq, peer_keyPair, true);
    /// test handleSignMsg
    /// case1: without enough signReq and commitReq, only addSign to the cache
    SignReq signReq2;
    int64_t block_number = obtainBlockNumber(fake_pbft);
    fake_pbft.consensus()->handleSignMsg(signReq2, pbftMsg);
    BOOST_CHECK(signReq2 == signReq);
    /// check the signReq has been added to the cache
    BOOST_CHECK(fake_pbft.consensus()->reqCache()->isExistSign(signReq2));
    CheckBlockChain(fake_pbft, block_number);

    /// case2： with enough SignReq
    fake_pbft.consensus()->reqCache()->clearAll();
    fake_pbft.consensus()->reqCache()->addPrepareReq(prepareReq);
    BlockHeader highest;
    FakeSignAndCommitCache(fake_pbft, prepareReq, highest, 0, 0,
        fake_pbft.consensus()->minValidNodes().convert_to<size_t>() - 1, 0, false, false);
    fake_pbft.consensus()->handleSignMsg(signReq2, pbftMsg);

    BOOST_CHECK(fake_pbft.consensus()->reqCache()->committedPrepareCache() ==
                fake_pbft.consensus()->reqCache()->rawPrepareCache());
    /// check backupMsg
    bytes data;
    fake_pbft.consensus()->reqCache()->committedPrepareCache().encode(data);
    checkBackupMsg(fake_pbft, FakePBFTConsensus::backupKeyCommitted(), data);
    CheckBlockChain(fake_pbft, block_number);

    /// case3: with enough SignReq and CommitReq
    fake_pbft.consensus()->reqCache()->clearAll();
    fake_pbft.consensus()->reqCache()->addPrepareReq(prepareReq);
    FakeSignAndCommitCache(fake_pbft, prepareReq, highest, 0, 0,
        fake_pbft.consensus()->minValidNodes().convert_to<size_t>() - 1, 2, false, false);
    fake_pbft.consensus()->handleSignMsg(signReq2, pbftMsg);

    BOOST_CHECK(fake_pbft.consensus()->reqCache()->committedPrepareCache() ==
                fake_pbft.consensus()->reqCache()->rawPrepareCache());
    /// check backupMsg
    fake_pbft.consensus()->reqCache()->committedPrepareCache().encode(data);
    checkBackupMsg(fake_pbft, FakePBFTConsensus::backupKeyCommitted(), data);
    CheckBlockChain(fake_pbft, block_number + 1);
}

BOOST_AUTO_TEST_CASE(testIsCommitReqValid)
{
    FakeConsensus<FakePBFTConsensus> fake_pbft(1, ProtocolID::PBFT);
    PBFTMsgPacket pbftMsg;
    CommitReq commitReq;
    PrepareReq prepareReq;
    KeyPair peer_keyPair = KeyPair::create();
    TestIsValidCommitReq(fake_pbft, pbftMsg, commitReq, prepareReq, peer_keyPair, false);
}

/// test handleCommitMsg
BOOST_AUTO_TEST_CASE(testHandleCommitMsg)
{
    FakeConsensus<FakePBFTConsensus> fake_pbft(1, ProtocolID::PBFT);
    PBFTMsgPacket pbftMsg;
    CommitReq commitReq;
    PrepareReq prepareReq;
    KeyPair peer_keyPair = KeyPair::create();
    TestIsValidCommitReq(fake_pbft, pbftMsg, commitReq, prepareReq, peer_keyPair, true);
    /// case1: without enough signReq and commitReq
    CommitReq commitReq2;
    int64_t block_number = obtainBlockNumber(fake_pbft);
    fake_pbft.consensus()->handleCommitMsg(commitReq2, pbftMsg);
    BOOST_CHECK(commitReq2 == commitReq);
    BOOST_CHECK(fake_pbft.consensus()->reqCache()->isExistCommit(commitReq2));
    CheckBlockChain(fake_pbft, block_number);

    /// case2: with enough signReq but not commitReq
    fake_pbft.consensus()->reqCache()->clearAll();
    fake_pbft.consensus()->reqCache()->addPrepareReq(prepareReq);
    BlockHeader highest;
    FakeSignAndCommitCache(fake_pbft, prepareReq, highest, 0, 0,
        fake_pbft.consensus()->minValidNodes().convert_to<size_t>(), 0, false, false);
    fake_pbft.consensus()->handleCommitMsg(commitReq2, pbftMsg);
    BOOST_CHECK(fake_pbft.consensus()->reqCache()->isExistCommit(commitReq2));
    CheckBlockChain(fake_pbft, block_number);

    /// case3 : with enough signReq and commitReq
    fake_pbft.consensus()->reqCache()->clearAll();
    fake_pbft.consensus()->reqCache()->addPrepareReq(prepareReq);
    FakeSignAndCommitCache(fake_pbft, prepareReq, highest, 0, 0,
        fake_pbft.consensus()->minValidNodes().convert_to<size_t>(), 2, false, false);
    fake_pbft.consensus()->handleCommitMsg(commitReq2, pbftMsg);
    CheckBlockChain(fake_pbft, block_number + 1);
}

BOOST_AUTO_TEST_CASE(testShouldSeal)
{
    FakeConsensus<FakePBFTConsensus> fake_pbft(1, ProtocolID::PBFT);
    /// case 1: m_cfgErr is true
    fake_pbft.consensus()->resetConfig();
    BOOST_CHECK(fake_pbft.consensus()->shouldSeal() == false);

    /// case 2: the node is a miner, but getLeader failed for highest is a invalid block
    FakePBFTMiner(fake_pbft);
    BOOST_CHECK(fake_pbft.consensus()->shouldSeal() == false);

    /// case 3: normal case
    PrepareReq prepareReq;
    TestIsValidPrepare(fake_pbft, prepareReq, true);

    fake_pbft.consensus()->setNodeIdx(fake_pbft.consensus()->getLeader().second);
    BOOST_CHECK(fake_pbft.consensus()->shouldSeal() == true);
    /// update the committedPrepareCache, callback reHandleCommitPrepareCache
    fake_pbft.consensus()->mutableConsensusNumber() = prepareReq.height;

    fake_pbft.consensus()->reqCache()->addRawPrepare(prepareReq);
    fake_pbft.consensus()->reqCache()->updateCommittedPrepare();
    /// update the rawPrepareReq
    PrepareReq new_req;
    new_req.height = fake_pbft.consensus()->mutableConsensusNumber() + 1;
    fake_pbft.consensus()->reqCache()->addRawPrepare(new_req);

    for (size_t i = 0; i < fake_pbft.m_minerList.size(); i++)
    {
        appendSessionInfo(fake_pbft, fake_pbft.m_minerList[i]);
    }
    int64_t block_number = obtainBlockNumber(fake_pbft);
    BOOST_CHECK(fake_pbft.consensus()->shouldSeal() == false);
    testReHandleCommitPrepareCache(fake_pbft, prepareReq);

    /// case 4: the node is not the leader
    fake_pbft.consensus()->setNodeIdx(
        (fake_pbft.consensus()->getLeader().second + 1) % fake_pbft.consensus()->nodeNum());
    FakeService* fake_service =
        dynamic_cast<FakeService*>(fake_pbft.consensus()->mutableService().get());
    fake_service->setConnected();
    BOOST_CHECK(fake_pbft.consensus()->shouldSeal() == false);
    BOOST_CHECK(fake_pbft.consensus()->timeManager().m_lastConsensusTime == 0);
    BOOST_CHECK(fake_pbft.consensus()->timeManager().m_lastSignTime == 0);
}

BOOST_AUTO_TEST_CASE(testCollectGarbage)
{
    FakeConsensus<FakePBFTConsensus> fake_pbft(1, ProtocolID::PBFT);
    BlockHeader highest;
    PrepareReq prepareReq;
    FakeSignAndCommitCache(fake_pbft, prepareReq, highest, 0, 0,
        fake_pbft.consensus()->minValidNodes().convert_to<size_t>(), 2);
    fake_pbft.consensus()->mutableTimeManager().m_lastGarbageCollection =
        std::chrono::system_clock::now();
    /// can't trigger collectGarbage
    fake_pbft.consensus()->collectGarbage();
    BOOST_CHECK(fake_pbft.consensus()->reqCache()->getSigCacheSize(prepareReq.block_hash) ==
                fake_pbft.consensus()->minValidNodes());
    BOOST_CHECK(fake_pbft.consensus()->reqCache()->getCommitCacheSize(prepareReq.block_hash) ==
                fake_pbft.consensus()->minValidNodes());

    /// can trigger collectGarbage
    fake_pbft.consensus()->mutableTimeManager().m_lastGarbageCollection =
        std::chrono::system_clock::now() - std::chrono::seconds(TimeManager::CollectInterval + 10);
    fake_pbft.consensus()->collectGarbage();
    BOOST_CHECK(
        fake_pbft.consensus()->reqCache()->getSigCacheSize(prepareReq.block_hash) == u256(0));
    BOOST_CHECK(
        fake_pbft.consensus()->reqCache()->getCommitCacheSize(prepareReq.block_hash) == u256(0));
}
/// test handle future block
BOOST_AUTO_TEST_CASE(testHandleFutureBlock)
{
    FakeConsensus<FakePBFTConsensus> fake_pbft(1, ProtocolID::PBFT);
    PrepareReq prepareReq;
    TestIsValidPrepare(fake_pbft, prepareReq, true);
    prepareReq.height == fake_pbft.consensus()->mutableConsensusNumber();
    prepareReq.view == fake_pbft.consensus()->view();
    fake_pbft.consensus()->reqCache()->addFuturePrepareCache(prepareReq);
    BOOST_CHECK(fake_pbft.consensus()->reqCache()->futurePrepareCache().block_hash != h256());
    fake_pbft.consensus()->handleFutureBlock();
    /// check the functurePrepareCache has been cleared
    BOOST_CHECK(fake_pbft.consensus()->reqCache()->futurePrepareCache().block_hash == h256());
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace dev
